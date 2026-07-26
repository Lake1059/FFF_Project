#include "pch.h"
#include "3FP/Render/VideoRenderer.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <d3dcompiler.h>
#include <d2d1helper.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
constexpr float TrueHdrOutputPeakNits = 1000.0f;

float Clamp01(const float value) noexcept { return std::clamp(value, 0.0f, 1.0f); }

int ToSwsColorSpace(const AVColorSpace colorSpace, const int width) noexcept {
    switch (colorSpace) {
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return SWS_CS_BT2020;
    case AVCOL_SPC_BT709:
        return SWS_CS_ITU709;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return SWS_CS_ITU601;
    default:
        // Untagged HD/UHD sources conventionally use Rec.709; SD uses Rec.601.
        return width >= 1280 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    }
}

bool IsFullRange(const AVFrame* frame) noexcept {
    if (frame->color_range == AVCOL_RANGE_JPEG) return true;
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;
}

bool IsRec2020(const AVFrame* frame) noexcept {
    return frame->color_primaries == AVCOL_PRI_BT2020 ||
        frame->colorspace == AVCOL_SPC_BT2020_NCL || frame->colorspace == AVCOL_SPC_BT2020_CL;
}

float PqToNits(float value) noexcept {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    value = std::pow(Clamp01(value), 1.0f / m2);
    const auto numerator = std::max(value - c1, 0.0f);
    const auto denominator = std::max(c2 - c3 * value, 1.0e-6f);
    return 10000.0f * std::pow(numerator / denominator, 1.0f / m1);
}

float NitsToPq(float nits) noexcept {
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;
    const auto powered = std::pow(Clamp01(nits / 10000.0f), m1);
    return std::pow((c1 + c2 * powered) / (1.0f + c3 * powered), m2);
}

float HlgToNits(float value) noexcept {
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;
    value = Clamp01(value);
    const auto scene = value <= 0.5f ? value * value / 3.0f :
        (std::exp((value - c) / a) + b) / 12.0f;
    return 1000.0f * std::pow(std::max(scene, 0.0f), 1.2f);
}

float Bt709ToLinear(float value) noexcept {
    value = Clamp01(value);
    return value < 0.081f ? value / 4.5f : std::pow((value + 0.099f) / 1.099f, 1.0f / 0.45f);
}

float LinearToBt709(float value) noexcept {
    value = std::max(value, 0.0f);
    return Clamp01(value < 0.018f ? 4.5f * value : 1.099f * std::pow(value, 0.45f) - 0.099f);
}

void Convert2020To709(float& r, float& g, float& b) noexcept {
    const auto nr = 1.660491f * r - 0.587641f * g - 0.072850f * b;
    const auto ng = -0.124550f * r + 1.132900f * g - 0.008349f * b;
    const auto nb = -0.018151f * r - 0.100579f * g + 1.118730f * b;
    r = nr; g = ng; b = nb;
}

void Convert709To2020(float& r, float& g, float& b) noexcept {
    const auto nr = 0.627404f * r + 0.329283f * g + 0.043313f * b;
    const auto ng = 0.069097f * r + 0.919540f * g + 0.011362f * b;
    const auto nb = 0.016392f * r + 0.088013f * g + 0.895595f * b;
    r = nr; g = ng; b = nb;
}

float ToneMapNits(const float nits, const float peak) noexcept {
    if (nits <= peak) return nits;
    const auto excess = nits - peak;
    return peak - peak * 0.25f * std::exp(-excess / std::max(peak, 1.0f));
}

constexpr const char* VertexShaderSource = R"(
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    Output value;
    value.uv = float2((id << 1) & 2, id & 2);
    value.position = float4(value.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return value;
})";

constexpr const char* PixelShaderSource = R"(
cbuffer Settings : register(b0) {
    uint ColorMode; uint Transfer; uint Source2020; uint Reserved;
    float SdrPeak; float HdrPeak; float PaperWhite; float Reserved2;
    float SourceWidth; float SourceHeight; float OutputWidth; float OutputHeight;
    uint InputLayout; float SampleScale; float YOffset; float YScale;
    float COffset; float CScale; float Kr; float Kb;
};
Texture2D<float4> Source : register(t0);
Texture2D<float4> ChromaU : register(t1);
Texture2D<float4> ChromaV : register(t2);
SamplerState LinearSampler : register(s0);
float3 PqToNits(float3 v) {
    const float m1=2610.0/16384.0, m2=2523.0/32.0, c1=3424.0/4096.0, c2=2413.0/128.0, c3=2392.0/128.0;
    v=pow(saturate(v),1.0/m2); return 10000.0*pow(max(v-c1,0.0)/max(c2-c3*v,0.000001),1.0/m1);
}
float3 NitsToPq(float3 v) {
    const float m1=2610.0/16384.0, m2=2523.0/32.0, c1=3424.0/4096.0, c2=2413.0/128.0, c3=2392.0/128.0;
    v=pow(saturate(v/10000.0),m1); return pow((c1+c2*v)/(1.0+c3*v),m2);
}
float HlgOne(float v) {
    const float a=0.17883277,b=0.28466892,c=0.55991073;
    float scene = v<=0.5 ? v*v/3.0 : (exp((v-c)/a)+b)/12.0;
    return 1000.0*pow(max(scene,0.0),1.2);
}
float3 HlgToNits(float3 v) { return float3(HlgOne(v.r),HlgOne(v.g),HlgOne(v.b)); }
float LinearOne(float v) { return v<0.081 ? v/4.5 : pow((v+0.099)/1.099,1.0/0.45); }
float3 ToLinear709(float3 v) { return float3(LinearOne(v.r),LinearOne(v.g),LinearOne(v.b)); }
float BtOne(float v) { v=max(v,0.0); return saturate(v<0.018 ? 4.5*v : 1.099*pow(v,0.45)-0.099); }
float3 ToBt709(float3 v) { return float3(BtOne(v.r),BtOne(v.g),BtOne(v.b)); }
float3 To2020(float3 v) { return mul(float3x3(0.627404,0.329283,0.043313, 0.069097,0.919540,0.011362, 0.016392,0.088013,0.895595),v); }
float3 To709(float3 v) { return mul(float3x3(1.660491,-0.587641,-0.072850, -0.124550,1.132900,-0.008349, -0.018151,-0.100579,1.118730),v); }
float ToneOne(float value,float peak) {
    float knee=peak*0.75;
    return value<=knee?value:knee+(peak-knee)*(1.0-exp(-(value-knee)/max(peak-knee,1.0)));
}
float3 Tone(float3 nits,float peak) {
    float maximum=max(max(nits.r,nits.g),nits.b);
    return maximum<=0.000001?nits:nits*(ToneOne(maximum,peak)/maximum);
}
float3 ReadSource(float2 uv) {
    if(InputLayout==0)return Source.Sample(LinearSampler,uv).rgb;
    float y=Source.Sample(LinearSampler,uv).r*SampleScale;
    float2 chroma=InputLayout==1
        ?float2(ChromaU.Sample(LinearSampler,uv).r,ChromaV.Sample(LinearSampler,uv).r)*SampleScale
        :ChromaU.Sample(LinearSampler,uv).rg*SampleScale;
    y=(y-YOffset)*YScale;
    chroma=(chroma-COffset)*CScale;
    float kg=1.0-Kr-Kb;
    return float3(y+(2.0-2.0*Kr)*chroma.y,
        y-Kb*(2.0-2.0*Kb)/kg*chroma.x-Kr*(2.0-2.0*Kr)/kg*chroma.y,
        y+(2.0-2.0*Kb)*chroma.x);
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float sourceAspect=SourceWidth/SourceHeight, outputAspect=OutputWidth/OutputHeight;
    float2 sampleUv=uv;
    if(outputAspect>sourceAspect){ float scale=sourceAspect/outputAspect; if(abs(uv.x-0.5)>scale*0.5)return float4(0,0,0,1); sampleUv.x=(uv.x-0.5)/scale+0.5; }
    else { float scale=outputAspect/sourceAspect; if(abs(uv.y-0.5)>scale*0.5)return float4(0,0,0,1); sampleUv.y=(uv.y-0.5)/scale+0.5; }
    float3 rgb=ReadSource(sampleUv);
    if(ColorMode==1)return float4(rgb,1);
    if(ColorMode==0&&Transfer==0){
        if(Source2020!=0)rgb=ToBt709(To709(ToLinear709(rgb)));
        return float4(rgb,1);
    }
    float3 nits=Transfer==1?PqToNits(rgb):(Transfer==2?HlgToNits(rgb):ToLinear709(rgb)*PaperWhite);
    if(ColorMode==2){ if(Source2020==0)nits=To2020(nits); return float4(NitsToPq(Tone(nits,1000.0)),1); }
    if(Source2020!=0)nits=To709(nits);
    nits*=SdrPeak*0.75/max(PaperWhite,1.0);
    return float4(ToBt709(Tone(nits,SdrPeak)/SdrPeak),1);
})";

constexpr const char* TimedTextPixelShaderSource = R"(
cbuffer Settings : register(b0) {
    uint ColorMode; uint Transfer; uint Source2020; uint Reserved;
    float SdrPeak; float HdrPeak; float PaperWhite; float Reserved2;
};
Texture2D<float4> Overlay : register(t0);
SamplerState LinearSampler : register(s0);
float LinearOne(float v) { return v<0.081 ? v/4.5 : pow((v+0.099)/1.099,1.0/0.45); }
float3 ToLinear709(float3 v) { return float3(LinearOne(v.r),LinearOne(v.g),LinearOne(v.b)); }
float3 To2020(float3 v) { return mul(float3x3(0.627404,0.329283,0.043313, 0.069097,0.919540,0.011362, 0.016392,0.088013,0.895595),v); }
float3 NitsToPq(float3 v) {
    const float m1=2610.0/16384.0, m2=2523.0/32.0, c1=3424.0/4096.0, c2=2413.0/128.0, c3=2392.0/128.0;
    v=pow(saturate(v/10000.0),m1); return pow((c1+c2*v)/(1.0+c3*v),m2);
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float4 value=Overlay.Sample(LinearSampler,uv);
    if(value.a<=0.000001)return 0;
    float3 straight=value.rgb/value.a;
    if(ColorMode==2)straight=NitsToPq(To2020(ToLinear709(straight)*PaperWhite));
    return float4(straight*value.a,value.a);
})";

struct ShaderSettings {
    std::uint32_t colorMode, transfer, source2020, reserved;
    float sdrPeak, hdrPeak, paperWhite, reserved2;
    float sourceWidth, sourceHeight, outputWidth, outputHeight;
    std::uint32_t inputLayout;
    float sampleScale, yOffset, yScale;
    float cOffset, cScale, kr, kb;
};

struct InputDescription {
    std::uint32_t layout = 0;
    std::uint32_t bitDepth = 16;
    float sampleScale = 1.0f;
};

InputDescription DescribeInput(const AVPixelFormat format) noexcept {
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return {1, 8, 1.0f};
    case AV_PIX_FMT_YUV420P10LE:
        return {1, 10, 65535.0f / 1023.0f};
    case AV_PIX_FMT_NV12:
        return {2, 8, 1.0f};
    case AV_PIX_FMT_P010LE:
        return {2, 10, 1.0f};
    default:
        return {};
    }
}

void YuvCoefficients(const AVColorSpace colorSpace, const int width, float& kr, float& kb) noexcept {
    if (colorSpace == AVCOL_SPC_BT2020_NCL || colorSpace == AVCOL_SPC_BT2020_CL) {
        kr = 0.2627f; kb = 0.0593f;
    } else if (colorSpace == AVCOL_SPC_BT709 || (colorSpace == AVCOL_SPC_UNSPECIFIED && width >= 1280)) {
        kr = 0.2126f; kb = 0.0722f;
    } else {
        kr = 0.2990f; kb = 0.1140f;
    }
}

D2D1_COLOR_F ToD2dColor(const std::uint32_t argb) noexcept {
    constexpr float scale = 1.0f / 255.0f;
    return D2D1::ColorF(static_cast<float>((argb >> 16) & 0xff) * scale,
        static_cast<float>((argb >> 8) & 0xff) * scale,
        static_cast<float>(argb & 0xff) * scale,
        static_cast<float>((argb >> 24) & 0xff) * scale);
}

DWRITE_TEXT_ALIGNMENT ToTextAlignment(const FFF3FPTimedTextAlignment value) noexcept {
    switch (value) {
    case FFF3FPTimedTextAlignment::Center: return DWRITE_TEXT_ALIGNMENT_CENTER;
    case FFF3FPTimedTextAlignment::Far: return DWRITE_TEXT_ALIGNMENT_TRAILING;
    default: return DWRITE_TEXT_ALIGNMENT_LEADING;
    }
}

DWRITE_PARAGRAPH_ALIGNMENT ToParagraphAlignment(const FFF3FPTimedTextAlignment value) noexcept {
    switch (value) {
    case FFF3FPTimedTextAlignment::Center: return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    case FFF3FPTimedTextAlignment::Far: return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
    default: return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    }
}
}

PlayerVideoRenderer::PlayerVideoRenderer() noexcept
    : window_(nullptr), device_(nullptr), context_(nullptr), swapChain_(nullptr),
      vertexShader_(nullptr), pixelShader_(nullptr), timedTextPixelShader_(nullptr), sampler_(nullptr), constants_(nullptr),
      sourceTextures_{nullptr, nullptr, nullptr}, sourceViews_{nullptr, nullptr, nullptr},
      timedTextTexture_(nullptr), timedTextView_(nullptr), timedTextBlend_(nullptr),
      d2dFactory_(nullptr), d2dDevice_(nullptr), d2dContext_(nullptr), d2dTarget_(nullptr),
      writeFactory_(nullptr), scaler_(nullptr),
      swapWidth_(0), swapHeight_(0), swapHdr_(false), sourceWidth_(0), sourceHeight_(0),
      sourceInputLayout_(UINT32_MAX), sourceBitDepth_(0),
      requestedMode_(FFF3FPColorMode::MapToSdr), actualMode_(FFF3FPColorMode::MapToSdr),
      sdrPeakNits_(100.0f), hdrPeakNits_(TrueHdrOutputPeakNits),
      paperWhiteNits_(203.0f), timedTextRenderedSequence_(0), timedTextRenderedCommandCount_(0),
      timedTextWidth_(0), timedTextHeight_(0) {}

PlayerVideoRenderer::~PlayerVideoRenderer() { Close(); }

FFFResult PlayerVideoRenderer::SetWindow(const HWND window) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (window != nullptr && !IsWindow(window)) return FFFResult::InvalidArgument;
    if (swapChain_ != nullptr) { swapChain_->Release(); swapChain_ = nullptr; }
    window_ = window;
    swapWidth_ = swapHeight_ = 0;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        fallbackReason_.clear();
        actualMode_ = OutputSupportsHdr() ? FFF3FPColorMode::MapToHdr : FFF3FPColorMode::MapToSdr;
        if (actualMode_ != requestedMode_)
            fallbackReason_ = "The target display or Windows Advanced Color mode does not support true HDR output.";
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetColorMode(const FFF3FPColorMode mode, const float sdrPeakNits,
    const float hdrPeakNits, const float paperWhiteNits) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdrPeakNits) || sdrPeakNits <= 0.0f ||
        !std::isfinite(hdrPeakNits) || hdrPeakNits <= 0.0f || hdrPeakNits > 10000.0f ||
        !std::isfinite(paperWhiteNits) || paperWhiteNits <= 0.0f) return FFFResult::InvalidArgument;
    requestedMode_ = mode;
    sdrPeakNits_ = sdrPeakNits;
    // Target-luminance selection has no UI yet; true HDR is fixed at 1000 nits.
    hdrPeakNits_ = TrueHdrOutputPeakNits;
    paperWhiteNits_ = paperWhiteNits;
    fallbackReason_.clear();
    actualMode_ = requestedMode_;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr && !OutputSupportsHdr()) {
        actualMode_ = FFF3FPColorMode::MapToSdr;
        fallbackReason_ = "The target display or Windows Advanced Color mode does not support true HDR output.";
    }
    if (swapChain_ != nullptr && swapHdr_ != (actualMode_ == FFF3FPColorMode::MapToHdr)) {
        swapChain_->Release(); swapChain_ = nullptr; swapWidth_ = swapHeight_ = 0;
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureDevice() noexcept {
    if (device_ != nullptr) return FFFResult::Success;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selected{};
    const auto result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &selected, &context_);
    if (FAILED(result)) { SetError("Could not create the D3D11 playback device."); return FFFResult::DeviceFailure; }
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&multithread)))) multithread->SetMultithreadProtected(TRUE);
    return FFFResult::Success;
}

bool PlayerVideoRenderer::OutputSupportsHdr() noexcept {
    if (window_ == nullptr || !IsWindow(window_)) return false;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
    const auto monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC description{};
            if (FAILED(output->GetDesc(&description)) || description.Monitor != monitor) continue;
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 description1{};
            if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&description1))) return false;
            return description1.BitsPerColor >= 10 &&
                (description1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                 description1.ColorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020);
        }
    }
    return false;
}

FFFResult PlayerVideoRenderer::EnsureSwapChain(std::uint32_t width, std::uint32_t height) noexcept {
    if (window_ == nullptr) return FFFResult::Success;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        const auto nextMode = OutputSupportsHdr() ? FFF3FPColorMode::MapToHdr : FFF3FPColorMode::MapToSdr;
        if (nextMode != actualMode_) {
            actualMode_ = nextMode;
            fallbackReason_ = nextMode == requestedMode_ ? std::string{} :
                "The target display or Windows Advanced Color mode does not support true HDR output.";
            if (swapChain_ != nullptr) { swapChain_->Release(); swapChain_ = nullptr; }
            swapWidth_ = swapHeight_ = 0;
        }
    }
    const auto deviceResult = EnsureDevice();
    if (deviceResult != FFFResult::Success) return deviceResult;
    RECT client{};
    if (!GetClientRect(window_, &client)) return FFFResult::DeviceFailure;
    width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(client.right - client.left));
    height = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(client.bottom - client.top));
    const bool hdr = actualMode_ == FFF3FPColorMode::MapToHdr;
    if (swapChain_ != nullptr && width == swapWidth_ && height == swapHeight_ && hdr == swapHdr_) return FFFResult::Success;
    if (swapChain_ != nullptr && hdr == swapHdr_) {
        context_->ClearState();
        if (SUCCEEDED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
            swapWidth_ = width; swapHeight_ = height; ReleaseTimedTextResources();
            return FFFResult::Success;
        }
        swapChain_->Release(); swapChain_ = nullptr;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        SetError("Could not obtain the DXGI playback factory."); return FFFResult::DeviceFailure;
    }
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width; description.Height = height;
    description.Format = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1; description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2; description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE; description.Scaling = DXGI_SCALING_STRETCH;
    ComPtr<IDXGISwapChain1> chain1;
    const auto result = factory->CreateSwapChainForHwnd(device_, window_, &description, nullptr, nullptr, &chain1);
    if (FAILED(result) || FAILED(chain1->QueryInterface(IID_PPV_ARGS(&swapChain_)))) {
        SetError("Could not create the playback swap chain."); return FFFResult::DeviceFailure;
    }
    swapWidth_ = width; swapHeight_ = height; swapHdr_ = hdr;
    ReleaseTimedTextResources();
    if (hdr) {
        UINT support = 0;
        if (FAILED(swapChain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support)) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020))) {
            fallbackReason_ = "The swap chain rejected the Rec.2020 PQ color space.";
            actualMode_ = FFF3FPColorMode::MapToSdr;
            swapChain_->Release(); swapChain_ = nullptr;
            return EnsureSwapChain(width, height);
        }
        SetHdrMetadata();
    } else {
        swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsurePipeline(const std::uint32_t sourceWidth,
    const std::uint32_t sourceHeight, const std::uint32_t inputLayout,
    const std::uint32_t bitDepth) noexcept {
    if (vertexShader_ == nullptr || pixelShader_ == nullptr || timedTextPixelShader_ == nullptr) {
        ComPtr<ID3DBlob> vertexCode, pixelCode, timedTextPixelCode, errors;
        if (FAILED(D3DCompile(VertexShaderSource, std::strlen(VertexShaderSource), nullptr, nullptr, nullptr,
            "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertexCode, &errors)) ||
            FAILED(D3DCompile(PixelShaderSource, std::strlen(PixelShaderSource), nullptr, nullptr, nullptr,
                "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelCode, &errors)) ||
            FAILED(D3DCompile(TimedTextPixelShaderSource, std::strlen(TimedTextPixelShaderSource), nullptr, nullptr, nullptr,
                "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &timedTextPixelCode, &errors)) ||
            FAILED(device_->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_)) ||
            FAILED(device_->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader_)) ||
            FAILED(device_->CreatePixelShader(timedTextPixelCode->GetBufferPointer(), timedTextPixelCode->GetBufferSize(), nullptr, &timedTextPixelShader_))) {
            SetError(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Could not create the HDR presentation shaders.");
            return FFFResult::DeviceFailure;
        }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(ShaderSettings); buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateSamplerState(&sampler, &sampler_)) || FAILED(device_->CreateBuffer(&buffer, nullptr, &constants_))) {
            SetError("Could not create the presentation shader resources."); return FFFResult::DeviceFailure;
        }
    }
    if (sourceTextures_[0] != nullptr && sourceWidth_ == sourceWidth && sourceHeight_ == sourceHeight &&
        sourceInputLayout_ == inputLayout && sourceBitDepth_ == bitDepth)
        return FFFResult::Success;
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        if (sourceViews_[plane] != nullptr) { sourceViews_[plane]->Release(); sourceViews_[plane] = nullptr; }
        if (sourceTextures_[plane] != nullptr) { sourceTextures_[plane]->Release(); sourceTextures_[plane] = nullptr; }
    }
    const auto planeCount = inputLayout == 1 ? 3u : (inputLayout == 2 ? 2u : 1u);
    for (std::uint32_t plane = 0; plane < planeCount; ++plane) {
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = plane == 0 ? sourceWidth : (sourceWidth + 1) / 2;
        texture.Height = plane == 0 ? sourceHeight : (sourceHeight + 1) / 2;
        texture.MipLevels = texture.ArraySize = 1;
        if (inputLayout == 0) texture.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
        else if (inputLayout == 2 && plane == 1)
            texture.Format = bitDepth > 8 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
        else texture.Format = bitDepth > 8 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT; texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&texture, nullptr, &sourceTextures_[plane])) ||
            FAILED(device_->CreateShaderResourceView(sourceTextures_[plane], nullptr, &sourceViews_[plane]))) {
            SetError("Could not create the decoded frame textures."); return FFFResult::DeviceFailure;
        }
    }
    sourceWidth_ = sourceWidth; sourceHeight_ = sourceHeight;
    sourceInputLayout_ = inputLayout; sourceBitDepth_ = bitDepth;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetTimedTextLayer(TimedTextRenderLayer layer) noexcept {
    try {
        std::lock_guard lock(timedTextMutex_);
        if (layer.sequence == 0) layer.sequence = timedTextLayer_.sequence + 1;
        timedTextLayer_ = std::move(layer);
        return FFFResult::Success;
    } catch (...) {
        SetError("Could not retain the timed-text command layer.");
        return FFFResult::NativeFailure;
    }
}

FFFResult PlayerVideoRenderer::GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (status.size < sizeof(FFF3FPTimedTextStatus) || status.version != 1)
        return FFFResult::InvalidArgument;
    {
        std::lock_guard lock(timedTextMutex_);
        status.size = sizeof(status); status.version = 1;
        status.submittedSequence = timedTextLayer_.sequence;
        status.renderedSequence = timedTextRenderedSequence_;
        status.commandCount = timedTextRenderedCommandCount_;
        status.canvasWidth = timedTextWidth_; status.canvasHeight = timedTextHeight_;
        status.reserved = 0; status.visiblePixelCount = 0;
    }
    if (status.submittedSequence != status.renderedSequence || status.commandCount == 0 ||
        timedTextTexture_ == nullptr || device_ == nullptr || context_ == nullptr)
        return FFFResult::Success;
    D3D11_TEXTURE2D_DESC description{};
    timedTextTexture_->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &staging))) return FFFResult::DeviceFailure;
    context_->CopyResource(staging.Get(), timedTextTexture_);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return FFFResult::DeviceFailure;
    std::uint64_t visible = 0;
    for (std::uint32_t y = 0; y < description.Height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
            static_cast<std::size_t>(mapped.RowPitch) * y;
        for (std::uint32_t x = 0; x < description.Width; ++x)
            if (row[static_cast<std::size_t>(x) * 4 + 3] != 0) ++visible;
    }
    context_->Unmap(staging.Get(), 0);
    status.visiblePixelCount = visible;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureTimedTextResources() noexcept {
    if (swapWidth_ == 0 || swapHeight_ == 0) return FFFResult::Success;
    if (timedTextTexture_ != nullptr && timedTextWidth_ == swapWidth_ && timedTextHeight_ == swapHeight_)
        return FFFResult::Success;
    ReleaseTimedTextResources();
    if (d2dFactory_ == nullptr && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        IID_PPV_ARGS(&d2dFactory_)))) {
        SetError("Could not create the Direct2D timed-text factory."); return FFFResult::DeviceFailure;
    }
    if (writeFactory_ == nullptr && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory_)))) {
        SetError("Could not create the DirectWrite timed-text factory."); return FFFResult::DeviceFailure;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_)) ||
        FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_))) {
        SetError("Could not bind Direct2D to the D3D11 playback device."); return FFFResult::DeviceFailure;
    }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = swapWidth_; texture.Height = swapHeight_;
    texture.MipLevels = texture.ArraySize = 1; texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture.SampleDesc.Count = 1; texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &timedTextTexture_)) ||
        FAILED(device_->CreateShaderResourceView(timedTextTexture_, nullptr, &timedTextView_))) {
        SetError("Could not create the GPU timed-text surface."); return FFFResult::DeviceFailure;
    }
    ComPtr<IDXGISurface> surface;
    if (FAILED(timedTextTexture_->QueryInterface(IID_PPV_ARGS(&surface)))) {
        SetError("Could not expose the GPU timed-text surface to Direct2D."); return FFFResult::DeviceFailure;
    }
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &d2dTarget_))) {
        SetError("Could not make the GPU timed-text surface a Direct2D target."); return FFFResult::DeviceFailure;
    }
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blend, &timedTextBlend_))) {
        SetError("Could not create the GPU timed-text blend state."); return FFFResult::DeviceFailure;
    }
    timedTextWidth_ = swapWidth_; timedTextHeight_ = swapHeight_;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::DrawTimedText() noexcept {
    TimedTextRenderLayer layer;
    {
        std::lock_guard lock(timedTextMutex_);
        if (timedTextLayer_.sequence == timedTextRenderedSequence_) return FFFResult::Success;
        try { layer = timedTextLayer_; }
        catch (...) { SetError("Could not snapshot the timed-text command layer."); return FFFResult::NativeFailure; }
    }
    if (layer.commands.empty() && timedTextTexture_ == nullptr) {
        std::lock_guard lock(timedTextMutex_);
        timedTextRenderedSequence_ = layer.sequence;
        timedTextRenderedCommandCount_ = 0;
        timedTextWidth_ = swapWidth_; timedTextHeight_ = swapHeight_;
        return FFFResult::Success;
    }
    const auto resourceResult = EnsureTimedTextResources();
    if (resourceResult != FFFResult::Success) return resourceResult;
    d2dContext_->SetTarget(d2dTarget_);
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2dContext_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0, 0));
    const auto scaleX = layer.canvasWidth == 0 ? 1.0f : static_cast<float>(swapWidth_) / layer.canvasWidth;
    const auto scaleY = layer.canvasHeight == 0 ? 1.0f : static_cast<float>(swapHeight_) / layer.canvasHeight;
    for (const auto& command : layer.commands) {
        const auto destination = D2D1::RectF(command.x * scaleX, command.y * scaleY,
            (command.x + command.width) * scaleX, (command.y + command.height) * scaleY);
        if (command.type == FFF3FPTimedTextCommandType::Bitmap) {
            D2D1_BITMAP_PROPERTIES properties{};
            properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED);
            properties.dpiX = properties.dpiY = 96.0f;
            ComPtr<ID2D1Bitmap> bitmap;
            if (SUCCEEDED(d2dContext_->CreateBitmap(D2D1::SizeU(command.bitmapWidth, command.bitmapHeight),
                command.bitmap.data(), command.bitmapStride, &properties, &bitmap)))
                d2dContext_->DrawBitmap(bitmap.Get(), destination, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            continue;
        }
        ComPtr<IDWriteTextFormat> format;
        const auto weight = (static_cast<std::uint32_t>(command.flags) &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Bold)) != 0
            ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        const auto style = (static_cast<std::uint32_t>(command.flags) &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Italic)) != 0
            ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        const auto fontSize = std::max(command.fontSize * scaleY, 1.0f);
        if (FAILED(writeFactory_->CreateTextFormat(command.fontFamily.c_str(), nullptr, weight, style,
            DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &format))) continue;
        format->SetTextAlignment(ToTextAlignment(command.horizontalAlignment));
        format->SetParagraphAlignment(ToParagraphAlignment(command.verticalAlignment));
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(writeFactory_->CreateTextLayout(command.text.c_str(),
            static_cast<UINT32>(command.text.size()), format.Get(),
            std::max(destination.right - destination.left, 1.0f),
            std::max(destination.bottom - destination.top, 1.0f), &layout))) continue;
        if ((static_cast<std::uint32_t>(command.flags) & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Underline)) != 0)
            layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(command.text.size())});
        if ((static_cast<std::uint32_t>(command.flags) & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Strikeout)) != 0)
            layout->SetStrikethrough(TRUE, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(command.text.size())});
        const auto origin = D2D1::Point2F(destination.left, destination.top);
        const auto outline = std::max(command.outlineWidth * (scaleX + scaleY) * 0.5f, 0.0f);
        if (outline > 0 && (command.outlineArgb >> 24) != 0) {
            ComPtr<ID2D1SolidColorBrush> outlineBrush;
            if (SUCCEEDED(d2dContext_->CreateSolidColorBrush(ToD2dColor(command.outlineArgb), &outlineBrush))) {
                const auto radius = std::max(1, static_cast<int>(std::ceil(outline)));
                constexpr int directions[][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
                for (const auto& direction : directions)
                    d2dContext_->DrawTextLayout(D2D1::Point2F(origin.x + direction[0] * radius,
                        origin.y + direction[1] * radius), layout.Get(), outlineBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        if ((command.foregroundArgb >> 24) != 0) {
            ComPtr<ID2D1SolidColorBrush> foreground;
            if (SUCCEEDED(d2dContext_->CreateSolidColorBrush(ToD2dColor(command.foregroundArgb), &foreground)))
                d2dContext_->DrawTextLayout(origin, layout.Get(), foreground.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    const auto end = d2dContext_->EndDraw();
    d2dContext_->SetTarget(nullptr);
    if (FAILED(end)) { SetError("Direct2D could not render the timed-text layer."); return FFFResult::DeviceFailure; }
    {
        std::lock_guard lock(timedTextMutex_);
        timedTextRenderedSequence_ = layer.sequence;
        timedTextRenderedCommandCount_ = static_cast<std::uint32_t>(layer.commands.size());
    }
    return FFFResult::Success;
}

void PlayerVideoRenderer::CompositeTimedText(ID3D11RenderTargetView* target) noexcept {
    if (timedTextView_ == nullptr || timedTextRenderedCommandCount_ == 0) return;
    ID3D11ShaderResourceView* views[] = {timedTextView_, nullptr, nullptr};
    constexpr float blendFactor[] = {0, 0, 0, 0};
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->OMSetBlendState(timedTextBlend_, blendFactor, UINT_MAX);
    context_->PSSetShader(timedTextPixelShader_, nullptr, 0);
    context_->PSSetShaderResources(0, ARRAYSIZE(views), views);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    context_->OMSetBlendState(nullptr, blendFactor, UINT_MAX);
}

void PlayerVideoRenderer::ReleaseTimedTextResources() noexcept {
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    if (d2dTarget_ != nullptr) { d2dTarget_->Release(); d2dTarget_ = nullptr; }
    if (d2dContext_ != nullptr) { d2dContext_->Release(); d2dContext_ = nullptr; }
    if (d2dDevice_ != nullptr) { d2dDevice_->Release(); d2dDevice_ = nullptr; }
    if (timedTextBlend_ != nullptr) { timedTextBlend_->Release(); timedTextBlend_ = nullptr; }
    if (timedTextView_ != nullptr) { timedTextView_->Release(); timedTextView_ = nullptr; }
    if (timedTextTexture_ != nullptr) { timedTextTexture_->Release(); timedTextTexture_ = nullptr; }
    {
        std::lock_guard lock(timedTextMutex_);
        timedTextWidth_ = timedTextHeight_ = 0;
        timedTextRenderedSequence_ = 0;
        timedTextRenderedCommandCount_ = 0;
    }
}

void PlayerVideoRenderer::SetHdrMetadata() noexcept {
    if (swapChain_ == nullptr || !swapHdr_) return;
    DXGI_HDR_METADATA_HDR10 metadata{};
    metadata.RedPrimary[0] = 35400; metadata.RedPrimary[1] = 14600;
    metadata.GreenPrimary[0] = 8500; metadata.GreenPrimary[1] = 39850;
    metadata.BluePrimary[0] = 6550; metadata.BluePrimary[1] = 2300;
    metadata.WhitePoint[0] = 15635; metadata.WhitePoint[1] = 16450;
    metadata.MaxMasteringLuminance = static_cast<UINT>(TrueHdrOutputPeakNits * 10000.0f);
    metadata.MinMasteringLuminance = 50;
    metadata.MaxContentLightLevel = static_cast<USHORT>(TrueHdrOutputPeakNits);
    metadata.MaxFrameAverageLightLevel = metadata.MaxContentLightLevel;
    swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
}

FFFResult PlayerVideoRenderer::Render(const AVFrame* frame) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return FFFResult::InvalidArgument;
    if (window_ == nullptr) return FFFResult::Success;
    const auto chainResult = EnsureSwapChain(frame->width, frame->height);
    if (chainResult != FFFResult::Success) return chainResult;
    const auto width = static_cast<std::uint32_t>(frame->width);
    const auto height = static_cast<std::uint32_t>(frame->height);
    auto input = DescribeInput(static_cast<AVPixelFormat>(frame->format));
    const auto directYuv = input.layout != 0;
    const auto pipelineResult = EnsurePipeline(width, height, input.layout, input.bitDepth);
    if (pipelineResult != FFFResult::Success) return pipelineResult;
    if (directYuv) {
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, frame->data[0], frame->linesize[0], 0);
        context_->UpdateSubresource(sourceTextures_[1], 0, nullptr, frame->data[1], frame->linesize[1], 0);
        if (input.layout == 1)
            context_->UpdateSubresource(sourceTextures_[2], 0, nullptr, frame->data[2], frame->linesize[2], 0);
    } else {
        scaler_ = sws_getCachedContext(scaler_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_RGBA64LE,
            SWS_BILINEAR | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
        if (scaler_ == nullptr) { SetError("FFmpeg could not create the video conversion context."); return FFFResult::FfmpegFailure; }
        const auto* sourceCoefficients = sws_getCoefficients(ToSwsColorSpace(frame->colorspace, frame->width));
        const auto* destinationCoefficients = sws_getCoefficients(SWS_CS_ITU709);
        if (sourceCoefficients == nullptr || destinationCoefficients == nullptr ||
            sws_setColorspaceDetails(scaler_, sourceCoefficients, IsFullRange(frame) ? 1 : 0,
                destinationCoefficients, 1, 0, 1 << 16, 1 << 16) < 0) {
            SetError("FFmpeg could not configure the frame color matrix and range.");
            return FFFResult::FfmpegFailure;
        }
        rgba64_.resize(static_cast<std::size_t>(width) * height * 8);
        std::uint8_t* outputData[] = { rgba64_.data(), nullptr, nullptr, nullptr };
        int outputLines[] = { static_cast<int>(width * 8), 0, 0, 0 };
        if (sws_scale(scaler_, frame->data, frame->linesize, 0, frame->height, outputData, outputLines) <= 0) {
            SetError("FFmpeg could not convert the decoded video frame."); return FFFResult::FfmpegFailure;
        }
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, rgba64_.data(), width * 8, 0);
    }
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return FFFResult::DeviceFailure;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &target))) return FFFResult::DeviceFailure;
    ShaderSettings settings{};
    settings.colorMode = static_cast<std::uint32_t>(actualMode_);
    settings.transfer = frame->color_trc == AVCOL_TRC_SMPTE2084 ? 1u : (frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ? 2u : 0u);
    settings.source2020 = IsRec2020(frame) ? 1u : 0u;
    settings.sdrPeak = sdrPeakNits_; settings.hdrPeak = hdrPeakNits_; settings.paperWhite = paperWhiteNits_;
    settings.sourceWidth = static_cast<float>(width); settings.sourceHeight = static_cast<float>(height);
    settings.outputWidth = static_cast<float>(swapWidth_); settings.outputHeight = static_cast<float>(swapHeight_);
    settings.inputLayout = input.layout; settings.sampleScale = input.sampleScale;
    const auto maximum = static_cast<float>((1u << input.bitDepth) - 1u);
    const auto shift = input.bitDepth > 8 ? input.bitDepth - 8 : 0;
    if (directYuv && !IsFullRange(frame)) {
        settings.yOffset = static_cast<float>(16u << shift) / maximum;
        settings.yScale = maximum / static_cast<float>(219u << shift);
        settings.cOffset = static_cast<float>(128u << shift) / maximum;
        settings.cScale = maximum / static_cast<float>(224u << shift);
    } else {
        settings.yOffset = 0.0f; settings.yScale = 1.0f;
        settings.cOffset = 0.5f; settings.cScale = 1.0f;
    }
    YuvCoefficients(frame->colorspace, frame->width, settings.kr, settings.kb);
    context_->UpdateSubresource(constants_, 0, nullptr, &settings, 0, 0);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(swapWidth_), static_cast<float>(swapHeight_), 0, 1};
    context_->OMSetRenderTargets(1, target.GetAddressOf(), nullptr); context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0); context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &constants_); context_->PSSetSamplers(0, 1, &sampler_);
    context_->PSSetShaderResources(0, ARRAYSIZE(sourceViews_), sourceViews_); context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    const auto timedTextResult = DrawTimedText();
    if (timedTextResult != FFFResult::Success) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        return timedTextResult;
    }
    CompositeTimedText(target.Get());
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    const auto present = swapChain_->Present(1, 0);
    if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
        SetError("The D3D11 playback device was removed."); return FFFResult::DeviceFailure;
    }
    return FAILED(present) ? FFFResult::DeviceFailure : FFFResult::Success;
}

void PlayerVideoRenderer::ClearSurface() noexcept {
    if (context_ != nullptr && device_ != nullptr && swapChain_ != nullptr) {
        ComPtr<ID3D11Texture2D> backBuffer;
        ComPtr<ID3D11RenderTargetView> target;
        if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) &&
            SUCCEEDED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &target))) {
            constexpr float black[] = {0, 0, 0, 1};
            context_->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
            context_->ClearRenderTargetView(target.Get(), black);
            context_->OMSetRenderTargets(0, nullptr, nullptr);
            context_->Flush();
            swapChain_->Present(0, 0);
        }
    }
    if (window_ != nullptr && IsWindow(window_))
        InvalidateRect(window_, nullptr, TRUE);
}

void PlayerVideoRenderer::Close() noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    ClearSurface();
    if (scaler_ != nullptr) { sws_freeContext(scaler_); scaler_ = nullptr; }
    ReleaseTimedTextResources();
    if (writeFactory_ != nullptr) { writeFactory_->Release(); writeFactory_ = nullptr; }
    if (d2dFactory_ != nullptr) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
    {
        std::lock_guard lock(timedTextMutex_);
        timedTextLayer_ = {};
    }
    if (swapChain_ != nullptr) { swapChain_->Release(); swapChain_ = nullptr; }
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        if (sourceViews_[plane] != nullptr) { sourceViews_[plane]->Release(); sourceViews_[plane] = nullptr; }
        if (sourceTextures_[plane] != nullptr) { sourceTextures_[plane]->Release(); sourceTextures_[plane] = nullptr; }
    }
    if (constants_ != nullptr) { constants_->Release(); constants_ = nullptr; }
    if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
    if (pixelShader_ != nullptr) { pixelShader_->Release(); pixelShader_ = nullptr; }
    if (timedTextPixelShader_ != nullptr) { timedTextPixelShader_->Release(); timedTextPixelShader_ = nullptr; }
    if (vertexShader_ != nullptr) { vertexShader_->Release(); vertexShader_ = nullptr; }
    if (context_ != nullptr) { context_->Release(); context_ = nullptr; }
    if (device_ != nullptr) { device_->Release(); device_ = nullptr; }
    rgba64_.clear(); swapWidth_ = swapHeight_ = sourceWidth_ = sourceHeight_ = 0;
    sourceInputLayout_ = UINT32_MAX; sourceBitDepth_ = 0;
}

FFF3FPColorMode PlayerVideoRenderer::ActualColorMode() const noexcept { return actualMode_; }
std::string PlayerVideoRenderer::FallbackReason() const { return fallbackReason_; }
std::string PlayerVideoRenderer::LastError() const { return lastError_; }
void PlayerVideoRenderer::SetError(std::string message) noexcept { try { lastError_ = std::move(message); } catch (...) {} }
