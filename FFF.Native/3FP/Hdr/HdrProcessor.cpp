#include "pch.h"
#include "3FP/Hdr/HdrProcessor.h"

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

float ValidPeak(const double value) noexcept {
    return std::isfinite(value) && value > 0.0 ?
        static_cast<float>(std::clamp(value, 1.0, 10000.0)) : 0.0f;
}

float PqCodeToNits(const std::uint16_t code) noexcept {
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 32.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 128.0;
    constexpr double c3 = 2392.0 / 128.0;
    const auto value = std::clamp(static_cast<double>(code) / 4095.0, 0.0, 1.0);
    const auto powered = std::pow(value, 1.0 / m2);
    const auto denominator = std::max(c2 - c3 * powered, 1.0e-9);
    return ValidPeak(10000.0 * std::pow(std::max(powered - c1, 0.0) / denominator, 1.0 / m1));
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
    // The open-source build may identify Dolby metadata, but it is not a Dolby
    // licensed implementation. Convert every profile to the public Rec.2020/PQ
    // HDR10 output contract and never advertise Dolby Vision or HDR10+ output.
    // P5 has no HDR10-compatible base layer, so that conversion is best effort.
    state.processingPath = FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback;
    state.fallback = true;
}

float ResolveStaticSourcePeak(const AVFrame* frame, const float fallback) noexcept {
    const auto* lightData = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (lightData != nullptr && lightData->size >= sizeof(AVContentLightMetadata)) {
        const auto* light = reinterpret_cast<const AVContentLightMetadata*>(lightData->data);
        if (light->MaxCLL > 0) return ValidPeak(light->MaxCLL);
    }
    const auto* masteringData = av_frame_get_side_data(
        frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (masteringData != nullptr && masteringData->size >= sizeof(AVMasteringDisplayMetadata)) {
        const auto* mastering = reinterpret_cast<const AVMasteringDisplayMetadata*>(masteringData->data);
        if (mastering->has_luminance) {
            const auto peak = ValidPeak(av_q2d(mastering->max_luminance));
            if (peak > 0.0f) return peak;
        }
    }
    return fallback;
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
    }
    std::lock_guard lock(mutex_);
    next.display = frameState_.display;
    next.targetPeakNits = ResolveTargetPeak(targetPeakOverrideNits_, next.display);
    streamState_ = next;
    frameState_ = next;
}

HdrFrameState HdrProcessor::ProcessFrame(const AVFrame* frame,
    const float targetPeakOverrideNits, const float paperWhiteNits) noexcept {
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

    if (const auto* vividData = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_VIVID);
        vividData != nullptr && vividData->size >= sizeof(AVDynamicHDRVivid)) {
        const auto* vivid = reinterpret_cast<const AVDynamicHDRVivid*>(vividData->data);
        next.format = FFF3FPHdrFormat::HdrVivid;
        next.compatibility |= Compatibility(FFF3FPHdrCompatibility::HdrVivid) |
            Compatibility(FFF3FPHdrCompatibility::Hdr10);
        next.processingPath = FFF3FPHdrProcessingPath::HdrVividDynamic;
        next.dynamicMetadata = true;
        if (vivid->num_windows > 0) {
            const auto peak = ValidPeak(av_q2d(vivid->params[0].maximum_maxrgb) * 10000.0);
            if (peak > 0.0f) next.sourcePeakNits = peak;
        }
    } else if (const auto* doviData = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        doviData != nullptr && doviData->size >= sizeof(AVDOVIMetadata)) {
        const auto* metadata = reinterpret_cast<const AVDOVIMetadata*>(doviData->data);
        const auto* header = av_dovi_get_header(metadata);
        next.format = FFF3FPHdrFormat::DolbyVision;
        next.compatibility |= Compatibility(FFF3FPHdrCompatibility::DolbyVision);
        next.hasRpu = true;
        // RPU is retained for diagnostics/FEL identification only. Applying it
        // as Dolby Vision requires a separately licensed system implementation.
        next.dynamicMetadata = false;
        if (header != nullptr && next.hasEnhancementLayer) {
            next.enhancementLayer = header->disable_residual_flag != 0 ?
                FFF3FPDolbyVisionEnhancementLayer::Mel :
                FFF3FPDolbyVisionEnhancementLayer::Fel;
            if (next.enhancementLayer == FFF3FPDolbyVisionEnhancementLayer::Fel) {
                next.processingPath = FFF3FPHdrProcessingPath::DolbyVisionFelFallback;
                next.fallback = true;
            } else {
                next.processingPath = FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback;
            }
        } else {
            next.processingPath = FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback;
        }
        if (const auto* level1 = av_dovi_find_level(metadata, 1); level1 != nullptr) {
            const auto peak = PqCodeToNits(level1->l1.max_pq);
            if (peak > 0.0f) next.sourcePeakNits = peak;
        } else if (const auto* color = av_dovi_get_color(metadata); color != nullptr) {
            const auto peak = PqCodeToNits(color->source_max_pq);
            if (peak > 0.0f) next.sourcePeakNits = peak;
        }
    } else if (const auto* plusData = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
        plusData != nullptr && plusData->size >= sizeof(AVDynamicHDRPlus)) {
        const auto* plus = reinterpret_cast<const AVDynamicHDRPlus*>(plusData->data);
        next.format = FFF3FPHdrFormat::Hdr10Plus;
        next.compatibility |= Compatibility(FFF3FPHdrCompatibility::Hdr10);
        next.processingPath = FFF3FPHdrProcessingPath::Hdr10PlusDynamic;
        next.dynamicMetadata = true;
        if (plus->num_windows > 0) {
            const auto& window = plus->params[0];
            auto maximum = 0.0;
            for (const auto& channel : window.maxscl)
                maximum = std::max(maximum, av_q2d(channel));
            const auto peak = ValidPeak(maximum * 10000.0);
            if (peak > 0.0f) next.sourcePeakNits = peak;
        }
    }

    if (next.format != FFF3FPHdrFormat::Sdr) {
        next.sourcePeakNits = ResolveStaticSourcePeak(frame, next.sourcePeakNits);
        next.sourcePeakNits = std::clamp(next.sourcePeakNits,
            std::max(1.0f, paperWhiteNits), 10000.0f);
    }
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
}

HdrFrameState HdrProcessor::State() const noexcept {
    std::lock_guard lock(mutex_);
    return frameState_;
}

bool HdrProcessor::IsHdrSource() const noexcept { return State().format != FFF3FPHdrFormat::Sdr; }

bool HdrProcessor::RequiresMetadataAwareShader() const noexcept {
    const auto state = State();
    return state.dynamicMetadata || state.format == FFF3FPHdrFormat::Hlg ||
        state.format == FFF3FPHdrFormat::DolbyVision ||
        state.format == FFF3FPHdrFormat::HdrVivid;
}

void HdrProcessor::BuildDxgiHdr10Metadata(DXGI_HDR_METADATA_HDR10& metadata) const noexcept {
    const auto state = State();
    std::memset(&metadata, 0, sizeof(metadata));
    // The shader output contract is always Rec.2020/PQ, regardless of the
    // source HDR specification. Advertise the actual display-mapped range.
    metadata.RedPrimary[0] = 35400; metadata.RedPrimary[1] = 14600;
    metadata.GreenPrimary[0] = 8500; metadata.GreenPrimary[1] = 39850;
    metadata.BluePrimary[0] = 6550; metadata.BluePrimary[1] = 2300;
    metadata.WhitePoint[0] = 15635; metadata.WhitePoint[1] = 16450;
    const auto peak = std::clamp(state.targetPeakNits, 1.0f, 10000.0f);
    const auto minimum = std::clamp(state.display.minimumNits, 0.0f, peak);
    metadata.MaxMasteringLuminance = static_cast<UINT>(std::lround(peak * 10000.0f));
    metadata.MinMasteringLuminance = static_cast<UINT>(std::lround(minimum * 10000.0f));
    metadata.MaxContentLightLevel = static_cast<USHORT>(std::lround(std::min(peak, 65535.0f)));
    const auto fullFrame = state.display.maximumFullFrameNits > 0.0f ?
        std::min(state.display.maximumFullFrameNits, peak) : peak;
    metadata.MaxFrameAverageLightLevel = static_cast<USHORT>(
        std::lround(std::min(fullFrame, 65535.0f)));
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
    case FFF3FPHdrFormat::DolbyVision: return "Dolby Vision";
    case FFF3FPHdrFormat::HdrVivid: return "HDR Vivid";
    default: return "SDR";
    }
}

const char* HdrProcessor::ProcessingPathName(const FFF3FPHdrProcessingPath path) noexcept {
    switch (path) {
    case FFF3FPHdrProcessingPath::StaticHdr10: return "HDR10 static metadata";
    case FFF3FPHdrProcessingPath::Hdr10PlusDynamic: return "HDR10+ metadata-guided display mapping";
    case FFF3FPHdrProcessingPath::HlgDisplayMapped: return "HLG display mapping";
    case FFF3FPHdrProcessingPath::DolbyVisionHdr10Fallback: return "Dolby Vision source -> HDR10-compatible fallback";
    case FFF3FPHdrProcessingPath::DolbyVisionFelFallback: return "Dolby Vision BL -> HDR10 fallback (FEL ignored)";
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
    if ((compatibility & Compatibility(FFF3FPHdrCompatibility::DolbyVision)) != 0) append("Dolby Vision");
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
