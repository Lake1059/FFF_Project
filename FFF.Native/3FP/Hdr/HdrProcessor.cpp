#include "pch.h"
#include "3FP/Hdr/HdrProcessor.h"
#include "3FP/Hdr/HdrMetadataParse.h"

extern "C" {
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hdr_dynamic_vivid_metadata.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
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

// Separate, much smaller cap for kind-3 (external colour transform) entries:
// each one carries a ~421 KB 3-D LUT, so the generic 4096-entry cap would let a
// host ask the kernel to retain ~1.7 GB of tables. Sixteen entries is far more
// than any scene list needs and keeps the worst case under 7 MB.
constexpr std::uint32_t kMaxExternalTransformEntries = 16;

// Kind-3 payload validation. The kernel never interprets the table's contents
// -- what produced it is the caller's business -- but it does refuse shapes it
// cannot sample: a wrong grid resolution, an unknown domain, or a non-finite
// matrix would produce garbage pixels rather than a clean rejection.
bool ValidColorTransform(const FFF3FPColorTransformPayload& payload) noexcept {
    if (payload.size != sizeof(FFF3FPColorTransformPayload) || payload.version != 1)
        return false;
    if (payload.lutResolution != kColorTransformLutResolution) return false;
    if (payload.lutDomain > 1u) return false;
    for (const float value : payload.outputMatrix)
        if (!std::isfinite(value)) return false;
    for (std::uint32_t index = 0; index < kColorTransformLutResolution *
        kColorTransformLutResolution * kColorTransformLutResolution * 3; ++index)
        if (!std::isfinite(payload.curveLut[index])) return false;
    return true;
}

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
    } else if (previous <= source.kneePointY) {
        // Anchors are validated non-decreasing from the knee, so this fires exactly
        // when the declared curve never rises above it: a flat line. Every Bezier
        // segment then has both endpoints and both slopes at zero, which maps the
        // whole signal range to 0 -- with a typical 11 to 12 anchor preset that is
        // about 91% of the 256-row curve texture. The bitstream parser keeps its own
        // tolerance (shipped HDR10+ legitimately uses the no-curve shape above);
        // this gate is on the host-supplied API, which already promises a malformed
        // entry is rejected rather than applied.
        return false;
    }
    return true;
}
}

void HdrProcessor::ConfigureStream(const AVCodecParameters* parameters) noexcept {
    HdrFrameState next{};
    if (parameters != nullptr) {
        if (av_packet_side_data_get(parameters->coded_side_data,
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

    // HDR Vivid carries no transfer-function of its own: `frame->color_trc` is
    // AVCOL_TRC_UNSPECIFIED on real Vivid streams (verified with ffprobe on the
    // FATE Vivid and HDR10+ vectors -- stream-level color_transfer is populated,
    // frame-level color_trc is not). The baseline is therefore taken from the
    // stream-level classification that ConfigureStream() stored in streamState_
    // (for the FATE Vivid vector that is Hlg(3), which is correct).
    //
    // Deliberately read a single scalar (`format`) rather than the accumulating
    // `compatibility` bit set. `compatibility` is OR-ed in several places (e.g.
    // the Vivid branch below ORs in the Hdr10 bit) and
    // `next` inherits it from streamState_, so testing the Hlg *bit* here could
    // pin a genuinely PQ-baseline stream to the HLG branch. `format` is assigned,
    // never OR-ed, so it cannot go sticky.
    //
    // Must be captured BEFORE the Vivid branch below re-labels next.format, and it must
    // read `next`: the per-frame B67 classification above writes `next.format` only, while
    // `streamState_` is refreshed on stream configure, so reading the stream-level copy here
    // left the gate false for any stream whose container omits color_trc.
    const bool vividBaselineIsHlg = next.format == FFF3FPHdrFormat::Hlg;

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
            // 1/4095 grid; the FFmpeg header documents no unit for it. The
            // absolute-domain reading below (x10000) is only self-consistent for
            // the PQ baseline, where PQ EOTF(1.0) == 10000 cd/m2 and the value sits
            // near full code. It is NOT valid for an HLG baseline, which is
            // relative/scene-referred: a full-code HLG signal means "1000 cd/m2 on
            // the nominal BT.2100 reference display", not 10000. Applying the
            // absolute reading there inflated sourcePeakNits by ~10x and, together
            // with the transfer-function mix-up, drove highlight blow-out.
            //
            // HLG baseline therefore keeps the relative-domain reference peak. The
            // pre-Vivid HLG path already set exactly this value (see the HLG branch
            // above, sourcePeakNits = 1000.0f); we simply stop overwriting it.
            if (!vividBaselineIsHlg) {
                const auto peak = ValidPeak(av_q2d(vivid->params[0].maximum_maxrgb) * 10000.0);
                if (peak > 0.0f) {
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
        // the previous snapshot, a frame whose fresh parse carries no usable
        // peak would fall back to the static peak and the picture would flicker
        // between two mappings. Hold the last snapshot instead, but only for a
        // bounded number of frames: past that the stream is treated as not
        // carrying dynamic metadata at all, which also stops a stale snapshot
        // surviving a seek.
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
    activeTransform_ = nullptr;
}

HdrFrameState HdrProcessor::State() const noexcept {
    std::lock_guard lock(mutex_);
    return frameState_;
}

bool HdrProcessor::IsHdrSource() const noexcept { return State().format != FFF3FPHdrFormat::Sdr; }

bool HdrProcessor::BuildInjectedStateVivid(const FFF3FPHdrVividMetadataPayload& payload,
    HdrDynamicMetadata& metadata) noexcept {
    metadata = HdrDynamicMetadata{};
    metadata.kind = HdrMetadataKind::HdrVivid;
    // ST 2094-30 allows num_windows in [1,3] just like ST 2094-40; anything
    // else is out of spec and rejected rather than guessed at.
    if (payload.numWindows < 1 || payload.numWindows > kHdrMaxProcessingWindows)
        return false;
    metadata.windowCount = payload.numWindows;
    for (std::uint32_t index = 0; index < payload.numWindows; ++index) {
        const auto& source = payload.windows[index];
        auto& window = metadata.windows[index];
        // Mirror FromVivid exactly: full-frame windows, peak-only reading. The
        // CUVA 005.1 curve is a parametric spline the kernel does not interpret
        // (the specification is not publicly available), so the window is
        // deliberately left without a tone-mapping function and the honest
        // ToneMappingAbsent degrade rides along -- exactly what a bitstream
        // Vivid frame produces.
        if (!std::isfinite(source.averageMaxRGB) || source.averageMaxRGB < 0.0f ||
            source.averageMaxRGB > 1.0f)
            return false;
        float maximumMaxRgb = 0.0f;
        for (int channel = 0; channel < 3; ++channel) {
            if (!std::isfinite(source.maxRGB[channel]) || source.maxRGB[channel] < 0.0f ||
                source.maxRGB[channel] > 1.0f)
                return false;
            window.maxScl[channel] = source.maxRGB[channel];
            maximumMaxRgb = std::max(maximumMaxRgb, source.maxRGB[channel]);
        }
        window.valid = true;
        window.upperLeftX = 0.0f;
        window.upperLeftY = 0.0f;
        window.lowerRightX = 1.0f;
        window.lowerRightY = 1.0f;
        window.averageMaxRgb = source.averageMaxRGB;
        window.signalPeakNits = HdrMetadataParse::LinearizedToNits(maximumMaxRgb);
        window.toneMappingPresent = false;
        metadata.degrade = HdrMetadataDegrade::ToneMappingAbsent;
    }
    if (std::isfinite(payload.targetedDisplayNits) && payload.targetedDisplayNits > 0.0f) {
        // Same units and clamps as the bitstream Vivid path applies.
        metadata.targetedDisplayNits =
            std::clamp(payload.targetedDisplayNits, 1.0f, 10000.0f);
        metadata.hasTargetedDisplay = true;
    }
    return true;
}

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

    // The header promises windows[0] spans the picture
    // when numWindows == 1, and the renderer is free to ignore the rectangle below
    // two windows -- so a narrower single window is a contract violation, not an
    // alternative reading. Reject it instead of accepting a table whose geometry is
    // silently dropped. Checked after the per-window build so the coordinates are
    // already known finite and in range.
    if (entry.numWindows == 1) {
        const auto& only = metadata.windows[0];
        if (!(only.upperLeftX <= 1.0e-4f && only.upperLeftY <= 1.0e-4f &&
              only.lowerRightX >= 1.0f - 1.0e-4f &&
              only.lowerRightY >= 1.0f - 1.0e-4f)) return false;
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
    std::vector<InjectedEntry> validated;
    validated.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& entry = entries[index];
        if (entry.size < sizeof(FFF3FPHdrDynamicMetadataEntry) ||
            entry.version != 1) return FFFResult::InvalidArgument;
        // Validate through the same path the renderer uses, so a table that
        // would be rejected at render time is rejected here instead.
        HdrDynamicMetadata probe{};
        if (!BuildInjectedState(entry, probe)) return FFFResult::InvalidArgument;
        InjectedEntry tagged;
        tagged.kind = HdrMetadataKind::Hdr10Plus;
        tagged.frameIndex = entry.frameIndex;
        tagged.hdr10Plus = entry;
        validated.push_back(std::move(tagged));
    }
    return InstallInjectedTable(std::move(validated));
}

// The generic carrier: same table, same matching rule, same rejection semantics,
// one kind tag per packet. Unknown kinds fail the whole call with NotSupported
// so a newer host talking to an older kernel degrades cleanly instead of
// half-applying something the kernel cannot interpret.
FFFResult HdrProcessor::SetInjectedMetadataPackets(
    const FFF3FPDynamicMetadataPacket* packets, const std::uint32_t count) noexcept {
    if (count > 0 && packets == nullptr) return FFFResult::InvalidArgument;
    if (count > kMaxInjectedEntries) return FFFResult::InvalidArgument;
    std::vector<InjectedEntry> validated;
    validated.reserve(count);
    std::uint32_t transformCount = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& packet = packets[index];
        if (packet.size < sizeof(FFF3FPDynamicMetadataPacket) || packet.version != 1 ||
            packet.reserved != 0)
            return FFFResult::InvalidArgument;
        // payloadSize is gated by equality, not by a smaller-prefix tolerance.
        // The header states that kindVersion fixes the exact layout, but a `<` test
        // let a larger struct through while the kernel copied only sizeof(...)
        // bytes: a newer host got Success and saw an
        // older shape mapped. The enclosing size/version/reserved checks keep
        // their >= reading, so appended packet fields stay forward-compatible.
        if (packet.kind == static_cast<std::uint32_t>(HdrMetadataKind::Hdr10Plus)) {
            if (packet.kindVersion != 1 || packet.payload == nullptr ||
                packet.payloadSize != sizeof(FFF3FPHdrDynamicMetadataEntry))
                return FFFResult::InvalidArgument;
            FFF3FPHdrDynamicMetadataEntry entry{};
            std::memcpy(&entry, packet.payload, sizeof(entry));
            HdrDynamicMetadata probe{};
            if (!BuildInjectedState(entry, probe)) return FFFResult::InvalidArgument;
            InjectedEntry tagged;
            tagged.kind = HdrMetadataKind::Hdr10Plus;
            tagged.frameIndex = packet.frameIndex;
            tagged.hdr10Plus = entry;
            validated.push_back(std::move(tagged));
        } else if (packet.kind == static_cast<std::uint32_t>(HdrMetadataKind::HdrVivid)) {
            if (packet.kindVersion != 1 || packet.payload == nullptr ||
                packet.payloadSize != sizeof(FFF3FPHdrVividMetadataPayload))
                return FFFResult::InvalidArgument;
            FFF3FPHdrVividMetadataPayload payload{};
            std::memcpy(&payload, packet.payload, sizeof(payload));
            HdrDynamicMetadata probe{};
            if (!BuildInjectedStateVivid(payload, probe)) return FFFResult::InvalidArgument;
            InjectedEntry tagged;
            tagged.kind = HdrMetadataKind::HdrVivid;
            tagged.frameIndex = packet.frameIndex;
            tagged.vivid = payload;
            validated.push_back(std::move(tagged));
        } else if (packet.kind == static_cast<std::uint32_t>(HdrMetadataKind::ExternalColorTransform)) {
            if (packet.kindVersion != 1 || packet.payload == nullptr ||
                packet.payloadSize != sizeof(FFF3FPColorTransformPayload))
                return FFFResult::InvalidArgument;
            if (transformCount >= kMaxExternalTransformEntries)
                return FFFResult::InvalidArgument;
            auto payload = std::make_shared<FFF3FPColorTransformPayload>();
            std::memcpy(payload.get(), packet.payload, sizeof(*payload));
            if (!ValidColorTransform(*payload)) return FFFResult::InvalidArgument;
            ++transformCount;
            InjectedEntry tagged;
            tagged.kind = HdrMetadataKind::ExternalColorTransform;
            tagged.frameIndex = packet.frameIndex;
            tagged.colorTransform = std::move(payload);
            validated.push_back(std::move(tagged));
        } else {
            return FFFResult::NotSupported;
        }
    }
    return InstallInjectedTable(std::move(validated));
}

FFFResult HdrProcessor::InstallInjectedTable(std::vector<InjectedEntry>&& validated) noexcept {
    // Ascending by frameIndex; the lookup walks for "last <= frame".
    std::sort(validated.begin(), validated.end(),
        [](const InjectedEntry& a, const InjectedEntry& b) noexcept {
            return a.frameIndex < b.frameIndex;
        });
    std::lock_guard lock(mutex_);
    injected_ = std::move(validated);
    // Evaluate against the frame already on screen, so a paused or ended session
    // reports and applies the new table instead of the last bitstream parse.
    ReapplyInjectedMetadataLocked();
    return FFFResult::Success;
}

void HdrProcessor::ClearInjectedMetadata(const std::uint32_t kind) noexcept {
    std::lock_guard lock(mutex_);
    if (kind == 0) {
        injected_.clear();
        injectedActive_ = false;
    } else {
        // Selective clear: unknown kinds match nothing and are a no-op. The
        // remaining table is re-evaluated against the frame on screen.
        const auto wanted = static_cast<HdrMetadataKind>(kind);
        injected_.erase(std::remove_if(injected_.begin(), injected_.end(),
                            [wanted](const InjectedEntry& entry) noexcept {
                                return entry.kind == wanted;
                            }),
            injected_.end());
    }
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
    activeTransform_ = nullptr;
    if (injected_.empty() || lastLookupIndex_ == kUnknownFrameIndex) return;
    const InjectedEntry* chosen = nullptr;
    for (const auto& candidate : injected_) {
        if (candidate.frameIndex <= lastLookupIndex_) chosen = &candidate;
        else break;
    }
    if (chosen == nullptr) chosen = &injected_.front();
    // ST 2094-40 curves are defined for signals that carry their own transfer
    // characteristic. On an SDR-transfer source there is nothing to re-map, so the
    // table stays installed but inert and the source keeps reporting Bitstream --
    // claiming it here would move the diagnostics without moving the picture.
    // A caller-supplied colour transform is exempt: grading a table onto an SDR
    // picture is a legitimate use (creative LUTs do exactly that), and the table's
    // own domain field says where in the pipeline it belongs.
    if (state.format == FFF3FPHdrFormat::Sdr &&
        chosen->kind != HdrMetadataKind::ExternalColorTransform)
        return;
    if (chosen->kind == HdrMetadataKind::ExternalColorTransform) {
        // Kind 3 owns the frame state wholesale: no windows, no curve, no peak of
        // its own. The source keeps its own classification and peak (an external
        // table maps colour, it does not re-grade luminance), and no compatibility
        // bit is set because the transform is not a metadata format the stream
        // could be tagged with.
        state.dynamic = HdrDynamicMetadata{};
        state.dynamic.kind = HdrMetadataKind::ExternalColorTransform;
        state.dynamic.externalTransform = true;
        state.processingPath = FFF3FPHdrProcessingPath::ExternalDynamic;
        state.dynamicMetadata = true;
        injectedActive_ = true;
        activeTransform_ = chosen->colorTransform;
        return;
    }
    const auto previousDynamic = state.dynamic;
    // The kind decides which builder validates the packet and which processing
    // path is reported. Both builders share the ST 2094-40 window representation.
    const bool built = chosen->kind == HdrMetadataKind::HdrVivid
        ? BuildInjectedStateVivid(chosen->vivid, state.dynamic)
        : BuildInjectedState(chosen->hdr10Plus, state.dynamic);
    if (!built) {
        state.dynamic = previousDynamic;
        return;
    }
    // Deliberately not state.format: the injection owns the metadata in force, not
    // the classification of the samples. Overwriting it made the decode transfer
    // function and the gamut switch read a signal the pixels are not. This holds
    // for every kind -- an injected Vivid entry on an HDR10 stream reports
    // HdrVividDynamic as the path while the samples stay classified as they are.
    state.compatibility |= chosen->kind == HdrMetadataKind::HdrVivid
        ? Compatibility(FFF3FPHdrCompatibility::HdrVivid)
        : Compatibility(FFF3FPHdrCompatibility::Hdr10);
    state.processingPath = chosen->kind == HdrMetadataKind::HdrVivid
        ? FFF3FPHdrProcessingPath::HdrVividDynamic
        : FFF3FPHdrProcessingPath::Hdr10PlusDynamic;
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

bool HdrProcessor::ActiveColorTransform(
    std::shared_ptr<const FFF3FPColorTransformPayload>& out) const noexcept {
    std::lock_guard lock(mutex_);
    out = activeTransform_;
    return out != nullptr;
}

bool HdrProcessor::RequiresMetadataAwareShader() const noexcept {
    const auto state = State();
    return state.dynamicMetadata || state.format == FFF3FPHdrFormat::Hlg ||
        state.format == FFF3FPHdrFormat::HdrVivid;
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
    // it frame by frame -- that is precisely the information HDR10+ adds over
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

void HdrProcessor::BuildTrueHdrMetadata(
    DXGI_HDR_METADATA_HDR10& metadata) const noexcept {
    std::memset(&metadata, 0, sizeof(metadata));
    // BT.2020 primaries and D65 white, per the TrueHDR output contract.
    metadata.RedPrimary[0] = DxgiChromaticity(0.708f);
    metadata.RedPrimary[1] = DxgiChromaticity(0.292f);
    metadata.GreenPrimary[0] = DxgiChromaticity(0.170f);
    metadata.GreenPrimary[1] = DxgiChromaticity(0.797f);
    metadata.BluePrimary[0] = DxgiChromaticity(0.131f);
    metadata.BluePrimary[1] = DxgiChromaticity(0.046f);
    metadata.WhitePoint[0] = DxgiChromaticity(0.3127f);
    metadata.WhitePoint[1] = DxgiChromaticity(0.3290f);
    metadata.MaxMasteringLuminance = DxgiNits(1000.0f);
    metadata.MinMasteringLuminance = DxgiMinNits(0.0001f);
    metadata.MaxContentLightLevel = DxgiContentLight(1000.0f);
    // The frame-average level is left at 0 (unknown) on purpose: claiming a
    // bright average would push DWM's tone map to over-brighten midtones for a
    // number the driver does not actually publish. A compositor that ignores
    // the block because of it lands on the un-signalled behaviour, which is
    // exactly today's rendering -- a missing bound must never look worse than
    // no block at all.
    metadata.MaxFrameAverageLightLevel = DxgiContentLight(0.0f);
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
    case FFF3FPHdrFormat::HdrVivid: return "HDR Vivid";
    default: return "SDR";
    }
}

const char* HdrProcessor::ProcessingPathName(const FFF3FPHdrProcessingPath path) noexcept {
    switch (path) {
    case FFF3FPHdrProcessingPath::StaticHdr10: return "HDR10 static metadata";
    case FFF3FPHdrProcessingPath::Hdr10PlusDynamic: return "HDR10+ per-window tone mapping";
    case FFF3FPHdrProcessingPath::HlgDisplayMapped: return "HLG display mapping";
    case FFF3FPHdrProcessingPath::ExternalDynamic: return "Caller-supplied colour transform";
    case FFF3FPHdrProcessingPath::HdrVividDynamic: return "HDR Vivid dynamic peak mapping";
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
    if ((compatibility & Compatibility(FFF3FPHdrCompatibility::HdrVivid)) != 0) append("HDR Vivid");
    return result;
}

FFFResult HdrProcessor::EvaluateProbe(FFF3FPHdrProcessingProbe& probe) noexcept {
    if (probe.size < sizeof(probe) || probe.version != 20 ||
        probe.transfer > FFF3FPColorTransfer::Hlg ||
        probe.hdr10PlusMetadata > 1 || probe.hdrVividMetadata > 1 ||
        !std::isfinite(probe.displayPeakNits) || probe.displayPeakNits < 0.0f ||
        !std::isfinite(probe.displayFullFramePeakNits) ||
        probe.displayFullFramePeakNits < 0.0f ||
        !std::isfinite(probe.targetPeakOverrideNits) ||
        probe.targetPeakOverrideNits < 0.0f || probe.targetPeakOverrideNits > 10000.0f)
        return FFFResult::InvalidArgument;

    HdrFrameState state{};
    // A stream carrying external metadata is no longer classified here: its base layer is
    // presented as plain HDR10 through the transfer branch below, and any
    // stream-specific handling belongs to the FFmpeg patch layer, not the player.
    if (probe.hdrVividMetadata != 0) {
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
    probe.outputEnhancementLayer = FFF3FPHDRdpEnhancementLayer::None;  // deprecated field; no layer is ever reported
    probe.outputDynamicMetadata = state.dynamicMetadata ? 1u : 0u;
    probe.outputFallback = state.fallback ? 1u : 0u;
    probe.outputTargetPeakNits = static_cast<std::uint32_t>(std::lround(state.targetPeakNits));
    return FFFResult::Success;
}
