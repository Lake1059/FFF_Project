#include "pch.h"
#include "3FP/Hdr/HdrProcessor.h"
#include "3FP/Render/ColorExtension.h"
#include "3FP/Hdr/HdrMetadataParse.h"
#include "3FP/Hdr/HdrToneCurve.h"

extern "C" {
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hdr_dynamic_vivid_metadata.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

#include <cmath>
#include <limits>

namespace {
constexpr std::uint32_t Compatibility(const FFF3FPHdrCompatibility value) noexcept {
    return static_cast<std::uint32_t>(value);
}

// How many consecutive frames may reuse the previous ST 2094 snapshot before it
// is considered stale. HDR10+ does not require metadata on every frame, so
// holding is necessary; an unbounded hold would let a pre-seek snapshot linger.
constexpr std::uint64_t kHdrMetadataHoldFrameLimit = 60;

// Upper bound on an injected scene list. Real scene lists are tens of entries;
// the cap keeps a misbehaving host from growing the table without limit.
constexpr std::uint32_t kMaxInjectedEntries = 4096;

float ValidPeak(const double value) noexcept {
    return std::isfinite(value) && value > 0.0 ?
        static_cast<float>(std::clamp(value, 1.0, 10000.0)) : 0.0f;
}

float ValidLuminance(const double value) noexcept {
    return std::isfinite(value) && value > 0.0 ?
        static_cast<float>(std::clamp(value, 0.0, 10000.0)) : 0.0f;
}

float ValidChromaticity(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0 ?
        static_cast<float>(value) : 0.0f;
}

void ClassifyDolbyVision(const AVDOVIDecoderConfigurationRecord* configuration,
    HdrFrameState& state) noexcept {
    if (configuration == nullptr) return;
    state.format = FFF3FPHdrFormat::DolbyVision;
    state.compatibility = Compatibility(FFF3FPHdrCompatibility::DolbyVision);
    state.dolbyVisionProfile = configuration->dv_profile;
    state.dolbyVisionLevel = configuration->dv_level;
    state.hasRpu = configuration->rpu_present_flag != 0;
    state.hasEnhancementLayer = configuration->el_present_flag != 0;
    if (state.hasEnhancementLayer)
        state.enhancementLayer = FFF3FPDolbyVisionEnhancementLayer::Unknown;
    if (configuration->dv_bl_signal_compatibility_id == 1 || configuration->dv_profile == 7)
        state.compatibility |= Compatibility(FFF3FPHdrCompatibility::Hdr10);
    if (configuration->dv_bl_signal_compatibility_id == 4)
        state.compatibility |= Compatibility(FFF3FPHdrCompatibility::Hlg);
    // Use the base-layer compatibility path until an external extension accepts
    // the current frame. P5 has no HDR10-compatible base layer, so its fallback
    // is best effort.
    state.processingPath = FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback;
    state.fallback = true;
}

void ApplyContentLightMetadata(const AVContentLightMetadata* light,
    HdrStaticMetadata& metadata) noexcept {
    if (light == nullptr) return;
    const auto maxContent = ValidPeak(light->MaxCLL);
    const auto maxAverage = ValidLuminance(light->MaxFALL);
    if (maxContent <= 0.0f && maxAverage <= 0.0f) return;
    metadata.hasContentLight = true;
    if (maxContent > 0.0f)
        metadata.maximumContentLightLevelNits = maxContent;
    if (maxAverage > 0.0f)
        metadata.maximumFrameAverageLightLevelNits = maxAverage;
}

void ApplyMasteringDisplayMetadata(const AVMasteringDisplayMetadata* mastering,
    HdrStaticMetadata& metadata) noexcept {
    if (mastering == nullptr) return;
    if (mastering->has_primaries) {
        const auto redX = ValidChromaticity(av_q2d(mastering->display_primaries[0][0]));
        const auto redY = ValidChromaticity(av_q2d(mastering->display_primaries[0][1]));
        const auto greenX = ValidChromaticity(av_q2d(mastering->display_primaries[1][0]));
        const auto greenY = ValidChromaticity(av_q2d(mastering->display_primaries[1][1]));
        const auto blueX = ValidChromaticity(av_q2d(mastering->display_primaries[2][0]));
        const auto blueY = ValidChromaticity(av_q2d(mastering->display_primaries[2][1]));
        const auto whiteX = ValidChromaticity(av_q2d(mastering->white_point[0]));
        const auto whiteY = ValidChromaticity(av_q2d(mastering->white_point[1]));
        if (redX > 0.0f && redY > 0.0f && greenX > 0.0f && greenY > 0.0f &&
            blueX > 0.0f && blueY > 0.0f && whiteX > 0.0f && whiteY > 0.0f) {
            metadata.hasPrimaries = true;
            metadata.redX = redX; metadata.redY = redY;
            metadata.greenX = greenX; metadata.greenY = greenY;
            metadata.blueX = blueX; metadata.blueY = blueY;
            metadata.whiteX = whiteX; metadata.whiteY = whiteY;
        }
    }
    if (mastering->has_luminance) {
        const auto minimum = ValidLuminance(av_q2d(mastering->min_luminance));
        const auto maximum = ValidPeak(av_q2d(mastering->max_luminance));
        if (maximum > 0.0f) {
            metadata.hasLuminance = true;
            metadata.minimumLuminanceNits = minimum;
            metadata.maximumMasteringLuminanceNits = maximum;
        }
    }
}

void ApplyFrameStaticMetadata(const AVFrame* frame, HdrStaticMetadata& metadata) noexcept {
    if (frame == nullptr) return;
    if (const auto* lightData = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        lightData != nullptr && lightData->size >= sizeof(AVContentLightMetadata)) {
        ApplyContentLightMetadata(
            reinterpret_cast<const AVContentLightMetadata*>(lightData->data), metadata);
    }
    if (const auto* masteringData = av_frame_get_side_data(
            frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        masteringData != nullptr && masteringData->size >= sizeof(AVMasteringDisplayMetadata)) {
        ApplyMasteringDisplayMetadata(
            reinterpret_cast<const AVMasteringDisplayMetadata*>(masteringData->data), metadata);
    }
}

void ApplyStreamStaticMetadata(const AVCodecParameters* parameters,
    HdrStaticMetadata& metadata) noexcept {
    if (parameters == nullptr) return;
    if (const auto* lightData = av_packet_side_data_get(parameters->coded_side_data,
            parameters->nb_coded_side_data, AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
        lightData != nullptr && lightData->size >= sizeof(AVContentLightMetadata)) {
        ApplyContentLightMetadata(
            reinterpret_cast<const AVContentLightMetadata*>(lightData->data), metadata);
    }
    if (const auto* masteringData = av_packet_side_data_get(parameters->coded_side_data,
            parameters->nb_coded_side_data, AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
        masteringData != nullptr && masteringData->size >= sizeof(AVMasteringDisplayMetadata)) {
        ApplyMasteringDisplayMetadata(
            reinterpret_cast<const AVMasteringDisplayMetadata*>(masteringData->data), metadata);
    }
}

float StaticSourcePeak(const HdrStaticMetadata& metadata) noexcept {
    if (metadata.maximumContentLightLevelNits > 0.0f)
        return metadata.maximumContentLightLevelNits;
    if (metadata.maximumMasteringLuminanceNits > 0.0f)
        return metadata.maximumMasteringLuminanceNits;
    return 0.0f;
}

std::uint16_t DxgiChromaticity(const float value) noexcept {
    return static_cast<std::uint16_t>(std::lround(
        std::clamp(value, 0.0f, 1.0f) * 50000.0f));
}

std::uint32_t DxgiNits(const float value) noexcept {
    return static_cast<std::uint32_t>(std::lround(
        std::clamp(value, 0.0f, 10000.0f)));
}

std::uint32_t DxgiMinNits(const float value) noexcept {
    return static_cast<std::uint32_t>(std::lround(
        std::clamp(value, 0.0f, 6.5535f) * 10000.0f));
}

std::uint16_t DxgiContentLight(const float value) noexcept {
    return static_cast<std::uint16_t>(std::lround(
        std::clamp(value, 0.0f, 65535.0f)));
}
}

void HdrProcessor::ConfigureStream(const AVCodecParameters* parameters) noexcept {
    HdrFrameState next{};
    if (parameters != nullptr) {
        const auto* doviData = av_packet_side_data_get(parameters->coded_side_data,
            parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
        if (doviData != nullptr && doviData->size >= sizeof(AVDOVIDecoderConfigurationRecord)) {
            ClassifyDolbyVision(
                reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(doviData->data), next);
        } else if (av_packet_side_data_get(parameters->coded_side_data,
            parameters->nb_coded_side_data, AV_PKT_DATA_DYNAMIC_HDR10_PLUS) != nullptr) {
            next.format = FFF3FPHdrFormat::Hdr10Plus;
            next.compatibility = Compatibility(FFF3FPHdrCompatibility::Hdr10);
            next.processingPath = FFF3FPHdrProcessingPath::Hdr10PlusDynamic;
            next.sourcePeakNits = 1000.0f;
        } else if (parameters->color_trc == AVCOL_TRC_ARIB_STD_B67) {
            next.format = FFF3FPHdrFormat::Hlg;
            next.compatibility = Compatibility(FFF3FPHdrCompatibility::Hlg);
            next.processingPath = FFF3FPHdrProcessingPath::HlgDisplayMapped;
            next.sourcePeakNits = 1000.0f;
        } else if (parameters->color_trc == AVCOL_TRC_SMPTE2084) {
            next.format = FFF3FPHdrFormat::Hdr10;
            next.compatibility = Compatibility(FFF3FPHdrCompatibility::Hdr10);
            next.processingPath = FFF3FPHdrProcessingPath::StaticHdr10;
            next.sourcePeakNits = 1000.0f;
        }
        ApplyStreamStaticMetadata(parameters, next.staticMetadata);
        if (next.format != FFF3FPHdrFormat::Sdr) {
            if (const auto staticPeak = StaticSourcePeak(next.staticMetadata);
                staticPeak > 0.0f) {
                next.sourcePeakNits = staticPeak;
            }
        }
    }
    std::lock_guard lock(mutex_);
    next.display = frameState_.display;
    next.targetPeakNits = ResolveTargetPeak(targetPeakOverrideNits_, next.display);
    streamState_ = next;
    frameState_ = next;
}

HdrFrameState HdrProcessor::ProcessFrame(const AVFrame* frame,
    const float targetPeakOverrideNits, const float paperWhiteNits,
    const std::int64_t frameIndex) noexcept {
    std::lock_guard lock(mutex_);
    targetPeakOverrideNits_ = std::isfinite(targetPeakOverrideNits) &&
        targetPeakOverrideNits > 0.0f ? targetPeakOverrideNits : 0.0f;
    auto next = streamState_;
    next.display = frameState_.display;
    next.targetPeakNits = ResolveTargetPeak(targetPeakOverrideNits_, next.display);
    if (frame == nullptr) {
        frameState_ = next;
        return frameState_;
    }

    auto dynamicSourcePeak = false;
    if (next.format == FFF3FPHdrFormat::Sdr) {
        if (frame->color_trc == AVCOL_TRC_ARIB_STD_B67) {
            next.format = FFF3FPHdrFormat::Hlg;
            next.compatibility = Compatibility(FFF3FPHdrCompatibility::Hlg);
            next.processingPath = FFF3FPHdrProcessingPath::HlgDisplayMapped;
            next.sourcePeakNits = 1000.0f;
        } else if (frame->color_trc == AVCOL_TRC_SMPTE2084) {
            next.format = FFF3FPHdrFormat::Hdr10;
            next.compatibility = Compatibility(FFF3FPHdrCompatibility::Hdr10);
            next.processingPath = FFF3FPHdrProcessingPath::StaticHdr10;
            next.sourcePeakNits = 1000.0f;
        }
    }

    // Deliberately read a single scalar (`format`) rather than the accumulating
    // `compatibility` bit set: compatibility is OR-ed in several places (e.g.
    // ClassifyDolbyVision) and `next` inherits it from streamState_, so testing the
    // Hlg *bit* here could pin a genuinely PQ-baseline stream to the HLG branch.
    // `format` is assigned, never OR-ed, so it cannot go sticky.
    // Must be captured BEFORE the Vivid branch below re-labels next.format.
    const bool vividBaselineIsHlg = streamState_.format == FFF3FPHdrFormat::Hlg;

    if (const auto* vividData = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_VIVID);
        vividData != nullptr && vividData->size >= sizeof(AVDynamicHDRVivid)) {
        const auto* vivid = reinterpret_cast<const AVDynamicHDRVivid*>(vividData->data);
        next.format = FFF3FPHdrFormat::HdrVivid;
        next.compatibility |= Compatibility(FFF3FPHdrCompatibility::HdrVivid) |
            Compatibility(FFF3FPHdrCompatibility::Hdr10);
        next.processingPath = FFF3FPHdrProcessingPath::HdrVividDynamic;
        next.dynamicMetadata = true;
        if (vivid->num_windows > 0) {
            // `maximum_maxrgb` is a maxRGB statistic normalised into [0,1] on a
            // 1/4095 grid; the FFmpeg header documents no unit for it. The absolute
            // reading below (x10000) is only self-consistent for a PQ baseline, where
            // PQ EOTF(1.0) == 10000 cd/m2 and the value sits near full code. It is NOT
            // valid for an HLG baseline, which is relative/scene-referred: a full-code
            // HLG signal means "1000 cd/m2 on the nominal BT.2100 reference display",
            // not 10000. Taking the absolute reading there inflated sourcePeakNits by
            // ~10x and blew out the highlights.
            //
            // HLG baseline therefore keeps the relative-domain reference peak, which
            // the HLG classification above already set (1000.0f): just don't overwrite
            // it: an HLG-baseline stream that read 10000 now reads 4000,
            if (!vividBaselineIsHlg) {
                const auto peak = ValidPeak(av_q2d(vivid->params[0].maximum_maxrgb) * 10000.0);
                if (peak > 0.0f) {
                    next.sourcePeakNits = peak;
                    dynamicSourcePeak = true;
                }
            }
        }
    } else if (const auto* doviData = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        doviData != nullptr && doviData->size >= sizeof(AVDOVIMetadata)) {
        next.format = FFF3FPHdrFormat::DolbyVision;
        next.compatibility |= Compatibility(FFF3FPHdrCompatibility::DolbyVision);
        next.hasRpu = true;
        next.dynamicMetadata = false;
        next.externalExtensionActive = false;
        next.fallback = true;
        next.processingPath = FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback;
        if (const auto* api = GetColorExtension(); api != nullptr) {
            const FFFColorExtensionInput request{sizeof(request), FFFColorExtensionVersion, 0,
                next.dolbyVisionProfile, doviData->data, doviData->size};
            FFFColorExtensionMetadataInfo info{sizeof(info)};
            if (api->analyzeMetadata(&request, next.hasEnhancementLayer ? 1u : 0u, &info)) {
                if (info.enhancementLayer <= 3)
                    next.enhancementLayer = static_cast<FFF3FPDolbyVisionEnhancementLayer>(info.enhancementLayer);
                if (info.requiresEnhancement)
                    next.processingPath = FFF3FPHdrProcessingPath::DolbyVisionFelFallback;
                if (const auto peak = ValidPeak(info.sourcePeakNits); peak > 0.0f) {
                    next.sourcePeakNits = peak;
                    dynamicSourcePeak = true;
                }
            }
        }
    } else if (const auto* plusData = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
        plusData != nullptr && plusData->size >= sizeof(AVDynamicHDRPlus)) {
        const auto* plus = reinterpret_cast<const AVDynamicHDRPlus*>(plusData->data);
        next.format = FFF3FPHdrFormat::Hdr10Plus;
        next.compatibility |= Compatibility(FFF3FPHdrCompatibility::Hdr10);
        next.processingPath = FFF3FPHdrProcessingPath::Hdr10PlusDynamic;
        next.dynamicMetadata = true;
        // Full ST 2094-40 extraction: the window curve parameters (knee, bezier
        // anchors, distribution) drive the tone mapping; maxscl only bounds the
        // peak. maxscl is *linearized* RGB, so nits = value * 10000.
        next.dynamic = HdrMetadataParse::FromHdr10Plus(*plus);
        next.dynamic.serial = frameState_.dynamic.kind == HdrMetadataKind::Hdr10Plus
            ? frameState_.dynamic.serial + 1 : 1;
        if (const auto peak = ValidPeak(next.dynamic.windows[0].signalPeakNits);
            peak > 0.0f) {
            next.sourcePeakNits = peak;
            dynamicSourcePeak = true;
        }
    }

    ApplyInjectionLocked(frameIndex, next);
    // An injected entry owns the whole dynamic state, including its peak: the two
    // fallbacks below must not touch it, or the frame that carries no SEI would quietly
    // inherit the held bitstream snapshot (and be dropped past the hold limit) while the
    // source still reports Injected. ApplyInjectionLocked resolves the peak itself.

    if (next.format != FFF3FPHdrFormat::Sdr) {
        ApplyFrameStaticMetadata(frame, next.staticMetadata);
        // ST 2094-40 metadata is not required on every frame. Without holding
        // the previous snapshot, a frame that carries no SEI would fall back to
        // the static peak and the picture would flicker between two mappings.
        // Hold the last snapshot instead, but only for a bounded number of
        // frames: past that the stream is treated as not carrying dynamic
        // metadata at all, which also stops a stale snapshot surviving a seek.
        const bool carriedMetadata = next.dynamic.kind != HdrMetadataKind::None;
        if (carriedMetadata && !dynamicSourcePeak && !injectedActive_) {
            if (frameState_.dynamic.kind == next.dynamic.kind &&
                frameState_.dynamic.heldFrames < kHdrMetadataHoldFrameLimit) {
                next.dynamic = frameState_.dynamic;
                next.dynamic.heldFrames += 1;
                next.dynamicMetadata = true;
                const auto held = ValidPeak(next.dynamic.windows[0].signalPeakNits);
                if (held > 0.0f) {
                    next.sourcePeakNits = held;
                    dynamicSourcePeak = true;
                }
            } else if (frameState_.dynamic.heldFrames >= kHdrMetadataHoldFrameLimit) {
                // Stale beyond the limit: drop it so the static path takes over.
                next.dynamic = HdrDynamicMetadata{};
                next.dynamicMetadata = false;
            }
        }
        if (!dynamicSourcePeak && !injectedActive_) {
            if (const auto staticPeak = StaticSourcePeak(next.staticMetadata);
                staticPeak > 0.0f) {
                next.sourcePeakNits = staticPeak;
            }
        }
        next.sourcePeakNits = std::clamp(next.sourcePeakNits,
            std::max(1.0f, paperWhiteNits), 10000.0f);
    }
    if (!injectedActive_) lastUninjectedState_ = next;
    if (!injectedActive_) haveUninjectedState_ = true;
    frameState_ = next;
    return frameState_;
}

void HdrProcessor::SetDisplayCapabilities(const HdrDisplayCapabilities& display) noexcept {
    std::lock_guard lock(mutex_);
    streamState_.display = display;
    frameState_.display = display;
    streamState_.targetPeakNits = ResolveTargetPeak(targetPeakOverrideNits_, display);
    frameState_.targetPeakNits = ResolveTargetPeak(targetPeakOverrideNits_, display);
}

void HdrProcessor::SetTargetPeakOverride(const float targetPeakOverrideNits) noexcept {
    std::lock_guard lock(mutex_);
    targetPeakOverrideNits_ = std::isfinite(targetPeakOverrideNits) &&
        targetPeakOverrideNits > 0.0f ? targetPeakOverrideNits : 0.0f;
    streamState_.targetPeakNits = ResolveTargetPeak(
        targetPeakOverrideNits_, streamState_.display);
    frameState_.targetPeakNits = ResolveTargetPeak(
        targetPeakOverrideNits_, frameState_.display);
}

void HdrProcessor::Reset() noexcept {
    std::lock_guard lock(mutex_);
    const auto display = frameState_.display;
    streamState_ = {};
    streamState_.display = display;
    streamState_.targetPeakNits = ResolveTargetPeak(targetPeakOverrideNits_, display);
    frameState_ = streamState_;
    // A new stream has no finished frame of its own yet, so neither the
    // bitstream-only baseline nor the index a table was last resolved at may
    // survive from the previous stream.
    lastUninjectedState_ = {};
    haveUninjectedState_ = false;
    lastLookupIndex_ = kUnknownFrameIndex;
    // Cleared here too: until the new stream's first frame is resolved, reporting
    // the previous stream's Injected source would be a claim with nothing behind it.
    injectedActive_ = false;
}

HdrFrameState HdrProcessor::State() const noexcept {
    std::lock_guard lock(mutex_);
    return frameState_;
}

bool HdrProcessor::IsHdrSource() const noexcept { return State().format != FFF3FPHdrFormat::Sdr; }

void HdrProcessor::SetExtensionAvailability(const bool available) noexcept {
    std::lock_guard lock(mutex_);
    streamState_.externalExtensionAvailable = available;
    frameState_.externalExtensionAvailable = available;
    if (!available) {
        streamState_.externalExtensionActive = false;
        frameState_.externalExtensionActive = false;
    }
}

void HdrProcessor::SetExtensionProcessing(const float sourcePeakNits, const bool active) noexcept {
    std::lock_guard lock(mutex_);
    for (auto* state : {&streamState_, &frameState_}) {
        state->externalExtensionActive = active;
        if (active) {
            state->processingPath = FFF3FPHdrProcessingPath::ExternalDynamic;
            state->dynamicMetadata = true;
            state->fallback = false;
        } else if (state->processingPath == FFF3FPHdrProcessingPath::ExternalDynamic) {
            state->dynamicMetadata = false;
            if (state->format == FFF3FPHdrFormat::DolbyVision) {
                const bool frameFel = state == &frameState_ && state->hasEnhancementLayer &&
                    state->enhancementLayer == FFF3FPDolbyVisionEnhancementLayer::Fel;
                state->processingPath = frameFel ? FFF3FPHdrProcessingPath::DolbyVisionFelFallback
                    : FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback;
                state->fallback = true;
            }
        }
    }
    if (active && std::isfinite(sourcePeakNits) && sourcePeakNits > 0.0f)
        frameState_.sourcePeakNits = std::clamp(sourcePeakNits, 1.0f, 10000.0f);
}

bool HdrProcessor::RequiresMetadataAwareShader() const noexcept {
    const auto state = State();
    return state.dynamicMetadata || state.format == FFF3FPHdrFormat::Hlg ||
        state.format == FFF3FPHdrFormat::DolbyVision ||
        state.format == FFF3FPHdrFormat::HdrVivid;
}

namespace {
// Validate and copy one injected window. Returns false on any malformed field.
// Shared by every window of an entry so the single- and multi-window shapes are
// held to exactly the same standard.
bool BuildInjectedWindow(const FFF3FPHdrMetadataWindow& source,
    HdrProcessingWindow& window, HdrMetadataDegrade& degrade) noexcept {
    if (!std::isfinite(source.upperLeftX) || !std::isfinite(source.upperLeftY) ||
        !std::isfinite(source.lowerRightX) || !std::isfinite(source.lowerRightY))
        return false;
    if (source.upperLeftX < 0.0f || source.upperLeftY < 0.0f ||
        source.lowerRightX > 1.0f || source.lowerRightY > 1.0f)
        return false;
    // A degenerate or inverted rectangle selects no pixels, which would make the
    // window's curve unreachable rather than merely unused.
    if (source.lowerRightX <= source.upperLeftX ||
        source.lowerRightY <= source.upperLeftY)
        return false;
    window.valid = true;
    window.upperLeftX = source.upperLeftX;
    window.upperLeftY = source.upperLeftY;
    window.lowerRightX = source.lowerRightX;
    window.lowerRightY = source.lowerRightY;

    float peak = 0.0f;
    for (std::uint32_t channel = 0; channel < 3; ++channel) {
        const auto value = source.maxScl[channel];
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
        window.maxScl[channel] = value;
        peak = std::max(peak, value);
    }
    if (!std::isfinite(source.averageMaxRgb) ||
        source.averageMaxRgb < 0.0f || source.averageMaxRgb > 1.0f) return false;
    window.averageMaxRgb = source.averageMaxRgb;
    if (std::isfinite(source.fractionBrightPixels) &&
        source.fractionBrightPixels >= 0.0f && source.fractionBrightPixels <= 1.0f)
        window.fractionBrightPixels = source.fractionBrightPixels;
    window.signalPeakNits = HdrMetadataParse::LinearizedToNits(peak);

    // No curve block: the window only bounds its peak, which is a valid shape
    // (the filter's `curve=off` and most shipped HDR10+ both use it).
    if (source.toneMappingFlag == 0) {
        window.toneMappingPresent = false;
        if (degrade == HdrMetadataDegrade::None)
            degrade = HdrMetadataDegrade::ToneMappingAbsent;
        return true;
    }

    if (!std::isfinite(source.kneePointX) || !std::isfinite(source.kneePointY) ||
        source.kneePointX < 0.0f || source.kneePointX >= 1.0f ||
        source.kneePointY < 0.0f || source.kneePointY > 1.0f ||
        source.kneePointY > source.kneePointX + 1.0e-4f) {
        return false;
    }
    window.kneePointX = source.kneePointX;
    window.kneePointY = source.kneePointY;
    window.toneMappingPresent = true;

    if (source.numCurveAnchors > kHdrMaxBezierAnchors) return false;
    float previous = source.kneePointY;
    for (std::uint32_t index = 0; index < source.numCurveAnchors; ++index) {
        const auto value = source.curveAnchors[index];
        // Ascending and in range: a non-monotone anchor list cannot describe a
        // tone curve, and silently accepting it would produce banding.
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
        if (value < previous) return false;
        previous = value;
        window.bezierAnchors[index] = value;
    }
    window.bezierAnchorCount = source.numCurveAnchors;
    if (source.numCurveAnchors == 0) {
        window.toneMappingPresent = false;
        if (degrade == HdrMetadataDegrade::None)
            degrade = HdrMetadataDegrade::TooFewAnchors;
    }
    return true;
}
}  // namespace

bool HdrProcessor::BuildInjectedState(const FFF3FPHdrDynamicMetadataEntry& entry,
    HdrDynamicMetadata& metadata) noexcept {
    metadata = HdrDynamicMetadata{};
    metadata.kind = HdrMetadataKind::Hdr10Plus;
    // ST 2094-40 defines num_windows in [1,3]; anything else is out of spec and
    // rejected rather than guessed at.
    if (entry.numWindows < 1 || entry.numWindows > kHdrMaxProcessingWindows)
        return false;
    metadata.windowCount = entry.numWindows;

    for (std::uint32_t index = 0; index < entry.numWindows; ++index) {
        if (!BuildInjectedWindow(entry.windows[index], metadata.windows[index],
                metadata.degrade))
            return false;
    }

    if (std::isfinite(entry.targetedDisplayNits) && entry.targetedDisplayNits > 0.0f) {
        metadata.targetedDisplayNits =
            HdrMetadataParse::TargetedDisplayToNits(entry.targetedDisplayNits);
        metadata.hasTargetedDisplay = metadata.targetedDisplayNits > 0.0f;
    }
    return true;
}

FFFResult HdrProcessor::SetInjectedMetadata(
    const FFF3FPHdrDynamicMetadataEntry* entries, const std::uint32_t count) noexcept {
    if (count > 0 && entries == nullptr) return FFFResult::InvalidArgument;
    if (count > kMaxInjectedEntries) return FFFResult::InvalidArgument;
    std::vector<FFF3FPHdrDynamicMetadataEntry> validated;
    validated.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& entry = entries[index];
        if (entry.size < sizeof(FFF3FPHdrDynamicMetadataEntry) ||
            entry.version != 1) return FFFResult::InvalidArgument;
        // Validate through the same path the renderer uses, so a table that
        // would be rejected at render time is rejected here instead.
        HdrDynamicMetadata probe{};
        if (!BuildInjectedState(entry, probe)) return FFFResult::InvalidArgument;
        validated.push_back(entry);
    }
    // Ascending by frameIndex; the lookup walks backwards for "last <= frame".
    std::sort(validated.begin(), validated.end(),
        [](const FFF3FPHdrDynamicMetadataEntry& a,
           const FFF3FPHdrDynamicMetadataEntry& b) noexcept {
            return a.frameIndex < b.frameIndex;
        });
    std::lock_guard lock(mutex_);
    injected_ = std::move(validated);
    // Evaluate against the frame already on screen, so a paused or ended session
    // reports and applies the new table instead of the last bitstream parse.
    ReapplyInjectedMetadataLocked();
    return FFFResult::Success;
}

void HdrProcessor::ClearInjectedMetadata() noexcept {
    std::lock_guard lock(mutex_);
    injected_.clear();
    injectedActive_ = false;
    ReapplyInjectedMetadataLocked();
}

void HdrProcessor::ReapplyInjectedMetadataLocked() noexcept {
    auto next = haveUninjectedState_ ? lastUninjectedState_ : streamState_;
    ApplyInjectionLocked(lastLookupIndex_, next);
    frameState_ = next;
}

// Call with mutex_ held.
void HdrProcessor::ApplyInjectionLocked(const std::int64_t frameIndex,
    HdrFrameState& state) noexcept {
    // Only an indexed frame may move the key. A presentation timestamp is a
    // different space entirely (1/15360 at 60 fps puts frame 4 at pts 1024), so
    // storing one here made every later lookup match the table's last entry, and
    // a stream without pts matched its first. Callers that do not index the frame
    // (cover art, still images, warm-up) leave the last indexed entry in force.
    if (frameIndex != kUnknownFrameIndex) lastLookupIndex_ = frameIndex;
    // Injected metadata (FFF3FP_SetHdrDynamicMetadata) takes precedence over
    // whatever the bitstream supplied: the host asked for it explicitly, and a
    // test harness needs it to override real SEI. The lookup takes the last
    // entry whose frameIndex is <= this frame, so a sparse scene list works --
    // keyed on the frame index, which is what the API documents as the key.
    injectedActive_ = false;
    if (injected_.empty() || lastLookupIndex_ == kUnknownFrameIndex) return;
    // ST 2094-40 curves are defined for signals that carry their own transfer
    // characteristic. On an SDR-transfer source there is nothing to re-map, so the
    // table stays installed but inert and the source keeps reporting Bitstream --
    // claiming it here would move the diagnostics without moving the picture.
    if (state.format == FFF3FPHdrFormat::Sdr) return;
    const FFF3FPHdrDynamicMetadataEntry* chosen = nullptr;
    for (const auto& candidate : injected_) {
        if (candidate.frameIndex <= lastLookupIndex_) chosen = &candidate;
        else break;
    }
    if (chosen == nullptr) chosen = &injected_.front();
    const auto previousDynamic = state.dynamic;
    if (!BuildInjectedState(*chosen, state.dynamic)) {
        state.dynamic = previousDynamic;
        return;
    }
    // Deliberately not state.format: the injection owns the metadata in force, not
    // the classification of the samples. Overwriting it made the decode transfer
    // function and the gamut switch read a signal the pixels are not.
    state.compatibility |= Compatibility(FFF3FPHdrCompatibility::Hdr10);
    state.processingPath = FFF3FPHdrProcessingPath::Hdr10PlusDynamic;
    state.dynamicMetadata = true;
    injectedActive_ = true;
    // The injected entry owns the peak: use the one it carries, otherwise fall back to the
    // stream-level baseline -- never to a peak this frame's bitstream parse wrote into the
    // frame state, which would leave the curve injected and the peak from the stream.
    if (const auto peak = ValidPeak(state.dynamic.windows[0].signalPeakNits);
        peak > 0.0f)
        state.sourcePeakNits = peak;
    else
        state.sourcePeakNits = streamState_.sourcePeakNits;
}

bool HdrProcessor::InjectedMetadataActive() const noexcept {
    std::lock_guard lock(mutex_);
    return injectedActive_;
}

void HdrProcessor::BuildDxgiHdr10Metadata(DXGI_HDR_METADATA_HDR10& metadata) const noexcept {
    const auto state = State();
    std::memset(&metadata, 0, sizeof(metadata));
    // The HDR swap chain is 16-bit scRGB (linear Rec.709 primaries, 1.0 = 80
    // nits). The shader converts the source to linear Rec.709, but the HDR10
    // metadata primaries still describe the *source* mastering display (usually
    // Rec.2020) rather than the output signal: DWM's scRGB compositor consumes
    // the luminance fields for tone mapping while the primaries document the
    // content. Keep mastering luminance and MaxCLL separate: HDR->SDR uses
    // MaxCLL as a tone-map bound, but HDR10 metadata expects the actual
    // mastering display peak when it is present.
    const auto& source = state.staticMetadata;
    metadata.RedPrimary[0] = DxgiChromaticity(source.redX);
    metadata.RedPrimary[1] = DxgiChromaticity(source.redY);
    metadata.GreenPrimary[0] = DxgiChromaticity(source.greenX);
    metadata.GreenPrimary[1] = DxgiChromaticity(source.greenY);
    metadata.BluePrimary[0] = DxgiChromaticity(source.blueX);
    metadata.BluePrimary[1] = DxgiChromaticity(source.blueY);
    metadata.WhitePoint[0] = DxgiChromaticity(source.whiteX);
    metadata.WhitePoint[1] = DxgiChromaticity(source.whiteY);
    const auto contentPeak = std::clamp(state.sourcePeakNits, 1.0f, 10000.0f);
    const auto masteringPeak = source.maximumMasteringLuminanceNits > 0.0f ?
        source.maximumMasteringLuminanceNits : contentPeak;
    auto maxCll = source.maximumContentLightLevelNits > 0.0f ?
        source.maximumContentLightLevelNits : contentPeak;
    auto maxFall = source.maximumFrameAverageLightLevelNits;
    // ST 2094 metadata describes one scene, so the content-light fields follow
    // it frame by frame -- that is precisely the information HDR10 adds over
    // HDR10. The mastering luminance stays static: it documents the display the
    // content was graded on, which does not change per frame.
    if (state.dynamic.kind != HdrMetadataKind::None && state.dynamic.windowCount > 0) {
        const auto& window = state.dynamic.windows[0];
        if (const auto dynamicPeak = window.signalPeakNits; dynamicPeak > 0.0f)
            maxCll = dynamicPeak;
        // average_maxrgb is a linearized RGB maximum, not a light level, so it
        // only serves as a frame-average bound when it is below the peak.
        if (const auto dynamicAverage = window.averageMaxRgb * 10000.0f;
            dynamicAverage > 0.0f && dynamicAverage < maxCll)
            maxFall = dynamicAverage;
    }
    metadata.MaxMasteringLuminance = DxgiNits(masteringPeak);
    metadata.MinMasteringLuminance = DxgiMinNits(source.minimumLuminanceNits);
    metadata.MaxContentLightLevel = DxgiContentLight(maxCll);
    metadata.MaxFrameAverageLightLevel = DxgiContentLight(maxFall);
}

float HdrProcessor::ResolveTargetPeak(const float overrideNits,
    const HdrDisplayCapabilities& display) noexcept {
    if (std::isfinite(overrideNits) && overrideNits > 0.0f)
        return std::clamp(overrideNits, 80.0f, 10000.0f);
    if (std::isfinite(display.maximumNits) && display.maximumNits > 0.0f)
        return std::clamp(display.maximumNits, 80.0f, 10000.0f);
    if (std::isfinite(display.maximumFullFrameNits) && display.maximumFullFrameNits > 0.0f)
        return std::clamp(display.maximumFullFrameNits, 80.0f, 10000.0f);
    return 1000.0f;
}

const char* HdrProcessor::FormatName(const FFF3FPHdrFormat format) noexcept {
    switch (format) {
    case FFF3FPHdrFormat::Hdr10: return "HDR10";
    case FFF3FPHdrFormat::Hdr10Plus: return "HDR10+";
    case FFF3FPHdrFormat::Hlg: return "HLG";
    case FFF3FPHdrFormat::DolbyVision:
        if (const auto* api = GetColorExtension(); api != nullptr) {
            if (const auto* text = api->getStatusText(FFFColorExtensionDolbyFormatText, 0); text != nullptr)
                return text;
        }
        return "Dolby Vision";
    case FFF3FPHdrFormat::HdrVivid: return "HDR Vivid";
    default: return "SDR";
    }
}

const char* HdrProcessor::ProcessingPathName(const FFF3FPHdrProcessingPath path) noexcept {
    switch (path) {
    case FFF3FPHdrProcessingPath::StaticHdr10: return "HDR10 static metadata";
    case FFF3FPHdrProcessingPath::Hdr10PlusDynamic: return "HDR10+ per-window tone mapping";
    case FFF3FPHdrProcessingPath::HlgDisplayMapped: return "HLG display mapping";
    case FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback: return "DolbyVisionFallback";
    case FFF3FPHdrProcessingPath::ExternalDynamic: return "ExternalDynamic";
    case FFF3FPHdrProcessingPath::DolbyVisionFelFallback: return "DolbyVisionFELFallback";
    case FFF3FPHdrProcessingPath::HdrVividDynamic: return "HDR Vivid metadata-guided display mapping";
    default: return "None";
    }
}

const char* HdrProcessor::EnhancementLayerName(
    const FFF3FPDolbyVisionEnhancementLayer layer) noexcept {
    switch (layer) {
    case FFF3FPDolbyVisionEnhancementLayer::Mel: return "MEL";
    case FFF3FPDolbyVisionEnhancementLayer::Fel: return "FEL";
    case FFF3FPDolbyVisionEnhancementLayer::Unknown: return "Unknown";
    default: return "None";
    }
}

std::string HdrProcessor::CompatibilityNames(const std::uint32_t compatibility) {
    std::string result;
    const auto append = [&result](const char* value) {
        if (!result.empty()) result += ", ";
        result += value;
    };
    if ((compatibility & Compatibility(FFF3FPHdrCompatibility::Hdr10)) != 0) append("HDR10");
    if ((compatibility & Compatibility(FFF3FPHdrCompatibility::Hlg)) != 0) append("HLG");
    if ((compatibility & Compatibility(FFF3FPHdrCompatibility::DolbyVision)) != 0)
        append(FormatName(FFF3FPHdrFormat::DolbyVision));
    if ((compatibility & Compatibility(FFF3FPHdrCompatibility::HdrVivid)) != 0) append("HDR Vivid");
    return result;
}

FFFResult HdrProcessor::EvaluateProbe(FFF3FPHdrProcessingProbe& probe) noexcept {
    if (probe.size < sizeof(probe) || probe.version != 1 ||
        probe.transfer > FFF3FPColorTransfer::Hlg ||
        probe.dolbyVisionProfile > 15 || probe.dolbyVisionLevel > 15 ||
        probe.dolbyVisionCompatibilityId > 15 || probe.dolbyVisionRpu > 1 ||
        probe.dolbyVisionEnhancementLayer > 1 || probe.dolbyVisionResidual > 2 ||
        probe.hdr10PlusMetadata > 1 || probe.hdrVividMetadata > 1 ||
        !std::isfinite(probe.displayPeakNits) || probe.displayPeakNits < 0.0f ||
        !std::isfinite(probe.displayFullFramePeakNits) ||
        probe.displayFullFramePeakNits < 0.0f ||
        !std::isfinite(probe.targetPeakOverrideNits) ||
        probe.targetPeakOverrideNits < 0.0f || probe.targetPeakOverrideNits > 10000.0f)
        return FFFResult::InvalidArgument;

    HdrFrameState state{};
    if (probe.dolbyVisionProfile > 0) {
        AVDOVIDecoderConfigurationRecord configuration{};
        configuration.dv_profile = static_cast<std::uint8_t>(probe.dolbyVisionProfile);
        configuration.dv_level = static_cast<std::uint8_t>(probe.dolbyVisionLevel);
        configuration.dv_bl_signal_compatibility_id =
            static_cast<std::uint8_t>(probe.dolbyVisionCompatibilityId);
        configuration.rpu_present_flag = static_cast<std::uint8_t>(probe.dolbyVisionRpu);
        configuration.el_present_flag =
            static_cast<std::uint8_t>(probe.dolbyVisionEnhancementLayer);
        ClassifyDolbyVision(&configuration, state);
        if (state.hasEnhancementLayer && probe.dolbyVisionResidual != 0) {
            state.enhancementLayer = probe.dolbyVisionResidual == 1 ?
                FFF3FPDolbyVisionEnhancementLayer::Mel :
                FFF3FPDolbyVisionEnhancementLayer::Fel;
            if (state.enhancementLayer == FFF3FPDolbyVisionEnhancementLayer::Fel)
                state.processingPath = FFF3FPHdrProcessingPath::DolbyVisionFelFallback;
        }
    } else if (probe.hdrVividMetadata != 0) {
        state.format = FFF3FPHdrFormat::HdrVivid;
        state.compatibility = Compatibility(FFF3FPHdrCompatibility::HdrVivid) |
            Compatibility(FFF3FPHdrCompatibility::Hdr10);
        state.processingPath = FFF3FPHdrProcessingPath::HdrVividDynamic;
        state.dynamicMetadata = true;
    } else if (probe.hdr10PlusMetadata != 0) {
        state.format = FFF3FPHdrFormat::Hdr10Plus;
        state.compatibility = Compatibility(FFF3FPHdrCompatibility::Hdr10);
        state.processingPath = FFF3FPHdrProcessingPath::Hdr10PlusDynamic;
        state.dynamicMetadata = true;
    } else if (probe.transfer == FFF3FPColorTransfer::Hlg) {
        state.format = FFF3FPHdrFormat::Hlg;
        state.compatibility = Compatibility(FFF3FPHdrCompatibility::Hlg);
        state.processingPath = FFF3FPHdrProcessingPath::HlgDisplayMapped;
    } else if (probe.transfer == FFF3FPColorTransfer::Pq) {
        state.format = FFF3FPHdrFormat::Hdr10;
        state.compatibility = Compatibility(FFF3FPHdrCompatibility::Hdr10);
        state.processingPath = FFF3FPHdrProcessingPath::StaticHdr10;
    }
    state.display.supported = probe.displayPeakNits > 0.0f;
    state.display.maximumNits = probe.displayPeakNits;
    state.display.maximumFullFrameNits = probe.displayFullFramePeakNits;
    state.targetPeakNits = ResolveTargetPeak(probe.targetPeakOverrideNits, state.display);

    probe.outputFormat = state.format;
    probe.outputCompatibility = state.compatibility;
    probe.outputProcessingPath = state.processingPath;
    probe.outputEnhancementLayer = state.enhancementLayer;
    probe.outputDynamicMetadata = state.dynamicMetadata ? 1u : 0u;
    probe.outputFallback = state.fallback ? 1u : 0u;
    probe.outputTargetPeakNits = static_cast<std::uint32_t>(std::lround(state.targetPeakNits));
    return FFFResult::Success;
}
