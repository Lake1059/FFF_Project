#include "pch.h"
#include "Ffmpeg/VideoMuxer.h"
#include "Audio/AudioTrackEncoder.h"
#include "Audio/AudioMixer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
}

#include <d3dcompiler.h>

#include <array>

namespace {
enum class VideoCodecFamily : std::uint8_t {
    Av1,
    Hevc,
    H264
};

enum class EncoderPresetFamily : std::uint8_t {
    SvtAv1,
    Nvenc,
    Qsv,
    Amf,
    X26x
};

enum class BitstreamColorMetadataPath : std::uint8_t {
    None,
    EncoderNative,
    H265MetadataBsf
};

struct ColorPipelineDefinition final {
    bool supported;
    RgbToYuvPath conversionPath;
    AVPixelFormat softwarePixelFormat;
    YuvBitPacking bitPacking;
    AVColorPrimaries colorPrimaries;
    AVColorTransferCharacteristic colorTransfer;
    AVColorSpace colorSpace;
    AVChromaLocation chromaLocation;
    AVColorRange colorRange;
    BitstreamColorMetadataPath bitstreamMetadataPath;
};

constexpr std::size_t ColorPipelineCount = 12;

constexpr ColorPipelineDefinition UnsupportedPipeline{
    false, RgbToYuvPath::None, AV_PIX_FMT_NONE, YuvBitPacking::EightBit,
    AVCOL_PRI_UNSPECIFIED, AVCOL_TRC_UNSPECIFIED, AVCOL_SPC_UNSPECIFIED,
    AVCHROMA_LOC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, BitstreamColorMetadataPath::None
};

constexpr ColorPipelineDefinition Sdr8Pipeline(const RgbToYuvPath path,
    const AVPixelFormat format, const AVChromaLocation chromaLocation,
    const BitstreamColorMetadataPath bitstreamMetadataPath =
        BitstreamColorMetadataPath::EncoderNative) noexcept {
    return { true, path, format, YuvBitPacking::EightBit, AVCOL_PRI_BT709,
        AVCOL_TRC_BT709, AVCOL_SPC_BT709, chromaLocation,
        AVCOL_RANGE_JPEG, bitstreamMetadataPath };
}

constexpr ColorPipelineDefinition Sdr10Pipeline(const RgbToYuvPath path,
    const AVPixelFormat format, const YuvBitPacking packing,
    const AVChromaLocation chromaLocation,
    const BitstreamColorMetadataPath bitstreamMetadataPath =
        BitstreamColorMetadataPath::EncoderNative) noexcept {
    return { true, path, format, packing, AVCOL_PRI_BT709,
        AVCOL_TRC_BT709, AVCOL_SPC_BT709, chromaLocation, AVCOL_RANGE_JPEG,
        bitstreamMetadataPath };
}

constexpr ColorPipelineDefinition Hdr10Pipeline(const RgbToYuvPath path,
    const AVPixelFormat format, const YuvBitPacking packing,
    const AVChromaLocation chromaLocation,
    const BitstreamColorMetadataPath bitstreamMetadataPath =
        BitstreamColorMetadataPath::EncoderNative) noexcept {
    return { true, path, format, packing, AVCOL_PRI_BT2020,
        AVCOL_TRC_SMPTE2084, AVCOL_SPC_BT2020_NCL, chromaLocation, AVCOL_RANGE_JPEG,
        bitstreamMetadataPath };
}

struct EncoderStrategy final {
    const char* name;
    VideoEncoderBackend backend;
    VideoCodecFamily codec;
    // Fixed order: SDR8, SDR10, HDR8, HDR10; within each group: 420, 444, 422.
    std::array<ColorPipelineDefinition, ColorPipelineCount> colorPipelines;
    EncoderPresetFamily presetFamily;
    const char* defaultPreset;
};

// Each encoder owns all twelve color combinations. Unsupported entries are intentional,
// reviewable records rather than gaps inferred from codec/backend conditionals. Subsampled
// 4:2:0 uses left chroma siting because FFmpeg can represent it in HEVC, H.264, AV1 and
// Matroska; the shader uses the matching sampling phase. FFmpeg's NVENC wrapper does not
// copy AVCodecContext::chroma_sample_location into the NVIDIA HEVC VUI fields, so the NVENC
// HEVC 4:2:0 records explicitly select h265_metadata to write the same phase into the SPS.
constexpr EncoderStrategy EncoderStrategies[] = {
    { "libsvtav1", VideoEncoderBackend::Software, VideoCodecFamily::Av1, {
        Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::SvtAv1, "8" },
    { "av1_nvenc", VideoEncoderBackend::Nvenc, VideoCodecFamily::Av1, {
        Sdr8Pipeline(RgbToYuvPath::D3D11VideoProcessor420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        Sdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Nvenc, "p4" },
    { "av1_qsv", VideoEncoderBackend::Qsv, VideoCodecFamily::Av1, {
        Sdr8Pipeline(RgbToYuvPath::D3D11VideoProcessor420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        Sdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Qsv, "medium" },
    { "av1_amf", VideoEncoderBackend::Amf, VideoCodecFamily::Av1, {
        Sdr8Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        Sdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Amf, "balanced" },
    { "libx265", VideoEncoderBackend::Software, VideoCodecFamily::Hevc, {
        Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P, AVCHROMA_LOC_LEFT), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P, AVCHROMA_LOC_UNSPECIFIED), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV422P, AVCHROMA_LOC_CENTER),
        Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_LEFT), Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_UNSPECIFIED), Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV422P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_CENTER),
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_LEFT), Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_UNSPECIFIED), Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV422P10, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_CENTER)
    }, EncoderPresetFamily::X26x, "medium" },
    { "hevc_nvenc", VideoEncoderBackend::Nvenc, VideoCodecFamily::Hevc, {
        Sdr8Pipeline(RgbToYuvPath::D3D11VideoProcessor420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT, BitstreamColorMetadataPath::H265MetadataBsf), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P, AVCHROMA_LOC_UNSPECIFIED), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_NV16, AVCHROMA_LOC_CENTER),
        Sdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT, BitstreamColorMetadataPath::H265MetadataBsf), Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P10MSB, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_UNSPECIFIED), Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_P210, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_CENTER),
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT, BitstreamColorMetadataPath::H265MetadataBsf), Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P10MSB, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_UNSPECIFIED), Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_P210, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_CENTER)
    }, EncoderPresetFamily::Nvenc, "p4" },
    { "hevc_qsv", VideoEncoderBackend::Qsv, VideoCodecFamily::Hevc, {
        Sdr8Pipeline(RgbToYuvPath::D3D11VideoProcessor420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_VUYX, AVCHROMA_LOC_UNSPECIFIED), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUYV422, AVCHROMA_LOC_CENTER),
        Sdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_XV30, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_UNSPECIFIED), Sdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_Y210, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_CENTER),
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_XV30, YuvBitPacking::TenBitLsb, AVCHROMA_LOC_UNSPECIFIED), Hdr10Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_Y210, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_CENTER)
    }, EncoderPresetFamily::Qsv, "medium" },
    { "hevc_amf", VideoEncoderBackend::Amf, VideoCodecFamily::Hevc, {
        Sdr8Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        Sdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        Hdr10Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_P010, YuvBitPacking::TenBitMsb, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Amf, "balanced" },
    { "libx264", VideoEncoderBackend::Software, VideoCodecFamily::H264, {
        Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV420P, AVCHROMA_LOC_LEFT), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P, AVCHROMA_LOC_UNSPECIFIED), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV422P, AVCHROMA_LOC_CENTER),
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::X26x, "medium" },
    { "h264_nvenc", VideoEncoderBackend::Nvenc, VideoCodecFamily::H264, {
        Sdr8Pipeline(RgbToYuvPath::D3D11VideoProcessor420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_YUV444P, AVCHROMA_LOC_UNSPECIFIED), Sdr8Pipeline(RgbToYuvPath::SoftwarePlanar, AV_PIX_FMT_NV16, AVCHROMA_LOC_CENTER),
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Nvenc, "p4" },
    { "h264_qsv", VideoEncoderBackend::Qsv, VideoCodecFamily::H264, {
        Sdr8Pipeline(RgbToYuvPath::D3D11VideoProcessor420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Qsv, "medium" },
    { "h264_amf", VideoEncoderBackend::Amf, VideoCodecFamily::H264, {
        Sdr8Pipeline(RgbToYuvPath::D3D11ComputeShader420, AV_PIX_FMT_NV12, AVCHROMA_LOC_LEFT), UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline,
        UnsupportedPipeline, UnsupportedPipeline, UnsupportedPipeline
    }, EncoderPresetFamily::Amf, "balanced" }
};

constexpr bool ValidateColorPipelineTable() noexcept {
    for (const auto& strategy : EncoderStrategies) {
        for (std::size_t index = 0; index < strategy.colorPipelines.size(); ++index) {
            const auto& pipeline = strategy.colorPipelines[index];
            if (!pipeline.supported) continue;
            if (pipeline.conversionPath == RgbToYuvPath::None ||
                pipeline.softwarePixelFormat == AV_PIX_FMT_NONE ||
                pipeline.colorPrimaries == AVCOL_PRI_UNSPECIFIED ||
                pipeline.colorTransfer == AVCOL_TRC_UNSPECIFIED ||
                pipeline.colorSpace == AVCOL_SPC_UNSPECIFIED) return false;
            const bool tenBitSlot = ((index / 3U) & 1U) != 0;
            if (tenBitSlot == (pipeline.bitPacking == YuvBitPacking::EightBit)) return false;
            const bool hdr8Slot = index >= 6U && index < 9U;
            if (hdr8Slot) return false;
            if (pipeline.bitstreamMetadataPath == BitstreamColorMetadataPath::None) return false;
            if (pipeline.colorRange != AVCOL_RANGE_JPEG) return false;
            if (pipeline.bitstreamMetadataPath == BitstreamColorMetadataPath::H265MetadataBsf &&
                (strategy.codec != VideoCodecFamily::Hevc || index % 3U != 0U ||
                    pipeline.chromaLocation != AVCHROMA_LOC_LEFT)) return false;
        }
    }
    return true;
}

static_assert(ValidateColorPipelineTable(),
    "Every supported color pipeline must be complete and match its bit-depth slot.");

struct SoftwareEncoderRuntimeStrategy final {
    const char* name;
    std::uint32_t maximumWorkerThreads;
    std::uint32_t defaultLookaheadFrames;
    std::uint32_t maximumLookaheadFrames;
};

// CPU encoders must leave scheduling capacity for capture, audio, UI, and the packet writer.
// The lookahead caps bound the amount of delayed work that a normal Stop must drain.
constexpr SoftwareEncoderRuntimeStrategy SoftwareRuntimeStrategies[] = {
    { "libsvtav1", 16, 16, 32 },
    { "libx265",   16, 16, 32 },
    { "libx264",   16, 16, 32 }
};

struct ResolvedEncoderPlan final {
    const EncoderStrategy* strategy = nullptr;
    const ColorPipelineDefinition* colorPipeline = nullptr;
};

const char* SamplingName(const std::uint32_t chromaSampling) noexcept {
    return chromaSampling == 0 ? "4:2:0" : (chromaSampling == 1 ? "4:4:4" : "4:2:2");
}

constexpr std::size_t ColorPipelineIndex(const bool tenBit, const bool hdr10,
    const std::uint32_t chromaSampling) noexcept {
    return (hdr10 ? 6U : 0U) + (tenBit ? 3U : 0U) + chromaSampling;
}

bool ResolveEncoderPlan(const std::string& encoderName, const bool tenBit, const bool hdr10,
    const std::uint32_t chromaSampling, ResolvedEncoderPlan& plan, std::string& error) {
    const auto strategy = std::find_if(std::begin(EncoderStrategies), std::end(EncoderStrategies),
        [&](const EncoderStrategy& candidate) { return encoderName == candidate.name; });
    if (strategy == std::end(EncoderStrategies)) {
        error = "The selected encoder is not present in the application's explicit strategy table.";
        return false;
    }
    const auto& colorPipeline = strategy->colorPipelines[
        ColorPipelineIndex(tenBit, hdr10, chromaSampling)];
    if (!colorPipeline.supported) {
        const auto mode = hdr10 ? (tenBit ? "HDR10" : "HDR8") : (tenBit ? "SDR10" : "SDR8");
        error = std::string(strategy->name) + " does not support " + mode + " " +
            SamplingName(chromaSampling) + " in the application encoder strategy.";
        return false;
    }

    plan.strategy = &*strategy;
    plan.colorPipeline = &colorPipeline;
    return true;
}

const SoftwareEncoderRuntimeStrategy* FindSoftwareRuntimeStrategy(const std::string& encoderName) noexcept {
    const auto strategy = std::find_if(std::begin(SoftwareRuntimeStrategies),
        std::end(SoftwareRuntimeStrategies), [&](const auto& candidate) { return encoderName == candidate.name; });
    return strategy == std::end(SoftwareRuntimeStrategies) ? nullptr : &*strategy;
}

bool IsNumericPreset(const std::string& value, const int minimum, const int maximum) noexcept {
    if (value.empty() || !std::all_of(value.begin(), value.end(),
        [](const char character) { return character >= '0' && character <= '9'; })) return false;
    try {
        const auto number = std::stoi(value);
        return number >= minimum && number <= maximum;
    } catch (...) {
        return false;
    }
}

bool IsOneOf(const std::string& value, const std::initializer_list<const char*>& choices) noexcept {
    return std::any_of(choices.begin(), choices.end(),
        [&](const char* choice) { return value == choice; });
}

bool ResolveEncoderPreset(const EncoderStrategy& strategy, const std::string& requested,
    std::string& selected, std::string& error) {
    selected = requested.empty() ? strategy.defaultPreset : requested;
    if (strategy.presetFamily == EncoderPresetFamily::X26x) {
        // These presets were exposed by older builds but are intentionally no longer offered
        // for real-time capture.  Keep old settings usable without reintroducing their drain cost.
        if (selected == "placebo" || selected == "veryslow" || selected == "slower") selected = "slow";
        if (IsOneOf(selected, { "slow", "medium", "fast", "faster", "veryfast", "superfast", "ultrafast" })) return true;
    } else if (strategy.presetFamily == EncoderPresetFamily::SvtAv1) {
        if (IsNumericPreset(selected, 1, 13)) return true;
    } else if (strategy.presetFamily == EncoderPresetFamily::Nvenc) {
        if (IsOneOf(selected, { "p1", "p2", "p3", "p4", "p5", "p6", "p7" })) return true;
    } else if (strategy.presetFamily == EncoderPresetFamily::Qsv) {
        if (IsOneOf(selected, { "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" })) return true;
    } else if (strategy.presetFamily == EncoderPresetFamily::Amf) {
        if (IsOneOf(selected, { "high_quality", "quality", "balanced", "speed" })) return true;
    }
    error = std::string(strategy.name) + " does not accept preset '" + selected + "'.";
    return false;
}

std::uint32_t RealtimeWorkerCount(const std::uint32_t maximum) noexcept {
    const auto logicalProcessors = std::max(1U, std::thread::hardware_concurrency());
    // Keep two logical processors free on normal desktop CPUs. Small systems retain one.
    const auto reserved = logicalProcessors > 4 ? 2U : 1U;
    const auto available = logicalProcessors > reserved ? logicalProcessors - reserved : 1U;
    return std::max(1U, std::min(maximum, std::min(available, 16U)));
}

std::uint32_t RealtimeLookahead(const SoftwareEncoderRuntimeStrategy& strategy,
    const std::uint32_t requested) noexcept {
    const auto selected = requested == 0 ? strategy.defaultLookaheadFrames : requested;
    return std::min(selected, strategy.maximumLookaheadFrames);
}

void ConfigureSoftwareEncoderRuntime(const std::string& encoderName, AVCodecContext* codecContext,
    AVDictionary** encoderOptions, const std::uint32_t requestedLookahead) noexcept {
    const auto* strategy = FindSoftwareRuntimeStrategy(encoderName);
    if (strategy == nullptr || codecContext == nullptr || encoderOptions == nullptr) return;
    const auto workers = RealtimeWorkerCount(strategy->maximumWorkerThreads);
    const auto lookahead = RealtimeLookahead(*strategy, requestedLookahead);
    if (encoderName == "libx264") {
        // x264 owns its worker pool; FFmpeg's count is the explicit upper bound.
        codecContext->thread_count = static_cast<int>(workers);
        av_dict_set_int(encoderOptions, "rc-lookahead", lookahead, 0);
    } else if (encoderName == "libx265") {
        // x265 has a pool plus frame-parallel workers. Bound both instead of allowing the
        // default pool to consume every logical processor during capture and drain.
        const auto frameThreads = std::max(1U, std::min(4U, workers / 4U));
        codecContext->thread_count = static_cast<int>(frameThreads);
        const auto parameters = "pools=" + std::to_string(workers) +
            ":frame-threads=" + std::to_string(frameThreads) +
            ":rc-lookahead=" + std::to_string(lookahead);
        av_dict_set(encoderOptions, "x265-params", parameters.c_str(), 0);
    } else {
        // SVT-AV1 uses its own task graph (not FFmpeg frame threading). lp is a parallelism
        // level, not a thread count; level 3 avoids the large end-of-stream picture backlog.
        codecContext->thread_count = 1;
        codecContext->thread_type = 0;
        const auto parallelism = std::thread::hardware_concurrency() >= 8 ? 3U : 2U;
        const auto parameters = "lp=" + std::to_string(parallelism) +
            ":lookahead=" + std::to_string(lookahead);
        av_dict_set(encoderOptions, "svtav1-params", parameters.c_str(), 0);
    }
}

constexpr char RgbToYuvShader[] = R"(
Texture2D<float4> Source : register(t0);

RWTexture2D<float> Luma : register(u0);
#if PLANAR_444
RWTexture2D<float> ChromaU : register(u1);
RWTexture2D<float> ChromaV : register(u2);
#elif PLANAR_422
RWTexture2D<float> ChromaU : register(u1);
RWTexture2D<float> ChromaV : register(u2);
#elif PLANAR_420
RWTexture2D<float> ChromaU : register(u1);
RWTexture2D<float> ChromaV : register(u2);
#else
RWTexture2D<float2> Chroma : register(u1);
#endif

#if HDR_BT2020
static const float Kr = 0.2627;
static const float Kb = 0.0593;
#else
static const float Kr = 0.2126;
static const float Kb = 0.0722;
#endif

float3 ConvertRgb(float3 rgb)
{
    // The incoming channels are already display-referred R'G'B': BT.709 gamma for
    // SDR or BT.2020 PQ for HDR. This stage changes only the matrix representation;
    // decoding/re-encoding the transfer function here would make HDR green/washed.
    // The difference form makes neutral RGB produce exactly zero chroma even
    // when the shader compiler reassociates floating-point operations.
    float y = rgb.g + Kr * (rgb.r - rgb.g) + Kb * (rgb.b - rgb.g);
    float cb = (rgb.b - y) / (2.0 * (1.0 - Kb));
    float cr = (rgb.r - y) / (2.0 * (1.0 - Kr));
    return float3(saturate(y), clamp(float2(cb, cr), -0.5, 0.5));
}

float StoreLuma(float value)
{
#if TEN_BIT
    return round(saturate(value) * 1023.0) * (64.0 / 65535.0);
#else
    return saturate(value);
#endif
}

float2 StoreChroma(float2 value)
{
#if TEN_BIT
    return clamp(round(value * 1023.0) + 512.0, 0.0, 1023.0) * (64.0 / 65535.0);
#else
    return clamp(round(value * 255.0) + 128.0, 0.0, 255.0) / 255.0;
#endif
}

float3 LoadLeftSitedChromaRow(uint2 position, uint width, uint height)
{
    uint2 left = uint2(position.x == 0 ? 0 : position.x - 1, position.y);
    uint2 right = min(position + uint2(1, 0), uint2(width - 1, height - 1));
    return Source.Load(int3(left, 0)).rgb * 0.25 +
        Source.Load(int3(position, 0)).rgb * 0.5 +
        Source.Load(int3(right, 0)).rgb * 0.25;
}

[numthreads(8, 8, 1)]
void Convert(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Source.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float3 yuv = ConvertRgb(Source.Load(int3(id.xy, 0)).rgb);
    Luma[id.xy] = StoreLuma(yuv.x);
#if PLANAR_444
    ChromaU[id.xy] = StoreChroma(yuv.yz).x;
    ChromaV[id.xy] = StoreChroma(yuv.zz).x;
#elif PLANAR_422
    if ((id.x & 1) == 0) {
        uint2 p1 = min(id.xy + uint2(1, 0), uint2(width - 1, height - 1));
        float3 rgb = (Source.Load(int3(id.xy, 0)).rgb + Source.Load(int3(p1, 0)).rgb) * 0.5;
        float2 chroma = StoreChroma(ConvertRgb(rgb).yz);
        ChromaU[uint2(id.x / 2, id.y)] = chroma.x;
        ChromaV[uint2(id.x / 2, id.y)] = chroma.y;
    }
#elif PLANAR_420
    if ((id.x & 1) == 0 && (id.y & 1) == 0) {
#if CHROMA_LEFT
        uint2 nextRow = min(id.xy + uint2(0, 1), uint2(width - 1, height - 1));
        float3 rgb = (LoadLeftSitedChromaRow(id.xy, width, height) +
            LoadLeftSitedChromaRow(nextRow, width, height)) * 0.5;
#else
        uint2 p1 = min(id.xy + uint2(1, 0), uint2(width - 1, height - 1));
        uint2 p2 = min(id.xy + uint2(0, 1), uint2(width - 1, height - 1));
        uint2 p3 = min(id.xy + uint2(1, 1), uint2(width - 1, height - 1));
        float3 rgb = (Source.Load(int3(id.xy, 0)).rgb + Source.Load(int3(p1, 0)).rgb +
            Source.Load(int3(p2, 0)).rgb + Source.Load(int3(p3, 0)).rgb) * 0.25;
#endif
        float2 chroma = StoreChroma(ConvertRgb(rgb).yz);
        ChromaU[id.xy / 2] = chroma.x;
        ChromaV[id.xy / 2] = chroma.y;
    }
#else
    if ((id.x & 1) == 0 && (id.y & 1) == 0) {
#if CHROMA_LEFT
        uint2 nextRow = min(id.xy + uint2(0, 1), uint2(width - 1, height - 1));
        float3 rgb = (LoadLeftSitedChromaRow(id.xy, width, height) +
            LoadLeftSitedChromaRow(nextRow, width, height)) * 0.5;
#else
        uint2 p1 = min(id.xy + uint2(1, 0), uint2(width - 1, height - 1));
        uint2 p2 = min(id.xy + uint2(0, 1), uint2(width - 1, height - 1));
        uint2 p3 = min(id.xy + uint2(1, 1), uint2(width - 1, height - 1));
        float3 rgb = (Source.Load(int3(id.xy, 0)).rgb + Source.Load(int3(p1, 0)).rgb +
            Source.Load(int3(p2, 0)).rgb + Source.Load(int3(p3, 0)).rgb) * 0.25;
#endif
        Chroma[id.xy / 2] = StoreChroma(ConvertRgb(rgb).yz);
    }
#endif
}
)";
}

// 构造一个尚未绑定设备或文件的封装器。所有原生指针初始化为空，保证任何中途失败路径都可
// 统一调用 ReleaseResources；构造函数不触发 FFmpeg、GPU 或文件系统操作。
VideoMuxer::VideoMuxer() noexcept
    : immediateContext_(nullptr), d3d11Device_(nullptr), d3d11Device3_(nullptr), rgbToYuvShader_(nullptr),
      hardwareDevice_(nullptr), hardwareFrames_(nullptr),
      encoderHardwareDevice_(nullptr), encoderFrames_(nullptr), videoBitstreamFilter_(nullptr),
      codecContext_(nullptr), formatContext_(nullptr),
      videoStream_(nullptr), width_(0), height_(0), inputDxgiFormat_(0),
      encoderBackend_(VideoEncoderBackend::None), rgbToYuvPath_(RgbToYuvPath::None),
      yuvBitPacking_(YuvBitPacking::EightBit), tenBit_(false), chromaSampling_(0),
      videoDevice_(nullptr), videoContext_(nullptr),
      videoProcessorEnumerator_(nullptr),
      videoProcessor_(nullptr),
      initialized_(false), headerWritten_(false), finished_(false),
      writerStopRequested_(false), writerFailed_(false), queueDepth_(0), peakQueueDepth_(0),
      lastWriteMicroseconds_(0), peakWriteMicroseconds_(0), videoBytes_(0), audioBytes_(0) {
}

// 析构时只执行无条件资源清理，不尝试补写 trailer。正常调用方必须先用 Finish 排空编码器；
// 这样析构函数即使运行在异常展开期间也不会再次进行可能失败的文件写入。
VideoMuxer::~VideoMuxer() {
    ReleaseResources(false);
}

// 使用调用方现有 D3D11 Device 建立 FFmpeg D3D11VA 设备、硬件帧池、硬件编码器和 Matroska
// 输出。device 仅在初始化期间借用，FFmpeg 设备上下文和本类 Immediate Context 都各自持有引用。
// 输入可为 BGRA8 SDR 或 RGB10 SDR/HDR；策略表决定 RGB 到 4:2:0/4:2:2/4:4:4 的唯一转换路径。
FFFResult VideoMuxer::Initialize(ID3D11Device* device, const std::string& outputPath,
    const std::string& encoderName, const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t frameRateNumerator, const std::uint32_t frameRateDenominator,
    const std::int64_t bitRate, const std::uint32_t gopSize, const std::uint32_t bFrameCount,
    const bool tenBit, const bool hdr10, const bool mixAudioSources,
    const std::uint32_t inputTextureFormat, const std::uint32_t chromaSampling,
    const std::uint32_t rateControl, const std::uint32_t qualityMode, const std::string& customVideoParameters,
    const std::int32_t quality, const std::int64_t maximumBitRate,
    const std::uint32_t lookaheadFrames, const std::string& preset, const std::string& profile,
    const std::string& sceneOptimization, const std::uint32_t multipass,
    const std::uint32_t colorRange, const std::vector<float>& audioSourceGains,
    const std::string& audioEncoderName, const std::uint32_t audioSampleRate,
    const std::uint32_t audioChannelCount, const std::int64_t audioBitRate,
    const std::uint32_t audioMode) noexcept {
    if (initialized_ || device == nullptr || outputPath.empty() || encoderName.empty() ||
        width == 0 || height == 0 || frameRateNumerator == 0 || frameRateDenominator == 0) {
        return FFFResult::InvalidArgument;
    }
    if (colorRange != static_cast<std::uint32_t>(AVCOL_RANGE_JPEG)) {
        lastError_ = "3FR records full-range video only.";
        return FFFResult::InvalidArgument;
    }
    if (hdr10 && (!tenBit || inputTextureFormat != 2)) {
        lastError_ = "HDR10 requires a native ten-bit RGB10 GPU texture; refusing false HDR output.";
        return FFFResult::NotSupported;
    }
    if ((!tenBit && inputTextureFormat != 0) || (tenBit && inputTextureFormat != 2)) {
        lastError_ = "The configured bit depth and D3D11 RGB input texture format do not match.";
        return FFFResult::NotSupported;
    }
    if (chromaSampling > 2) {
        lastError_ = "The requested chroma sampling mode is invalid.";
        return FFFResult::InvalidArgument;
    }
    if (chromaSampling == 0 && ((width & 1U) != 0 || (height & 1U) != 0)) {
        lastError_ = "4:2:0 encoding requires even output width and height.";
        return FFFResult::InvalidArgument;
    }
    if (chromaSampling == 2 && (width & 1U) != 0) {
        lastError_ = "4:2:2 encoding requires an even output width.";
        return FFFResult::InvalidArgument;
    }

    ResolvedEncoderPlan encoderPlan;
    std::string strategyError;
    if (!ResolveEncoderPlan(encoderName, tenBit, hdr10, chromaSampling, encoderPlan, strategyError)) {
        lastError_ = std::move(strategyError);
        return FFFResult::NotSupported;
    }

    width_ = width;
    height_ = height;
    encoderBackend_ = encoderPlan.strategy->backend;
    const auto& colorPipeline = *encoderPlan.colorPipeline;
    rgbToYuvPath_ = colorPipeline.conversionPath;
    yuvBitPacking_ = colorPipeline.bitPacking;
    tenBit_ = tenBit;
    chromaSampling_ = chromaSampling;
    const auto softwareFormat = colorPipeline.softwarePixelFormat;
    const bool softwareYuv = rgbToYuvPath_ == RgbToYuvPath::SoftwarePlanar;
    const bool videoProcessorConversion = rgbToYuvPath_ == RgbToYuvPath::D3D11VideoProcessor420;
    const bool shaderConversion = softwareYuv ||
        rgbToYuvPath_ == RgbToYuvPath::D3D11ComputeShader420;
    const bool softwareEncoder = encoderBackend_ == VideoEncoderBackend::Software;
    const bool nvencEncoder = encoderBackend_ == VideoEncoderBackend::Nvenc;
    const bool qsvEncoder = encoderBackend_ == VideoEncoderBackend::Qsv;
    const bool amfEncoder = encoderBackend_ == VideoEncoderBackend::Amf;
    const bool av1Codec = encoderPlan.strategy->codec == VideoCodecFamily::Av1;
    const bool hevcCodec = encoderPlan.strategy->codec == VideoCodecFamily::Hevc;
    switch (inputTextureFormat) {
    case 0:
        inputDxgiFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case 2:
        inputDxgiFormat_ = DXGI_FORMAT_R10G10B10A2_UNORM;
        break;
    default:
        lastError_ = "The requested D3D11 encoder input format is not supported.";
        return FFFResult::NotSupported;
    }
    d3d11Device_ = device;
    const AVCodec* codec = avcodec_find_encoder_by_name(encoderName.c_str());
    if (codec == nullptr) {
        lastError_ = "The selected encoder is not present in this FFmpeg build.";
        return FFFResult::NotSupported;
    }

    hardwareDevice_ = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (hardwareDevice_ == nullptr) {
        lastError_ = "Could not allocate the FFmpeg D3D11 hardware device context.";
        return FFFResult::FfmpegFailure;
    }
    auto* deviceContext = reinterpret_cast<AVHWDeviceContext*>(hardwareDevice_->data);
    auto* d3d11Context = reinterpret_cast<AVD3D11VADeviceContext*>(deviceContext->hwctx);
    device->AddRef();
    d3d11Context->device = device;
    device->GetImmediateContext(&immediateContext_);
    d3d11Context->device_context = immediateContext_;
    immediateContext_->AddRef();

    auto result = av_hwdevice_ctx_init(hardwareDevice_);
    if (result < 0) {
        SetFfmpegError("av_hwdevice_ctx_init", result);
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }

    if (!softwareYuv) {
        hardwareFrames_ = av_hwframe_ctx_alloc(hardwareDevice_);
        if (hardwareFrames_ == nullptr) {
            lastError_ = "Could not allocate the FFmpeg D3D11 frame context.";
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
        auto* frameContext = reinterpret_cast<AVHWFramesContext*>(hardwareFrames_->data);
        frameContext->format = AV_PIX_FMT_D3D11;
        frameContext->sw_format = softwareFormat;
        frameContext->width = static_cast<int>(width);
        frameContext->height = static_cast<int>(height);
        // NV12/P010 render-target arrays are rejected by some D3D11 drivers. A dynamic pool
        // gives the video processor one independently viewable texture per encoder surface.
        frameContext->initial_pool_size = 0;
        auto* d3d11Frames = reinterpret_cast<AVD3D11VAFramesContext*>(frameContext->hwctx);
        d3d11Frames->BindFlags = videoProcessorConversion ?
            D3D11_BIND_RENDER_TARGET : D3D11_BIND_UNORDERED_ACCESS;
        result = av_hwframe_ctx_init(hardwareFrames_);
        if (result < 0) {
            SetFfmpegError("av_hwframe_ctx_init", result);
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
    }

    if (qsvEncoder) {
        result = av_hwdevice_ctx_create_derived(&encoderHardwareDevice_, AV_HWDEVICE_TYPE_QSV,
            hardwareDevice_, 0);
        if (result < 0) {
            SetFfmpegError("av_hwdevice_ctx_create_derived(QSV from D3D11)", result);
            ReleaseResources(false);
            return FFFResult::NotSupported;
        }
        if (!softwareYuv) {
            result = av_hwframe_ctx_create_derived(&encoderFrames_, AV_PIX_FMT_QSV,
                encoderHardwareDevice_, hardwareFrames_, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
            if (result < 0) {
                SetFfmpegError("av_hwframe_ctx_create_derived(QSV from D3D11 frames)", result);
                ReleaseResources(false);
                return FFFResult::NotSupported;
            }
        }
    } else if (!softwareEncoder) {
        encoderHardwareDevice_ = av_buffer_ref(hardwareDevice_);
        if (hardwareFrames_ != nullptr) encoderFrames_ = av_buffer_ref(hardwareFrames_);
        if (encoderHardwareDevice_ == nullptr || (!softwareYuv && encoderFrames_ == nullptr)) {
            lastError_ = "Could not retain the FFmpeg encoder hardware contexts.";
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
    }
    if (videoProcessorConversion) {
        const auto processorResult = InitializeVideoProcessor(frameRateNumerator,
            frameRateDenominator);
        if (processorResult != FFFResult::Success) {
            ReleaseResources(false);
            return processorResult;
        }
    }
    if (shaderConversion) {
        const auto converterResult = InitializeRgbToYuvConverter(tenBit, hdr10,
            chromaSampling, softwareYuv,
            colorPipeline.chromaLocation == AVCHROMA_LOC_LEFT);
        if (converterResult != FFFResult::Success) {
            ReleaseResources(false);
            return converterResult;
        }
    }

    codecContext_ = avcodec_alloc_context3(codec);
    if (codecContext_ == nullptr) {
        lastError_ = "Could not allocate the FFmpeg video encoder context.";
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }
    codecContext_->width = static_cast<int>(width);
    codecContext_->height = static_cast<int>(height);
    codecContext_->time_base = { static_cast<int>(frameRateDenominator), static_cast<int>(frameRateNumerator) };
    codecContext_->framerate = { static_cast<int>(frameRateNumerator), static_cast<int>(frameRateDenominator) };
    codecContext_->pix_fmt = softwareYuv ? softwareFormat :
        (qsvEncoder ? AV_PIX_FMT_QSV : AV_PIX_FMT_D3D11);
    codecContext_->sw_pix_fmt = softwareFormat;
    codecContext_->bit_rate = bitRate;
    codecContext_->gop_size = static_cast<int>(gopSize);
    codecContext_->max_b_frames = static_cast<int>(bFrameCount);
    codecContext_->rc_max_rate = maximumBitRate;
    // Constant-quality modes must not also advertise an ABR target.  SVT-AV1 rejects
    // that combination outright and x265 otherwise silently switches back to ABR.
    if (softwareEncoder && rateControl == 1 && qualityMode != 4) {
        codecContext_->bit_rate = 0;
        codecContext_->rc_max_rate = 0;
    }
    codecContext_->color_range = colorPipeline.colorRange;
    codecContext_->color_primaries = colorPipeline.colorPrimaries;
    codecContext_->color_trc = colorPipeline.colorTransfer;
    codecContext_->colorspace = colorPipeline.colorSpace;
    codecContext_->chroma_sample_location = colorPipeline.chromaLocation;
    codecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (!softwareEncoder) {
        codecContext_->hw_device_ctx = av_buffer_ref(encoderHardwareDevice_);
        if (encoderFrames_ != nullptr) codecContext_->hw_frames_ctx = av_buffer_ref(encoderFrames_);
    }
    if (qsvEncoder && rateControl == 1) {
        codecContext_->bit_rate = 0;
        codecContext_->rc_max_rate = 0;
    } else if (rateControl == 2) {
        codecContext_->rc_max_rate = bitRate;
    }

    AVDictionary* encoderOptions = nullptr;
    const bool nvenc = nvencEncoder;
    const bool qsv = qsvEncoder;
    const bool amf = amfEncoder;
    std::string selectedPreset;
    std::string presetError;
    if (!ResolveEncoderPreset(*encoderPlan.strategy, preset, selectedPreset, presetError)) {
        lastError_ = std::move(presetError);
        ReleaseResources(false);
        return FFFResult::InvalidArgument;
    }
    if (softwareEncoder) ConfigureSoftwareEncoderRuntime(encoderName, codecContext_,
        &encoderOptions, lookaheadFrames);
    if ((qsv || amf) && multipass != 0) {
        lastError_ = "The selected backend does not expose the requested multipass mode.";
        av_dict_free(&encoderOptions);
        ReleaseResources(false);
        return FFFResult::NotSupported;
    }
    if (nvenc) {
        av_dict_set(&encoderOptions, "preset", selectedPreset.c_str(), 0);
        av_dict_set(&encoderOptions, "tune", sceneOptimization.empty() ? "hq" : sceneOptimization.c_str(), 0);
        av_dict_set(&encoderOptions, "surfaces", "8", 0);
        const auto selectedProfile = profile.empty() && tenBit && hevcCodec
            ? (chromaSampling != 0 ? "rext" : "main10") : profile.c_str();
        if (selectedProfile != nullptr && *selectedProfile != '\0')
            av_dict_set(&encoderOptions, "profile", selectedProfile, 0);
        if (tenBit && av1Codec)
            av_dict_set(&encoderOptions, "highbitdepth", "1", 0);
        const char* rateControlName = qualityMode == 2 ? "vbr" :
            (rateControl == 1 ? "constqp" : (rateControl == 2 ? "cbr" : "vbr"));
        av_dict_set(&encoderOptions, "rc", rateControlName, 0);
        if (quality >= 0 && qualityMode != 4)
            av_dict_set_int(&encoderOptions, qualityMode == 2 ? "cq" : "qp", quality, 0);
        if (lookaheadFrames > 0) av_dict_set_int(&encoderOptions, "rc-lookahead", lookaheadFrames, 0);
        av_dict_set(&encoderOptions, "multipass", multipass == 2 ? "fullres" :
            (multipass == 1 ? "qres" : "disabled"), 0);
    } else if (qsv) {
        av_dict_set(&encoderOptions, "preset", selectedPreset.c_str(), 0);
        const auto selectedProfile = profile.empty() && chromaSampling != 0 && hevcCodec ?
            "rext" : profile.c_str();
        if (selectedProfile != nullptr && *selectedProfile != '\0')
            av_dict_set(&encoderOptions, "profile", selectedProfile, 0);
        if (!sceneOptimization.empty())
            av_dict_set(&encoderOptions, "scenario", sceneOptimization.c_str(), 0);
        if (rateControl == 1 && quality >= 0) {
            codecContext_->flags |= AV_CODEC_FLAG_QSCALE;
            codecContext_->global_quality = quality * FF_QP2LAMBDA;
        }
        if (lookaheadFrames > 0) {
            av_dict_set(&encoderOptions, "look_ahead", "1", 0);
            av_dict_set_int(&encoderOptions, "look_ahead_depth", lookaheadFrames, 0);
        }
    } else if (amf) {
        av_dict_set(&encoderOptions, "preset", selectedPreset.c_str(), 0);
        if (!profile.empty()) av_dict_set(&encoderOptions, "profile", profile.c_str(), 0);
        if (!sceneOptimization.empty()) av_dict_set(&encoderOptions, "usage", sceneOptimization.c_str(), 0);
        av_dict_set(&encoderOptions, "rc", rateControl == 1 ? "cqp" :
            (rateControl == 2 ? "cbr" : "vbr_peak"), 0);
        if (quality >= 0 && rateControl == 1) {
            av_dict_set_int(&encoderOptions, "qp_i", quality, 0);
            av_dict_set_int(&encoderOptions, "qp_p", quality, 0);
            av_dict_set_int(&encoderOptions, "qp_b", quality, 0);
        }
        if (lookaheadFrames > 0) {
            av_dict_set(&encoderOptions, "preanalysis", "1", 0);
            av_dict_set_int(&encoderOptions, "pa_lookahead_buffer_depth", lookaheadFrames, 0);
        }
    } else {
        av_dict_set(&encoderOptions, "preset", selectedPreset.c_str(), 0);
        if (!profile.empty()) av_dict_set(&encoderOptions, "profile", profile.c_str(), 0);
        if (!sceneOptimization.empty()) av_dict_set(&encoderOptions, "tune", sceneOptimization.c_str(), 0);
        if (rateControl == 1 && quality >= 0)
            av_dict_set_int(&encoderOptions, qualityMode == 1 ? "crf" : "qp", quality, 0);
    }
    // Custom options are applied after every built-in quality mode, so explicit keys can
    // override encoder defaults in QP, CRF, CQ, global_quality, and custom mode alike.
    if (!customVideoParameters.empty()) {
        std::istringstream arguments(customVideoParameters);
        std::string token;
        while (arguments >> token) {
            if (!token.empty() && token.front() == '-') token.erase(token.begin());
            const auto separator = token.find('=');
            if (separator == std::string::npos || separator == 0 || separator + 1 >= token.size()) {
                lastError_ = "Custom video parameters must use key=value tokens.";
                av_dict_free(&encoderOptions);
                ReleaseResources(false);
                return FFFResult::InvalidArgument;
            }
            av_dict_set(&encoderOptions, token.substr(0, separator).c_str(),
                token.substr(separator + 1).c_str(), 0);
        }
    }
    result = avcodec_open2(codecContext_, codec, &encoderOptions);
    if (result >= 0 && av_dict_count(encoderOptions) > 0) {
        std::string unsupported;
        const AVDictionaryEntry* entry = nullptr;
        while ((entry = av_dict_iterate(encoderOptions, entry)) != nullptr) {
            if (!unsupported.empty()) unsupported += ", ";
            unsupported += entry->key;
        }
        lastError_ = "The selected encoder did not accept these options: " + unsupported + ".";
        av_dict_free(&encoderOptions);
        ReleaseResources(false);
        return FFFResult::NotSupported;
    }
    av_dict_free(&encoderOptions);
    if (result < 0) {
        SetFfmpegError("avcodec_open2", result);
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }

    if (colorPipeline.bitstreamMetadataPath == BitstreamColorMetadataPath::H265MetadataBsf) {
        const auto* filter = av_bsf_get_by_name("hevc_metadata");
        if (filter == nullptr) {
            lastError_ = "This FFmpeg build does not provide the HEVC metadata bitstream filter.";
            ReleaseResources(false);
            return FFFResult::NotSupported;
        }
        result = av_bsf_alloc(filter, &videoBitstreamFilter_);
        if (result >= 0) {
            result = avcodec_parameters_from_context(videoBitstreamFilter_->par_in, codecContext_);
        }
        if (result >= 0) {
            videoBitstreamFilter_->time_base_in = codecContext_->time_base;
            // HEVC chroma_sample_loc_type 0 is AVCHROMA_LOC_LEFT (the enum value minus one).
            result = av_opt_set_int(videoBitstreamFilter_->priv_data,
                "chroma_sample_loc_type", 0, 0);
        }
        if (result >= 0) result = av_bsf_init(videoBitstreamFilter_);
        if (result < 0) {
            SetFfmpegError("initialize hevc_metadata bitstream filter", result);
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
    }

    result = avformat_alloc_output_context2(&formatContext_, nullptr, "matroska", outputPath.c_str());
    if (result < 0 || formatContext_ == nullptr) {
        SetFfmpegError("avformat_alloc_output_context2", result);
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }
    videoStream_ = avformat_new_stream(formatContext_, nullptr);
    if (videoStream_ == nullptr) {
        lastError_ = "Could not create the Matroska video stream.";
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }
    videoStream_->time_base = codecContext_->time_base;
    videoStream_->avg_frame_rate = codecContext_->framerate;
    videoStream_->r_frame_rate = codecContext_->framerate;
    result = videoBitstreamFilter_ != nullptr ?
        avcodec_parameters_copy(videoStream_->codecpar, videoBitstreamFilter_->par_out) :
        avcodec_parameters_from_context(videoStream_->codecpar, codecContext_);
    if (result < 0) {
        SetFfmpegError("avcodec_parameters_from_context", result);
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }
    // Keep the container track metadata aligned with the encoder VUI. Some Matroska readers use
    // codec parameters instead of parsing the HEVC headers when reporting the color range.
    videoStream_->codecpar->color_range = codecContext_->color_range;
    videoStream_->codecpar->color_primaries = codecContext_->color_primaries;
    videoStream_->codecpar->color_trc = codecContext_->color_trc;
    videoStream_->codecpar->color_space = codecContext_->colorspace;
    videoStream_->codecpar->chroma_location = codecContext_->chroma_sample_location;
    videoStream_->codecpar->codec_tag = 0;
    if (mixAudioSources && audioSourceGains.size() < 2) {
        lastError_ = "Mixed audio requires at least two sources.";
        ReleaseResources(false);
        return FFFResult::InvalidArgument;
    }
    const auto audioTrackCount = mixAudioSources ? 1U : audioSourceGains.size();
    audioTracks_.reserve(audioTrackCount);
    for (std::size_t trackIndex = 0; trackIndex < audioTrackCount; ++trackIndex) {
        auto track = std::make_unique<AudioTrackEncoder>();
        const auto gain = mixAudioSources ? 1.0F : audioSourceGains[trackIndex];
        const auto audioResult = track->Initialize(formatContext_,
            [this](const AVPacket* packet) { return EnqueuePacket(packet); },
            gain, audioEncoderName, audioSampleRate,
            audioChannelCount, audioBitRate, audioMode);
        if (audioResult != FFFResult::Success) {
            lastError_ = track->LastError();
            ReleaseResources(false);
            return audioResult;
        }
        audioTracks_.push_back(std::move(track));
    }
    if (mixAudioSources) {
        audioMixer_ = std::make_unique<AudioMixer>(audioTracks_[0].get(), audioSourceGains);
    }
    if ((formatContext_->oformat->flags & AVFMT_NOFILE) == 0) {
        result = avio_open(&formatContext_->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (result < 0) {
            SetFfmpegError("avio_open", result);
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
    }
    AVDictionary* muxerOptions = nullptr;
    av_dict_set(&muxerOptions, "cluster_time_limit", "1000", 0);
    av_dict_set(&muxerOptions, "cluster_size_limit", "5242880", 0);
    result = avformat_write_header(formatContext_, &muxerOptions);
    av_dict_free(&muxerOptions);
    if (result < 0) {
        SetFfmpegError("avformat_write_header", result);
        ReleaseResources(false);
        return FFFResult::FfmpegFailure;
    }
    headerWritten_ = true;
    const auto writerStarted = StartWriter();
    if (writerStarted != FFFResult::Success) {
        ReleaseResources(false);
        return writerStarted;
    }
    initialized_ = true;
    return FFFResult::Success;
}

FFFResult VideoMuxer::InitializeRgbToYuvConverter(const bool tenBit, const bool hdr10,
    const std::uint32_t chromaSampling, const bool softwareYuv, const bool leftChroma) noexcept {
    auto result = d3d11Device_->QueryInterface(IID_PPV_ARGS(&d3d11Device3_));
    if (FAILED(result)) {
        lastError_ = "Full-range GPU color conversion requires ID3D11Device3.";
        return FFFResult::NotSupported;
    }

    const D3D_SHADER_MACRO macros[] = {
        { "PLANAR_444", softwareYuv && chromaSampling == 1 ? "1" : "0" },
        { "PLANAR_422", softwareYuv && chromaSampling == 2 ? "1" : "0" },
        { "PLANAR_420", softwareYuv && chromaSampling == 0 ? "1" : "0" },
        { "TEN_BIT", tenBit ? "1" : "0" },
        { "HDR_BT2020", hdr10 ? "1" : "0" },
        { "CHROMA_LEFT", leftChroma ? "1" : "0" },
        { nullptr, nullptr }
    };
    Microsoft::WRL::ComPtr<ID3DBlob> byteCode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    result = D3DCompile(RgbToYuvShader, sizeof(RgbToYuvShader) - 1, "RgbToYuv", macros, nullptr,
        "Convert", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &byteCode, &errors);
    if (FAILED(result)) {
        lastError_ = "Could not compile the full-range RGB to YUV shader";
        if (errors != nullptr && errors->GetBufferPointer() != nullptr) {
            lastError_ += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize());
        }
        return FFFResult::NativeFailure;
    }
    result = d3d11Device_->CreateComputeShader(byteCode->GetBufferPointer(), byteCode->GetBufferSize(),
        nullptr, &rgbToYuvShader_);
    if (FAILED(result)) {
        lastError_ = "Could not create the full-range RGB to YUV compute shader: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    if (softwareYuv) {
        D3D11_TEXTURE2D_DESC description{};
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = tenBit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        for (std::size_t plane = 0; plane < 3; ++plane) {
            description.Width = plane == 0 || chromaSampling == 1 ? width_ : width_ / 2;
            description.Height = plane == 0 || chromaSampling != 0 ? height_ : height_ / 2;
            result = d3d11Device_->CreateTexture2D(&description, nullptr, &planarYuvGpuTextures_[plane]);
            if (FAILED(result)) {
                lastError_ = "Could not create the GPU software YUV conversion plane: " +
                    std::to_string(static_cast<long>(result));
                return FFFResult::DeviceFailure;
            }
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            result = d3d11Device_->CreateTexture2D(&description, nullptr, &planarYuvStagingTextures_[plane]);
            if (FAILED(result)) {
                lastError_ = "Could not create the staging software YUV conversion plane: " +
                    std::to_string(static_cast<long>(result));
                return FFFResult::DeviceFailure;
            }
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            description.CPUAccessFlags = 0;
        }
    }
    return FFFResult::Success;
}

FFFResult VideoMuxer::ConvertTextureToEncoderSurfaceWithShader(ID3D11Texture2D* sourceTexture,
    const std::uint32_t sourceArrayIndex, ID3D11Texture2D* destinationTexture,
    const std::uint32_t destinationArrayIndex) noexcept {
    D3D11_TEXTURE2D_DESC sourceDescription{};
    sourceTexture->GetDesc(&sourceDescription);
    if (sourceDescription.ArraySize != 1 || sourceArrayIndex != 0) {
        lastError_ = "The full-range RGB converter requires a single-surface source texture.";
        return FFFResult::NotSupported;
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView;
    auto result = d3d11Device_->CreateShaderResourceView(sourceTexture, nullptr, &sourceView);
    if (FAILED(result)) {
        lastError_ = "Could not create the RGB encoder input view: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }

    D3D11_TEXTURE2D_DESC destinationDescription{};
    destinationTexture->GetDesc(&destinationDescription);
    auto createOutputView = [&](const DXGI_FORMAT format, const std::uint32_t plane,
        ID3D11UnorderedAccessView1** outputView) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC1 description{};
        description.Format = format;
        if (destinationDescription.ArraySize > 1) {
            description.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            description.Texture2DArray.MipSlice = 0;
            description.Texture2DArray.FirstArraySlice = destinationArrayIndex;
            description.Texture2DArray.ArraySize = 1;
            description.Texture2DArray.PlaneSlice = plane;
        } else {
            description.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            description.Texture2D.MipSlice = 0;
            description.Texture2D.PlaneSlice = plane;
        }
        return d3d11Device3_->CreateUnorderedAccessView1(destinationTexture, &description, outputView);
    };

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView1> outputViews[2];
    const auto lumaFormat = tenBit_ ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    const auto chromaFormat = tenBit_ ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    result = createOutputView(lumaFormat, 0, outputViews[0].GetAddressOf());
    if (SUCCEEDED(result)) result = createOutputView(chromaFormat, 1, outputViews[1].GetAddressOf());
    if (FAILED(result)) {
        lastError_ = "Could not create the full-range YUV encoder output view: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }

    ID3D11ShaderResourceView* sourceViews[] = { sourceView.Get() };
    ID3D11UnorderedAccessView* unorderedViews[] = { outputViews[0].Get(), outputViews[1].Get() };
    immediateContext_->CSSetShader(rgbToYuvShader_, nullptr, 0);
    immediateContext_->CSSetShaderResources(0, 1, sourceViews);
    immediateContext_->CSSetUnorderedAccessViews(0, 2, unorderedViews, nullptr);
    immediateContext_->Dispatch((width_ + 7U) / 8U, (height_ + 7U) / 8U, 1);
    ID3D11ShaderResourceView* nullSource[] = { nullptr };
    ID3D11UnorderedAccessView* nullOutputs[] = { nullptr, nullptr };
    immediateContext_->CSSetShaderResources(0, 1, nullSource);
    immediateContext_->CSSetUnorderedAccessViews(0, 2, nullOutputs, nullptr);
    immediateContext_->CSSetShader(nullptr, nullptr, 0);
    return FFFResult::Success;
}

FFFResult VideoMuxer::EncodeSoftwareYuv(ID3D11Texture2D* sourceTexture,
    const std::uint32_t sourceArrayIndex, const std::int64_t presentationTimestamp) noexcept {
    D3D11_TEXTURE2D_DESC sourceDescription{};
    sourceTexture->GetDesc(&sourceDescription);
    if (sourceDescription.ArraySize != 1 || sourceArrayIndex != 0) {
        lastError_ = "The full-range software YUV converter requires a single-surface source texture.";
        return FFFResult::NotSupported;
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView;
    auto result = d3d11Device_->CreateShaderResourceView(sourceTexture, nullptr, &sourceView);
    if (FAILED(result)) {
        lastError_ = "Could not create the RGB software YUV input view: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> outputViews[3];
    for (std::size_t plane = 0; plane < 3; ++plane) {
        result = d3d11Device_->CreateUnorderedAccessView(planarYuvGpuTextures_[plane], nullptr,
            &outputViews[plane]);
        if (FAILED(result)) {
            lastError_ = "Could not create the planar software YUV output view: " +
                std::to_string(static_cast<long>(result));
            return FFFResult::DeviceFailure;
        }
    }

    ID3D11ShaderResourceView* sourceViews[] = { sourceView.Get() };
    ID3D11UnorderedAccessView* unorderedViews[] = {
        outputViews[0].Get(), outputViews[1].Get(), outputViews[2].Get()
    };
    immediateContext_->CSSetShader(rgbToYuvShader_, nullptr, 0);
    immediateContext_->CSSetShaderResources(0, 1, sourceViews);
    immediateContext_->CSSetUnorderedAccessViews(0, 3, unorderedViews, nullptr);
    immediateContext_->Dispatch((width_ + 7U) / 8U, (height_ + 7U) / 8U, 1);
    ID3D11ShaderResourceView* nullSource[] = { nullptr };
    ID3D11UnorderedAccessView* nullOutputs[] = { nullptr, nullptr, nullptr };
    immediateContext_->CSSetShaderResources(0, 1, nullSource);
    immediateContext_->CSSetUnorderedAccessViews(0, 3, nullOutputs, nullptr);
    immediateContext_->CSSetShader(nullptr, nullptr, 0);
    for (std::size_t plane = 0; plane < 3; ++plane) {
        immediateContext_->CopyResource(planarYuvStagingTextures_[plane], planarYuvGpuTextures_[plane]);
    }

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        lastError_ = "Could not allocate the software YUV encoder frame.";
        return FFFResult::FfmpegFailure;
    }
    frame->format = codecContext_->pix_fmt;
    frame->width = static_cast<int>(width_);
    frame->height = static_cast<int>(height_);
    result = av_frame_get_buffer(frame, 32);
    if (result < 0) {
        SetFfmpegError("av_frame_get_buffer(software YUV)", result);
        av_frame_free(&frame);
        return FFFResult::FfmpegFailure;
    }
    D3D11_MAPPED_SUBRESOURCE mappedPlanes[3]{};
    std::size_t mappedPlaneCount = 0;
    for (std::size_t plane = 0; plane < 3; ++plane) {
        const auto mappedResult = immediateContext_->Map(planarYuvStagingTextures_[plane], 0,
            D3D11_MAP_READ, 0, &mappedPlanes[plane]);
        if (FAILED(mappedResult)) {
            for (std::size_t mappedPlane = 0; mappedPlane < mappedPlaneCount; ++mappedPlane)
                immediateContext_->Unmap(planarYuvStagingTextures_[mappedPlane], 0);
            lastError_ = "Could not read back the converted software YUV plane: " +
                std::to_string(static_cast<long>(mappedResult));
            av_frame_free(&frame);
            return FFFResult::DeviceFailure;
        }
        ++mappedPlaneCount;
    }
    if (codecContext_->pix_fmt == AV_PIX_FMT_XV30) {
        for (std::uint32_t row = 0; row < height_; ++row) {
            auto* output = reinterpret_cast<std::uint32_t*>(frame->data[0] +
                static_cast<std::size_t>(row) * frame->linesize[0]);
            const auto* y = reinterpret_cast<const std::uint16_t*>(
                static_cast<const std::uint8_t*>(mappedPlanes[0].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[0].RowPitch);
            const auto* u = reinterpret_cast<const std::uint16_t*>(
                static_cast<const std::uint8_t*>(mappedPlanes[1].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[1].RowPitch);
            const auto* v = reinterpret_cast<const std::uint16_t*>(
                static_cast<const std::uint8_t*>(mappedPlanes[2].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[2].RowPitch);
            for (std::uint32_t column = 0; column < width_; ++column)
                output[column] = (u[column] >> 6) | ((y[column] >> 6) << 10) |
                    ((v[column] >> 6) << 20);
        }
    } else if (codecContext_->pix_fmt == AV_PIX_FMT_VUYX) {
        for (std::uint32_t row = 0; row < height_; ++row) {
            auto* output = frame->data[0] + static_cast<std::size_t>(row) * frame->linesize[0];
            const auto* y = static_cast<const std::uint8_t*>(mappedPlanes[0].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[0].RowPitch;
            const auto* u = static_cast<const std::uint8_t*>(mappedPlanes[1].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[1].RowPitch;
            const auto* v = static_cast<const std::uint8_t*>(mappedPlanes[2].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[2].RowPitch;
            for (std::uint32_t column = 0; column < width_; ++column) {
                output[column * 4] = v[column]; output[column * 4 + 1] = u[column];
                output[column * 4 + 2] = y[column]; output[column * 4 + 3] = 0xFF;
            }
        }
    } else if (codecContext_->pix_fmt == AV_PIX_FMT_Y210 ||
        codecContext_->pix_fmt == AV_PIX_FMT_YUYV422) {
        for (std::uint32_t row = 0; row < height_; ++row) {
            auto* output = frame->data[0] + static_cast<std::size_t>(row) * frame->linesize[0];
            const auto* y = static_cast<const std::uint8_t*>(mappedPlanes[0].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[0].RowPitch;
            const auto* u = static_cast<const std::uint8_t*>(mappedPlanes[1].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[1].RowPitch;
            const auto* v = static_cast<const std::uint8_t*>(mappedPlanes[2].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[2].RowPitch;
            if (tenBit_) {
                auto* words = reinterpret_cast<std::uint16_t*>(output);
                const auto* yy = reinterpret_cast<const std::uint16_t*>(y);
                const auto* uu = reinterpret_cast<const std::uint16_t*>(u);
                const auto* vv = reinterpret_cast<const std::uint16_t*>(v);
                for (std::uint32_t column = 0; column < width_ / 2; ++column) {
                    words[column * 4] = yy[column * 2]; words[column * 4 + 1] = uu[column];
                    words[column * 4 + 2] = yy[column * 2 + 1]; words[column * 4 + 3] = vv[column];
                }
            } else {
                for (std::uint32_t column = 0; column < width_ / 2; ++column) {
                    output[column * 4] = y[column * 2]; output[column * 4 + 1] = u[column];
                    output[column * 4 + 2] = y[column * 2 + 1]; output[column * 4 + 3] = v[column];
                }
            }
        }
    } else if (codecContext_->pix_fmt == AV_PIX_FMT_P210 ||
        codecContext_->pix_fmt == AV_PIX_FMT_NV16) {
        for (std::uint32_t row = 0; row < height_; ++row) {
            const auto componentBytes = tenBit_ ? 2U : 1U;
            std::memcpy(frame->data[0] + static_cast<std::size_t>(row) * frame->linesize[0],
                static_cast<const std::uint8_t*>(mappedPlanes[0].pData) +
                    static_cast<std::size_t>(row) * mappedPlanes[0].RowPitch,
                static_cast<std::size_t>(width_) * componentBytes);
            auto* destination = frame->data[1] + static_cast<std::size_t>(row) * frame->linesize[1];
            const auto* sourceU = static_cast<const std::uint8_t*>(mappedPlanes[1].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[1].RowPitch;
            const auto* sourceV = static_cast<const std::uint8_t*>(mappedPlanes[2].pData) +
                static_cast<std::size_t>(row) * mappedPlanes[2].RowPitch;
            if (tenBit_) {
                auto* output = reinterpret_cast<std::uint16_t*>(destination);
                const auto* inputU = reinterpret_cast<const std::uint16_t*>(sourceU);
                const auto* inputV = reinterpret_cast<const std::uint16_t*>(sourceV);
                for (std::uint32_t column = 0; column < width_ / 2; ++column) {
                    output[column * 2] = inputU[column];
                    output[column * 2 + 1] = inputV[column];
                }
            } else {
                for (std::uint32_t column = 0; column < width_ / 2; ++column) {
                    destination[column * 2] = sourceU[column];
                    destination[column * 2 + 1] = sourceV[column];
                }
            }
        }
    } else {
        const bool lowAlignedTenBit = yuvBitPacking_ == YuvBitPacking::TenBitLsb;
        const bool planar = lowAlignedTenBit ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV420P ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV422P ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV444P ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV444P10MSB;
        if (!planar) {
            for (std::size_t plane = 0; plane < 3; ++plane)
                immediateContext_->Unmap(planarYuvStagingTextures_[plane], 0);
            lastError_ = "The selected encoder software pixel format is not packable.";
            av_frame_free(&frame);
            return FFFResult::NotSupported;
        }
        for (std::size_t plane = 0; plane < 3; ++plane) {
            const auto planeWidth = plane == 0 || chromaSampling_ == 1 ? width_ : width_ / 2;
            const auto planeHeight = plane == 0 || chromaSampling_ != 0 ? height_ : height_ / 2;
            for (std::uint32_t row = 0; row < planeHeight; ++row) {
                auto* destination = frame->data[plane] +
                    static_cast<std::size_t>(row) * frame->linesize[plane];
                const auto* source = static_cast<const std::uint8_t*>(mappedPlanes[plane].pData) +
                    static_cast<std::size_t>(row) * mappedPlanes[plane].RowPitch;
                if (tenBit_ && lowAlignedTenBit) {
                    auto* output = reinterpret_cast<std::uint16_t*>(destination);
                    const auto* input = reinterpret_cast<const std::uint16_t*>(source);
                    for (std::uint32_t column = 0; column < planeWidth; ++column)
                        output[column] = input[column] >> 6;
                } else {
                    std::memcpy(destination, source,
                        static_cast<std::size_t>(planeWidth) * (tenBit_ ? 2U : 1U));
                }
            }
        }
    }
    for (std::size_t plane = 0; plane < 3; ++plane)
        immediateContext_->Unmap(planarYuvStagingTextures_[plane], 0);
    frame->pts = presentationTimestamp;
    frame->duration = 1;
    frame->color_range = codecContext_->color_range;
    frame->color_primaries = codecContext_->color_primaries;
    frame->color_trc = codecContext_->color_trc;
    frame->colorspace = codecContext_->colorspace;
    frame->chroma_location = codecContext_->chroma_sample_location;
    result = avcodec_send_frame(codecContext_, frame);
    av_frame_free(&frame);
    if (result < 0) {
        SetFfmpegError("avcodec_send_frame(software YUV)", result);
        return FFFResult::FfmpegFailure;
    }
    return DrainPackets();
}

// 为 SDR 4:2:0 编码路径创建 D3D11 Video Processor。HDR 永远不进入此函数：DXGI 没有
// full-range PQ YCbCr 输出空间，误用 Video Processor 会改变传递函数并造成严重偏色。
FFFResult VideoMuxer::InitializeVideoProcessor(const std::uint32_t frameRateNumerator,
    const std::uint32_t frameRateDenominator) noexcept {
    auto result = d3d11Device_->QueryInterface(IID_PPV_ARGS(&videoDevice_));
    if (FAILED(result)) {
        lastError_ = "The D3D11 device does not expose ID3D11VideoDevice for color conversion.";
        return FFFResult::NotSupported;
    }
    result = immediateContext_->QueryInterface(IID_PPV_ARGS(&videoContext_));
    if (FAILED(result)) {
        lastError_ = "The D3D11 context does not expose ID3D11VideoContext for color conversion.";
        return FFFResult::NotSupported;
    }
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC description{};
    description.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    description.InputFrameRate = { frameRateNumerator, frameRateDenominator };
    description.InputWidth = width_;
    description.InputHeight = height_;
    description.OutputFrameRate = description.InputFrameRate;
    description.OutputWidth = width_;
    description.OutputHeight = height_;
    description.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    result = videoDevice_->CreateVideoProcessorEnumerator(&description, &videoProcessorEnumerator_);
    if (FAILED(result)) {
        lastError_ = "CreateVideoProcessorEnumerator failed for GPU color conversion: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    UINT inputFormatSupport = 0;
    UINT outputFormatSupport = 0;
    const auto outputFormat = tenBit_ ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
    result = videoProcessorEnumerator_->CheckVideoProcessorFormat(
        static_cast<DXGI_FORMAT>(inputDxgiFormat_), &inputFormatSupport);
    if (SUCCEEDED(result)) {
        result = videoProcessorEnumerator_->CheckVideoProcessorFormat(outputFormat, &outputFormatSupport);
    }
    if (FAILED(result) ||
        (inputFormatSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
        (outputFormatSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
        lastError_ = "The D3D11 video processor does not support the required RGB to NV12/P010 conversion.";
        return FFFResult::NotSupported;
    }
    result = videoDevice_->CreateVideoProcessor(videoProcessorEnumerator_, 0, &videoProcessor_);
    if (FAILED(result)) {
        lastError_ = "CreateVideoProcessor failed for GPU color conversion: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    videoContext_->VideoProcessorSetStreamFrameFormat(videoProcessor_, 0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    Microsoft::WRL::ComPtr<ID3D11VideoContext1> videoContext1;
    if (SUCCEEDED(immediateContext_->QueryInterface(IID_PPV_ARGS(&videoContext1)))) {
        videoContext1->VideoProcessorSetStreamColorSpace1(videoProcessor_, 0,
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        videoContext1->VideoProcessorSetOutputColorSpace1(videoProcessor_,
            DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709);
    } else {
        // The legacy controls still select BT.709 and the full nominal range on
        // systems that expose ID3D11VideoContext but not ID3D11VideoContext1.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
        inputColor.Nominal_Range = 2;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
        outputColor.YCbCr_Matrix = 1;
        outputColor.Nominal_Range = 2;
        videoContext_->VideoProcessorSetStreamColorSpace(videoProcessor_, 0, &inputColor);
        videoContext_->VideoProcessorSetOutputColorSpace(videoProcessor_, &outputColor);
    }
    return FFFResult::Success;
}

// 为源纹理和目标 NV12/P010 surface 创建短生命周期 VideoProcessor view，并执行一次 GPU blit。
// arrayIndex 会原样写入 view 描述，兼容 FFmpeg 固定纹理数组池；返回前不等待 CPU readback。
FFFResult VideoMuxer::ConvertTextureToEncoderSurface(ID3D11Texture2D* sourceTexture,
    const std::uint32_t sourceArrayIndex, ID3D11Texture2D* destinationTexture,
    const std::uint32_t destinationArrayIndex) noexcept {
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDescription{};
    D3D11_TEXTURE2D_DESC sourceDescription{};
    sourceTexture->GetDesc(&sourceDescription);
    inputDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDescription.Texture2D.MipSlice = 0;
    inputDescription.Texture2D.ArraySlice = sourceDescription.ArraySize > 1 ? sourceArrayIndex : 0;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
    auto result = videoDevice_->CreateVideoProcessorInputView(sourceTexture,
        videoProcessorEnumerator_, &inputDescription, &inputView);
    if (FAILED(result)) {
        lastError_ = "CreateVideoProcessorInputView failed for GPU color conversion: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDescription{};
    D3D11_TEXTURE2D_DESC destinationDescription{};
    destinationTexture->GetDesc(&destinationDescription);
    if (destinationDescription.ArraySize > 1) {
        outputDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2DARRAY;
        outputDescription.Texture2DArray.MipSlice = 0;
        outputDescription.Texture2DArray.FirstArraySlice = destinationArrayIndex;
        outputDescription.Texture2DArray.ArraySize = 1;
    } else {
        outputDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        outputDescription.Texture2D.MipSlice = 0;
    }
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
    result = videoDevice_->CreateVideoProcessorOutputView(destinationTexture,
        videoProcessorEnumerator_, &outputDescription, &outputView);
    if (FAILED(result)) {
        lastError_ = "CreateVideoProcessorOutputView failed for GPU color conversion: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.pInputSurface = inputView.Get();
    result = videoContext_->VideoProcessorBlt(videoProcessor_, outputView.Get(), 0, 1, &stream);
    if (FAILED(result)) {
        lastError_ = "VideoProcessorBlt failed for GPU color conversion: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    return FFFResult::Success;
}

// 从 FFmpeg 自有 D3D11 surface 池取得一帧，将调用方纹理做 GPU Copy 后立即送入编码器。
// sourceTexture 只在调用期间借用；源纹理必须是同设备、同尺寸、BGRA8 且数组索引有效。
// presentationTimestamp 已处于 codec time_base，必须严格单调，packet 写入在本调用内同步完成。
FFFResult VideoMuxer::Encode(ID3D11Texture2D* sourceTexture, const std::uint32_t sourceArrayIndex,
    const std::int64_t presentationTimestamp) noexcept {
    if (!initialized_ || finished_) return FFFResult::InvalidState;
    if (sourceTexture == nullptr || presentationTimestamp < 0) return FFFResult::InvalidArgument;

    D3D11_TEXTURE2D_DESC sourceDescription{};
    sourceTexture->GetDesc(&sourceDescription);
    if (sourceDescription.Width != width_ || sourceDescription.Height != height_ ||
        sourceDescription.Format != inputDxgiFormat_ ||
        sourceArrayIndex >= sourceDescription.ArraySize) {
        lastError_ = "Submitted texture format or dimensions do not match the configured encoder input.";
        return FFFResult::InvalidArgument;
    }

    if (rgbToYuvPath_ == RgbToYuvPath::SoftwarePlanar) {
        return EncodeSoftwareYuv(sourceTexture, sourceArrayIndex, presentationTimestamp);
    }

    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        lastError_ = "Could not allocate an FFmpeg video frame.";
        return FFFResult::FfmpegFailure;
    }
    auto result = av_hwframe_get_buffer(hardwareFrames_, frame, 0);
    if (result < 0) {
        SetFfmpegError("av_hwframe_get_buffer", result);
        av_frame_free(&frame);
        return FFFResult::FfmpegFailure;
    }
    auto* destinationTexture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
    const auto destinationIndex = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
    if (rgbToYuvPath_ == RgbToYuvPath::D3D11ComputeShader420) {
        const auto converted = ConvertTextureToEncoderSurfaceWithShader(sourceTexture, sourceArrayIndex,
            destinationTexture, destinationIndex);
        if (converted != FFFResult::Success) {
            av_frame_free(&frame);
            return converted;
        }
    } else if (rgbToYuvPath_ == RgbToYuvPath::D3D11VideoProcessor420) {
        const auto converted = ConvertTextureToEncoderSurface(sourceTexture, sourceArrayIndex,
            destinationTexture, destinationIndex);
        if (converted != FFFResult::Success) {
            av_frame_free(&frame);
            return converted;
        }
    } else {
        av_frame_free(&frame);
        lastError_ = "The resolved encoder strategy has no RGB to YUV conversion path.";
        return FFFResult::InvalidState;
    }
    immediateContext_->Flush();
    frame->pts = presentationTimestamp;
    frame->duration = 1;
    frame->color_range = codecContext_->color_range;
    frame->color_primaries = codecContext_->color_primaries;
    frame->color_trc = codecContext_->color_trc;
    frame->colorspace = codecContext_->colorspace;
    frame->chroma_location = codecContext_->chroma_sample_location;

    AVFrame* encoderFrame = frame;
    AVFrame* mappedFrame = nullptr;
    if (encoderBackend_ == VideoEncoderBackend::Qsv) {
        mappedFrame = av_frame_alloc();
        if (mappedFrame == nullptr) {
            lastError_ = "Could not allocate the mapped QSV frame.";
            av_frame_free(&frame);
            return FFFResult::FfmpegFailure;
        }
        mappedFrame->format = AV_PIX_FMT_QSV;
        mappedFrame->hw_frames_ctx = av_buffer_ref(encoderFrames_);
        result = mappedFrame->hw_frames_ctx == nullptr ? AVERROR(ENOMEM) :
            av_hwframe_map(mappedFrame, frame, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
        if (result < 0) {
            SetFfmpegError("av_hwframe_map(D3D11 to QSV)", result);
            av_frame_free(&mappedFrame);
            av_frame_free(&frame);
            return FFFResult::FfmpegFailure;
        }
        mappedFrame->pts = presentationTimestamp;
        mappedFrame->duration = 1;
        mappedFrame->color_range = codecContext_->color_range;
        mappedFrame->color_primaries = codecContext_->color_primaries;
        mappedFrame->color_trc = codecContext_->color_trc;
        mappedFrame->colorspace = codecContext_->colorspace;
        mappedFrame->chroma_location = codecContext_->chroma_sample_location;
        encoderFrame = mappedFrame;
    }

    result = avcodec_send_frame(codecContext_, encoderFrame);
    av_frame_free(&mappedFrame);
    av_frame_free(&frame);
    if (result < 0) {
        SetFfmpegError("avcodec_send_frame", result);
        if (d3d11Device_ != nullptr) {
            const auto removedReason = d3d11Device_->GetDeviceRemovedReason();
            lastError_ += " D3D11 removed reason=" + std::to_string(static_cast<long>(removedReason)) + ".";
        }
        return FFFResult::FfmpegFailure;
    }
    return DrainPackets();
}

// 停止接收新帧，向编码器发送空帧并取尽所有延迟 packet，随后写入 Matroska trailer。
// 成功后保持幂等；任何 drain 或 trailer 错误都保留在 LastError，资源仍会被完整释放。
FFFResult VideoMuxer::Finish() noexcept {
    if (finished_) return FFFResult::Success;
    if (!initialized_) return FFFResult::InvalidState;
    if (audioMixer_ != nullptr) {
        const auto mixed = audioMixer_->Finish();
        if (mixed != FFFResult::Success) {
            lastError_ = audioMixer_->LastError();
            ReleaseResources(false);
            finished_ = true;
            return mixed;
        }
    }
    for (auto& track : audioTracks_) {
        const auto audioResult = track->Finish();
        if (audioResult != FFFResult::Success) {
            lastError_ = track->LastError();
            ReleaseResources(false);
            finished_ = true;
            return audioResult;
        }
    }
    auto result = avcodec_send_frame(codecContext_, nullptr);
    if (result < 0 && result != AVERROR_EOF) {
        SetFfmpegError("avcodec_send_frame(drain)", result);
        ReleaseResources(false);
        finished_ = true;
        return FFFResult::FfmpegFailure;
    }
    const auto drained = DrainPackets();
    if (drained != FFFResult::Success) {
        ReleaseResources(false);
        finished_ = true;
        return drained;
    }
    StopWriter(false);
    if (writerFailed_) {
        ReleaseResources(false);
        finished_ = true;
        return FFFResult::FfmpegFailure;
    }
    result = av_write_trailer(formatContext_);
    if (result < 0) {
        SetFfmpegError("av_write_trailer", result);
        ReleaseResources(false);
        finished_ = true;
        return FFFResult::FfmpegFailure;
    }
    headerWritten_ = false;
    ReleaseResources(false);
    finished_ = true;
    return FFFResult::Success;
}

// 把一个端点包交给对应 AAC 轨道。trackIndex 由 Session 在创建采集器时固定，调用期间 data
// 只借用到返回；AudioTrackEncoder 会在 WASAPI ReleaseBuffer 前完成重采样或复制进 FIFO。
FFFResult VideoMuxer::EncodeAudio(const std::size_t trackIndex, const std::uint8_t* data,
    const std::uint32_t frameCount, const std::uint32_t flags,
    const std::int64_t targetPresentationSample, const WasapiSampleFormat& inputFormat) noexcept {
    if (!initialized_ || finished_) return FFFResult::InvalidState;
    if (audioMixer_ != nullptr) {
        const auto result = audioMixer_->Encode(trackIndex, data, frameCount, flags,
            targetPresentationSample, inputFormat);
        if (result != FFFResult::Success) lastError_ = audioMixer_->LastError();
        return result;
    }
    if (trackIndex >= audioTracks_.size()) return FFFResult::InvalidArgument;
    const auto result = audioTracks_[trackIndex]->Encode(data, frameCount, flags,
        targetPresentationSample, inputFormat);
    if (result != FFFResult::Success) lastError_ = audioTracks_[trackIndex]->LastError();
    return result;
}

// 强制释放编码、COM 和文件句柄，不发送 drain 帧也不写 trailer。该方法幂等且不抛异常，
// 仅供设备移除、时间戳损坏或进程关闭等无法安全继续编码的路径使用。
void VideoMuxer::Abort() noexcept {
    StopWriter(true);
    ReleaseResources(false);
    finished_ = true;
}

// 返回最近一次失败消息的值副本。视频、音频和文件写线程会并发更新错误文本，存储对象内部
// 使用独立互斥锁，不会重新串行化正常的编码路径。
std::string VideoMuxer::LastError() const {
    return lastError_.Copy();
}

// 返回当前等待文件写线程处理的已编码 packet 数。该值不包含编码器内部 surface 或正在写入的
// 单个 packet，可无锁用于高频状态展示。
std::uint32_t VideoMuxer::QueueDepth() const noexcept { return queueDepth_.load(); }

// 返回本次会话 packet 队列达到过的最大深度，用于判断磁盘抖动是否形成持续积压。
std::uint32_t VideoMuxer::PeakQueueDepth() const noexcept { return peakQueueDepth_.load(); }

// 返回最近一个 Matroska packet 的实际交错写入耗时，单位微秒。
std::uint64_t VideoMuxer::LastWriteMicroseconds() const noexcept { return lastWriteMicroseconds_.load(); }

// 返回本次会话单个 Matroska packet 的最大写入耗时，单位微秒。
std::uint64_t VideoMuxer::PeakWriteMicroseconds() const noexcept { return peakWriteMicroseconds_.load(); }

std::uint64_t VideoMuxer::VideoBytes() const noexcept { return videoBytes_.load(); }

std::uint64_t VideoMuxer::AudioBytes() const noexcept { return audioBytes_.load(); }

// 读取一个逻辑音频源最近的时间线误差。混音模式查询 mixer，独立轨模式查询对应 AAC 轨。
std::int64_t VideoMuxer::AudioTimelineErrorSamples(const std::size_t sourceIndex) const noexcept {
    if (audioMixer_ != nullptr) return audioMixer_->TimelineErrorSamples(sourceIndex);
    return sourceIndex < audioTracks_.size() ? audioTracks_[sourceIndex]->TimelineErrorSamples() : 0;
}

// 读取一个逻辑音频源最近实际应用的重采样补偿 ppm，未启用音频时返回零。
std::int32_t VideoMuxer::AudioCompensationPpm(const std::size_t sourceIndex) const noexcept {
    if (audioMixer_ != nullptr) return audioMixer_->CompensationPpm(sourceIndex);
    return sourceIndex < audioTracks_.size() ? audioTracks_[sourceIndex]->CompensationPpm() : 0;
}

// 循环接收编码 packet，转换到 Matroska stream time_base 后交错写入。EAGAIN 表示本轮已取尽，
// EOF 表示 drain 完成，两者都不是错误；packet 无论写入成功与否都由 av_packet_free 释放。
FFFResult VideoMuxer::DrainPackets() noexcept {
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) {
        lastError_ = "Could not allocate an FFmpeg packet.";
        return FFFResult::FfmpegFailure;
    }
    const auto enqueueVideoPacket = [&](AVPacket* output, const AVRational sourceTimeBase) {
        if (output->duration <= 0) output->duration = 1;
        av_packet_rescale_ts(output, sourceTimeBase, videoStream_->time_base);
        output->stream_index = videoStream_->index;
        return EnqueuePacket(output);
    };
    const auto receiveFilteredPackets = [&]() {
        while (true) {
            const auto filteredResult = av_bsf_receive_packet(videoBitstreamFilter_, packet);
            if (filteredResult == AVERROR(EAGAIN) || filteredResult == AVERROR_EOF)
                return FFFResult::Success;
            if (filteredResult < 0) {
                SetFfmpegError("av_bsf_receive_packet(hevc_metadata)", filteredResult);
                return FFFResult::FfmpegFailure;
            }
            const auto writeResult = enqueueVideoPacket(packet, videoBitstreamFilter_->time_base_out);
            av_packet_unref(packet);
            if (writeResult != FFFResult::Success) return writeResult;
        }
    };
    while (true) {
        const auto result = avcodec_receive_packet(codecContext_, packet);
        if (result == AVERROR(EAGAIN)) {
            av_packet_free(&packet);
            return FFFResult::Success;
        }
        if (result == AVERROR_EOF) {
            if (videoBitstreamFilter_ != nullptr) {
                const auto flushResult = av_bsf_send_packet(videoBitstreamFilter_, nullptr);
                if (flushResult < 0 && flushResult != AVERROR_EOF) {
                    SetFfmpegError("av_bsf_send_packet(hevc_metadata drain)", flushResult);
                    av_packet_free(&packet);
                    return FFFResult::FfmpegFailure;
                }
                const auto filteredResult = receiveFilteredPackets();
                av_packet_free(&packet);
                return filteredResult;
            }
            av_packet_free(&packet);
            return FFFResult::Success;
        }
        if (result < 0) {
            SetFfmpegError("avcodec_receive_packet", result);
            av_packet_free(&packet);
            return FFFResult::FfmpegFailure;
        }
        // The codec time base is one output frame. Supplying an explicit packet duration keeps
        // Matroska DefaultDuration and player CFR detection deterministic even when an encoder
        // omits packet durations for hardware frames.
        FFFResult writeResult;
        if (videoBitstreamFilter_ != nullptr) {
            const auto filterResult = av_bsf_send_packet(videoBitstreamFilter_, packet);
            if (filterResult < 0) {
                SetFfmpegError("av_bsf_send_packet(hevc_metadata)", filterResult);
                av_packet_free(&packet);
                return FFFResult::FfmpegFailure;
            }
            writeResult = receiveFilteredPackets();
        } else {
            writeResult = enqueueVideoPacket(packet, codecContext_->time_base);
            av_packet_unref(packet);
        }
        if (writeResult != FFFResult::Success) {
            av_packet_free(&packet);
            return writeResult;
        }
    }
}

// 克隆一个编码 packet 并放入有界写队列。调用返回后编码器可立即复用原 packet；队列达到
// 2048 个 packet 时明确失败，避免慢盘无限消耗内存。短时抖动只增加队列深度，不阻塞调用线程。
FFFResult VideoMuxer::EnqueuePacket(const AVPacket* packet) noexcept {
    if (packet == nullptr) return FFFResult::InvalidArgument;
    AVPacket* copy = av_packet_clone(packet);
    if (copy == nullptr) {
        std::scoped_lock lock(packetMutex_);
        lastError_ = "Could not clone an encoded packet for the asynchronous writer.";
        return FFFResult::FfmpegFailure;
    }
    {
        std::scoped_lock lock(packetMutex_);
        if (writerFailed_ || writerStopRequested_) {
            av_packet_free(&copy);
            if (lastError_.empty()) lastError_ = "The asynchronous Matroska writer is not available.";
            return FFFResult::FfmpegFailure;
        }
        if (packetQueue_.size() >= 2048) {
            av_packet_free(&copy);
            lastError_ = "The Matroska packet queue exceeded 2048 packets; the output device is too slow.";
            return FFFResult::FfmpegFailure;
        }
        packetQueue_.push_back(copy);
        const auto depth = static_cast<std::uint32_t>(packetQueue_.size());
        queueDepth_.store(depth);
        auto peak = peakQueueDepth_.load();
        while (peak < depth && !peakQueueDepth_.compare_exchange_weak(peak, depth)) {}
    }
    packetCondition_.notify_one();
    return FFFResult::Success;
}

// 在 Matroska header 成功写入后创建唯一文件写线程。线程启动失败会保留可读错误并阻止会话
// 进入 Running，确保不会产生只有 header 而无人消费 packet 的伪成功文件。
FFFResult VideoMuxer::StartWriter() noexcept {
    std::scoped_lock lock(packetMutex_);
    writerStopRequested_ = false;
    writerFailed_ = false;
    queueDepth_.store(0);
    peakQueueDepth_.store(0);
    lastWriteMicroseconds_.store(0);
    peakWriteMicroseconds_.store(0);
    videoBytes_.store(0);
    audioBytes_.store(0);
    try {
        writerThread_ = std::thread(&VideoMuxer::WriterThread, this);
        return FFFResult::Success;
    } catch (...) {
        writerFailed_ = true;
        lastError_ = "Could not create the asynchronous Matroska writer thread.";
        return FFFResult::NativeFailure;
    }
}

// 请求文件写线程退出并等待其释放。正常停止会先写完队列；强制中止会释放尚未写入的 packet，
// 但不能取消已经进入操作系统的单次同步写调用。方法可重复调用且不会从写线程自我 join。
void VideoMuxer::StopWriter(const bool discardPending) noexcept {
    {
        std::scoped_lock lock(packetMutex_);
        if (discardPending) {
            for (auto*& packet : packetQueue_) av_packet_free(&packet);
            packetQueue_.clear();
            queueDepth_.store(0);
        }
        writerStopRequested_ = true;
    }
    packetCondition_.notify_all();
    if (writerThread_.joinable() && writerThread_.get_id() != std::this_thread::get_id()) writerThread_.join();
}

// 串行消费音视频 packet 并调用 av_interleaved_write_frame。写入耗时和队列深度在原子字段中
// 更新；首次 IO/封装错误会清空后续队列、保存 FFmpeg 文本并让所有生产者立即失败。
void VideoMuxer::WriterThread() noexcept {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    while (true) {
        AVPacket* packet = nullptr;
        {
            std::unique_lock lock(packetMutex_);
            packetCondition_.wait(lock, [this] { return writerStopRequested_ || !packetQueue_.empty(); });
            if (packetQueue_.empty()) {
                if (writerStopRequested_) return;
                continue;
            }
            packet = packetQueue_.front();
            packetQueue_.pop_front();
            queueDepth_.store(static_cast<std::uint32_t>(packetQueue_.size()));
        }
        LARGE_INTEGER started{};
        LARGE_INTEGER finished{};
        const auto packetBytes = static_cast<std::uint64_t>(std::max(packet->size, 0));
        const auto videoPacket = packet->stream_index == videoStream_->index;
        QueryPerformanceCounter(&started);
        const auto writeResult = av_interleaved_write_frame(formatContext_, packet);
        QueryPerformanceCounter(&finished);
        av_packet_free(&packet);
        const auto microseconds = frequency.QuadPart > 0 ? static_cast<std::uint64_t>(
            std::max<LONGLONG>(0, (finished.QuadPart - started.QuadPart) * 1'000'000LL / frequency.QuadPart)) : 0;
        lastWriteMicroseconds_.store(microseconds);
        auto peak = peakWriteMicroseconds_.load();
        while (peak < microseconds && !peakWriteMicroseconds_.compare_exchange_weak(peak, microseconds)) {}
        if (writeResult < 0) {
            std::scoped_lock lock(packetMutex_);
            SetFfmpegError("av_interleaved_write_frame", writeResult);
            writerFailed_ = true;
            writerStopRequested_ = true;
            for (auto*& queued : packetQueue_) av_packet_free(&queued);
            packetQueue_.clear();
            queueDepth_.store(0);
            return;
        }
        if (videoPacket) videoBytes_.fetch_add(packetBytes);
        else audioBytes_.fetch_add(packetBytes);
    }
}

// 将一个 FFmpeg 错误码格式化为“操作: 文本 (数值)”并保存。固定栈缓冲区避免跨 CRT
// 分配；av_strerror 失败时仍保留数值码，便于从日志定位具体 API。
void VideoMuxer::SetFfmpegError(const char* operation, const int error) noexcept {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error, buffer, sizeof(buffer)) == 0) {
        lastError_ = std::string(operation) + ": " + buffer + " (" + std::to_string(error) + ")";
    } else {
        lastError_ = std::string(operation) + ": FFmpeg error " + std::to_string(error);
    }
}

// 按与初始化相反的顺序释放 AVIO、format、codec、hardware frames/device 和 D3D11 Context。
// writeTrailer 仅供未来恢复策略保留；当前正常路径在 Finish 中显式写 trailer，避免重复写入。
void VideoMuxer::ReleaseResources(const bool writeTrailer) noexcept {
    StopWriter(true);
    if (writeTrailer && headerWritten_ && formatContext_ != nullptr) av_write_trailer(formatContext_);
    headerWritten_ = false;
    audioMixer_.reset();
    audioTracks_.clear();
    if (formatContext_ != nullptr && formatContext_->pb != nullptr &&
        (formatContext_->oformat->flags & AVFMT_NOFILE) == 0) avio_closep(&formatContext_->pb);
    if (formatContext_ != nullptr) avformat_free_context(formatContext_);
    formatContext_ = nullptr;
    videoStream_ = nullptr;
    if (videoBitstreamFilter_ != nullptr) av_bsf_free(&videoBitstreamFilter_);
    if (codecContext_ != nullptr) avcodec_free_context(&codecContext_);
    if (videoProcessor_ != nullptr) {
        videoProcessor_->Release();
        videoProcessor_ = nullptr;
    }
    if (videoProcessorEnumerator_ != nullptr) {
        videoProcessorEnumerator_->Release();
        videoProcessorEnumerator_ = nullptr;
    }
    if (videoContext_ != nullptr) {
        videoContext_->Release();
        videoContext_ = nullptr;
    }
    if (videoDevice_ != nullptr) {
        videoDevice_->Release();
        videoDevice_ = nullptr;
    }
    if (rgbToYuvShader_ != nullptr) {
        rgbToYuvShader_->Release();
        rgbToYuvShader_ = nullptr;
    }
    for (std::size_t plane = 0; plane < 3; ++plane) {
        if (planarYuvStagingTextures_[plane] != nullptr) {
            planarYuvStagingTextures_[plane]->Release();
            planarYuvStagingTextures_[plane] = nullptr;
        }
        if (planarYuvGpuTextures_[plane] != nullptr) {
            planarYuvGpuTextures_[plane]->Release();
            planarYuvGpuTextures_[plane] = nullptr;
        }
    }
    if (d3d11Device3_ != nullptr) {
        d3d11Device3_->Release();
        d3d11Device3_ = nullptr;
    }
    if (encoderFrames_ != nullptr) av_buffer_unref(&encoderFrames_);
    if (encoderHardwareDevice_ != nullptr) av_buffer_unref(&encoderHardwareDevice_);
    if (hardwareFrames_ != nullptr) av_buffer_unref(&hardwareFrames_);
    if (hardwareDevice_ != nullptr) av_buffer_unref(&hardwareDevice_);
    if (immediateContext_ != nullptr) {
        immediateContext_->Release();
        immediateContext_ = nullptr;
    }
    initialized_ = false;
    d3d11Device_ = nullptr;
    inputDxgiFormat_ = 0;
    encoderBackend_ = VideoEncoderBackend::None;
    rgbToYuvPath_ = RgbToYuvPath::None;
    yuvBitPacking_ = YuvBitPacking::EightBit;
    tenBit_ = false;
    chromaSampling_ = 0;
}
