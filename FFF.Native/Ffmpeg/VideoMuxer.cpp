#include "pch.h"
#include "Ffmpeg/VideoMuxer.h"
#include "Audio/AudioTrackEncoder.h"
#include "Audio/AudioMixer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
}

#include <d3dcompiler.h>

namespace {
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
static const float Kg = 1.0 - Kr - Kb;

float3 ConvertRgb(float3 rgb)
{
    float y = dot(rgb, float3(Kr, Kg, Kb));
    float cb = (rgb.b - y) / (2.0 * (1.0 - Kb)) + 0.5;
    float cr = (rgb.r - y) / (2.0 * (1.0 - Kr)) + 0.5;
    return saturate(float3(y, cb, cr));
}

float StoreLuma(float value)
{
#if FULL_RANGE
#if TEN_BIT
    return round(saturate(value) * 1023.0) * (64.0 / 65535.0);
#else
    return saturate(value);
#endif
#else
#if TEN_BIT
    return (round(saturate(value) * 876.0) + 64.0) * (64.0 / 65535.0);
#else
    return (round(saturate(value) * 219.0) + 16.0) / 255.0;
#endif
#endif
}

float2 StoreChroma(float2 value)
{
#if FULL_RANGE
#if TEN_BIT
    return round(saturate(value) * 1023.0) * (64.0 / 65535.0);
#else
    return saturate(value);
#endif
#else
#if TEN_BIT
    return (round(saturate(value) * 896.0) + 64.0) * (64.0 / 65535.0);
#else
    return (round(saturate(value) * 224.0) + 16.0) / 255.0;
#endif
#endif
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
    ChromaU[id.xy] = StoreChroma(yuv.yy).x;
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
        uint2 p1 = min(id.xy + uint2(1, 0), uint2(width - 1, height - 1));
        uint2 p2 = min(id.xy + uint2(0, 1), uint2(width - 1, height - 1));
        uint2 p3 = min(id.xy + uint2(1, 1), uint2(width - 1, height - 1));
        float3 rgb = (Source.Load(int3(id.xy, 0)).rgb + Source.Load(int3(p1, 0)).rgb +
            Source.Load(int3(p2, 0)).rgb + Source.Load(int3(p3, 0)).rgb) * 0.25;
        float2 chroma = StoreChroma(ConvertRgb(rgb).yz);
        ChromaU[id.xy / 2] = chroma.x;
        ChromaV[id.xy / 2] = chroma.y;
    }
#else
    if ((id.x & 1) == 0 && (id.y & 1) == 0) {
        uint2 p1 = min(id.xy + uint2(1, 0), uint2(width - 1, height - 1));
        uint2 p2 = min(id.xy + uint2(0, 1), uint2(width - 1, height - 1));
        uint2 p3 = min(id.xy + uint2(1, 1), uint2(width - 1, height - 1));
        float3 rgb = (Source.Load(int3(id.xy, 0)).rgb + Source.Load(int3(p1, 0)).rgb +
            Source.Load(int3(p2, 0)).rgb + Source.Load(int3(p3, 0)).rgb) * 0.25;
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
      encoderHardwareDevice_(nullptr), encoderFrames_(nullptr), codecContext_(nullptr), formatContext_(nullptr),
      videoStream_(nullptr), width_(0), height_(0), inputDxgiFormat_(0), qsvEncoder_(false),
      softwareEncoder_(false), videoProcessorConversion_(false), shaderConversion_(false),
      softwareYuvConversion_(false), tenBit_(false), chromaSampling_(0), hdr10_(false),
      videoDevice_(nullptr), videoContext_(nullptr), videoContext1_(nullptr), videoProcessorEnumerator_(nullptr),
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
// 输入可为 BGRA8 SDR 或 RGB10 HDR，具体 RGB 到 4:2:0/4:4:4 转换由经过真实探测的硬件编码器完成。
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
    if (hdr10 && (!tenBit || inputTextureFormat != 2)) {
        lastError_ = "HDR10 requires a native ten-bit RGB10 GPU texture; refusing false HDR output.";
        return FFFResult::NotSupported;
    }
    if (hdr10 && encoderName.find("h264") != std::string::npos) {
        lastError_ = "H.264 is not enabled for the HDR10 recording path; use HEVC or AV1.";
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

    width_ = width;
    height_ = height;
    hdr10_ = hdr10;
    qsvEncoder_ = encoderName.find("_qsv") != std::string::npos;
    const bool x264Encoder = encoderName == "libx264";
    const bool x265Encoder = encoderName == "libx265";
    const bool svtAv1Encoder = encoderName == "libsvtav1";
    softwareEncoder_ = x264Encoder || x265Encoder || svtAv1Encoder;
    const bool nvencEncoder = encoderName.find("_nvenc") != std::string::npos;
    const bool amfEncoder = encoderName.find("_amf") != std::string::npos;
    softwareYuvConversion_ = softwareEncoder_ ||
        ((nvencEncoder || qsvEncoder_) && chromaSampling != 0);
    if (chromaSampling != 0 && encoderName.find("av1") != std::string::npos) {
        lastError_ = "The selected FFmpeg AV1 encoder has no valid non-4:2:0 encoding path.";
        return FFFResult::NotSupported;
    }
    if (amfEncoder && chromaSampling != 0) {
        lastError_ = "The FFmpeg AMF encoders only expose NV12/P010 4:2:0 input formats.";
        return FFFResult::NotSupported;
    }
    if (qsvEncoder_ && chromaSampling != 0 && encoderName.find("hevc") == std::string::npos) {
        lastError_ = "Only the FFmpeg HEVC QSV encoder exposes 4:2:2 and 4:4:4 input formats.";
        return FFFResult::NotSupported;
    }
    videoProcessorConversion_ = qsvEncoder_ && !softwareYuvConversion_;
    shaderConversion_ = nvencEncoder || amfEncoder || softwareYuvConversion_;
    tenBit_ = tenBit;
    chromaSampling_ = chromaSampling;
    AVPixelFormat softwareFormat = AV_PIX_FMT_BGRA;
    switch (inputTextureFormat) {
    case 0:
        inputDxgiFormat_ = DXGI_FORMAT_B8G8R8A8_UNORM;
        softwareFormat = AV_PIX_FMT_BGRA;
        break;
    case 2:
        inputDxgiFormat_ = DXGI_FORMAT_R10G10B10A2_UNORM;
        softwareFormat = AV_PIX_FMT_X2BGR10;
        break;
    default:
        lastError_ = "The requested D3D11 encoder input format is not supported.";
        return FFFResult::NotSupported;
    }
    if (softwareEncoder_) {
        if (x264Encoder && tenBit) {
            lastError_ = "The libx264 fallback accepts eight-bit input only.";
            return FFFResult::NotSupported;
        }
        if (chromaSampling == 1)
            softwareFormat = tenBit ? AV_PIX_FMT_YUV444P10 : AV_PIX_FMT_YUV444P;
        else if (chromaSampling == 2)
            softwareFormat = tenBit ? AV_PIX_FMT_YUV422P10 : AV_PIX_FMT_YUV422P;
        else
            softwareFormat = tenBit ? AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;
    } else if (qsvEncoder_) {
        if (chromaSampling == 2)
            softwareFormat = tenBit ? AV_PIX_FMT_Y210 : AV_PIX_FMT_YUYV422;
        else if (chromaSampling == 1)
            softwareFormat = tenBit ? AV_PIX_FMT_XV30 : AV_PIX_FMT_VUYX;
        else
            softwareFormat = tenBit ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
    } else if (nvencEncoder) {
        softwareFormat = softwareYuvConversion_
            ? (chromaSampling == 2
                ? (tenBit ? AV_PIX_FMT_P210 : AV_PIX_FMT_NV16)
                : (tenBit ? AV_PIX_FMT_YUV444P10MSB : AV_PIX_FMT_YUV444P))
            : (tenBit ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12);
    } else if (amfEncoder) {
        softwareFormat = tenBit ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
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

    if (!softwareYuvConversion_) {
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
        frameContext->initial_pool_size = (softwareEncoder_ || shaderConversion_)
            ? 0 : std::max(24U, bFrameCount + 10U);
        if (videoProcessorConversion_ || shaderConversion_) {
            auto* d3d11Frames = reinterpret_cast<AVD3D11VAFramesContext*>(frameContext->hwctx);
            d3d11Frames->BindFlags = videoProcessorConversion_
                ? D3D11_BIND_RENDER_TARGET : D3D11_BIND_UNORDERED_ACCESS;
        }
        result = av_hwframe_ctx_init(hardwareFrames_);
        if (result < 0) {
            SetFfmpegError("av_hwframe_ctx_init", result);
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
    }

    if (qsvEncoder_) {
        result = av_hwdevice_ctx_create_derived(&encoderHardwareDevice_, AV_HWDEVICE_TYPE_QSV,
            hardwareDevice_, 0);
        if (result < 0) {
            SetFfmpegError("av_hwdevice_ctx_create_derived(QSV from D3D11)", result);
            ReleaseResources(false);
            return FFFResult::NotSupported;
        }
        if (!softwareYuvConversion_) {
            result = av_hwframe_ctx_create_derived(&encoderFrames_, AV_PIX_FMT_QSV,
                encoderHardwareDevice_, hardwareFrames_, AV_HWFRAME_MAP_READ | AV_HWFRAME_MAP_DIRECT);
            if (result < 0) {
                SetFfmpegError("av_hwframe_ctx_create_derived(QSV from D3D11 frames)", result);
                ReleaseResources(false);
                return FFFResult::NotSupported;
            }
        }
    } else if (!softwareEncoder_) {
        encoderHardwareDevice_ = av_buffer_ref(hardwareDevice_);
        if (hardwareFrames_ != nullptr) encoderFrames_ = av_buffer_ref(hardwareFrames_);
        if (encoderHardwareDevice_ == nullptr || (!softwareYuvConversion_ && encoderFrames_ == nullptr)) {
            lastError_ = "Could not retain the FFmpeg encoder hardware contexts.";
            ReleaseResources(false);
            return FFFResult::FfmpegFailure;
        }
    }
    if (videoProcessorConversion_) {
        const auto processorResult = InitializeVideoProcessor(frameRateNumerator,
            frameRateDenominator, hdr10, colorRange);
        if (processorResult != FFFResult::Success) {
            ReleaseResources(false);
            return processorResult;
        }
    }
    if (shaderConversion_) {
        const auto converterResult = InitializeRgbToYuvConverter(tenBit, hdr10,
            chromaSampling, softwareYuvConversion_, colorRange);
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
    codecContext_->pix_fmt = softwareYuvConversion_ ? softwareFormat :
        (qsvEncoder_ ? AV_PIX_FMT_QSV : AV_PIX_FMT_D3D11);
    codecContext_->sw_pix_fmt = softwareFormat;
    codecContext_->bit_rate = bitRate;
    codecContext_->gop_size = static_cast<int>(gopSize);
    codecContext_->max_b_frames = static_cast<int>(bFrameCount);
    codecContext_->rc_max_rate = maximumBitRate;
    // Treat the automatic/default mode as PC full range. Only an explicit limited
    // selection requests TV range; this keeps existing recorder configurations full-range.
    codecContext_->color_range = colorRange == 1 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
    codecContext_->color_primaries = hdr10 ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
    codecContext_->color_trc = hdr10 ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;
    codecContext_->colorspace = hdr10 ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
    codecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (!softwareEncoder_) {
        codecContext_->hw_device_ctx = av_buffer_ref(encoderHardwareDevice_);
        if (encoderFrames_ != nullptr) codecContext_->hw_frames_ctx = av_buffer_ref(encoderFrames_);
    }
    if (qsvEncoder_ && rateControl == 1) {
        codecContext_->bit_rate = 0;
        codecContext_->rc_max_rate = 0;
    } else if (rateControl == 2) {
        codecContext_->rc_max_rate = bitRate;
    }

    AVDictionary* encoderOptions = nullptr;
    const bool nvenc = nvencEncoder;
    const bool qsv = qsvEncoder_;
    const bool amf = encoderName.find("_amf") != std::string::npos;
    if ((qsv || amf) && multipass != 0) {
        lastError_ = "The selected backend does not expose the requested multipass mode.";
        av_dict_free(&encoderOptions);
        ReleaseResources(false);
        return FFFResult::NotSupported;
    }
    if (nvenc) {
        av_dict_set(&encoderOptions, "preset", preset.empty() ? "p4" : preset.c_str(), 0);
        av_dict_set(&encoderOptions, "tune", sceneOptimization.empty() ? "hq" : sceneOptimization.c_str(), 0);
        av_dict_set(&encoderOptions, "surfaces", "8", 0);
        const auto selectedProfile = profile.empty() && tenBit && encoderName.find("hevc") != std::string::npos
            ? (chromaSampling != 0 ? "rext" : "main10") : profile.c_str();
        if (selectedProfile != nullptr && *selectedProfile != '\0')
            av_dict_set(&encoderOptions, "profile", selectedProfile, 0);
        if (tenBit && encoderName.find("av1") != std::string::npos)
            av_dict_set(&encoderOptions, "highbitdepth", "1", 0);
        const char* rateControlName = rateControl == 1 ? "constqp" : (rateControl == 2 ? "cbr" : "vbr");
        av_dict_set(&encoderOptions, "rc", rateControlName, 0);
        if (quality >= 0) av_dict_set_int(&encoderOptions,
            rateControl == 1 && qualityMode == 2 ? "cq" : "qp", quality, 0);
        if (lookaheadFrames > 0) av_dict_set_int(&encoderOptions, "rc-lookahead", lookaheadFrames, 0);
        av_dict_set(&encoderOptions, "multipass", multipass == 2 ? "fullres" :
            (multipass == 1 ? "qres" : "disabled"), 0);
    } else if (qsv) {
        std::string selectedPreset = preset;
        if (preset.size() == 2 && preset[0] == 'p' && preset[1] >= '1' && preset[1] <= '7') {
            static constexpr const char* Presets[] = {
                "", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"
            };
            selectedPreset = Presets[preset[1] - '0'];
        }
        if (!selectedPreset.empty()) av_dict_set(&encoderOptions, "preset", selectedPreset.c_str(), 0);
        const auto selectedProfile = profile.empty() && chromaSampling != 0 &&
            encoderName.find("hevc") != std::string::npos ? "rext" : profile.c_str();
        if (selectedProfile != nullptr && *selectedProfile != '\0')
            av_dict_set(&encoderOptions, "profile", selectedProfile, 0);
        if (rateControl == 1 && quality >= 0) {
            codecContext_->flags |= AV_CODEC_FLAG_QSCALE;
            codecContext_->global_quality = quality * FF_QP2LAMBDA;
        }
        if (lookaheadFrames > 0) {
            av_dict_set(&encoderOptions, "look_ahead", "1", 0);
            av_dict_set_int(&encoderOptions, "look_ahead_depth", lookaheadFrames, 0);
        }
    } else if (amf) {
        std::string selectedPreset = preset;
        if (preset.size() == 2 && preset[0] == 'p') selectedPreset = "balanced";
        if (!selectedPreset.empty()) av_dict_set(&encoderOptions, "preset", selectedPreset.c_str(), 0);
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
        const auto selectedPreset = preset.empty() && encoderName == "libx264" ? "medium" : preset.c_str();
        if (selectedPreset != nullptr && *selectedPreset != '\0')
            av_dict_set(&encoderOptions, "preset", selectedPreset, 0);
        if (!profile.empty()) av_dict_set(&encoderOptions, "profile", profile.c_str(), 0);
        if (!sceneOptimization.empty()) av_dict_set(&encoderOptions, "tune", sceneOptimization.c_str(), 0);
        if (rateControl == 1 && quality >= 0)
            av_dict_set_int(&encoderOptions, qualityMode == 1 ? "crf" : "qp", quality, 0);
    }
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
    result = avcodec_parameters_from_context(videoStream_->codecpar, codecContext_);
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
    const std::uint32_t chromaSampling, const bool softwareYuv,
    const std::uint32_t colorRange) noexcept {
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
        { "FULL_RANGE", colorRange == 1 ? "0" : "1" },
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
            result = d3d11Device_->CreateTexture2D(&description, nullptr, &yuv444GpuTextures_[plane]);
            if (FAILED(result)) {
                lastError_ = "Could not create the GPU software YUV conversion plane: " +
                    std::to_string(static_cast<long>(result));
                return FFFResult::DeviceFailure;
            }
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            result = d3d11Device_->CreateTexture2D(&description, nullptr, &yuv444StagingTextures_[plane]);
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
        result = d3d11Device_->CreateUnorderedAccessView(yuv444GpuTextures_[plane], nullptr,
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
        immediateContext_->CopyResource(yuv444StagingTextures_[plane], yuv444GpuTextures_[plane]);
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
        const auto mappedResult = immediateContext_->Map(yuv444StagingTextures_[plane], 0,
            D3D11_MAP_READ, 0, &mappedPlanes[plane]);
        if (FAILED(mappedResult)) {
            for (std::size_t mappedPlane = 0; mappedPlane < mappedPlaneCount; ++mappedPlane)
                immediateContext_->Unmap(yuv444StagingTextures_[mappedPlane], 0);
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
        const bool lowAlignedTenBit =
            codecContext_->pix_fmt == AV_PIX_FMT_YUV420P10 ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV422P10 ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV444P10;
        const bool planar = lowAlignedTenBit ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV420P ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV422P ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV444P ||
            codecContext_->pix_fmt == AV_PIX_FMT_YUV444P10MSB;
        if (!planar) {
            for (std::size_t plane = 0; plane < 3; ++plane)
                immediateContext_->Unmap(yuv444StagingTextures_[plane], 0);
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
        immediateContext_->Unmap(yuv444StagingTextures_[plane], 0);
    frame->pts = presentationTimestamp;
    frame->duration = 1;
    frame->color_range = codecContext_->color_range;
    frame->color_primaries = codecContext_->color_primaries;
    frame->color_trc = codecContext_->color_trc;
    frame->colorspace = codecContext_->colorspace;
    result = avcodec_send_frame(codecContext_, frame);
    av_frame_free(&frame);
    if (result < 0) {
        SetFfmpegError("avcodec_send_frame(software YUV)", result);
        return FFFResult::FfmpegFailure;
    }
    return DrainPackets();
}

// 为需要 NV12/P010 的编码路径创建 D3D11 Video Processor 和颜色空间配置。输入是托管 shader 生成的 BGRA8 SDR
// 或 RGB10 PQ，输出是 FFmpeg surface pool 的 NV12/P010；所有接口都来自同一个 D3D11 Device。
FFFResult VideoMuxer::InitializeVideoProcessor(const std::uint32_t frameRateNumerator,
    const std::uint32_t frameRateDenominator, const bool hdr10,
    const std::uint32_t colorRange) noexcept {
    const bool fullRange = colorRange != 1;
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
    immediateContext_->QueryInterface(IID_PPV_ARGS(&videoContext1_));
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
    result = videoDevice_->CreateVideoProcessor(videoProcessorEnumerator_, 0, &videoProcessor_);
    if (FAILED(result)) {
        lastError_ = "CreateVideoProcessor failed for GPU color conversion: " +
            std::to_string(static_cast<long>(result));
        return FFFResult::DeviceFailure;
    }
    videoContext_->VideoProcessorSetStreamFrameFormat(videoProcessor_, 0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    if (videoContext1_ != nullptr) {
        videoContext1_->VideoProcessorSetStreamColorSpace1(videoProcessor_, 0,
            hdr10 ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        // DXGI has no full-range YCbCr PQ enum. Use the full-range P2020 matrix here;
        // SMPTE 2084 is still carried by the AVFrame, codec VUI, and container metadata.
        videoContext1_->VideoProcessorSetOutputColorSpace1(videoProcessor_,
            hdr10 ? (fullRange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 :
                DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020) :
                (fullRange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 :
                    DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709));
    } else if (hdr10) {
        lastError_ = "HDR conversion requires ID3D11VideoContext1 color-space controls.";
        return FFFResult::NotSupported;
    } else {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inputColor{};
        inputColor.Nominal_Range = 2;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};
        outputColor.YCbCr_Matrix = 1;
        outputColor.Nominal_Range = fullRange ? 2 : 1;
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

    if (softwareYuvConversion_) {
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
    if (shaderConversion_) {
        const auto converted = ConvertTextureToEncoderSurfaceWithShader(sourceTexture, sourceArrayIndex,
            destinationTexture, destinationIndex);
        if (converted != FFFResult::Success) {
            av_frame_free(&frame);
            return converted;
        }
    } else if (videoProcessorConversion_) {
        const auto converted = ConvertTextureToEncoderSurface(sourceTexture, sourceArrayIndex,
            destinationTexture, destinationIndex);
        if (converted != FFFResult::Success) {
            av_frame_free(&frame);
            return converted;
        }
    } else {
        immediateContext_->CopySubresourceRegion(destinationTexture, destinationIndex, 0, 0, 0,
            sourceTexture, sourceArrayIndex, nullptr);
    }
    immediateContext_->Flush();
    frame->pts = presentationTimestamp;
    frame->duration = 1;
    frame->color_range = codecContext_->color_range;
    frame->color_primaries = codecContext_->color_primaries;
    frame->color_trc = codecContext_->color_trc;
    frame->colorspace = codecContext_->colorspace;

    AVFrame* encoderFrame = frame;
    AVFrame* mappedFrame = nullptr;
    AVFrame* softwareFrame = nullptr;
    if (qsvEncoder_) {
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
        encoderFrame = mappedFrame;
    } else if (softwareEncoder_) {
        softwareFrame = av_frame_alloc();
        if (softwareFrame == nullptr) {
            lastError_ = "Could not allocate the software encoder frame.";
            av_frame_free(&frame);
            return FFFResult::FfmpegFailure;
        }
        result = av_hwframe_transfer_data(softwareFrame, frame, 0);
        if (result < 0) {
            SetFfmpegError("av_hwframe_transfer_data(D3D11 to software)", result);
            av_frame_free(&softwareFrame);
            av_frame_free(&frame);
            return FFFResult::FfmpegFailure;
        }
        softwareFrame->pts = presentationTimestamp;
        softwareFrame->duration = 1;
        softwareFrame->color_range = codecContext_->color_range;
        softwareFrame->color_primaries = codecContext_->color_primaries;
        softwareFrame->color_trc = codecContext_->color_trc;
        softwareFrame->colorspace = codecContext_->colorspace;
        encoderFrame = softwareFrame;
    }

    result = avcodec_send_frame(codecContext_, encoderFrame);
    av_frame_free(&softwareFrame);
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

// 返回最近一次失败消息的值副本。当前类由 RecorderSession 串行调用，因此无需额外互斥锁；
// 若后续引入独立编码线程，应由队列所有者在读取和写入此字段时加同一把锁。
std::string VideoMuxer::LastError() const {
    std::scoped_lock lock(packetMutex_);
    return lastError_;
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
    while (true) {
        const auto result = avcodec_receive_packet(codecContext_, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
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
        if (packet->duration <= 0) packet->duration = 1;
        av_packet_rescale_ts(packet, codecContext_->time_base, videoStream_->time_base);
        packet->stream_index = videoStream_->index;
        const auto writeResult = EnqueuePacket(packet);
        av_packet_unref(packet);
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
    if (codecContext_ != nullptr) avcodec_free_context(&codecContext_);
    if (videoProcessor_ != nullptr) {
        videoProcessor_->Release();
        videoProcessor_ = nullptr;
    }
    if (videoProcessorEnumerator_ != nullptr) {
        videoProcessorEnumerator_->Release();
        videoProcessorEnumerator_ = nullptr;
    }
    if (videoContext1_ != nullptr) {
        videoContext1_->Release();
        videoContext1_ = nullptr;
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
        if (yuv444StagingTextures_[plane] != nullptr) {
            yuv444StagingTextures_[plane]->Release();
            yuv444StagingTextures_[plane] = nullptr;
        }
        if (yuv444GpuTextures_[plane] != nullptr) {
            yuv444GpuTextures_[plane]->Release();
            yuv444GpuTextures_[plane] = nullptr;
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
    qsvEncoder_ = false;
    softwareEncoder_ = false;
    videoProcessorConversion_ = false;
    shaderConversion_ = false;
    softwareYuvConversion_ = false;
    tenBit_ = false;
    chromaSampling_ = 0;
    hdr10_ = false;
}
