#include "pch.h"
#include "3FP/Render/VideoRenderer.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <d3dcompiler.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
float Clamp01(const float value) noexcept { return std::clamp(value, 0.0f, 1.0f); }

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
};
Texture2D<float4> Source : register(t0);
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
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float sourceAspect=SourceWidth/SourceHeight, outputAspect=OutputWidth/OutputHeight;
    float2 sampleUv=uv;
    if(outputAspect>sourceAspect){ float scale=sourceAspect/outputAspect; if(abs(uv.x-0.5)>scale*0.5)return float4(0,0,0,1); sampleUv.x=(uv.x-0.5)/scale+0.5; }
    else { float scale=outputAspect/sourceAspect; if(abs(uv.y-0.5)>scale*0.5)return float4(0,0,0,1); sampleUv.y=(uv.y-0.5)/scale+0.5; }
    float3 rgb=Source.Sample(LinearSampler,sampleUv).rgb;
    if(ColorMode==1)return float4(rgb,1);
    float3 nits=Transfer==1?PqToNits(rgb):(Transfer==2?HlgToNits(rgb):ToLinear709(rgb)*PaperWhite);
    if(ColorMode==2){ if(Source2020==0)nits=To2020(nits); return float4(NitsToPq(Tone(nits,HdrPeak)),1); }
    if(Source2020!=0)nits=To709(nits);
    return float4(ToBt709(Tone(nits,SdrPeak)/SdrPeak),1);
})";

struct ShaderSettings {
    std::uint32_t colorMode, transfer, source2020, reserved;
    float sdrPeak, hdrPeak, paperWhite, reserved2;
    float sourceWidth, sourceHeight, outputWidth, outputHeight;
};
}

PlayerVideoRenderer::PlayerVideoRenderer() noexcept
    : window_(nullptr), device_(nullptr), context_(nullptr), swapChain_(nullptr),
      vertexShader_(nullptr), pixelShader_(nullptr), sampler_(nullptr), constants_(nullptr),
      sourceTexture_(nullptr), sourceView_(nullptr), scaler_(nullptr),
      swapWidth_(0), swapHeight_(0), swapHdr_(false), sourceWidth_(0), sourceHeight_(0), requestedMode_(FFF3FPColorMode::MapToSdr),
      actualMode_(FFF3FPColorMode::MapToSdr), sdrPeakNits_(100.0f), hdrPeakNits_(1000.0f),
      paperWhiteNits_(203.0f) {}

PlayerVideoRenderer::~PlayerVideoRenderer() { Close(); }

FFFResult PlayerVideoRenderer::SetWindow(const HWND window) noexcept {
    if (window != nullptr && !IsWindow(window)) return FFFResult::InvalidArgument;
    if (window_ == window) return FFFResult::Success;
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
    if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdrPeakNits) || sdrPeakNits <= 0.0f ||
        !std::isfinite(hdrPeakNits) || hdrPeakNits <= 0.0f || hdrPeakNits > 10000.0f ||
        !std::isfinite(paperWhiteNits) || paperWhiteNits <= 0.0f) return FFFResult::InvalidArgument;
    requestedMode_ = mode;
    sdrPeakNits_ = sdrPeakNits;
    hdrPeakNits_ = hdrPeakNits;
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
            swapWidth_ = width; swapHeight_ = height; return FFFResult::Success;
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
    const std::uint32_t sourceHeight) noexcept {
    if (vertexShader_ == nullptr || pixelShader_ == nullptr) {
        ComPtr<ID3DBlob> vertexCode, pixelCode, errors;
        if (FAILED(D3DCompile(VertexShaderSource, std::strlen(VertexShaderSource), nullptr, nullptr, nullptr,
            "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertexCode, &errors)) ||
            FAILED(D3DCompile(PixelShaderSource, std::strlen(PixelShaderSource), nullptr, nullptr, nullptr,
            "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelCode, &errors)) ||
            FAILED(device_->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_)) ||
            FAILED(device_->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader_))) {
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
    if (sourceTexture_ != nullptr && sourceWidth_ == sourceWidth && sourceHeight_ == sourceHeight) return FFFResult::Success;
    if (sourceView_ != nullptr) { sourceView_->Release(); sourceView_ = nullptr; }
    if (sourceTexture_ != nullptr) { sourceTexture_->Release(); sourceTexture_ = nullptr; }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = sourceWidth; texture.Height = sourceHeight; texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_R16G16B16A16_UNORM; texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT; texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &sourceTexture_)) ||
        FAILED(device_->CreateShaderResourceView(sourceTexture_, nullptr, &sourceView_))) {
        SetError("Could not create the decoded frame texture."); return FFFResult::DeviceFailure;
    }
    sourceWidth_ = sourceWidth; sourceHeight_ = sourceHeight;
    return FFFResult::Success;
}

void PlayerVideoRenderer::SetHdrMetadata() noexcept {
    if (swapChain_ == nullptr || !swapHdr_) return;
    DXGI_HDR_METADATA_HDR10 metadata{};
    metadata.RedPrimary[0] = 35400; metadata.RedPrimary[1] = 14600;
    metadata.GreenPrimary[0] = 8500; metadata.GreenPrimary[1] = 39850;
    metadata.BluePrimary[0] = 6550; metadata.BluePrimary[1] = 2300;
    metadata.WhitePoint[0] = 15635; metadata.WhitePoint[1] = 16450;
    metadata.MaxMasteringLuminance = static_cast<UINT>(std::clamp(hdrPeakNits_, 1.0f, 10000.0f) * 10000.0f);
    metadata.MinMasteringLuminance = 50;
    metadata.MaxContentLightLevel = static_cast<USHORT>(std::clamp(hdrPeakNits_, 1.0f, 10000.0f));
    metadata.MaxFrameAverageLightLevel = metadata.MaxContentLightLevel;
    swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
}

void PlayerVideoRenderer::ConvertPixels(const AVFrame* frame, const std::uint32_t width,
    const std::uint32_t height, const bool hdrOutput) noexcept {
    const auto* source = reinterpret_cast<const std::uint16_t*>(rgba64_.data());
    const bool pq = frame->color_trc == AVCOL_TRC_SMPTE2084;
    const bool hlg = frame->color_trc == AVCOL_TRC_ARIB_STD_B67;
    const bool hdrSource = pq || hlg;
    const bool source2020 = frame->color_primaries == AVCOL_PRI_BT2020;
    if (hdrOutput) output_.resize(static_cast<std::size_t>(width) * height * 4);
    else output_.resize(static_cast<std::size_t>(width) * height * 4);
    auto* destination = reinterpret_cast<std::uint32_t*>(output_.data());
    const auto count = static_cast<std::size_t>(width) * height;
    for (std::size_t index = 0; index < count; ++index) {
        float r = source[index * 4] / 65535.0f;
        float g = source[index * 4 + 1] / 65535.0f;
        float b = source[index * 4 + 2] / 65535.0f;
        if (actualMode_ == FFF3FPColorMode::RawHdrAsSdr) {
            const auto rb = static_cast<std::uint32_t>(std::lround(Clamp01(r) * 255.0f));
            const auto gb = static_cast<std::uint32_t>(std::lround(Clamp01(g) * 255.0f));
            const auto bb = static_cast<std::uint32_t>(std::lround(Clamp01(b) * 255.0f));
            destination[index] = bb | (gb << 8) | (rb << 16) | 0xff000000u;
            continue;
        }
        if (hdrSource) {
            r = pq ? PqToNits(r) : HlgToNits(r);
            g = pq ? PqToNits(g) : HlgToNits(g);
            b = pq ? PqToNits(b) : HlgToNits(b);
        } else {
            r = Bt709ToLinear(r) * paperWhiteNits_;
            g = Bt709ToLinear(g) * paperWhiteNits_;
            b = Bt709ToLinear(b) * paperWhiteNits_;
        }
        if (hdrOutput) {
            if (!source2020) Convert709To2020(r, g, b);
            r = ToneMapNits(r, hdrPeakNits_); g = ToneMapNits(g, hdrPeakNits_); b = ToneMapNits(b, hdrPeakNits_);
            const auto ri = static_cast<std::uint32_t>(std::lround(Clamp01(NitsToPq(r)) * 1023.0f));
            const auto gi = static_cast<std::uint32_t>(std::lround(Clamp01(NitsToPq(g)) * 1023.0f));
            const auto bi = static_cast<std::uint32_t>(std::lround(Clamp01(NitsToPq(b)) * 1023.0f));
            destination[index] = ri | (gi << 10) | (bi << 20) | 0xc0000000u;
        } else {
            if (source2020) Convert2020To709(r, g, b);
            r = LinearToBt709(ToneMapNits(r, sdrPeakNits_) / sdrPeakNits_);
            g = LinearToBt709(ToneMapNits(g, sdrPeakNits_) / sdrPeakNits_);
            b = LinearToBt709(ToneMapNits(b, sdrPeakNits_) / sdrPeakNits_);
            const auto rb = static_cast<std::uint32_t>(std::lround(r * 255.0f));
            const auto gb = static_cast<std::uint32_t>(std::lround(g * 255.0f));
            const auto bb = static_cast<std::uint32_t>(std::lround(b * 255.0f));
            destination[index] = bb | (gb << 8) | (rb << 16) | 0xff000000u;
        }
    }
}

FFFResult PlayerVideoRenderer::Render(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return FFFResult::InvalidArgument;
    if (window_ == nullptr) return FFFResult::Success;
    const auto chainResult = EnsureSwapChain(frame->width, frame->height);
    if (chainResult != FFFResult::Success) return chainResult;
    const auto width = static_cast<std::uint32_t>(frame->width);
    const auto height = static_cast<std::uint32_t>(frame->height);
    const auto pipelineResult = EnsurePipeline(width, height);
    if (pipelineResult != FFFResult::Success) return pipelineResult;
    scaler_ = sws_getCachedContext(scaler_, frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_RGBA64LE,
        SWS_BICUBIC | SWS_FULL_CHR_H_INT, nullptr, nullptr, nullptr);
    if (scaler_ == nullptr) { SetError("FFmpeg could not create the video conversion context."); return FFFResult::FfmpegFailure; }
    rgba64_.resize(static_cast<std::size_t>(width) * height * 8);
    std::uint8_t* outputData[] = { rgba64_.data(), nullptr, nullptr, nullptr };
    int outputLines[] = { static_cast<int>(width * 8), 0, 0, 0 };
    if (sws_scale(scaler_, frame->data, frame->linesize, 0, frame->height, outputData, outputLines) <= 0) {
        SetError("FFmpeg could not convert the decoded video frame."); return FFFResult::FfmpegFailure;
    }
    context_->UpdateSubresource(sourceTexture_, 0, nullptr, rgba64_.data(), width * 8, 0);
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return FFFResult::DeviceFailure;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &target))) return FFFResult::DeviceFailure;
    ShaderSettings settings{};
    settings.colorMode = static_cast<std::uint32_t>(actualMode_);
    settings.transfer = frame->color_trc == AVCOL_TRC_SMPTE2084 ? 1u : (frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ? 2u : 0u);
    settings.source2020 = frame->color_primaries == AVCOL_PRI_BT2020 ? 1u : 0u;
    settings.sdrPeak = sdrPeakNits_; settings.hdrPeak = hdrPeakNits_; settings.paperWhite = paperWhiteNits_;
    settings.sourceWidth = static_cast<float>(width); settings.sourceHeight = static_cast<float>(height);
    settings.outputWidth = static_cast<float>(swapWidth_); settings.outputHeight = static_cast<float>(swapHeight_);
    context_->UpdateSubresource(constants_, 0, nullptr, &settings, 0, 0);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(swapWidth_), static_cast<float>(swapHeight_), 0, 1};
    context_->OMSetRenderTargets(1, target.GetAddressOf(), nullptr); context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0); context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &constants_); context_->PSSetSamplers(0, 1, &sampler_);
    context_->PSSetShaderResources(0, 1, &sourceView_); context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullView = nullptr; context_->PSSetShaderResources(0, 1, &nullView);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    const auto present = swapChain_->Present(1, 0);
    if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
        SetError("The D3D11 playback device was removed."); return FFFResult::DeviceFailure;
    }
    return FAILED(present) ? FFFResult::DeviceFailure : FFFResult::Success;
}

void PlayerVideoRenderer::Close() noexcept {
    if (scaler_ != nullptr) { sws_freeContext(scaler_); scaler_ = nullptr; }
    if (swapChain_ != nullptr) { swapChain_->Release(); swapChain_ = nullptr; }
    if (sourceView_ != nullptr) { sourceView_->Release(); sourceView_ = nullptr; }
    if (sourceTexture_ != nullptr) { sourceTexture_->Release(); sourceTexture_ = nullptr; }
    if (constants_ != nullptr) { constants_->Release(); constants_ = nullptr; }
    if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
    if (pixelShader_ != nullptr) { pixelShader_->Release(); pixelShader_ = nullptr; }
    if (vertexShader_ != nullptr) { vertexShader_->Release(); vertexShader_ = nullptr; }
    if (context_ != nullptr) { context_->Release(); context_ = nullptr; }
    if (device_ != nullptr) { device_->Release(); device_ = nullptr; }
    rgba64_.clear(); output_.clear(); swapWidth_ = swapHeight_ = sourceWidth_ = sourceHeight_ = 0;
}

FFF3FPColorMode PlayerVideoRenderer::ActualColorMode() const noexcept { return actualMode_; }
std::string PlayerVideoRenderer::FallbackReason() const { return fallbackReason_; }
std::string PlayerVideoRenderer::LastError() const { return lastError_; }
void PlayerVideoRenderer::SetError(std::string message) noexcept { try { lastError_ = std::move(message); } catch (...) {} }
