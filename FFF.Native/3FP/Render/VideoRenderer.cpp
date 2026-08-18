#include "pch.h"
#include "3FP/Render/VideoRenderer.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

#include <d3dcompiler.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <roapi.h>
#include <windows.graphics.display.h>
#include <windows.graphics.display.interop.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <climits>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace {
bool ReadWindowsDisplayLuminance(const HMONITOR monitor,
    HdrDisplayCapabilities& capabilities) noexcept {
    if (monitor == nullptr) return false;
    const auto initializeResult = RoInitialize(RO_INIT_MULTITHREADED);
    const auto shouldUninitialize = SUCCEEDED(initializeResult);
    bool read = false;
    do {
        HSTRING_HEADER classHeader{};
        HSTRING className = nullptr;
        if (FAILED(WindowsCreateStringReference(
                RuntimeClass_Windows_Graphics_Display_DisplayInformation,
                ARRAYSIZE(RuntimeClass_Windows_Graphics_Display_DisplayInformation) - 1,
                &classHeader, &className)) || className == nullptr) break;
        ComPtr<IDisplayInformationStaticsInterop> statics;
        if (FAILED(RoGetActivationFactory(className, IID_PPV_ARGS(&statics)))) break;
        ComPtr<ABI::Windows::Graphics::Display::IDisplayInformation5> information;
        if (FAILED(statics->GetForMonitor(monitor, IID_PPV_ARGS(&information)))) break;
        ComPtr<ABI::Windows::Graphics::Display::IAdvancedColorInfo> color;
        if (FAILED(information->GetAdvancedColorInfo(&color)) || color == nullptr) break;
        float minimum = 0.0f;
        float maximum = 0.0f;
        float fullFrame = 0.0f;
        if (FAILED(color->get_MinLuminanceInNits(&minimum)) ||
            FAILED(color->get_MaxLuminanceInNits(&maximum)) ||
            FAILED(color->get_MaxAverageFullFrameLuminanceInNits(&fullFrame))) break;
        if (!std::isfinite(maximum) || maximum <= 0.0f) break;
        capabilities.maximumNits = maximum;
        if (std::isfinite(minimum) && minimum >= 0.0f)
            capabilities.minimumNits = minimum;
        if (std::isfinite(fullFrame) && fullFrame > 0.0f)
            capabilities.maximumFullFrameNits = fullFrame;
        read = true;
    } while (false);
    if (shouldUninitialize) RoUninitialize();
    return read;
}

constexpr std::uint32_t OutputBitDepthForSource(const std::uint32_t sourceBitDepth,
    const bool hdr) noexcept {
    // HDR output uses a 16-bit scRGB floating-point swap chain (linear
    // Rec.709 primaries, 1.0 = 80 nits) so the display's tone mapper receives
    // full-precision linear light. Floating point is never used for SDR so DWM
    // applies the Windows HDR SDR-white adjustment exactly once.
    if (hdr) return 16;
    if (sourceBitDepth > 8) return 10;
    return 8;
}

float Clamp01(const float value) noexcept { return std::clamp(value, 0.0f, 1.0f); }

constexpr bool IsBt2020ColorSpace(const AVColorSpace colorSpace) noexcept {
    return colorSpace == AVCOL_SPC_BT2020_NCL || colorSpace == AVCOL_SPC_BT2020_CL;
}

constexpr bool IsJpegFullRangeFormat(const AVPixelFormat format) noexcept {
    switch (format) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ411P:
        return true;
    default:
        return false;
    }
}

static_assert(IsJpegFullRangeFormat(AV_PIX_FMT_YUVJ420P) &&
    !IsJpegFullRangeFormat(AV_PIX_FMT_YUV420P));

constexpr float FullRangeChromaOffset(const std::uint32_t bitDepth) noexcept {
    return bitDepth == 0 ? 0.5f :
        static_cast<float>(1u << (bitDepth - 1u)) /
        static_cast<float>((1u << bitDepth) - 1u);
}

static_assert(FullRangeChromaOffset(8) > 0.501f && FullRangeChromaOffset(8) < 0.502f &&
    FullRangeChromaOffset(10) > 0.500f && FullRangeChromaOffset(10) < 0.501f);

int ToSwsColorSpace(const AVFrame* frame, const bool rec2020Fallback) noexcept {
    const auto colorSpace = frame != nullptr ? frame->colorspace : AVCOL_SPC_UNSPECIFIED;
    const auto width = frame != nullptr ? frame->width : 0;
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
        if (rec2020Fallback) return SWS_CS_BT2020;
        // Untagged HD/UHD sources conventionally use Rec.709; SD uses Rec.601.
        return width >= 1280 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    }
}

bool IsFullRange(const AVFrame* frame) noexcept {
    if (frame->color_range == AVCOL_RANGE_JPEG) return true;
    if (IsJpegFullRangeFormat(static_cast<AVPixelFormat>(frame->format))) return true;
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;
}

bool IsRec2020(const AVFrame* frame) noexcept {
    return frame->color_primaries == AVCOL_PRI_BT2020 ||
        IsBt2020ColorSpace(frame->colorspace);
}

DXGI_COLOR_SPACE_TYPE VideoProcessorInputColorSpace(const int colorSpace,
    const bool fullRange, const std::uint32_t width) noexcept {
    if (IsBt2020ColorSpace(static_cast<AVColorSpace>(colorSpace))) {
        return fullRange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 :
            DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
    }
    if (colorSpace == AVCOL_SPC_BT709 ||
        (colorSpace == AVCOL_SPC_UNSPECIFIED && width >= 1280)) {
        return fullRange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 :
            DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    }
    return fullRange ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 :
        DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
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

float Bt2390HdrToSdrPq(const float pq, const float sourcePeak,
    const float targetPeak) noexcept {
    const auto sourceMaximum = std::max(sourcePeak, 1.0f);
    const auto targetMaximum = std::clamp(targetPeak, 1.0f, sourceMaximum);
    const auto sourcePq = NitsToPq(sourceMaximum);
    const auto targetPq = NitsToPq(targetMaximum);
    if (sourcePq <= 1.0e-6f || targetPq >= sourcePq)
        return std::clamp(pq, 0.0f, targetPq);

    const auto normalizedTarget = targetPq / sourcePq;
    const auto knee = std::clamp(1.5f * normalizedTarget - 0.5f, 0.0f, 1.0f);
    const auto signal = std::clamp(pq / sourcePq, 0.0f, 1.0f);
    auto mapped = signal;
    if (signal > knee && knee < 1.0f) {
        const auto t = (signal - knee) / (1.0f - knee);
        const auto t2 = t * t;
        const auto t3 = t2 * t;
        const auto h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const auto h10 = t3 - 2.0f * t2 + t;
        const auto h01 = -2.0f * t3 + 3.0f * t2;
        mapped = h00 * knee + h10 * (1.0f - knee) +
            h01 * normalizedTarget;
    }
    return std::clamp(mapped * sourcePq, 0.0f, targetPq);
}

struct Float3 { float r, g, b; };

Float3 Linear2020NitsToIpt(const Float3 value) noexcept {
    const Float3 lms{
        0.4120363867f * value.r + 0.5239119120f * value.g + 0.0640549816f * value.b,
        0.1666602187f * value.r + 0.7203952135f * value.g + 0.1129461230f * value.b,
        0.0241123586f * value.r + 0.0754749627f * value.g + 0.9004079374f * value.b};
    const Float3 lmsPq{NitsToPq(lms.r), NitsToPq(lms.g), NitsToPq(lms.b)};
    return {
        0.4000f * lmsPq.r + 0.4000f * lmsPq.g + 0.2000f * lmsPq.b,
        4.4550f * lmsPq.r - 4.8510f * lmsPq.g + 0.3960f * lmsPq.b,
        0.8056f * lmsPq.r + 0.3572f * lmsPq.g - 1.1628f * lmsPq.b};
}

Float3 IptToLinear2020Nits(const Float3 value) noexcept {
    const Float3 lmsPq{
        value.r + 0.0975689f * value.g + 0.205226f * value.b,
        value.r - 0.1138760f * value.g + 0.133217f * value.b,
        value.r + 0.0326151f * value.g - 0.676887f * value.b};
    const Float3 lms{PqToNits(lmsPq.r), PqToNits(lmsPq.g), PqToNits(lmsPq.b)};
    return {
        3.4368148291f * lms.r - 2.5067738012f * lms.g + 0.0699519280f * lms.b,
        -0.7910582378f * lms.r + 1.9836016695f * lms.g - 0.1925448343f * lms.b,
        -0.0257268061f * lms.r - 0.0991417663f * lms.g + 1.1248741444f * lms.b};
}

float IptChromaHull(const float intensity) noexcept {
    return ((intensity - 6.0f) * intensity + 9.0f) * intensity;
}

Float3 MapHdrToSdr(const Float3 rec2020Nits, const float sourcePeak,
    const float targetPeak) noexcept {
    auto ipt = Linear2020NitsToIpt(rec2020Nits);
    const auto originalIntensity = ipt.r;
    const auto mappedIntensity = Bt2390HdrToSdrPq(
        originalIntensity, sourcePeak, targetPeak);
    ipt.r = mappedIntensity;
    if (originalIntensity <= 1.0e-6f || mappedIntensity <= 1.0e-6f) {
        ipt.g = ipt.b = 0.0f;
    } else {
        // Reducing IPT intensity must not increase P/I or T/I saturation. The
        // destination hull can impose an even tighter chroma limit.
        const auto chromaScale = std::clamp(std::min(
            mappedIntensity / originalIntensity,
            IptChromaHull(mappedIntensity) /
                std::max(IptChromaHull(originalIntensity), 1.0e-6f)), 0.0f, 1.0f);
        ipt.g *= chromaScale;
        ipt.b *= chromaScale;
    }
    return IptToLinear2020Nits(ipt);
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
    float SdrPeak; float HdrPeak; float PaperWhite; float TargetPeak;
    float SourceWidth; float SourceHeight; float OutputWidth; float OutputHeight;
    uint InputLayout; float SampleScale; float YOffset; float YScale;
    float COffset; float CScale; float Kr; float Kb;
    float2 ChromaOffset; float2 Padding;
};
Texture2D<float4> Source : register(t0);
Texture2D<float4> ChromaU : register(t1);
Texture2D<float4> ChromaV : register(t2);
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
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
float LinearOne(float v) { v=saturate(v); return v<0.081 ? v/4.5 : pow((v+0.099)/1.099,1.0/0.45); }
float3 ToLinear709(float3 v) { return float3(LinearOne(v.r),LinearOne(v.g),LinearOne(v.b)); }
float BtOne(float v) { v=max(v,0.0); return saturate(v<0.018 ? 4.5*v : 1.099*pow(v,0.45)-0.099); }
float3 ToBt709(float3 v) { return float3(BtOne(v.r),BtOne(v.g),BtOne(v.b)); }
float3 To2020(float3 v) { return mul(float3x3(0.627404,0.329283,0.043313, 0.069097,0.919540,0.011362, 0.016392,0.088013,0.895595),v); }
float3 To709(float3 v) { return mul(float3x3(1.660491,-0.587641,-0.072850, -0.124550,1.132900,-0.008349, -0.018151,-0.100579,1.118730),v); }
float Bt2390HdrToSdrPq(float value,float sourcePeak,float targetPeak) {
    float sourceMaximum=max(sourcePeak,1.0);
    float targetMaximum=clamp(targetPeak,1.0,sourceMaximum);
    float sourcePq=NitsToPq(sourceMaximum.xxx).r;
    float targetPq=NitsToPq(targetMaximum.xxx).r;
    if(sourcePq<=0.000001||targetPq>=sourcePq)
        return clamp(value,0.0,targetPq);
    float normalizedTarget=targetPq/sourcePq;
    float knee=clamp(1.5*normalizedTarget-0.5,0.0,1.0);
    float signal=clamp(value/sourcePq,0.0,1.0);
    if(signal<=knee||knee>=1.0)return clamp(value,0.0,targetPq);
    float t=(signal-knee)/(1.0-knee);
    float t2=t*t,t3=t2*t;
    float mapped=(2.0*t3-3.0*t2+1.0)*knee+
        (t3-2.0*t2+t)*(1.0-knee)+(-2.0*t3+3.0*t2)*normalizedTarget;
    return min(mapped*sourcePq,targetPq);
}
float3 Linear2020NitsToIpt(float3 value) {
    float3 lms=mul(float3x3(
        0.4120363867,0.5239119120,0.0640549816,
        0.1666602187,0.7203952135,0.1129461230,
        0.0241123586,0.0754749627,0.9004079374),value);
    float3 lmsPq=NitsToPq(lms);
    return mul(float3x3(
        0.4000,0.4000,0.2000,
        4.4550,-4.8510,0.3960,
        0.8056,0.3572,-1.1628),lmsPq);
}
float3 IptToLinear2020Nits(float3 value) {
    float3 lmsPq=mul(float3x3(
        1.0,0.0975689,0.205226,
        1.0,-0.1138760,0.133217,
        1.0,0.0326151,-0.676887),value);
    float3 lms=PqToNits(lmsPq);
    return mul(float3x3(
        3.4368148291,-2.5067738012,0.0699519280,
        -0.7910582378,1.9836016695,-0.1925448343,
        -0.0257268061,-0.0991417663,1.1248741444),lms);
}
float IptChromaHull(float intensity) {
    return ((intensity-6.0)*intensity+9.0)*intensity;
}
float3 ToneHdrToSdr(float3 rec2020Nits,float sourcePeak,float targetPeak) {
    float3 ipt=Linear2020NitsToIpt(rec2020Nits);
    float originalIntensity=ipt.x;
    float mappedIntensity=Bt2390HdrToSdrPq(originalIntensity,sourcePeak,targetPeak);
    ipt.x=mappedIntensity;
    if(originalIntensity<=0.000001||mappedIntensity<=0.000001) {
        ipt.yz=0.0;
    } else {
        float2 hull=float2(IptChromaHull(originalIntensity),IptChromaHull(mappedIntensity));
        float chromaScale=saturate(min(mappedIntensity/originalIntensity,
            hull.y/max(hull.x,0.000001)));
        ipt.yz*=chromaScale;
    }
    return IptToLinear2020Nits(ipt);
}
float Sinc(float value) {
    value=abs(value);
    if(value<0.00001)return 1.0;
    const float angle=3.14159265359*value;
    return sin(angle)/angle;
}
float Lanczos3Weight(float value) {
    value=abs(value);
    return value>=3.0?0.0:Sinc(value)*Sinc(value/3.0);
}
float4 LoadClamped(Texture2D<float4> sourceTexture,int2 coordinate,uint2 dimensions) {
    return sourceTexture.Load(int3(clamp(coordinate,int2(0,0),int2(dimensions)-1),0));
}
float4 SampleLanczos3(Texture2D<float4> sourceTexture,float2 uv,uint2 dimensions) {
    float2 position=uv*float2(dimensions)-0.5;
    int2 origin=int2(floor(position));
    float2 fraction=position-float2(origin);
    float4 total=0.0;
    float weightTotal=0.0;
    [unroll] for(int y=-2;y<=3;++y) {
        const float wy=Lanczos3Weight(float(y)-fraction.y);
        [unroll] for(int x=-2;x<=3;++x) {
            const float weight=wy*Lanczos3Weight(float(x)-fraction.x);
            total+=LoadClamped(sourceTexture,origin+int2(x,y),dimensions)*weight;
            weightTotal+=weight;
        }
    }
    const float4 filtered=total/max(abs(weightTotal),0.000001);
    // Clamp the negative Lanczos lobes to the local bilinear footprint.  This
    // retains edge detail without creating bright or dark halos around lines.
    const float4 a=LoadClamped(sourceTexture,origin,dimensions);
    const float4 b=LoadClamped(sourceTexture,origin+int2(1,0),dimensions);
    const float4 c=LoadClamped(sourceTexture,origin+int2(0,1),dimensions);
    const float4 d=LoadClamped(sourceTexture,origin+int2(1,1),dimensions);
    return clamp(filtered,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
}
float4 SampleVideo(Texture2D<float4> sourceTexture,float2 uv) {
    uint width,height;
    sourceTexture.GetDimensions(width,height);
    const uint2 dimensions=uint2(width,height);
    if(abs(OutputWidth-float(width))<0.01&&abs(OutputHeight-float(height))<0.01)
        return sourceTexture.SampleLevel(PointSampler,uv,0);
    return SampleLanczos3(sourceTexture,uv,dimensions);
}
float3 ReadSource(float2 uv) {
    if(InputLayout==0)return SampleVideo(Source,uv).rgb;
    float2 chromaUv=uv+ChromaOffset;
    float y=SampleVideo(Source,uv).r*SampleScale;
    float2 chroma=InputLayout==1
        ?float2(SampleVideo(ChromaU,chromaUv).r,SampleVideo(ChromaV,chromaUv).r)*SampleScale
        :SampleVideo(ChromaU,chromaUv).rg*SampleScale;
    y=(y-YOffset)*YScale;
    chroma=(chroma-COffset)*CScale;
    float kg=1.0-Kr-Kb;
    return float3(y+(2.0-2.0*Kr)*chroma.y,
        y-Kb*(2.0-2.0*Kb)/kg*chroma.x-Kr*(2.0-2.0*Kr)/kg*chroma.y,
        y+(2.0-2.0*Kb)*chroma.x);
}
float3 ReadSourceLinear(float2 uv) {
    if(InputLayout==0)return Source.Sample(LinearSampler,uv).rgb;
    float2 chromaUv=uv+ChromaOffset;
    float y=Source.Sample(LinearSampler,uv).r*SampleScale;
    float2 chroma=InputLayout==1
        ?float2(ChromaU.Sample(LinearSampler,chromaUv).r,
                ChromaV.Sample(LinearSampler,chromaUv).r)*SampleScale
        :ChromaU.Sample(LinearSampler,chromaUv).rg*SampleScale;
    y=(y-YOffset)*YScale;
    chroma=(chroma-COffset)*CScale;
    float kg=1.0-Kr-Kb;
    return float3(y+(2.0-2.0*Kr)*chroma.y,
        y-Kb*(2.0-2.0*Kb)/kg*chroma.x-Kr*(2.0-2.0*Kr)/kg*chroma.y,
        y+(2.0-2.0*Kb)*chroma.x);
}
float2 CoverFillUv(float2 uv) {
    float sourceAspect=SourceWidth/max(SourceHeight,1.0);
    float outputAspect=OutputWidth/max(OutputHeight,1.0);
    if(sourceAspect>outputAspect)
        uv.x=(uv.x-0.5)*(outputAspect/sourceAspect)+0.5;
    else
        uv.y=(uv.y-0.5)*(sourceAspect/outputAspect)+0.5;
    return uv;
}
float3 ReadCoverBackdrop(float2 uv) {
    return ReadSourceLinear(CoverFillUv(uv));
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float3 rgb=Reserved==1?ReadCoverBackdrop(uv):ReadSource(uv);
    if(ColorMode==1)return float4(rgb,1);
    if(ColorMode==0&&Transfer==0){
        if(Source2020!=0)rgb=ToBt709(To709(ToLinear709(rgb)));
        return float4(rgb,1);
    }
    float3 nits=Transfer==1?PqToNits(rgb):(Transfer==2?HlgToNits(rgb):ToLinear709(rgb)*PaperWhite);
    if(ColorMode==2){
        // scRGB swap-chain contract: linear Rec.709 primaries, 1.0 = 80 nits.
        // Tone mapping is delegated to the display via the HDR metadata.
        float3 rec709Nits=Source2020==0?nits:To709(nits);
        return float4(rec709Nits/80.0,1);
    }
    if(Source2020==0)nits=To2020(nits);
    // BT.2390 operates on IPT intensity before Rec.2020-to-Rec.709 gamut
    // conversion. Chroma follows the reduced IPT gamut hull.
    float3 sdr=ToBt709(To709(ToneHdrToSdr(nits,HdrPeak,SdrPeak))/SdrPeak);
    return float4(sdr,1);
})";

constexpr const char* ScalePixelShaderSource = R"(
cbuffer ScaleSettings : register(b0) {
    float2 SourceSize;
    float2 DestinationSize;
    uint Axis;
    uint Filter;
    float2 Padding;
};
Texture2D<float4> ScaleSource : register(t0);
float Sinc(float value) {
    value=abs(value);
    if(value<0.00001)return 1.0;
    const float angle=3.14159265359*value;
    return sin(angle)/angle;
}
float ScaleWeight(float value) {
    value=abs(value);
    if(Filter==0)
        return value>=1.0?0.0:((2.0*value-3.0)*value*value+1.0);
    if(Filter==2)
        return value<0.5?1.0:0.0;
    return value>=3.0?0.0:Sinc(value)*Sinc(value/3.0);
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    const int2 outputPixel=int2(position.xy);
    const float sourceExtent=Axis==0?SourceSize.x:SourceSize.y;
    const float destinationExtent=Axis==0?DestinationSize.x:DestinationSize.y;
    const float scale=min(destinationExtent/sourceExtent,1.0);
    const float radius=Filter==2?0.5:(Filter==0?1.0:3.0);
    const float support=radius/max(scale,0.000001);
    const float sourcePosition=((Axis==0?float(outputPixel.x):float(outputPixel.y))+0.5)
        *sourceExtent/destinationExtent-0.5;
    const int first=int(ceil(sourcePosition-support));
    const int last=int(floor(sourcePosition+support));
    const int2 maximum=int2(SourceSize)-1;
    float4 total=0.0;
    float weightTotal=0.0;
    [loop] for(int sampleIndex=first;sampleIndex<=last;++sampleIndex) {
        const float weight=ScaleWeight((float(sampleIndex)-sourcePosition)*scale);
        int2 coordinate=outputPixel;
        if(Axis==0)coordinate.x=sampleIndex;else coordinate.y=sampleIndex;
        total+=ScaleSource.Load(int3(clamp(coordinate,int2(0,0),maximum),0))*weight;
        weightTotal+=weight;
    }
    return total/max(abs(weightTotal),0.000001);
}
)";

constexpr const char* CoverBackdropPixelShaderSource = R"(
cbuffer Settings : register(b0) {
    uint ColorMode; uint Transfer; uint Source2020; uint TintArgb;
    float SdrPeak; float HdrPeak; float PaperWhite; float TargetPeak;
};
Texture2D<float4> Backdrop : register(t0);
SamplerState LinearSampler : register(s0);
float LinearOne(float v) { v=saturate(v); return v<0.081 ? v/4.5 : pow((v+0.099)/1.099,1.0/0.45); }
float3 ToLinear709(float3 v) { return float3(LinearOne(v.r),LinearOne(v.g),LinearOne(v.b)); }
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float4 color=Backdrop.Sample(LinearSampler,uv);
    float4 tint=float4(float3((TintArgb>>16)&255u,(TintArgb>>8)&255u,TintArgb&255u),
                       float((TintArgb>>24)&255u))/255.0;
    if(ColorMode==2){
        // scRGB swap-chain contract: linear Rec.709 primaries, 1.0 = 80 nits.
        // The backdrop cache is FP16 and already holds the main shader's
        // linear scRGB output. Blend it directly; only the sRGB tint needs
        // linearization and conversion to scRGB so both operands share the
        // same scale before lerp.
        float3 tintScRgb=ToLinear709(tint.rgb)*(PaperWhite/80.0);
        float3 result=lerp(color.rgb,tintScRgb,tint.a);
        return float4(result,color.a);
    }
    return float4(lerp(color.rgb,tint.rgb,tint.a),color.a);
})";

constexpr const char* TimedTextPixelShaderSource = R"(
cbuffer Settings : register(b0) {
    uint ColorMode; uint Transfer; uint Source2020; uint OverlayFlags;
    float SdrPeak; float HdrPeak; float PaperWhite; float TargetPeak;
};
Texture2D<float4> Overlay : register(t0);
SamplerState LinearSampler : register(s0);
float LinearOne(float v) { v=saturate(v); return v<0.081 ? v/4.5 : pow((v+0.099)/1.099,1.0/0.45); }
float3 ToLinear709(float3 v) { return float3(LinearOne(v.r),LinearOne(v.g),LinearOne(v.b)); }
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float4 value=Overlay.Sample(LinearSampler,uv);
    if(value.a<=0.000001)return 0;
    float3 straight=value.rgb/value.a;
    if(ColorMode==2){
        float overlayPeak=(OverlayFlags&1u)!=0?TargetPeak:PaperWhite;
        // Bit 1 denotes an FP16 linear overlay surface.  When it is clear the
        // legacy premultiplied B8G8R8A8_UNORM surface needs an explicit
        // transfer decode; HDR FP16 layers are already linearized by D2D.
        if((OverlayFlags&2u)!=0)
            straight*=overlayPeak/80.0;
        else
            straight=ToLinear709(straight)*(overlayPeak/80.0);
    }
    return float4(straight*value.a,value.a);
})";

constexpr const char* TimedTextSpriteVertexShaderSource = R"(
struct InstanceData { float4 destination; float4 uv; };
StructuredBuffer<InstanceData> Instances : register(t1);
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Output main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    static const float2 corners[6] = {
        float2(0,0), float2(1,0), float2(0,1),
        float2(0,1), float2(1,0), float2(1,1)
    };
    InstanceData instance = Instances[instanceId];
    float2 corner = corners[vertexId];
    Output output;
    output.position = float4(lerp(instance.destination.xy, instance.destination.zw, corner), 0, 1);
    output.uv = lerp(instance.uv.xy, instance.uv.zw, corner);
    return output;
})";

constexpr const char* TimedTextSpritePixelShaderSource = R"(
Texture2D<float4> Atlas : register(t0);
SamplerState LinearSampler : register(s0);
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return Atlas.Sample(LinearSampler, uv);
})";

constexpr std::uint32_t InitialTimedTextAtlasSize = 1024;
constexpr std::uint32_t CoverBackdropEffect = 1;
// Start at 4 MiB and grow only when the visible text set cannot fit. The
// 4096 ceiling preserves the existing 100-item danmaku cache contract without
// making the 64 MiB allocation resident during ordinary subtitle playback.
constexpr std::uint32_t MaximumTimedTextAtlasSize = 4096;
constexpr std::uint32_t MaximumTimedTextSprites = 512;
constexpr std::size_t MaximumTimedTextBrushes = 256;
constexpr float TimedTextSoftShadowExtentFactor = 3.0f;
constexpr std::size_t VideoConversionBufferSlackBytes = 32 * 1024 * 1024;
constexpr auto HdrSupportProbeCacheDuration = std::chrono::milliseconds(750);

void ResizeVideoConversionBuffer(std::vector<std::uint8_t>& buffer,
    const std::size_t requiredBytes) {
    const auto capacity = buffer.capacity();
    if (requiredBytes < capacity / 2 &&
        capacity - requiredBytes > VideoConversionBufferSlackBytes) {
        std::vector<std::uint8_t> replacement(requiredBytes);
        buffer.swap(replacement);
        return;
    }
    buffer.resize(requiredBytes);
}

struct ShaderSettings {
    std::uint32_t colorMode, transfer, source2020, reserved;
    float sdrPeak, hdrPeak, paperWhite, targetPeak;
    float sourceWidth, sourceHeight, outputWidth, outputHeight;
    std::uint32_t inputLayout;
    float sampleScale, yOffset, yScale;
    float cOffset, cScale, kr, kb;
    float chromaOffsetX, chromaOffsetY, padding1, padding2;
};

struct ScaleShaderSettings {
    float sourceWidth, sourceHeight, destinationWidth, destinationHeight;
    std::uint32_t axis, filter;
    float padding1, padding2;
};
static_assert(sizeof(ScaleShaderSettings) == 32);

struct VideoDestination {
    std::uint32_t x, y, width, height;
};

constexpr VideoDestination CalculateVideoDestination(const std::uint32_t sourceWidth,
    const std::uint32_t sourceHeight, const std::uint32_t outputWidth,
    const std::uint32_t outputHeight, const bool limitToNativeSize = false) noexcept {
    if (sourceWidth == 0 || sourceHeight == 0 || outputWidth == 0 || outputHeight == 0)
        return {0, 0, 1, 1};
    if (limitToNativeSize && sourceWidth <= outputWidth && sourceHeight <= outputHeight)
        return {(outputWidth - sourceWidth) / 2, (outputHeight - sourceHeight) / 2,
            sourceWidth, sourceHeight};
    std::uint32_t width = outputWidth;
    std::uint32_t height = outputHeight;
    if (static_cast<std::uint64_t>(outputWidth) * sourceHeight <=
        static_cast<std::uint64_t>(outputHeight) * sourceWidth) {
        height = std::max(1u, static_cast<std::uint32_t>((
            static_cast<std::uint64_t>(outputWidth) * sourceHeight + sourceWidth / 2) / sourceWidth));
    } else {
        width = std::max(1u, static_cast<std::uint32_t>((
            static_cast<std::uint64_t>(outputHeight) * sourceWidth + sourceHeight / 2) / sourceHeight));
    }
    width = std::min(width, outputWidth);
    height = std::min(height, outputHeight);
    return {(outputWidth - width) / 2, (outputHeight - height) / 2, width, height};
}

constexpr VideoDestination CalculateLyricsCoverDestination(const std::uint32_t sourceWidth,
    const std::uint32_t sourceHeight, const std::uint32_t outputWidth,
    const std::uint32_t outputHeight, const float coverWidthPercentage,
    const float lyricsWidthPercentage, const float leftPaddingPercentage,
    const float rightPaddingPercentage,
    const float verticalPaddingPercentage) noexcept {
    const auto totalWidthPercentage = std::max(0.0001f,
        coverWidthPercentage + lyricsWidthPercentage);
    const auto regionWidth = std::max(1u, static_cast<std::uint32_t>(
        outputWidth * coverWidthPercentage / totalWidthPercentage + 0.5f));
    const auto leftPadding = std::min(regionWidth / 2,
        static_cast<std::uint32_t>(
            regionWidth * leftPaddingPercentage / 100.0f + 0.5f));
    const auto rightPadding = std::min(regionWidth / 2,
        static_cast<std::uint32_t>(
            regionWidth * rightPaddingPercentage / 100.0f + 0.5f));
    const auto verticalPadding = std::min(outputHeight / 2,
        static_cast<std::uint32_t>(
            outputHeight * verticalPaddingPercentage / 100.0f + 0.5f));
    const auto innerWidth = std::max(1u, regionWidth - leftPadding - rightPadding);
    const auto innerHeight = std::max(1u, outputHeight - verticalPadding * 2);
    const auto inner = CalculateVideoDestination(sourceWidth, sourceHeight,
        innerWidth, innerHeight, true);
    return {regionWidth - rightPadding - inner.width, verticalPadding + inner.y,
        inner.width, inner.height};
}

struct CoverBackdropCacheSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The backdrop is blurred once at a content-defined resolution.  Keeping a
// bounded source-relative size avoids making the blur depend on the current
// swap-chain or lyrics region, while preventing unusually large cover frames
// from allocating unbounded FP16 surfaces.
constexpr std::uint32_t MaximumCoverBackdropDimension = 2048;

constexpr CoverBackdropCacheSize CalculateCoverBackdropCacheSize(
    const std::uint32_t sourceWidth, const std::uint32_t sourceHeight,
    const std::uint32_t downsampleFactor) noexcept {
    if (sourceWidth == 0 || sourceHeight == 0) return {};
    const auto factor = std::max(1u, downsampleFactor);
    auto width = std::max(1u, static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(sourceWidth) + factor - 1) / factor));
    auto height = std::max(1u, static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(sourceHeight) + factor - 1) / factor));
    if (width <= MaximumCoverBackdropDimension && height <= MaximumCoverBackdropDimension)
        return {width, height};

    if (width >= height) {
        height = std::max(1u, static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(height) * MaximumCoverBackdropDimension +
                width / 2) / width));
        width = MaximumCoverBackdropDimension;
    } else {
        width = std::max(1u, static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(width) * MaximumCoverBackdropDimension +
                height / 2) / height));
        height = MaximumCoverBackdropDimension;
    }
    return {width, height};
}

static_assert(CalculateCoverBackdropCacheSize(1000, 1000, 4).width == 250 &&
    CalculateCoverBackdropCacheSize(1000, 1000, 4).height == 250);
static_assert(CalculateCoverBackdropCacheSize(8192, 4096, 1).width == 2048 &&
    CalculateCoverBackdropCacheSize(8192, 4096, 1).height == 1024);

static_assert(CalculateVideoDestination(1920, 1080, 1280, 1024).width == 1280 &&
    CalculateVideoDestination(1920, 1080, 1280, 1024).height == 720 &&
    CalculateVideoDestination(1920, 1080, 1280, 1024).y == 152);
static_assert(CalculateVideoDestination(1080, 1920, 1920, 1080).width == 608 &&
    CalculateVideoDestination(1080, 1920, 1920, 1080).x == 656);
static_assert(CalculateVideoDestination(640, 360, 1920, 1080, true).width == 640 &&
    CalculateVideoDestination(640, 360, 1920, 1080, true).height == 360 &&
    CalculateVideoDestination(640, 360, 1920, 1080, true).x == 640 &&
    CalculateVideoDestination(640, 360, 1920, 1080, true).y == 360);
static_assert(CalculateVideoDestination(2560, 1440, 1920, 1080, true).width == 1920 &&
    CalculateVideoDestination(2560, 1440, 1920, 1080, true).height == 1080);
static_assert(CalculateLyricsCoverDestination(512, 512, 800, 400,
    50.0f, 50.0f, 7.5f, 0.0f, 7.5f).x == 60 &&
    CalculateLyricsCoverDestination(512, 512, 800, 400,
    50.0f, 50.0f, 7.5f, 0.0f, 7.5f).width == 340);

struct InputDescription {
    std::uint32_t layout = 0;
    std::uint32_t bitDepth = 16;
    float sampleScale = 1.0f;
    std::uint32_t chromaWidthShift = 0;
    std::uint32_t chromaHeightShift = 0;
};

constexpr InputDescription DescribeInput(const AVPixelFormat format) noexcept {
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return {1, 8, 1.0f, 1, 1};
    case AV_PIX_FMT_YUV420P10LE:
        return {1, 10, 65535.0f / 1023.0f, 1, 1};
    case AV_PIX_FMT_YUV420P12LE:
        return {1, 12, 65535.0f / 4095.0f, 1, 1};
    case AV_PIX_FMT_YUV420P16LE:
        return {1, 16, 1.0f, 1, 1};
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
        return {1, 8, 1.0f, 1, 0};
    case AV_PIX_FMT_YUV422P10LE:
        return {1, 10, 65535.0f / 1023.0f, 1, 0};
    case AV_PIX_FMT_YUV422P12LE:
        return {1, 12, 65535.0f / 4095.0f, 1, 0};
    case AV_PIX_FMT_YUV422P16LE:
        return {1, 16, 1.0f, 1, 0};
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
        return {1, 8, 1.0f, 0, 0};
    case AV_PIX_FMT_YUV444P10LE:
        return {1, 10, 65535.0f / 1023.0f, 0, 0};
    case AV_PIX_FMT_YUV444P12LE:
        return {1, 12, 65535.0f / 4095.0f, 0, 0};
    case AV_PIX_FMT_YUV444P16LE:
        return {1, 16, 1.0f, 0, 0};
    case AV_PIX_FMT_NV12:
        return {2, 8, 1.0f, 1, 1};
    case AV_PIX_FMT_P010LE:
        return {2, 10, 65535.0f / 65472.0f, 1, 1};
    case AV_PIX_FMT_P012LE:
        return {2, 12, 65535.0f / 65520.0f, 1, 1};
    case AV_PIX_FMT_P016LE:
        return {2, 16, 1.0f, 1, 1};
    case AV_PIX_FMT_P210LE:
        return {2, 10, 65535.0f / 65472.0f, 1, 0};
    case AV_PIX_FMT_P212LE:
        return {2, 12, 65535.0f / 65520.0f, 1, 0};
    case AV_PIX_FMT_P216LE:
        return {2, 16, 1.0f, 1, 0};
    default:
        return {};
    }
}

static_assert(DescribeInput(AV_PIX_FMT_YUV420P).layout == 1 &&
    DescribeInput(AV_PIX_FMT_YUV420P).bitDepth == 8 &&
    DescribeInput(AV_PIX_FMT_YUV420P).chromaWidthShift == 1 &&
    DescribeInput(AV_PIX_FMT_YUV420P).chromaHeightShift == 1);
static_assert(DescribeInput(AV_PIX_FMT_YUV420P10LE).layout == 1 &&
    DescribeInput(AV_PIX_FMT_YUV420P10LE).bitDepth == 10);
static_assert(DescribeInput(AV_PIX_FMT_YUV422P).layout == 1 &&
    DescribeInput(AV_PIX_FMT_YUV422P).bitDepth == 8 &&
    DescribeInput(AV_PIX_FMT_YUV422P).chromaWidthShift == 1 &&
    DescribeInput(AV_PIX_FMT_YUV422P).chromaHeightShift == 0);
static_assert(DescribeInput(AV_PIX_FMT_YUV422P10LE).layout == 1 &&
    DescribeInput(AV_PIX_FMT_YUV422P10LE).bitDepth == 10);
static_assert(DescribeInput(AV_PIX_FMT_YUV444P).layout == 1 &&
    DescribeInput(AV_PIX_FMT_YUV444P).bitDepth == 8 &&
    DescribeInput(AV_PIX_FMT_YUV444P).chromaWidthShift == 0 &&
    DescribeInput(AV_PIX_FMT_YUV444P).chromaHeightShift == 0);
static_assert(DescribeInput(AV_PIX_FMT_YUV444P10LE).layout == 1 &&
    DescribeInput(AV_PIX_FMT_YUV444P10LE).bitDepth == 10);
static_assert(DescribeInput(AV_PIX_FMT_P010LE).sampleScale == 65535.0f / 65472.0f &&
    DescribeInput(AV_PIX_FMT_P012LE).sampleScale == 65535.0f / 65520.0f &&
    DescribeInput(AV_PIX_FMT_P210LE).sampleScale == 65535.0f / 65472.0f &&
    DescribeInput(AV_PIX_FMT_P212LE).sampleScale == 65535.0f / 65520.0f);

constexpr int ChromaOffsetNumerator256(const std::uint32_t shift,
    const int position256) noexcept {
    return shift == 0 ? 0 : static_cast<int>(((1u << shift) - 1u) * 128u) - position256;
}

static_assert(ChromaOffsetNumerator256(1, 0) == 128);
static_assert(ChromaOffsetNumerator256(1, 128) == 0);
static_assert(ChromaOffsetNumerator256(1, 256) == -128);

void ResolveChromaOffset(const AVFrame* frame, const InputDescription& input,
    const AVChromaLocation chromaLocation,
    float& offsetX, float& offsetY) noexcept {
    offsetX = offsetY = 0.0f;
    if (frame == nullptr || input.layout == 0 || chromaLocation == AVCHROMA_LOC_UNSPECIFIED)
        return;
    int positionX = 0;
    int positionY = 0;
    if (av_chroma_location_enum_to_pos(&positionX, &positionY, chromaLocation) < 0)
        return;
    if (input.chromaWidthShift != 0 && frame->width > 0) {
        offsetX = static_cast<float>(ChromaOffsetNumerator256(input.chromaWidthShift, positionX)) /
            (256.0f * static_cast<float>(frame->width));
    }
    if (input.chromaHeightShift != 0 && frame->height > 0) {
        offsetY = static_cast<float>(ChromaOffsetNumerator256(input.chromaHeightShift, positionY)) /
            (256.0f * static_cast<float>(frame->height));
    }
}

void YuvCoefficients(const AVFrame* frame, const bool rec2020Fallback,
    float& kr, float& kb) noexcept {
    const auto colorSpace = frame != nullptr ? frame->colorspace : AVCOL_SPC_UNSPECIFIED;
    const auto width = frame != nullptr ? frame->width : 0;
    if (IsBt2020ColorSpace(colorSpace) ||
        (colorSpace == AVCOL_SPC_UNSPECIFIED && rec2020Fallback)) {
        kr = 0.2627f; kb = 0.0593f;
    } else if (colorSpace == AVCOL_SPC_BT709 || (colorSpace == AVCOL_SPC_UNSPECIFIED && width >= 1280)) {
        kr = 0.2126f; kb = 0.0722f;
    } else {
        kr = 0.2990f; kb = 0.1140f;
    }
}

D2D1_COLOR_F ToD2dColor(const std::uint32_t argb, const bool linear) noexcept {
    constexpr float scale = 1.0f / 255.0f;
    const auto red = static_cast<float>((argb >> 16) & 0xff) * scale;
    const auto green = static_cast<float>((argb >> 8) & 0xff) * scale;
    const auto blue = static_cast<float>(argb & 0xff) * scale;
    return D2D1::ColorF(linear ? Bt709ToLinear(red) : red,
        linear ? Bt709ToLinear(green) : green,
        linear ? Bt709ToLinear(blue) : blue,
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

bool TimedTextUtf8ToWide(const char* value, std::wstring& result) noexcept {
    if (value == nullptr) return false;
    try {
        const auto bytes = std::strlen(value);
        if (bytes > INT_MAX) return false;
        if (bytes == 0) { result.clear(); return true; }
        const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
            static_cast<int>(bytes), nullptr, 0);
        if (count <= 0) return false;
        result.resize(static_cast<std::size_t>(count));
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
            static_cast<int>(bytes), result.data(), count) == count;
    } catch (...) {
        return false;
    }
}

struct ResolvedTimedTextFont {
    std::wstring family;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
};

struct TimedTextFontResolveCacheEntry {
    std::wstring family;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    DWRITE_FONT_STRETCH stretch = DWRITE_FONT_STRETCH_NORMAL;
    ResolvedTimedTextFont resolved;
    std::uint64_t lastUsed = 0;
};

std::mutex g_timedTextFontResolveMutex;
std::vector<TimedTextFontResolveCacheEntry> g_timedTextFontResolveCache;
std::uint64_t g_timedTextFontResolveClock = 0;
constexpr std::size_t MaximumTimedTextFontResolveCacheEntries = 64;

ResolvedTimedTextFont CreateFallbackTimedTextFont(const std::wstring& family,
    DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
    DWRITE_FONT_STRETCH stretch) {
    ResolvedTimedTextFont result;
    result.family = family;
    result.weight = static_cast<int>(weight) <= 0 ? DWRITE_FONT_WEIGHT_NORMAL : weight;
    result.style = style;
    result.stretch = stretch == DWRITE_FONT_STRETCH_UNDEFINED
        ? DWRITE_FONT_STRETCH_NORMAL : stretch;
    return result;
}

void TrimOuterWhitespace(std::wstring& value) noexcept {
    std::size_t first = 0;
    while (first < value.size() && std::iswspace(value[first])) ++first;
    std::size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) --last;
    if (last < value.size()) value.erase(last);
    if (first > 0) value.erase(0, first);
}

void TrimTrailingFontSeparators(std::wstring& value) noexcept {
    while (!value.empty() && (value.back() == L' ' || value.back() == L'-'))
        value.pop_back();
}

bool EqualOrdinalIgnoreCase(const std::wstring& left,
    const std::wstring& right) noexcept {
    if (left.size() != right.size()) return false;
    if (left.size() > static_cast<std::size_t>(INT_MAX)) return false;
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
        right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool EndsWithOrdinalIgnoreCase(const std::wstring& value,
    const std::wstring& token) noexcept {
    if (token.empty() || value.size() < token.size() ||
        token.size() > static_cast<std::size_t>(INT_MAX)) return false;
    return CompareStringOrdinal(value.c_str() + value.size() - token.size(),
        static_cast<int>(token.size()), token.c_str(),
        static_cast<int>(token.size()), TRUE) == CSTR_EQUAL;
}

bool ConsumeSuffix(std::wstring& value, const std::wstring_view suffix) noexcept {
    try {
        std::wstring token;
        token.reserve(suffix.size() + 1);
        token.push_back(L' ');
        token.append(suffix.data(), suffix.size());
        if (!EndsWithOrdinalIgnoreCase(value, token)) {
            token[0] = L'-';
            if (!EndsWithOrdinalIgnoreCase(value, token)) return false;
        }
        value.erase(value.size() - token.size());
        TrimTrailingFontSeparators(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool ConsumeKnownFontNameSuffix(std::wstring& familyName,
    ResolvedTimedTextFont& resolved) noexcept {
    if (ConsumeSuffix(familyName, L"ExtraBlack") || ConsumeSuffix(familyName, L"UltraBlack") ||
        ConsumeSuffix(familyName, L"Extra Black") || ConsumeSuffix(familyName, L"Ultra Black")) {
        resolved.weight = DWRITE_FONT_WEIGHT_EXTRA_BLACK;
        return true;
    }
    if (ConsumeSuffix(familyName, L"ExtraBold") || ConsumeSuffix(familyName, L"UltraBold") ||
        ConsumeSuffix(familyName, L"Extra Bold") || ConsumeSuffix(familyName, L"Ultra Bold")) {
        resolved.weight = DWRITE_FONT_WEIGHT_EXTRA_BOLD;
        return true;
    }
    if (ConsumeSuffix(familyName, L"DemiBold") || ConsumeSuffix(familyName, L"SemiBold") ||
        ConsumeSuffix(familyName, L"Demi Bold") || ConsumeSuffix(familyName, L"Semi Bold")) {
        resolved.weight = DWRITE_FONT_WEIGHT_DEMI_BOLD;
        return true;
    }
    if (ConsumeSuffix(familyName, L"ExtraLight") || ConsumeSuffix(familyName, L"UltraLight") ||
        ConsumeSuffix(familyName, L"Extra Light") || ConsumeSuffix(familyName, L"Ultra Light")) {
        resolved.weight = DWRITE_FONT_WEIGHT_EXTRA_LIGHT;
        return true;
    }
    if (ConsumeSuffix(familyName, L"SemiLight") || ConsumeSuffix(familyName, L"Semi Light")) {
        resolved.weight = DWRITE_FONT_WEIGHT_SEMI_LIGHT;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Bold")) {
        resolved.weight = DWRITE_FONT_WEIGHT_BOLD;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Medium")) {
        resolved.weight = DWRITE_FONT_WEIGHT_MEDIUM;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Regular") || ConsumeSuffix(familyName, L"Normal")) {
        resolved.weight = DWRITE_FONT_WEIGHT_NORMAL;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Light")) {
        resolved.weight = DWRITE_FONT_WEIGHT_LIGHT;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Thin")) {
        resolved.weight = DWRITE_FONT_WEIGHT_THIN;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Black") || ConsumeSuffix(familyName, L"Heavy")) {
        resolved.weight = DWRITE_FONT_WEIGHT_BLACK;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Italic")) {
        resolved.style = DWRITE_FONT_STYLE_ITALIC;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Oblique")) {
        resolved.style = DWRITE_FONT_STYLE_OBLIQUE;
        return true;
    }
    if (ConsumeSuffix(familyName, L"UltraCondensed") ||
        ConsumeSuffix(familyName, L"Ultra Condensed")) {
        resolved.stretch = DWRITE_FONT_STRETCH_ULTRA_CONDENSED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"ExtraCondensed") ||
        ConsumeSuffix(familyName, L"Extra Condensed")) {
        resolved.stretch = DWRITE_FONT_STRETCH_EXTRA_CONDENSED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"SemiCondensed") ||
        ConsumeSuffix(familyName, L"Semi Condensed")) {
        resolved.stretch = DWRITE_FONT_STRETCH_SEMI_CONDENSED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Condensed")) {
        resolved.stretch = DWRITE_FONT_STRETCH_CONDENSED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"UltraExpanded") ||
        ConsumeSuffix(familyName, L"Ultra Expanded")) {
        resolved.stretch = DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"ExtraExpanded") ||
        ConsumeSuffix(familyName, L"Extra Expanded")) {
        resolved.stretch = DWRITE_FONT_STRETCH_EXTRA_EXPANDED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"SemiExpanded") ||
        ConsumeSuffix(familyName, L"Semi Expanded")) {
        resolved.stretch = DWRITE_FONT_STRETCH_SEMI_EXPANDED;
        return true;
    }
    if (ConsumeSuffix(familyName, L"Expanded")) {
        resolved.stretch = DWRITE_FONT_STRETCH_EXPANDED;
        return true;
    }
    return false;
}

bool DWriteFamilyExists(IDWriteFactory* factory, const std::wstring& familyName) noexcept {
    if (factory == nullptr || familyName.empty()) return false;
    try {
        ComPtr<IDWriteFontCollection> collection;
        if (FAILED(factory->GetSystemFontCollection(collection.GetAddressOf(), FALSE)) ||
            collection == nullptr)
            return false;
        UINT32 index = 0;
        BOOL exists = FALSE;
        return SUCCEEDED(collection->FindFamilyName(familyName.c_str(), &index, &exists)) &&
            exists != FALSE;
    } catch (...) {
        return false;
    }
}

ResolvedTimedTextFont ResolveTimedTextFontNameUncached(IDWriteFactory* factory,
    const ResolvedTimedTextFont& fallback) {
    if (fallback.family.empty() || DWriteFamilyExists(factory, fallback.family))
        return fallback;

    auto candidate = fallback;
    auto familyName = fallback.family;
    TrimOuterWhitespace(familyName);

    bool changed = false;
    do {
        changed = ConsumeKnownFontNameSuffix(familyName, candidate);
    } while (changed && !familyName.empty());

    if (!familyName.empty() && !EqualOrdinalIgnoreCase(familyName, fallback.family) &&
        DWriteFamilyExists(factory, familyName)) {
        candidate.family = std::move(familyName);
        return candidate;
    }
    return fallback;
}

ResolvedTimedTextFont ResolveTimedTextFont(IDWriteFactory* factory,
    const std::wstring& family, const DWRITE_FONT_WEIGHT weight,
    const DWRITE_FONT_STYLE style, const DWRITE_FONT_STRETCH stretch) noexcept {
    try {
        const auto fallback = CreateFallbackTimedTextFont(family, weight, style, stretch);
        {
            std::lock_guard lock(g_timedTextFontResolveMutex);
            for (auto& entry : g_timedTextFontResolveCache) {
                if (entry.weight == fallback.weight && entry.style == fallback.style &&
                    entry.stretch == fallback.stretch &&
                    EqualOrdinalIgnoreCase(entry.family, fallback.family)) {
                    entry.lastUsed = ++g_timedTextFontResolveClock;
                    return entry.resolved;
                }
            }
        }

        const auto resolved = ResolveTimedTextFontNameUncached(factory, fallback);
        {
            std::lock_guard lock(g_timedTextFontResolveMutex);
            if (g_timedTextFontResolveCache.size() >= MaximumTimedTextFontResolveCacheEntries &&
                !g_timedTextFontResolveCache.empty()) {
                const auto oldest = std::min_element(g_timedTextFontResolveCache.begin(),
                    g_timedTextFontResolveCache.end(),
                    [](const auto& left, const auto& right) {
                        return left.lastUsed < right.lastUsed;
                    });
                g_timedTextFontResolveCache.erase(oldest);
            }
            g_timedTextFontResolveCache.push_back({fallback.family, fallback.weight,
                fallback.style, fallback.stretch, resolved, ++g_timedTextFontResolveClock});
        }
        return resolved;
    } catch (...) {
        return CreateFallbackTimedTextFont(family, weight, style, stretch);
    }
}

HRESULT CreateTimedTextLayout(IDWriteFactory* factory, const std::wstring& text,
    const std::wstring& fontFamily, const float fontSize,
    const FFF3FPTimedTextFlags flags, const FFF3FPTimedTextAlignment horizontalAlignment,
    const FFF3FPTimedTextAlignment verticalAlignment, const float width,
    const float height, IDWriteTextLayout** output) noexcept {
    if (factory == nullptr || output == nullptr || fontFamily.empty() ||
        !std::isfinite(fontSize) || fontSize <= 0.0f || !std::isfinite(width) ||
        width <= 0.0f || !std::isfinite(height) || height <= 0.0f)
        return E_INVALIDARG;
    *output = nullptr;
    const auto flagBits = static_cast<std::uint32_t>(flags);
    const auto weight = (flagBits & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Bold)) != 0
        ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    const auto style = (flagBits & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Italic)) != 0
        ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    const auto resolvedFont = ResolveTimedTextFont(factory, fontFamily, weight, style,
        DWRITE_FONT_STRETCH_NORMAL);
    ComPtr<IDWriteTextFormat> format;
    auto result = factory->CreateTextFormat(resolvedFont.family.c_str(), nullptr,
        resolvedFont.weight, resolvedFont.style, resolvedFont.stretch, fontSize, L"", &format);
    if (FAILED(result)) return result;
    if (FAILED(result = format->SetTextAlignment(ToTextAlignment(horizontalAlignment))) ||
        FAILED(result = format->SetParagraphAlignment(ToParagraphAlignment(verticalAlignment))) ||
        // Managed code owns wrapping and line splitting. Measurement and drawing
        // must never make independent wrapping decisions for the same command.
        FAILED(result = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP))) return result;
    ComPtr<IDWriteTextLayout> layout;
    result = factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
        format.Get(), width, height, &layout);
    if (FAILED(result)) return result;
    const DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(text.size())};
    if ((flagBits & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Underline)) != 0 &&
        FAILED(result = layout->SetUnderline(TRUE, range))) return result;
    if ((flagBits & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Strikeout)) != 0 &&
        FAILED(result = layout->SetStrikethrough(TRUE, range))) return result;
    *output = layout.Detach();
    return S_OK;
}

template <typename T>
void HashTimedText(std::uint64_t& hash, const T& value) noexcept {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

std::uint64_t TimedTextLayoutKey(const TimedTextRenderCommand& command,
    const D2D1_RECT_F& destination, const float fontSize) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    // The managed producer may assign a new content id while a scrolling item
    // is rebuilt. Cache the immutable glyph inputs instead of that transport id;
    // otherwise every animation tick rasterizes the same text again.
    if (command.content) {
        for (const auto value : command.content->text) HashTimedText(hash, value);
        for (const auto value : command.content->fontFamily) HashTimedText(hash, value);
    }
    HashTimedText(hash, std::bit_cast<std::uint32_t>(fontSize));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(destination.right - destination.left));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(destination.bottom - destination.top));
    HashTimedText(hash, static_cast<std::uint32_t>(command.flags));
    HashTimedText(hash, static_cast<std::uint32_t>(command.horizontalAlignment));
    HashTimedText(hash, static_cast<std::uint32_t>(command.verticalAlignment));
    return hash == 0 ? 1 : hash;
}

std::uint64_t TimedTextSpriteKey(const TimedTextRenderCommand& command,
    const D2D1_RECT_F& destination, const float fontSize, const float outline,
    const float shadowX, const float shadowY) noexcept {
    auto hash = TimedTextLayoutKey(command, destination, fontSize);
    HashTimedText(hash, command.foregroundArgb);
    HashTimedText(hash, command.outlineArgb);
    HashTimedText(hash, command.shadowArgb);
    HashTimedText(hash, std::bit_cast<std::uint32_t>(outline));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(shadowX));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(shadowY));
    return hash == 0 ? 1 : hash;
}

struct TimedTextEffectExtents {
    float left = 0, top = 0, right = 0, bottom = 0;
};

TimedTextEffectExtents DescribeTimedTextEffects(const float outline,
    const float shadowX, const float shadowY, const bool hasShadow,
    const bool softShadow = false) noexcept {
    TimedTextEffectExtents result{outline, outline, outline, outline};
    if (hasShadow) {
        if (softShadow) {
            const auto spread = std::max(std::abs(shadowX), std::abs(shadowY)) *
                TimedTextSoftShadowExtentFactor;
            result.left += spread; result.top += spread;
            result.right += spread; result.bottom += spread;
        } else {
            result.left += std::max(-shadowX, 0.0f);
            result.top += std::max(-shadowY, 0.0f);
            result.right += std::max(shadowX, 0.0f);
            result.bottom += std::max(shadowY, 0.0f);
        }
    }
    return result;
}

class TimedTextEffectRenderer final : public IDWriteTextRenderer {
public:
    TimedTextEffectRenderer(ID2D1Factory1* factory, ID2D1DeviceContext* context,
        ID2D1Brush* outlineBrush, ID2D1Brush* shadowBrush, const float outline,
        const float shadowX, const float shadowY) noexcept
        : factory_(factory), context_(context), outlineBrush_(outlineBrush),
          shadowBrush_(shadowBrush), outline_(outline), shadowX_(shadowX), shadowY_(shadowY) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) ||
            iid == __uuidof(IDWriteTextRenderer)) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        if (disabled == nullptr) return E_POINTER;
        *disabled = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        if (transform == nullptr) return E_POINTER;
        D2D1_MATRIX_3X2_F value{};
        context_->GetTransform(&value);
        transform->m11 = value._11; transform->m12 = value._12;
        transform->m21 = value._21; transform->m22 = value._22;
        transform->dx = value._31; transform->dy = value._32;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
        if (pixelsPerDip == nullptr) return E_POINTER;
        *pixelsPerDip = 1.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT baselineX, FLOAT baselineY,
        DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* glyphRun,
        const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        if (glyphRun == nullptr || glyphRun->fontFace == nullptr || glyphRun->glyphCount == 0)
            return S_OK;
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return E_FAIL;
        const auto outlineResult = glyphRun->fontFace->GetGlyphRunOutline(glyphRun->fontEmSize,
            glyphRun->glyphIndices, glyphRun->glyphAdvances, glyphRun->glyphOffsets,
            glyphRun->glyphCount, glyphRun->isSideways,
            (glyphRun->bidiLevel & 1u) != 0, sink.Get());
        const auto closeResult = sink->Close();
        if (FAILED(outlineResult) || FAILED(closeResult)) return E_FAIL;
        if (shadowBrush_ != nullptr) {
            DrawEffect(path.Get(), baselineX + shadowX_, baselineY + shadowY_,
                shadowBrush_, true);
        }
        if (outlineBrush_ != nullptr && outline_ > 0.0f) {
            DrawEffect(path.Get(), baselineX, baselineY, outlineBrush_, false);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT,
        const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT,
        const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*,
        BOOL, BOOL, IUnknown*) override { return S_OK; }

private:
    void DrawEffect(ID2D1Geometry* path, const float x, const float y,
        ID2D1Brush* brush, const bool fill) noexcept {
        ComPtr<ID2D1TransformedGeometry> transformed;
        if (FAILED(factory_->CreateTransformedGeometry(path,
            D2D1::Matrix3x2F::Translation(x, y), &transformed))) return;
        if (fill) context_->FillGeometry(transformed.Get(), brush);
        if (outline_ > 0.0f) {
            // D2D strokes are centered. Drawing a 2x stroke before the glyph fill
            // leaves exactly outline_ pixels visible outside the final glyph.
            context_->DrawGeometry(transformed.Get(), brush, outline_ * 2.0f);
        }
    }

    std::atomic<ULONG> references_{1};
    ID2D1Factory1* factory_;
    ID2D1DeviceContext* context_;
    ID2D1Brush* outlineBrush_;
    ID2D1Brush* shadowBrush_;
    float outline_;
    float shadowX_;
    float shadowY_;
};
}

FFFResult EvaluateVideoColorTransform(FFF3FPColorTransform& transform) noexcept {
    if (transform.size < sizeof(transform) || transform.version != 1 ||
        transform.colorMode > FFF3FPColorMode::MapToHdr ||
        transform.transfer > FFF3FPColorTransfer::Hlg || transform.source2020 > 1 ||
        !std::isfinite(transform.inputRed) || !std::isfinite(transform.inputGreen) ||
        !std::isfinite(transform.inputBlue) || !std::isfinite(transform.sdrPeakNits) ||
        transform.sdrPeakNits <= 0.0f || !std::isfinite(transform.sourcePeakNits) ||
        transform.sourcePeakNits <= 0.0f || transform.sourcePeakNits > 10000.0f ||
        !std::isfinite(transform.paperWhiteNits) || transform.paperWhiteNits <= 0.0f)
        return FFFResult::InvalidArgument;

    Float3 rgb{transform.inputRed, transform.inputGreen, transform.inputBlue};
    if (transform.colorMode != FFF3FPColorMode::RawHdrAsSdr) {
        if (transform.colorMode == FFF3FPColorMode::MapToSdr &&
            transform.transfer == FFF3FPColorTransfer::SdrBt709) {
            if (transform.source2020 != 0) {
                rgb = {Bt709ToLinear(rgb.r), Bt709ToLinear(rgb.g), Bt709ToLinear(rgb.b)};
                Convert2020To709(rgb.r, rgb.g, rgb.b);
                rgb = {LinearToBt709(rgb.r), LinearToBt709(rgb.g), LinearToBt709(rgb.b)};
            }
        } else {
            Float3 nits{};
            if (transform.transfer == FFF3FPColorTransfer::Pq)
                nits = {PqToNits(rgb.r), PqToNits(rgb.g), PqToNits(rgb.b)};
            else if (transform.transfer == FFF3FPColorTransfer::Hlg)
                nits = {HlgToNits(rgb.r), HlgToNits(rgb.g), HlgToNits(rgb.b)};
            else
                nits = {Bt709ToLinear(rgb.r) * transform.paperWhiteNits,
                    Bt709ToLinear(rgb.g) * transform.paperWhiteNits,
                    Bt709ToLinear(rgb.b) * transform.paperWhiteNits};

            if (transform.colorMode == FFF3FPColorMode::MapToHdr) {
                // The HDR swap chain is FP16 scRGB (linear Rec.709, 1.0 =
                // 80 nits). Keep this diagnostic in the same contract as the
                // production shader instead of returning the legacy PQ code.
                if (transform.source2020 != 0)
                    Convert2020To709(nits.r, nits.g, nits.b);
                rgb = {std::max(0.0f, nits.r / 80.0f),
                    std::max(0.0f, nits.g / 80.0f),
                    std::max(0.0f, nits.b / 80.0f)};
            } else {
                if (transform.source2020 == 0) Convert709To2020(nits.r, nits.g, nits.b);
                nits = MapHdrToSdr(nits, transform.sourcePeakNits,
                    transform.sdrPeakNits);
                Convert2020To709(nits.r, nits.g, nits.b);
                rgb = {LinearToBt709(nits.r / transform.sdrPeakNits),
                    LinearToBt709(nits.g / transform.sdrPeakNits),
                    LinearToBt709(nits.b / transform.sdrPeakNits)};
            }
        }
    }
    if (transform.colorMode == FFF3FPColorMode::MapToHdr) {
        transform.outputRed = rgb.r;
        transform.outputGreen = rgb.g;
        transform.outputBlue = rgb.b;
    } else {
        transform.outputRed = Clamp01(rgb.r);
        transform.outputGreen = Clamp01(rgb.g);
        transform.outputBlue = Clamp01(rgb.b);
    }
    return FFFResult::Success;
}

FFFResult EvaluateTimedTextRasterization(FFF3FPTimedTextRasterizationProbe& probe) noexcept {
    if (probe.size < sizeof(probe) || probe.version != 1 ||
        !std::isfinite(probe.outlineWidth) || probe.outlineWidth < 0.0f ||
        !std::isfinite(probe.shadowOffsetX) || !std::isfinite(probe.shadowOffsetY))
        return FFFResult::InvalidArgument;
    const auto extents = DescribeTimedTextEffects(probe.outlineWidth,
        probe.shadowOffsetX, probe.shadowOffsetY, true);
    probe.geometryStrokeWidth = probe.outlineWidth * 2.0f;
    probe.effectLeft = extents.left; probe.effectTop = extents.top;
    probe.effectRight = extents.right; probe.effectBottom = extents.bottom;
    constexpr float radiansToDegrees = 57.29577951308232f;
    probe.shadowAngleDegrees = std::atan2(probe.shadowOffsetY,
        probe.shadowOffsetX) * radiansToDegrees;
    probe.naturalSymmetricRendering = 1;
    probe.grayscaleAntialiasing = 1;
    probe.pixelSnappingDisabled = 1;
    probe.outlineIsExternal = 1;
    return FFFResult::Success;
}

FFFResult MeasureTimedText(const char* textUtf8, const char* fontFamilyUtf8,
    const float fontSize, const FFF3FPTimedTextFlags flags, const float maxWidth,
    const float outlineWidth, const float shadowOffsetX, const float shadowOffsetY,
    const bool shadowEnabled, FFF3FPTimedTextMeasurement& measurement) noexcept {
    if (measurement.size < sizeof(measurement) || measurement.version != 1 ||
        textUtf8 == nullptr || fontFamilyUtf8 == nullptr ||
        !std::isfinite(fontSize) || fontSize <= 0.0f ||
        !std::isfinite(maxWidth) || maxWidth <= 0.0f ||
        !std::isfinite(outlineWidth) || outlineWidth < 0.0f ||
        !std::isfinite(shadowOffsetX) || !std::isfinite(shadowOffsetY))
        return FFFResult::InvalidArgument;
    std::wstring text, fontFamily;
    if (!TimedTextUtf8ToWide(textUtf8, text) || !TimedTextUtf8ToWide(fontFamilyUtf8, fontFamily) ||
        fontFamily.empty()) return FFFResult::InvalidArgument;
    try {
        ComPtr<IDWriteFactory> factory;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return FFFResult::DeviceFailure;
        ComPtr<IDWriteTextLayout> layout;
        // First discover DirectWrite's natural single-line box, then constrain
        // the production-equivalent layout to that exact height before reading
        // overhangs. A large arbitrary layout height would make bottom overhang
        // relative to the wrong box and recreate the subtitle clipping bug.
        if (FAILED(CreateTimedTextLayout(factory.Get(), text, fontFamily, fontSize, flags,
            FFF3FPTimedTextAlignment::Center, FFF3FPTimedTextAlignment::Near,
            maxWidth, 65536.0f, &layout))) return FFFResult::DeviceFailure;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics)) || !std::isfinite(metrics.height) ||
            metrics.height <= 0.0f || FAILED(layout->SetMaxHeight(metrics.height)) ||
            FAILED(layout->GetMetrics(&metrics))) return FFFResult::DeviceFailure;
        DWRITE_OVERHANG_METRICS overhang{};
        if (FAILED(layout->GetOverhangMetrics(&overhang))) return FFFResult::DeviceFailure;
        const auto flagBits = static_cast<std::uint32_t>(flags);
        const auto softShadow = (flagBits &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::SoftShadow)) != 0;
        const auto extents = DescribeTimedTextEffects(outlineWidth, shadowOffsetX,
            shadowOffsetY, shadowEnabled, softShadow);
        measurement.layoutHeight = metrics.height;
        measurement.visibleTop = metrics.top - std::max(overhang.top, 0.0f) - extents.top;
        measurement.visibleBottom = metrics.top + metrics.height +
            std::max(overhang.bottom, 0.0f) + extents.bottom;
        return FFFResult::Success;
    } catch (...) {
        return FFFResult::NativeFailure;
    }
}

FFFResult MeasureTimedTextWidth(const char* textUtf8, const char* fontFamilyUtf8,
    const float fontSize, const FFF3FPTimedTextFlags flags, float& width) noexcept {
    if (textUtf8 == nullptr || fontFamilyUtf8 == nullptr ||
        !std::isfinite(fontSize) || fontSize <= 0.0f) return FFFResult::InvalidArgument;
    std::wstring text, fontFamily;
    if (!TimedTextUtf8ToWide(textUtf8, text) || !TimedTextUtf8ToWide(fontFamilyUtf8, fontFamily) ||
        fontFamily.empty()) return FFFResult::InvalidArgument;
    try {
        ComPtr<IDWriteFactory> factory;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) || factory == nullptr)
            return FFFResult::DeviceFailure;
        ComPtr<IDWriteTextLayout> layout;
        // No wrapping is used by all timed-text commands. A finite oversized
        // layout keeps DirectWrite's natural advance width available without
        // allowing a long information line to create a second line.
        if (FAILED(CreateTimedTextLayout(factory.Get(), text, fontFamily, fontSize, flags,
            FFF3FPTimedTextAlignment::Near, FFF3FPTimedTextAlignment::Near,
            65536.0f, 65536.0f, &layout))) return FFFResult::DeviceFailure;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics)))
            return FFFResult::DeviceFailure;
        const auto naturalWidth = std::max(metrics.width, metrics.widthIncludingTrailingWhitespace);
        if (!std::isfinite(naturalWidth) || naturalWidth < 0.0f)
            return FFFResult::NativeFailure;
        if (naturalWidth == 0.0f) {
            width = 0.0f;
            return FFFResult::Success;
        }
        // Overhang metrics are relative to the layout box. Read them once more
        // after constraining that box to the natural no-wrap width; querying a
        // 65536 DIP box makes an ordinary right overhang look like empty space.
        if (FAILED(layout->SetMaxWidth(naturalWidth))) return FFFResult::DeviceFailure;
        if (FAILED(layout->GetMetrics(&metrics))) return FFFResult::DeviceFailure;
        DWRITE_OVERHANG_METRICS overhang{};
        if (FAILED(layout->GetOverhangMetrics(&overhang)))
            return FFFResult::DeviceFailure;
        const auto advance = std::max(metrics.width, metrics.widthIncludingTrailingWhitespace);
        const auto left = std::max(0.0f, overhang.left);
        const auto right = std::max(0.0f, overhang.right);
        const auto measured = advance + left + right;
        if (!std::isfinite(measured) || measured < 0.0f) return FFFResult::NativeFailure;
        width = measured;
        return FFFResult::Success;
    } catch (...) {
        return FFFResult::NativeFailure;
    }
}

PlayerVideoRenderer::PlayerVideoRenderer(std::function<void()> recoveryCallback) noexcept
    : window_(nullptr), device_(nullptr), context_(nullptr), swapChain_(nullptr),
      vertexShader_(nullptr), pixelShader_(nullptr), coverBackdropPixelShader_(nullptr),
      timedTextPixelShader_(nullptr), scalePixelShader_(nullptr), sampler_(nullptr),
      pointSampler_(nullptr), constants_(nullptr), scaleConstants_(nullptr),
      sourceTextures_{nullptr, nullptr, nullptr}, sourceViews_{nullptr, nullptr, nullptr},
      scaledVideoGeneration_(UINT64_MAX), scaledOutputWidth_(0), scaledOutputHeight_(0),
      scaledSourceViews_{nullptr, nullptr, nullptr},
      videoDevice_(nullptr), videoContext_(nullptr), videoProcessorEnumerator_(nullptr),
      videoProcessor_(nullptr), videoProcessorRenderTexture_(nullptr),
      videoProcessorRenderTarget_(nullptr),
      coverBackdropTexture_(nullptr), coverBackdropView_(nullptr),
      coverBackdropSourceTexture_(nullptr), coverBackdropSourceTarget_(nullptr),
      timedTextTextures_{nullptr, nullptr, nullptr, nullptr},
      timedTextTargets_{nullptr, nullptr, nullptr, nullptr},
      timedTextViews_{nullptr, nullptr, nullptr, nullptr},
      timedTextPipelineQueries_{nullptr, nullptr, nullptr, nullptr},
      timedTextBlend_(nullptr),
      timedTextAtlasTexture_(nullptr), timedTextAtlasView_(nullptr),
      timedTextResourcesHdr_(false), timedTextAtlasHdr_(false),
      timedTextSpriteVertexShader_(nullptr), timedTextSpritePixelShader_(nullptr),
      timedTextSpriteInstanceBuffer_(nullptr), timedTextSpriteInstanceView_(nullptr),
      d2dFactory_(nullptr), d2dDevice_(nullptr), d2dContext_(nullptr),
      d2dCoverBackdropSource_(nullptr), d2dCoverBackdropTarget_(nullptr),
      coverBackdropBlurEffect_(nullptr),
      d2dTargets_{nullptr, nullptr, nullptr, nullptr},
      d2dAtlasTarget_(nullptr), d2dTimedTextShadowTarget_(nullptr),
      timedTextShadowBlurEffect_(nullptr),
      writeFactory_(nullptr), timedTextRenderingParams_(nullptr), scaler_(nullptr),
      swapWidth_(0), swapHeight_(0), swapHdr_(false), swapOutputBits_(8), sourceWidth_(0), sourceHeight_(0),
      sourceInputLayout_(UINT32_MAX), sourceBitDepth_(0),
      sourceChromaWidthShift_(0), sourceChromaHeightShift_(0),
      sourceExternal_(false), sourceLimitedToNativeSize_(false), sourceCoverArt_(false),
      coverBackdropWidth_(0), coverBackdropHeight_(0),
      coverBackdropVideoGeneration_(0),
      coverBackdropAppliedBlurSettingsGeneration_(0),
      videoProcessorInputFormat_(DXGI_FORMAT_UNKNOWN),
      videoProcessorOutputFormat_(DXGI_FORMAT_UNKNOWN),
      videoProcessorInputColorSpace_(DXGI_COLOR_SPACE_CUSTOM),
      videoProcessorOutputColorSpace_(DXGI_COLOR_SPACE_CUSTOM), videoProcessorInputWidth_(0),
      videoProcessorInputHeight_(0), videoProcessorOutputWidth_(0),
      videoProcessorOutputHeight_(0), videoProcessorConfigurationFailed_(false),
      sourceColorSpace_(AVCOL_SPC_UNSPECIFIED), sourceChromaLocation_(AVCHROMA_LOC_UNSPECIFIED),
      sourceFullRange_(false), sourceInterlaced_(false),
      actualVideoScalingMode_(FFF3FPVideoScalingMode::D3D11VideoProcessor),
      scalingQuality_(FFF3FPVideoScalingQuality::HighQuality),
      requestedMode_(FFF3FPColorMode::MapToSdr), actualMode_(FFF3FPColorMode::MapToSdr),
      sdrPeakNits_(100.0f), hdrPeakNits_(0.0f),
      paperWhiteNits_(203.0f), sourcePeakNits_(100.0f),
      viewZoomBits_(std::bit_cast<float>(1.0f)),
      viewPanXBits_(std::bit_cast<float>(0.0f)),
      viewPanYBits_(std::bit_cast<float>(0.0f)),
      timedTextThreadStop_(false), timedTextThreadRunning_(false),
      coverBackdropThreadStop_(false), coverBackdropRequestPending_(false),
      coverBackdropRequestGeneration_(0),
      presentationGeneration_(0), presentationFrameRate_(60.0f),
      timedTextRenderedSequences_{0, 0, 0, 0}, timedTextRenderedCommandCounts_{0, 0, 0, 0},
      timedTextRenderedHdrHighlights_{false, false, false, false},
      timedTextWidths_{0, 0, 0, 0}, timedTextHeights_{0, 0, 0, 0},
      timedTextPresentCounts_{0, 0, 0, 0},
      backBufferAcquisitionCount_(0),
      timedTextPipelineQueryInFlight_{false, false, false, false},
      timedTextCompositePixelInvocations_{0, 0, 0, 0},
      hasCachedVideo_(false), videoGeneration_(0), presentedVideoGeneration_(0),
      presentedVideoFrames_(0), coalescedVideoFrames_(0), swapChainPresents_(0),
      presentWait100ns_(0), deviceLockWait100ns_(0), softwareConvert100ns_(0),
      playbackWorkPending_(0),
      lyricsLayoutEnabled_(false),
      coverBackdropBlurRadiusBits_(std::bit_cast<std::uint32_t>(30.0f)),
      coverBackdropBlurPasses_(3), coverBackdropDownsampleFactor_(4),
      coverBackdropTintArgb_(0x78000000u),
      coverRegionWidthPercentageBits_(std::bit_cast<std::uint32_t>(50.0f)),
      lyricsRegionWidthPercentageBits_(std::bit_cast<std::uint32_t>(50.0f)),
      coverLeftPaddingPercentageBits_(std::bit_cast<std::uint32_t>(7.5f)),
      coverRightPaddingPercentageBits_(std::bit_cast<std::uint32_t>(0.0f)),
      coverVerticalPaddingPercentageBits_(std::bit_cast<std::uint32_t>(7.5f)),
      coverBackdropBlurSettingsGeneration_(1),
      deviceRecoveryRequested_(false), recoveryCallback_(std::move(recoveryCallback)),
       hdrMonitor_(nullptr), hdrSupportValid_(false), hdrSupported_(false),
      hdrSupportCheckedAt_(std::chrono::steady_clock::time_point::min()),
      hdrSwapChainRejected_(false),
      timedTextAtlasX_(0), timedTextAtlasY_(0), timedTextAtlasRowHeight_(0),
      timedTextAtlasSize_(0), timedTextSpriteInstanceCapacity_(0),
      timedTextSpriteCacheHits_(0), timedTextSpriteCacheMisses_(0) {}

PlayerVideoRenderer::~PlayerVideoRenderer() { Close(); }

FFFResult PlayerVideoRenderer::SetWindow(const HWND window) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (window != nullptr && !IsWindow(window)) return FFFResult::InvalidArgument;
    if (window == window_) return FFFResult::Success;
    if (swapChain_ != nullptr) {
        std::lock_guard presentLock(presentMutex_);
        swapChain_->Release(); swapChain_ = nullptr;
    }
    window_ = window;
    // A generation presented to the previous HWND says nothing about the new
    // swap chain. Reset only the acknowledgement; submitted video remains
    // cached and will be presented again by Redraw.
    presentedVideoGeneration_.store(0, std::memory_order_release);
    hdrSupportValid_ = false; hdrMonitor_ = nullptr;
    hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
    hdrSwapChainRejected_ = false;
    swapWidth_ = swapHeight_ = 0;
    swapHdr_ = false; swapOutputBits_ = 8;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        fallbackReason_.clear();
        const auto sourceHdr = hdrProcessor_.IsHdrSource();
        actualMode_ = sourceHdr && OutputSupportsHdr() ?
            FFF3FPColorMode::MapToHdr : FFF3FPColorMode::MapToSdr;
        if (actualMode_ != requestedMode_) {
            fallbackReason_ = sourceHdr ?
                "The target display or Windows Advanced Color mode does not support true HDR output." :
                "True HDR output is only available for HDR source video.";
        }
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetScalingQuality(
    const FFF3FPVideoScalingQuality quality) noexcept {
    if (quality > FFF3FPVideoScalingQuality::HighQuality)
        return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    if (scalingQuality_ == quality) return FFFResult::Success;
    scalingQuality_ = quality;
    scaledVideoGeneration_ = UINT64_MAX;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetViewTransform(const float zoom,
    const float panX, const float panY) noexcept {
    if (!std::isfinite(zoom) || zoom <= 0.0f || !std::isfinite(panX) || !std::isfinite(panY))
        return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    const auto zoomClamped = std::clamp(zoom, 0.05f, 64.0f);
    const auto panXClamped = std::clamp(panX, -1.0f, 1.0f);
    const auto panYClamped = std::clamp(panY, -1.0f, 1.0f);
    viewZoomBits_.store(std::bit_cast<float>(zoomClamped), std::memory_order_relaxed);
    viewPanXBits_.store(std::bit_cast<float>(panXClamped), std::memory_order_relaxed);
    viewPanYBits_.store(std::bit_cast<float>(panYClamped), std::memory_order_relaxed);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetColorMode(const FFF3FPColorMode mode, const float sdrPeakNits,
    const float hdrPeakNits, const float paperWhiteNits) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdrPeakNits) || sdrPeakNits <= 0.0f ||
        !std::isfinite(hdrPeakNits) || hdrPeakNits < 0.0f || hdrPeakNits > 10000.0f ||
        !std::isfinite(paperWhiteNits) || paperWhiteNits <= 0.0f) return FFFResult::InvalidArgument;
    requestedMode_ = mode;
    sdrPeakNits_ = sdrPeakNits;
    // Zero selects the current display's reported peak. Positive values are a
    // future user setting and intentionally override the monitor descriptor.
    hdrPeakNits_ = hdrPeakNits;
    paperWhiteNits_ = paperWhiteNits;
    fallbackReason_.clear();
    actualMode_ = requestedMode_;
    hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
    hdrSwapChainRejected_ = false;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        const auto sourceHdr = hdrProcessor_.IsHdrSource();
        if (!sourceHdr || !OutputSupportsHdr()) {
            actualMode_ = FFF3FPColorMode::MapToSdr;
            fallbackReason_ = sourceHdr ?
                "The target display or Windows Advanced Color mode does not support true HDR output." :
                "True HDR output is only available for HDR source video.";
        }
    }
    // hdrPeakNits_ is an output-display override, never the source mastering
    // peak. SDR callers pass zero; source peak metadata is configured per frame.
    hdrProcessor_.SetTargetPeakOverride(hdrPeakNits_);
    if (hasCachedVideo_) {
        cachedVideoSettings_.sdrPeak = sdrPeakNits_;
        cachedVideoSettings_.paperWhite = paperWhiteNits_;
        cachedVideoSettings_.targetPeak = hdrProcessor_.State().targetPeakNits;
    }
    const bool hdr = actualMode_ == FFF3FPColorMode::MapToHdr;
    const auto outputBits = PreferredOutputBitDepth(sourceBitDepth_, hdr);
    if (swapChain_ != nullptr && (swapHdr_ != hdr || swapOutputBits_ != outputBits)) {
        const auto result = ReconfigureSwapChain(hdr, outputBits);
        if (result != FFFResult::Success) return result;
    }
    if (swapHdr_) SetHdrMetadata();
    return FFFResult::Success;
}

void PlayerVideoRenderer::ConfigureHdrStream(const AVCodecParameters* parameters) noexcept {
    hdrProcessor_.ConfigureStream(parameters);
}

FFFResult PlayerVideoRenderer::ForceSdrOutputForSdrSource() noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    requestedMode_ = FFF3FPColorMode::MapToSdr;
    actualMode_ = FFF3FPColorMode::MapToSdr;
    if (swapChain_ != nullptr && swapHdr_) {
        const auto result = ReconfigureSwapChain(false, PreferredOutputBitDepth(sourceBitDepth_, false));
        if (result != FFFResult::Success) return result;
    }
    fallbackReason_.clear();
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureDevice() noexcept {
    if (device_ != nullptr) return FFFResult::Success;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selected{};
    ComPtr<IDXGIAdapter1> selectedAdapter;
    if (window_ != nullptr && IsWindow(window_)) {
        const auto monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        ComPtr<IDXGIFactory6> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            for (UINT adapterIndex = 0; !selectedAdapter; ++adapterIndex) {
                ComPtr<IDXGIAdapter1> candidate;
                if (factory->EnumAdapters1(adapterIndex, &candidate) == DXGI_ERROR_NOT_FOUND) break;
                for (UINT outputIndex = 0;; ++outputIndex) {
                    ComPtr<IDXGIOutput> output;
                    if (candidate->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
                    DXGI_OUTPUT_DESC description{};
                    if (SUCCEEDED(output->GetDesc(&description)) && description.Monitor == monitor) {
                        selectedAdapter = candidate;
                        break;
                    }
                }
            }
        }
    }
    const auto result = D3D11CreateDevice(selectedAdapter.Get(), selectedAdapter
        ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &selected, &context_);
    if (FAILED(result)) { SetError("Could not create the D3D11 playback device."); return FFFResult::DeviceFailure; }
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&multithread)))) multithread->SetMultithreadProtected(TRUE);
    return FFFResult::Success;
}

std::uint32_t PlayerVideoRenderer::PreferredOutputBitDepth(
    const std::uint32_t sourceBitDepth, const bool hdr) noexcept {
    return OutputBitDepthForSource(sourceBitDepth, hdr);
}

bool PlayerVideoRenderer::OutputSupportsHdr() noexcept {
    if (window_ == nullptr || !IsWindow(window_)) {
        hdrSupportValid_ = false;
        hdrMonitor_ = nullptr;
        hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
        hdrSwapChainRejected_ = false;
        hdrSupported_ = false;
        hdrProcessor_.SetDisplayCapabilities({});
        return false;
    }
    const auto monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    const auto now = std::chrono::steady_clock::now();
    const auto previousMonitor = hdrMonitor_;
    const auto previousCapabilities = hdrProcessor_.State().display;
    const auto preservePrevious = hdrSupportValid_ && previousMonitor == monitor;
    const auto cachedUsable = [this](const HdrDisplayCapabilities& capabilities) noexcept {
        return hdrSupported_ && (hdrPeakNits_ > 0.0f || capabilities.maximumNits > 0.0f);
    };
    if (!preservePrevious) hdrSwapChainRejected_ = false;
    if (preservePrevious && hdrSwapChainRejected_) return false;
    if (preservePrevious && now - hdrSupportCheckedAt_ < HdrSupportProbeCacheDuration)
        return cachedUsable(previousCapabilities);
    hdrMonitor_ = monitor;
    hdrSupportValid_ = true;
    hdrSupportCheckedAt_ = now;
    hdrSupported_ = false;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        if (preservePrevious) {
            hdrSupported_ = previousCapabilities.supported;
            hdrProcessor_.SetDisplayCapabilities(previousCapabilities);
            return cachedUsable(previousCapabilities);
        }
        hdrProcessor_.SetDisplayCapabilities({});
        return false;
    }
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
            if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&description1))) {
                if (preservePrevious) {
                    hdrSupported_ = previousCapabilities.supported;
                    hdrProcessor_.SetDisplayCapabilities(previousCapabilities);
                    return cachedUsable(previousCapabilities);
                }
                hdrProcessor_.SetDisplayCapabilities({});
                return false;
            }
            hdrSupported_ = description1.BitsPerColor >= 10 &&
                (description1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                 description1.ColorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020);
            HdrDisplayCapabilities capabilities{hdrSupported_, description1.MinLuminance,
                description1.MaxLuminance, description1.MaxFullFrameLuminance};
            // AdvancedColorInfo reflects the active Windows HDR calibration
            // shown in Settings. Some drivers leave the DXGI luminance fields
            // empty even though Advanced Color is active.
            if (!ReadWindowsDisplayLuminance(monitor, capabilities) && preservePrevious &&
                previousCapabilities.maximumNits > 0.0f) {
                // AdvancedColorInfo can briefly fail while Windows reapplies HDR
                // calibration. Keep the last value for this same monitor instead
                // of dropping to the generic 1000-nit fallback.
                capabilities.minimumNits = previousCapabilities.minimumNits;
                capabilities.maximumNits = previousCapabilities.maximumNits;
                capabilities.maximumFullFrameNits = previousCapabilities.maximumFullFrameNits;
            }
            hdrProcessor_.SetDisplayCapabilities(capabilities);
            const auto hasDisplayPeak = capabilities.maximumNits > 0.0f;
            // With automatic target selection, do not enter the HDR swap chain
            // until Windows has supplied a calibrated luminance. Otherwise the
            // first click can briefly present at the generic 1000-nit fallback.
            return hdrSupported_ && (hdrPeakNits_ > 0.0f || hasDisplayPeak);
        }
    }
    if (preservePrevious) {
        hdrSupported_ = previousCapabilities.supported;
        hdrProcessor_.SetDisplayCapabilities(previousCapabilities);
        return cachedUsable(previousCapabilities);
    }
    hdrProcessor_.SetDisplayCapabilities({});
    return false;
}

FFFResult PlayerVideoRenderer::CreateD3D11HardwareDeviceContext(AVBufferRef** output) noexcept {
    if (output == nullptr) return FFFResult::InvalidArgument;
    *output = nullptr;
    std::lock_guard deviceLock(deviceMutex_);
    const auto result = EnsureDevice();
    if (result != FFFResult::Success) return result;
    auto* reference = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (reference == nullptr) return FFFResult::NativeFailure;
    auto* hardware = reinterpret_cast<AVHWDeviceContext*>(reference->data);
    auto* d3d = static_cast<AVD3D11VADeviceContext*>(hardware->hwctx);
    device_->AddRef();
    d3d->device = device_;
    d3d->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (av_hwdevice_ctx_init(reference) < 0) {
        av_buffer_unref(&reference);
        SetError("FFmpeg could not bind hardware decoding to the playback D3D11 device.");
        return FFFResult::NotSupported;
    }
    *output = reference;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureSwapChain(std::uint32_t width, std::uint32_t height,
    const std::uint32_t sourceBitDepth) noexcept {
    if (window_ == nullptr) return FFFResult::Success;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        const auto sourceHdr = hdrProcessor_.IsHdrSource();
        const auto nextMode = sourceHdr && OutputSupportsHdr() ?
            FFF3FPColorMode::MapToHdr : FFF3FPColorMode::MapToSdr;
        const auto reason = sourceHdr ?
            "The target display or Windows Advanced Color mode does not support true HDR output." :
            "True HDR output is only available for HDR source video.";
        fallbackReason_ = nextMode == requestedMode_ ? std::string{} : reason;
        if (nextMode != actualMode_) {
            actualMode_ = nextMode;
            if (swapChain_ != nullptr) {
                const auto modeResult = ReconfigureSwapChain(
                    nextMode == FFF3FPColorMode::MapToHdr,
                    PreferredOutputBitDepth(sourceBitDepth, nextMode == FFF3FPColorMode::MapToHdr));
                if (modeResult != FFFResult::Success) return modeResult;
            }
        }
    }
    const auto deviceResult = EnsureDevice();
    if (deviceResult != FFFResult::Success) return deviceResult;
    RECT client{};
    if (!GetClientRect(window_, &client)) return FFFResult::DeviceFailure;
    width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(client.right - client.left));
    height = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(client.bottom - client.top));
    const bool hdr = actualMode_ == FFF3FPColorMode::MapToHdr;
    const auto outputBits = PreferredOutputBitDepth(sourceBitDepth, hdr);
    if (swapChain_ != nullptr && (hdr != swapHdr_ || outputBits != swapOutputBits_)) {
        const auto modeResult = ReconfigureSwapChain(hdr, outputBits);
        if (modeResult != FFFResult::Success) return modeResult;
    }
    if (swapChain_ != nullptr && width == swapWidth_ && height == swapHeight_ &&
        hdr == swapHdr_ && outputBits == swapOutputBits_) return FFFResult::Success;
    if (swapChain_ != nullptr && hdr == swapHdr_ && outputBits == swapOutputBits_) {
        context_->ClearState();
        std::lock_guard presentLock(presentMutex_);
        const auto resize = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (SUCCEEDED(resize)) {
            swapWidth_ = width; swapHeight_ = height;
            ReleaseTimedTextResources();
            return FFFResult::Success;
        }
        std::ostringstream message;
        message << "Could not resize the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(resize) << ").";
        SetError(message.str());
        return FFFResult::DeviceFailure;
    }
    return CreateSwapChain(width, height, hdr, outputBits);
}

FFFResult PlayerVideoRenderer::CreateSwapChain(const std::uint32_t width,
    const std::uint32_t height, const bool hdr, const std::uint32_t outputBits) noexcept {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        SetError("Could not obtain the DXGI playback factory."); return FFFResult::DeviceFailure;
    }
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width; description.Height = height;
    description.Format = outputBits >= 16 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
        (outputBits >= 10 ? DXGI_FORMAT_R10G10B10A2_UNORM :
            DXGI_FORMAT_B8G8R8A8_UNORM);
    description.SampleDesc.Count = 1; description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2; description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE; description.Scaling = DXGI_SCALING_NONE;
    ComPtr<IDXGISwapChain1> chain1;
    const auto result = factory->CreateSwapChainForHwnd(device_, window_, &description, nullptr, nullptr, &chain1);
    if (FAILED(result) || FAILED(chain1->QueryInterface(IID_PPV_ARGS(&swapChain_)))) {
        std::ostringstream message;
        message << "Could not create the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(result) << ").";
        SetError(message.str()); return FFFResult::DeviceFailure;
    }
    swapWidth_ = width; swapHeight_ = height; swapHdr_ = hdr; swapOutputBits_ = outputBits;
    ReleaseTimedTextResources();
    if (hdr) {
        UINT support = 0;
        if (FAILED(swapChain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support)) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))) {
            fallbackReason_ = "The swap chain rejected the scRGB color space.";
            actualMode_ = FFF3FPColorMode::MapToSdr;
            hdrSwapChainRejected_ = true;
            hdrSupportCheckedAt_ = std::chrono::steady_clock::now();
            swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
            swapChain_->Release();
            swapChain_ = nullptr;
            swapWidth_ = swapHeight_ = 0;
            swapHdr_ = false;
            swapOutputBits_ = 8;
            return CreateSwapChain(width, height, false,
                PreferredOutputBitDepth(sourceBitDepth_, false));
        }
        hdrSwapChainRejected_ = false;
        SetHdrMetadata();
    } else {
        // Keep a newly-created SDR swap chain on DXGI's default SDR contract.
        // Do not call SetColorSpace1 or SetHDRMetaData, even with NONE: either
        // call opts the window into an explicit Advanced Color presentation
        // contract instead of the ordinary SDR desktop path.
    }
    // Keep at most one complete composite queued. Decode and managed overlay
    // production retain only their latest state while Present is waiting.
    swapChain_->SetMaximumFrameLatency(1);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::ReconfigureSwapChain(const bool hdr, const std::uint32_t outputBits) noexcept {
    const auto formatBits = hdr ? std::max(16u, outputBits) : outputBits;
    if (swapChain_ == nullptr || (hdr == swapHdr_ && formatBits == swapOutputBits_)) return FFFResult::Success;
    if (context_ != nullptr) { context_->ClearState(); context_->Flush(); }
    ReleaseTimedTextResources();
    // The cache stores the main shader's encoded contract.  A mode/format
    // switch therefore requires one fresh render even when the video
    // generation itself did not change.
    coverBackdropVideoGeneration_ = 0;
    // Enter HDR by resizing the existing flip chain. Replacing an actively
    // presented HWND chain can leave the first PQ Present waiting indefinitely
    // in DWM. Leaving HDR still requires a fresh chain so the window returns to
    // DXGI's implicit SDR desktop contract instead of retaining Advanced Color.
    if (!hdr && swapHdr_) {
        const auto width = std::max(1u, swapWidth_);
        const auto height = std::max(1u, swapHeight_);
        std::lock_guard presentLock(presentMutex_);
        swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        swapChain_->Release();
        swapChain_ = nullptr;
        swapWidth_ = swapHeight_ = 0;
        swapHdr_ = false;
        swapOutputBits_ = 8;
        return CreateSwapChain(width, height, hdr, formatBits);
    }
    const auto format = formatBits >= 16 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
        (formatBits >= 10 ? DXGI_FORMAT_R10G10B10A2_UNORM :
            DXGI_FORMAT_B8G8R8A8_UNORM);
    std::unique_lock presentLock(presentMutex_);
    const auto resize = swapChain_->ResizeBuffers(0, std::max(1u, swapWidth_),
        std::max(1u, swapHeight_), format, 0);
    if (FAILED(resize)) {
        std::ostringstream message;
        message << "Could not reconfigure the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(resize) << ").";
        SetError(message.str());
        return FFFResult::DeviceFailure;
    }
    swapHdr_ = hdr; swapOutputBits_ = formatBits;
    if (hdr) {
        UINT support = 0;
        if (FAILED(swapChain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support)) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))) {
            fallbackReason_ = "The reconfigured swap chain rejected the scRGB color space.";
            actualMode_ = FFF3FPColorMode::MapToSdr;
            hdrSwapChainRejected_ = true;
            hdrSupportCheckedAt_ = std::chrono::steady_clock::now();
            swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
            swapChain_->Release();
            swapChain_ = nullptr;
            const auto width = std::max(1u, swapWidth_);
            const auto height = std::max(1u, swapHeight_);
            swapWidth_ = swapHeight_ = 0;
            swapHdr_ = false;
            swapOutputBits_ = 8;
            presentLock.unlock();
            return CreateSwapChain(width, height, false,
                PreferredOutputBitDepth(sourceBitDepth_, false));
        }
        hdrSwapChainRejected_ = false;
        SetHdrMetadata();
    } else {
        // This branch only changes precision within an already-SDR chain. Keep
        // the implicit SDR contract untouched.
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::AcquireBackBufferTarget(ID3D11Texture2D** buffer,
    ID3D11RenderTargetView** target) noexcept {
    if (buffer == nullptr || target == nullptr) return FFFResult::InvalidArgument;
    *buffer = nullptr; *target = nullptr;
    if (swapChain_ == nullptr || device_ == nullptr) return FFFResult::InvalidState;
    // D3D11 flip-model exposes the current writable buffer through logical
    // index 0. Its physical identity changes after Present, so neither this
    // texture nor its RTV may be cached across presentation cycles.
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(buffer))) ||
        FAILED(device_->CreateRenderTargetView(*buffer, nullptr, target))) {
        if (*target != nullptr) { (*target)->Release(); *target = nullptr; }
        if (*buffer != nullptr) { (*buffer)->Release(); *buffer = nullptr; }
        SetError("Could not acquire the current playback back-buffer render target.");
        return FFFResult::DeviceFailure;
    }
    ++backBufferAcquisitionCount_;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsurePipeline(const std::uint32_t sourceWidth,
    const std::uint32_t sourceHeight, const std::uint32_t inputLayout,
    const std::uint32_t bitDepth, const std::uint32_t chromaWidthShift,
    const std::uint32_t chromaHeightShift, const bool externalSource) noexcept {
    if (vertexShader_ == nullptr || pixelShader_ == nullptr || scalePixelShader_ == nullptr ||
        coverBackdropPixelShader_ == nullptr || timedTextPixelShader_ == nullptr ||
        sampler_ == nullptr || pointSampler_ == nullptr ||
        constants_ == nullptr || scaleConstants_ == nullptr) {
        ComPtr<ID3DBlob> vertexCode, pixelCode, scalePixelCode, coverBackdropPixelCode,
            timedTextPixelCode, errors;
        if (FAILED(D3DCompile(VertexShaderSource, std::strlen(VertexShaderSource), nullptr, nullptr, nullptr,
            "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertexCode, &errors)) ||
            FAILED(D3DCompile(PixelShaderSource, std::strlen(PixelShaderSource), nullptr, nullptr, nullptr,
                "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelCode, &errors)) ||
            FAILED(D3DCompile(ScalePixelShaderSource, std::strlen(ScalePixelShaderSource),
                nullptr, nullptr, nullptr, "main", "ps_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &scalePixelCode, &errors)) ||
            FAILED(D3DCompile(CoverBackdropPixelShaderSource,
                std::strlen(CoverBackdropPixelShaderSource), nullptr, nullptr, nullptr,
                "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                &coverBackdropPixelCode, &errors)) ||
            FAILED(D3DCompile(TimedTextPixelShaderSource, std::strlen(TimedTextPixelShaderSource), nullptr, nullptr, nullptr,
            "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &timedTextPixelCode, &errors)) ||
            FAILED(device_->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_)) ||
            FAILED(device_->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader_)) ||
            FAILED(device_->CreatePixelShader(scalePixelCode->GetBufferPointer(),
                scalePixelCode->GetBufferSize(), nullptr, &scalePixelShader_)) ||
            FAILED(device_->CreatePixelShader(coverBackdropPixelCode->GetBufferPointer(),
                coverBackdropPixelCode->GetBufferSize(), nullptr,
                &coverBackdropPixelShader_)) ||
            FAILED(device_->CreatePixelShader(timedTextPixelCode->GetBufferPointer(), timedTextPixelCode->GetBufferSize(), nullptr, &timedTextPixelShader_))) {
            SetError(errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                "Could not create the playback presentation shaders.");
            return FFFResult::DeviceFailure;
        }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_SAMPLER_DESC pointSampler = sampler;
        pointSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(ShaderSettings); buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_BUFFER_DESC scaleBuffer = buffer;
        scaleBuffer.ByteWidth = sizeof(ScaleShaderSettings);
        if (FAILED(device_->CreateSamplerState(&sampler, &sampler_)) ||
            FAILED(device_->CreateSamplerState(&pointSampler, &pointSampler_)) ||
            FAILED(device_->CreateBuffer(&buffer, nullptr, &constants_)) ||
            FAILED(device_->CreateBuffer(&scaleBuffer, nullptr, &scaleConstants_))) {
            SetError("Could not create the presentation shader resources."); return FFFResult::DeviceFailure;
        }
    }
    if (sourceTextures_[0] != nullptr && sourceExternal_ == externalSource &&
        sourceWidth_ == sourceWidth && sourceHeight_ == sourceHeight &&
        sourceInputLayout_ == inputLayout && sourceBitDepth_ == bitDepth &&
        sourceChromaWidthShift_ == chromaWidthShift &&
        sourceChromaHeightShift_ == chromaHeightShift)
        return FFFResult::Success;
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        if (sourceViews_[plane] != nullptr) { sourceViews_[plane]->Release(); sourceViews_[plane] = nullptr; }
        if (sourceTextures_[plane] != nullptr) { sourceTextures_[plane]->Release(); sourceTextures_[plane] = nullptr; }
    }
    ReleaseScaleResources();
    const auto planeCount = inputLayout == 1 ? 3u : (inputLayout == 2 ? 2u : 1u);
    if (externalSource) {
        if (inputLayout != 2) {
            SetError("The D3D11 decoder output is not a supported semiplanar surface.");
            return FFFResult::NotSupported;
        }
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = sourceWidth; texture.Height = sourceHeight;
        texture.MipLevels = texture.ArraySize = 1;
        texture.Format = bitDepth <= 8 ? DXGI_FORMAT_NV12 :
            (bitDepth <= 10 ? DXGI_FORMAT_P010 : DXGI_FORMAT_P016);
        texture.SampleDesc.Count = 1; texture.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SHADER_RESOURCE_VIEW_DESC luma{};
        luma.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        luma.Texture2D.MipLevels = 1;
        luma.Format = bitDepth > 8 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
        auto chroma = luma;
        chroma.Format = bitDepth > 8 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
        const auto createRetainedSurface = [&](const UINT bindFlags) noexcept {
            texture.BindFlags = bindFlags;
            if (FAILED(device_->CreateTexture2D(&texture, nullptr, &sourceTextures_[0])) ||
                FAILED(device_->CreateShaderResourceView(
                    sourceTextures_[0], &luma, &sourceViews_[0])) ||
                FAILED(device_->CreateShaderResourceView(
                    sourceTextures_[0], &chroma, &sourceViews_[1]))) {
                if (sourceViews_[1] != nullptr) {
                    sourceViews_[1]->Release();
                    sourceViews_[1] = nullptr;
                }
                if (sourceViews_[0] != nullptr) {
                    sourceViews_[0]->Release();
                    sourceViews_[0] = nullptr;
                }
                if (sourceTextures_[0] != nullptr) {
                    sourceTextures_[0]->Release();
                    sourceTextures_[0] = nullptr;
                }
                return false;
            }
            return true;
        };
        if (!createRetainedSurface(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DECODER) &&
            !createRetainedSurface(D3D11_BIND_SHADER_RESOURCE)) {
            SetError("The retained D3D11 video surface is not shader-readable.");
            return FFFResult::NotSupported;
        }
    }
    for (std::uint32_t plane = 0; plane < (externalSource ? 0u : planeCount); ++plane) {
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = plane == 0 ? sourceWidth :
            (sourceWidth + (1u << chromaWidthShift) - 1) >> chromaWidthShift;
        texture.Height = plane == 0 ? sourceHeight :
            (sourceHeight + (1u << chromaHeightShift) - 1) >> chromaHeightShift;
        texture.MipLevels = texture.ArraySize = 1;
        if (inputLayout == 0) texture.Format = bitDepth <= 8 ?
            DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
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
    sourceExternal_ = externalSource;
    sourceChromaWidthShift_ = chromaWidthShift;
    sourceChromaHeightShift_ = chromaHeightShift;
    return FFFResult::Success;
}

bool PlayerVideoRenderer::CanUseDirectVideoProcessor() const noexcept {
    if (hdrProcessor_.RequiresMetadataAwareShader()) return false;
    if (!sourceExternal_ || sourceInputLayout_ != 2 || sourceTextures_[0] == nullptr ||
        sourceBitDepth_ > 10 || sourceInterlaced_ ||
        sourceChromaLocation_ != AVCHROMA_LOC_LEFT ||
        cachedVideoSettings_.transfer != 0 || actualMode_ != FFF3FPColorMode::MapToSdr ||
        swapOutputBits_ > 10)
        return false;
    D3D11_TEXTURE2D_DESC inputDescription{};
    sourceTextures_[0]->GetDesc(&inputDescription);
    return (inputDescription.BindFlags & D3D11_BIND_DECODER) != 0;
}

FFFResult PlayerVideoRenderer::EnsureVideoProcessor(ID3D11Texture2D* inputTexture,
    ID3D11Texture2D* outputTexture, const std::uint32_t inputColorSpace,
    const std::uint32_t outputColorSpace) noexcept {
    if (inputTexture == nullptr || outputTexture == nullptr ||
        device_ == nullptr || context_ == nullptr)
        return FFFResult::NotSupported;
    D3D11_TEXTURE2D_DESC inputDescription{};
    D3D11_TEXTURE2D_DESC outputDescription{};
    inputTexture->GetDesc(&inputDescription);
    outputTexture->GetDesc(&outputDescription);
    if (inputDescription.ArraySize != 1 || outputDescription.ArraySize != 1)
        return FFFResult::NotSupported;
    const auto sameConfiguration = videoProcessorInputFormat_ == inputDescription.Format &&
        videoProcessorOutputFormat_ == outputDescription.Format &&
        videoProcessorInputColorSpace_ == inputColorSpace &&
        videoProcessorOutputColorSpace_ == outputColorSpace &&
        videoProcessorInputWidth_ == inputDescription.Width &&
        videoProcessorInputHeight_ == inputDescription.Height &&
        videoProcessorOutputWidth_ == outputDescription.Width &&
        videoProcessorOutputHeight_ == outputDescription.Height;
    if (sameConfiguration) {
        if (videoProcessorConfigurationFailed_) return FFFResult::NotSupported;
        if (videoProcessor_ != nullptr && videoProcessorEnumerator_ != nullptr &&
            videoDevice_ != nullptr && videoContext_ != nullptr)
            return FFFResult::Success;
    }

    ReleaseVideoProcessor();
    videoProcessorInputFormat_ = inputDescription.Format;
    videoProcessorOutputFormat_ = outputDescription.Format;
    videoProcessorInputColorSpace_ = inputColorSpace;
    videoProcessorOutputColorSpace_ = outputColorSpace;
    videoProcessorInputWidth_ = inputDescription.Width;
    videoProcessorInputHeight_ = inputDescription.Height;
    videoProcessorOutputWidth_ = outputDescription.Width;
    videoProcessorOutputHeight_ = outputDescription.Height;
    const auto fail = [this]() noexcept {
        videoProcessorConfigurationFailed_ = true;
        return FFFResult::NotSupported;
    };

    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoContext1> videoContext1;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&videoDevice))) ||
        FAILED(context_->QueryInterface(IID_PPV_ARGS(&videoContext))) ||
        FAILED(context_->QueryInterface(IID_PPV_ARGS(&videoContext1))))
        return fail();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {60, 1};
    content.InputWidth = inputDescription.Width;
    content.InputHeight = inputDescription.Height;
    content.OutputFrameRate = content.InputFrameRate;
    content.OutputWidth = outputDescription.Width;
    content.OutputHeight = outputDescription.Height;
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&content, &enumerator)))
        return fail();

    UINT inputSupport = 0;
    UINT outputSupport = 0;
    if (FAILED(enumerator->CheckVideoProcessorFormat(inputDescription.Format, &inputSupport)) ||
        FAILED(enumerator->CheckVideoProcessorFormat(outputDescription.Format, &outputSupport)) ||
        (inputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
        (outputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0)
        return fail();
    ComPtr<ID3D11VideoProcessorEnumerator1> enumerator1;
    BOOL conversionSupported = FALSE;
    if (FAILED(enumerator.As(&enumerator1)) ||
        FAILED(enumerator1->CheckVideoProcessorFormatConversion(inputDescription.Format,
            static_cast<DXGI_COLOR_SPACE_TYPE>(inputColorSpace), outputDescription.Format,
            static_cast<DXGI_COLOR_SPACE_TYPE>(outputColorSpace), &conversionSupported)) ||
        !conversionSupported)
        return fail();

    ComPtr<ID3D11VideoProcessor> processor;
    if (FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor)))
        return fail();
    videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(), 0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    // Driver auto-processing may silently add sharpening, noise reduction or
    // skin-tone changes. Keep only the explicitly configured scaler and color
    // conversion so VP output remains a reproducible rendering contract.
    videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
    videoContext1->VideoProcessorSetStreamColorSpace1(processor.Get(), 0,
        static_cast<DXGI_COLOR_SPACE_TYPE>(inputColorSpace));
    videoContext1->VideoProcessorSetOutputColorSpace1(processor.Get(),
        static_cast<DXGI_COLOR_SPACE_TYPE>(outputColorSpace));
    D3D11_VIDEO_COLOR background{};
    background.RGBA.A = 1.0f;
    videoContext->VideoProcessorSetOutputBackgroundColor(processor.Get(), FALSE, &background);

    videoDevice_ = videoDevice.Detach();
    videoContext_ = videoContext.Detach();
    videoProcessorEnumerator_ = enumerator.Detach();
    videoProcessor_ = processor.Detach();
    videoProcessorConfigurationFailed_ = false;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureVideoProcessorInputSurface(
    const std::uint32_t format) noexcept {
    if (device_ == nullptr || sourceWidth_ == 0 || sourceHeight_ == 0)
        return FFFResult::InvalidState;
    if (videoProcessorRenderTexture_ != nullptr && videoProcessorRenderTarget_ != nullptr) {
        D3D11_TEXTURE2D_DESC retained{};
        videoProcessorRenderTexture_->GetDesc(&retained);
        if (retained.Width == sourceWidth_ && retained.Height == sourceHeight_ &&
            retained.Format == static_cast<DXGI_FORMAT>(format))
            return FFFResult::Success;
    }

    ReleaseVideoProcessorInputSurface();
    D3D11_TEXTURE2D_DESC description{};
    description.Width = sourceWidth_;
    description.Height = sourceHeight_;
    description.MipLevels = description.ArraySize = 1;
    description.Format = static_cast<DXGI_FORMAT>(format);
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&description, nullptr,
            &videoProcessorRenderTexture_)) ||
        FAILED(device_->CreateRenderTargetView(videoProcessorRenderTexture_, nullptr,
            &videoProcessorRenderTarget_))) {
        ReleaseVideoProcessorInputSurface();
        SetError("Could not create the source-size video conversion surface.");
        return FFFResult::DeviceFailure;
    }

    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::DrawWithShader(ID3D11RenderTargetView* target,
    const float x, const float y, const float width, const float height,
    const std::uint32_t effect, ID3D11ShaderResourceView* const* sourceViews) noexcept {
    if (target == nullptr || context_ == nullptr || width <= 0.0f || height <= 0.0f)
        return FFFResult::InvalidArgument;
    cachedVideoSettings_.colorMode = static_cast<std::uint32_t>(actualMode_);
    cachedVideoSettings_.reserved = effect;
    cachedVideoSettings_.outputWidth = width;
    cachedVideoSettings_.outputHeight = height;
    context_->UpdateSubresource(constants_, 0, nullptr, &cachedVideoSettings_, 0, 0);
    context_->OMSetRenderTargets(1, &target, nullptr);
    const D3D11_VIEWPORT viewport{x, y, width, height, 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &constants_);
    ID3D11SamplerState* samplers[] = {sampler_, pointSampler_};
    context_->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);
    auto* views = sourceViews != nullptr ? sourceViews : sourceViews_;
    context_->PSSetShaderResources(0, ARRAYSIZE(sourceViews_), views);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return FFFResult::Success;
}

void PlayerVideoRenderer::ReleaseScaleResources() noexcept {
    for (auto& chain : planeScaleChains_) {
        for (auto& pass : chain.passes) {
            if (pass.view != nullptr) { pass.view->Release(); pass.view = nullptr; }
            if (pass.target != nullptr) { pass.target->Release(); pass.target = nullptr; }
            if (pass.texture != nullptr) { pass.texture->Release(); pass.texture = nullptr; }
        }
        chain = {};
    }
    scaledVideoGeneration_ = UINT64_MAX;
    scaledOutputWidth_ = scaledOutputHeight_ = 0;
    for (auto& view : scaledSourceViews_) view = nullptr;
}

FFFResult PlayerVideoRenderer::EnsurePlaneScaleChain(const std::size_t plane,
    const std::uint32_t sourceWidth, const std::uint32_t sourceHeight,
    const std::uint32_t targetWidth, const std::uint32_t targetHeight,
    const std::uint32_t format) noexcept {
    if (plane >= ARRAYSIZE(planeScaleChains_) || sourceWidth == 0 || sourceHeight == 0 ||
        targetWidth == 0 || targetHeight == 0 || targetWidth > sourceWidth ||
        targetHeight > sourceHeight)
        return FFFResult::InvalidArgument;
    auto& chain = planeScaleChains_[plane];
    if (chain.sourceWidth == sourceWidth && chain.sourceHeight == sourceHeight &&
        chain.targetWidth == targetWidth && chain.targetHeight == targetHeight &&
        chain.format == format)
        return FFFResult::Success;

    for (auto& pass : chain.passes) {
        if (pass.view != nullptr) pass.view->Release();
        if (pass.target != nullptr) pass.target->Release();
        if (pass.texture != nullptr) pass.texture->Release();
    }
    chain = {};
    chain.sourceWidth = sourceWidth;
    chain.sourceHeight = sourceHeight;
    chain.targetWidth = targetWidth;
    chain.targetHeight = targetHeight;
    chain.format = format;

    const auto addPass = [&](const std::uint32_t width, const std::uint32_t height,
        const std::uint32_t axis) noexcept -> bool {
        ScalePassResource pass{};
        pass.width = width;
        pass.height = height;
        pass.axis = axis;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = description.ArraySize = 1;
        description.Format = static_cast<DXGI_FORMAT>(format);
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&description, nullptr, &pass.texture)) ||
            FAILED(device_->CreateRenderTargetView(pass.texture, nullptr, &pass.target)) ||
            FAILED(device_->CreateShaderResourceView(pass.texture, nullptr, &pass.view))) {
            if (pass.view != nullptr) pass.view->Release();
            if (pass.target != nullptr) pass.target->Release();
            if (pass.texture != nullptr) pass.texture->Release();
            return false;
        }
        chain.passes.push_back(pass);
        return true;
    };

    auto width = sourceWidth;
    auto height = sourceHeight;
    while (width > targetWidth || height > targetHeight) {
        const auto nextWidth = width > targetWidth ?
            std::max(targetWidth, (width + 1) / 2) : width;
        const auto nextHeight = height > targetHeight ?
            std::max(targetHeight, (height + 1) / 2) : height;
        const auto horizontalFirst =
            static_cast<std::uint64_t>(nextWidth) * height <=
            static_cast<std::uint64_t>(width) * nextHeight;
        if (horizontalFirst) {
            if (nextWidth != width && !addPass(nextWidth, height, 0)) {
                ReleaseScaleResources();
                return FFFResult::DeviceFailure;
            }
            width = nextWidth;
            if (nextHeight != height && !addPass(width, nextHeight, 1)) {
                ReleaseScaleResources();
                return FFFResult::DeviceFailure;
            }
            height = nextHeight;
        } else {
            if (nextHeight != height && !addPass(width, nextHeight, 1)) {
                ReleaseScaleResources();
                return FFFResult::DeviceFailure;
            }
            height = nextHeight;
            if (nextWidth != width && !addPass(nextWidth, height, 0)) {
                ReleaseScaleResources();
                return FFFResult::DeviceFailure;
            }
            width = nextWidth;
        }
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::ExecuteScalePass(ID3D11ShaderResourceView* source,
    const std::uint32_t sourceWidth, const std::uint32_t sourceHeight,
    const ScalePassResource& pass, const std::uint32_t filter) noexcept {
    if (source == nullptr || pass.target == nullptr || context_ == nullptr ||
        scalePixelShader_ == nullptr || scaleConstants_ == nullptr)
        return FFFResult::InvalidState;
    const ScaleShaderSettings settings{
        static_cast<float>(sourceWidth), static_cast<float>(sourceHeight),
        static_cast<float>(pass.width), static_cast<float>(pass.height),
        pass.axis, filter,
        0.0f, 0.0f};
    context_->UpdateSubresource(scaleConstants_, 0, nullptr, &settings, 0, 0);
    context_->OMSetRenderTargets(1, &pass.target, nullptr);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(pass.width),
        static_cast<float>(pass.height), 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(scalePixelShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &scaleConstants_);
    context_->PSSetShaderResources(0, 1, &source);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullView = nullptr;
    context_->PSSetShaderResources(0, 1, &nullView);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::PrepareScaledVideo(const std::uint32_t outputWidth,
    const std::uint32_t outputHeight, ID3D11ShaderResourceView** views) noexcept {
    if (views == nullptr || outputWidth == 0 || outputHeight == 0)
        return FFFResult::InvalidArgument;
    const auto generation = videoGeneration_.load(std::memory_order_acquire);
    if (scaledVideoGeneration_ == generation && scaledOutputWidth_ == outputWidth &&
        scaledOutputHeight_ == outputHeight) {
        std::copy(std::begin(scaledSourceViews_), std::end(scaledSourceViews_), views);
        return FFFResult::Success;
    }

    const auto planeCount = sourceInputLayout_ == 1 ? 3u :
        (sourceInputLayout_ == 2 ? 2u : 1u);
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceViews_); ++plane) {
        if (plane >= planeCount || sourceViews_[plane] == nullptr) {
            scaledSourceViews_[plane] = nullptr;
            continue;
        }
        const auto planeWidth = plane == 0 ? sourceWidth_ :
            (sourceWidth_ + (1u << sourceChromaWidthShift_) - 1) >> sourceChromaWidthShift_;
        const auto planeHeight = plane == 0 ? sourceHeight_ :
            (sourceHeight_ + (1u << sourceChromaHeightShift_) - 1) >> sourceChromaHeightShift_;
        const auto targetWidth = std::min(planeWidth, outputWidth);
        const auto targetHeight = std::min(planeHeight, outputHeight);
        const auto format = sourceInputLayout_ == 0 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
            (sourceInputLayout_ == 2 && plane == 1 ? DXGI_FORMAT_R16G16_FLOAT :
                DXGI_FORMAT_R16_FLOAT);
        const auto ensure = EnsurePlaneScaleChain(plane, planeWidth, planeHeight,
            targetWidth, targetHeight, static_cast<std::uint32_t>(format));
        if (ensure != FFFResult::Success) return ensure;

        // Select the reconstruction filter once for the whole chain. The
        // decision uses the overall downsample ratio (display area / source)
        // rather than any single pass's instantaneous ratio, which the
        // halving chain keeps near 0.5. Heavy downscales use an area-average
        // (box) filter for anti-aliasing; otherwise honour the user's
        // scaling-quality preference (Lanczos-3 for high quality, bicubic
        // for balanced).
        const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(planeWidth);
        const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(planeHeight);
        const std::uint32_t filter = std::min(scaleX, scaleY) < 0.25f ? 2u :
            (scalingQuality_ == FFF3FPVideoScalingQuality::HighQuality ? 1u : 0u);

        auto* currentView = sourceViews_[plane];
        auto currentWidth = planeWidth;
        auto currentHeight = planeHeight;
        for (const auto& pass : planeScaleChains_[plane].passes) {
            const auto execute = ExecuteScalePass(currentView, currentWidth, currentHeight,
                pass, filter);
            if (execute != FFFResult::Success) return execute;
            currentView = pass.view;
            currentWidth = pass.width;
            currentHeight = pass.height;
        }
        scaledSourceViews_[plane] = currentView;
    }
    scaledVideoGeneration_ = generation;
    scaledOutputWidth_ = outputWidth;
    scaledOutputHeight_ = outputHeight;
    std::copy(std::begin(scaledSourceViews_), std::end(scaledSourceViews_), views);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::RenderVideoProcessorInput() noexcept {
    if (videoProcessorRenderTexture_ == nullptr || videoProcessorRenderTarget_ == nullptr ||
        context_ == nullptr)
        return FFFResult::InvalidState;
    return DrawWithShader(videoProcessorRenderTarget_, 0.0f, 0.0f,
        static_cast<float>(sourceWidth_), static_cast<float>(sourceHeight_));
}

FFFResult PlayerVideoRenderer::DrawWithVideoProcessor(ID3D11Texture2D* inputTexture,
    ID3D11Texture2D* outputTexture, const RECT& destination,
    const std::uint32_t inputColorSpace, const std::uint32_t outputColorSpace) noexcept {
    const auto ensure = EnsureVideoProcessor(inputTexture, outputTexture,
        inputColorSpace, outputColorSpace);
    if (ensure != FFFResult::Success) return ensure;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDescription{};
    inputDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDescription.Texture2D.MipSlice = 0;
    inputDescription.Texture2D.ArraySlice = 0;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    if (FAILED(videoDevice_->CreateVideoProcessorInputView(inputTexture,
        videoProcessorEnumerator_, &inputDescription, &inputView))) {
        videoProcessorConfigurationFailed_ = true;
        return FFFResult::NotSupported;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDescription{};
    outputDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDescription.Texture2D.MipSlice = 0;
    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    if (FAILED(videoDevice_->CreateVideoProcessorOutputView(outputTexture,
        videoProcessorEnumerator_, &outputDescription, &outputView))) {
        videoProcessorConfigurationFailed_ = true;
        return FFFResult::NotSupported;
    }

    D3D11_TEXTURE2D_DESC inputTextureDescription{};
    inputTexture->GetDesc(&inputTextureDescription);
    const RECT source{0, 0, static_cast<LONG>(inputTextureDescription.Width),
        static_cast<LONG>(inputTextureDescription.Height)};
    videoContext_->VideoProcessorSetStreamSourceRect(videoProcessor_, 0, TRUE, &source);
    videoContext_->VideoProcessorSetStreamDestRect(videoProcessor_, 0, TRUE, &destination);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.pInputSurface = inputView.Get();
    if (FAILED(videoContext_->VideoProcessorBlt(
        videoProcessor_, outputView.Get(), 0, 1, &stream))) {
        videoProcessorConfigurationFailed_ = true;
        return FFFResult::NotSupported;
    }
    return FFFResult::Success;
}

void PlayerVideoRenderer::ReleaseVideoProcessor() noexcept {
    if (videoProcessor_ != nullptr) { videoProcessor_->Release(); videoProcessor_ = nullptr; }
    if (videoProcessorEnumerator_ != nullptr) {
        videoProcessorEnumerator_->Release(); videoProcessorEnumerator_ = nullptr;
    }
    if (videoContext_ != nullptr) { videoContext_->Release(); videoContext_ = nullptr; }
    if (videoDevice_ != nullptr) { videoDevice_->Release(); videoDevice_ = nullptr; }
    videoProcessorInputFormat_ = videoProcessorOutputFormat_ = DXGI_FORMAT_UNKNOWN;
    videoProcessorInputColorSpace_ = videoProcessorOutputColorSpace_ = DXGI_COLOR_SPACE_CUSTOM;
    videoProcessorInputWidth_ = videoProcessorInputHeight_ = 0;
    videoProcessorOutputWidth_ = videoProcessorOutputHeight_ = 0;
    videoProcessorConfigurationFailed_ = false;
}

void PlayerVideoRenderer::ReleaseVideoProcessorInputSurface() noexcept {
    if (videoProcessorRenderTarget_ != nullptr) {
        videoProcessorRenderTarget_->Release();
        videoProcessorRenderTarget_ = nullptr;
    }
    if (videoProcessorRenderTexture_ != nullptr) {
        videoProcessorRenderTexture_->Release();
        videoProcessorRenderTexture_ = nullptr;
    }
}

FFFResult PlayerVideoRenderer::SetTimedTextLayer(TimedTextRenderLayer layer,
    const TimedTextLayerSlot slot) noexcept {
    try {
        const auto slotIndex = static_cast<std::size_t>(slot);
        if (slotIndex >= ARRAYSIZE(timedTextLayers_)) return FFFResult::InvalidArgument;
        auto retained = std::make_shared<TimedTextRenderLayer>(std::move(layer));
        if (slot == TimedTextLayerSlot::Lyrics) {
            lyricsLayoutEnabled_.store(!retained->commands.empty(), std::memory_order_release);
            bool blurSettingsChanged = false;
            const auto publishBlurSetting = [&blurSettingsChanged](auto& destination,
                const auto value) noexcept {
                if (destination.exchange(value, std::memory_order_acq_rel) != value)
                    blurSettingsChanged = true;
            };
            publishBlurSetting(coverBackdropBlurRadiusBits_,
                std::bit_cast<std::uint32_t>(retained->coverBackdropBlurRadius));
            publishBlurSetting(coverBackdropBlurPasses_, retained->coverBackdropBlurPasses);
            publishBlurSetting(coverBackdropDownsampleFactor_, retained->coverBackdropDownsampleFactor);
            coverBackdropTintArgb_.store(retained->coverBackdropTintArgb,
                std::memory_order_release);
            coverRegionWidthPercentageBits_.store(
                std::bit_cast<std::uint32_t>(retained->coverRegionWidthPercentage),
                std::memory_order_release);
            lyricsRegionWidthPercentageBits_.store(
                std::bit_cast<std::uint32_t>(retained->lyricsRegionWidthPercentage),
                std::memory_order_release);
            coverLeftPaddingPercentageBits_.store(
                std::bit_cast<std::uint32_t>(retained->coverLeftPaddingPercentage),
                std::memory_order_release);
            coverRightPaddingPercentageBits_.store(
                std::bit_cast<std::uint32_t>(retained->coverRightPaddingPercentage),
                std::memory_order_release);
            coverVerticalPaddingPercentageBits_.store(
                std::bit_cast<std::uint32_t>(retained->coverVerticalPaddingPercentage),
                std::memory_order_release);
            if (blurSettingsChanged)
                coverBackdropBlurSettingsGeneration_.fetch_add(1, std::memory_order_acq_rel);
            if (blurSettingsChanged) RequestCoverBackdropRender(true);
        }
        {
            std::lock_guard lock(timedTextMutex_);
            if (retained->sequence == 0)
                retained->sequence = timedTextLayers_[slotIndex]
                    ? timedTextLayers_[slotIndex]->sequence + 1 : 1;
            timedTextLayers_[slotIndex] = std::move(retained);
            presentationFrameRate_ = 1.0f;
            bool hasVisibleLayer = false;
            for (const auto& item : timedTextLayers_) {
                if (item != nullptr && !item->commands.empty()) {
                    hasVisibleLayer = true;
                    presentationFrameRate_ = std::max(presentationFrameRate_,
                        std::clamp(item->targetFrameRate, 1.0f, 240.0f));
                }
            }
            if (hasVisibleLayer) {
                timedTextThreadRunning_ = true;
                if (!timedTextThread_.joinable()) {
                    timedTextThreadStop_ = false;
                    timedTextThread_ = std::thread(&PlayerVideoRenderer::TimedTextThread, this);
                }
            }
            ++presentationGeneration_;
        }
        // Submission is intentionally publish-and-wake only. The UI timer must
        // never wait for 4K conversion, the D3D immediate-context lock or DXGI.
        timedTextCondition_.notify_one();
        return FFFResult::Success;
    } catch (...) {
        SetError("Could not retain the timed-text command layer.");
        return FFFResult::NativeFailure;
    }
}

void PlayerVideoRenderer::TimedTextThread() noexcept {
    std::uint64_t observedPresentationGeneration = 0;
    std::uint64_t observedVideoGeneration = 0;
    auto nextPresentation = std::chrono::steady_clock::time_point::min();
    for (;;) {
        float frameRate = 60.0f;
        bool videoChanged = false;
        bool devicePollOnly = false;
        {
            std::unique_lock lock(timedTextMutex_);
            const auto signaled = timedTextCondition_.wait_for(lock,
                std::chrono::milliseconds(500), [this, &observedPresentationGeneration,
                    &observedVideoGeneration] {
                return timedTextThreadStop_ ||
                    presentationGeneration_ != observedPresentationGeneration ||
                    (timedTextThreadRunning_ &&
                        videoGeneration_.load() != observedVideoGeneration);
            });
            if (timedTextThreadStop_) return;
            if (!signaled) {
                devicePollOnly = true;
            }
            if (!timedTextThreadRunning_) {
                observedPresentationGeneration = presentationGeneration_;
                observedVideoGeneration = videoGeneration_.load();
                continue;
            }
            if (!devicePollOnly) {
                videoChanged = videoGeneration_.load() != observedVideoGeneration;
                // A new decoded frame is never held behind the overlay cadence. Static
                // subtitle/danmaku updates are still coalesced to their requested rate.
                if (const auto now = std::chrono::steady_clock::now();
                    !videoChanged &&
                    nextPresentation != std::chrono::steady_clock::time_point::min() &&
                    now < nextPresentation) {
                    timedTextCondition_.wait_until(lock, nextPresentation,
                        [this, &observedVideoGeneration] {
                            return timedTextThreadStop_ ||
                                videoGeneration_.load() != observedVideoGeneration;
                        });
                    if (timedTextThreadStop_) return;
                }
                observedPresentationGeneration = presentationGeneration_;
                observedVideoGeneration = videoGeneration_.load();
                frameRate = presentationFrameRate_;
            }
        }
        if (devicePollOnly) {
            RequestRecoveryIfDeviceLost();
            continue;
        }
        const auto presentationStart = std::chrono::steady_clock::now();
        const auto result = PresentTimedText();
        if (result != FFFResult::Success) {
            if (result == FFFResult::DeviceFailure && RequestRecoveryIfDeviceLost()) continue;
            SetError("The independent timed-text presenter could not compose the latest layer.");
        }
        nextPresentation = presentationStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / std::clamp(static_cast<double>(frameRate), 1.0, 240.0)));
    }
}

void PlayerVideoRenderer::StopTimedTextThread() noexcept {
    {
        std::lock_guard lock(timedTextMutex_);
        timedTextThreadStop_ = true;
    }
    timedTextCondition_.notify_all();
    if (timedTextThread_.joinable()) timedTextThread_.join();
    std::lock_guard lock(timedTextMutex_);
    timedTextThreadRunning_ = false;
}

FFFResult PlayerVideoRenderer::GetTimedTextStatus(FFF3FPTimedTextStatus& status,
    const TimedTextLayerSlot slot) noexcept {
    const auto slotIndex = static_cast<std::size_t>(slot);
    if (slotIndex >= ARRAYSIZE(timedTextLayers_)) return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    if (status.size < sizeof(FFF3FPTimedTextStatus) || status.version != 1)
        return FFFResult::InvalidArgument;
    {
        std::lock_guard lock(timedTextMutex_);
        status.size = sizeof(status); status.version = 1;
        status.submittedSequence = timedTextLayers_[slotIndex] ? timedTextLayers_[slotIndex]->sequence : 0;
        status.renderedSequence = timedTextRenderedSequences_[slotIndex];
        status.commandCount = timedTextRenderedCommandCounts_[slotIndex];
        status.canvasWidth = timedTextWidths_[slotIndex]; status.canvasHeight = timedTextHeights_[slotIndex];
        status.reserved = timedTextPresentCounts_[slotIndex]; status.visiblePixelCount = 0;
        status.spriteCacheHits = timedTextSpriteCacheHits_;
        status.spriteCacheMisses = timedTextSpriteCacheMisses_;
        status.backBufferAcquisitionCount = backBufferAcquisitionCount_;
        status.compositePixelShaderInvocations =
            timedTextCompositePixelInvocations_[slotIndex];
    }
    if (status.submittedSequence != status.renderedSequence || status.commandCount == 0 ||
        timedTextTextures_[slotIndex] == nullptr || device_ == nullptr || context_ == nullptr)
        return FFFResult::Success;
    if (timedTextPipelineQueries_[slotIndex] == nullptr) {
        D3D11_QUERY_DESC query{}; query.Query = D3D11_QUERY_PIPELINE_STATISTICS;
        // Pipeline statistics are diagnostic-only. Do not allocate or poll them
        // during ordinary playback unless a caller explicitly requests status.
        device_->CreateQuery(&query, &timedTextPipelineQueries_[slotIndex]);
    }
    D3D11_TEXTURE2D_DESC description{};
    timedTextTextures_[slotIndex]->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &staging))) return FFFResult::DeviceFailure;
    context_->CopyResource(staging.Get(), timedTextTextures_[slotIndex]);
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

FFFResult PlayerVideoRenderer::EnsureD2DContext() noexcept {
    if (device_ == nullptr) return FFFResult::InvalidState;
    // The deferred cover backdrop worker and timed-text presenter share the
    // device context under deviceMutex_. Use a multithreaded factory so the
    // resource remains valid when ownership moves between those threads.
    if (d2dFactory_ == nullptr && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
        IID_PPV_ARGS(&d2dFactory_)))) {
        SetError("Could not create the Direct2D rendering factory.");
        return FFFResult::DeviceFailure;
    }
    if (d2dContext_ == nullptr) {
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
            FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_)) ||
            FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                &d2dContext_))) {
            SetError("Could not bind Direct2D to the D3D11 playback device.");
            return FFFResult::DeviceFailure;
        }
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureTimedTextResources(const TimedTextLayerSlot slot) noexcept {
    const auto slotIndex = static_cast<std::size_t>(slot);
    if (slotIndex >= ARRAYSIZE(timedTextTextures_)) return FFFResult::InvalidArgument;
    if (swapWidth_ == 0 || swapHeight_ == 0) return FFFResult::Success;
    const bool hdrLinear = actualMode_ == FFF3FPColorMode::MapToHdr;
    if (timedTextTextures_[slotIndex] != nullptr &&
        timedTextWidths_[slotIndex] == swapWidth_ && timedTextHeights_[slotIndex] == swapHeight_ &&
        timedTextResourcesHdr_ == hdrLinear)
        return FFFResult::Success;
    ReleaseTimedTextSlotResources(slot);
    const auto d2dResult = EnsureD2DContext();
    if (d2dResult != FFFResult::Success) return d2dResult;
    if (writeFactory_ == nullptr && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory_)))) {
        SetError("Could not create the DirectWrite timed-text factory."); return FFFResult::DeviceFailure;
    }
    if (timedTextRenderingParams_ == nullptr) {
        ComPtr<IDWriteRenderingParams> defaults;
        if (FAILED(writeFactory_->CreateRenderingParams(&defaults)) ||
            FAILED(writeFactory_->CreateCustomRenderingParams(defaults->GetGamma(),
                defaults->GetEnhancedContrast(), 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &timedTextRenderingParams_))) {
            SetError("Could not create high-quality timed-text rendering parameters.");
            return FFFResult::DeviceFailure;
        }
    }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = swapWidth_; texture.Height = swapHeight_;
    texture.MipLevels = texture.ArraySize = 1;
    texture.Format = hdrLinear ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    texture.SampleDesc.Count = 1; texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &timedTextTextures_[slotIndex])) ||
        FAILED(device_->CreateRenderTargetView(timedTextTextures_[slotIndex], nullptr,
            &timedTextTargets_[slotIndex])) ||
        FAILED(device_->CreateShaderResourceView(timedTextTextures_[slotIndex], nullptr,
            &timedTextViews_[slotIndex]))) {
        ReleaseTimedTextSlotResources(slot);
        SetError("Could not create the requested subtitle/danmaku surface.");
        return FFFResult::DeviceFailure;
    }
    ComPtr<IDXGISurface> surface;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(texture.Format, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(timedTextTextures_[slotIndex]->QueryInterface(IID_PPV_ARGS(&surface))) ||
        FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
            &d2dTargets_[slotIndex]))) {
        ReleaseTimedTextSlotResources(slot);
        SetError("Could not bind the requested subtitle/danmaku surface to Direct2D.");
        return FFFResult::DeviceFailure;
    }
    if (timedTextSpriteVertexShader_ == nullptr || timedTextSpritePixelShader_ == nullptr) {
        ComPtr<ID3DBlob> spriteVertexCode, spritePixelCode, shaderErrors;
        if (FAILED(D3DCompile(TimedTextSpriteVertexShaderSource,
            std::strlen(TimedTextSpriteVertexShaderSource), nullptr, nullptr, nullptr,
            "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &spriteVertexCode, &shaderErrors)) ||
            FAILED(D3DCompile(TimedTextSpritePixelShaderSource,
            std::strlen(TimedTextSpritePixelShaderSource), nullptr, nullptr, nullptr,
            "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &spritePixelCode, &shaderErrors)) ||
            FAILED(device_->CreateVertexShader(spriteVertexCode->GetBufferPointer(),
            spriteVertexCode->GetBufferSize(), nullptr, &timedTextSpriteVertexShader_)) ||
            FAILED(device_->CreatePixelShader(spritePixelCode->GetBufferPointer(),
            spritePixelCode->GetBufferSize(), nullptr, &timedTextSpritePixelShader_))) {
            SetError(shaderErrors ? static_cast<const char*>(shaderErrors->GetBufferPointer()) :
                "Could not create the timed-text sprite shaders.");
            return FFFResult::DeviceFailure;
        }
    }
    if (timedTextBlend_ == nullptr) {
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
    }
    const auto atlasResult = EnsureTimedTextAtlas(InitialTimedTextAtlasSize);
    if (atlasResult != FFFResult::Success) return atlasResult;
    timedTextResourcesHdr_ = hdrLinear;
    timedTextWidths_[slotIndex] = swapWidth_;
    timedTextHeights_[slotIndex] = swapHeight_;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureTimedTextAtlas(const std::uint32_t requestedSize) noexcept {
    const auto size = std::clamp(requestedSize, InitialTimedTextAtlasSize,
        MaximumTimedTextAtlasSize);
    const bool hdrLinear = actualMode_ == FFF3FPColorMode::MapToHdr;
    if (timedTextAtlasTexture_ != nullptr && timedTextAtlasSize_ >= size &&
        timedTextAtlasHdr_ == hdrLinear)
        return FFFResult::Success;
    if (d2dContext_ == nullptr || device_ == nullptr) return FFFResult::InvalidState;
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    if (timedTextShadowBlurEffect_ != nullptr) {
        timedTextShadowBlurEffect_->SetInput(0, nullptr);
        timedTextShadowBlurEffect_->Release(); timedTextShadowBlurEffect_ = nullptr;
    }
    if (d2dTimedTextShadowTarget_ != nullptr) {
        d2dTimedTextShadowTarget_->Release(); d2dTimedTextShadowTarget_ = nullptr;
    }
    if (d2dAtlasTarget_ != nullptr) { d2dAtlasTarget_->Release(); d2dAtlasTarget_ = nullptr; }
    if (timedTextAtlasView_ != nullptr) { timedTextAtlasView_->Release(); timedTextAtlasView_ = nullptr; }
    if (timedTextAtlasTexture_ != nullptr) { timedTextAtlasTexture_->Release(); timedTextAtlasTexture_ = nullptr; }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = size; texture.MipLevels = texture.ArraySize = 1;
    texture.Format = hdrLinear ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &timedTextAtlasTexture_)) ||
        FAILED(device_->CreateShaderResourceView(timedTextAtlasTexture_, nullptr, &timedTextAtlasView_))) {
        SetError("Could not create the GPU timed-text sprite atlas.");
        return FFFResult::DeviceFailure;
    }
    ComPtr<IDXGISurface> surface;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(texture.Format, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    const auto shadowProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(texture.Format, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (FAILED(timedTextAtlasTexture_->QueryInterface(IID_PPV_ARGS(&surface))) ||
        FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &d2dAtlasTarget_)) ||
        FAILED(d2dContext_->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0,
            &shadowProperties, &d2dTimedTextShadowTarget_)) ||
        FAILED(d2dContext_->CreateEffect(CLSID_D2D1GaussianBlur,
            &timedTextShadowBlurEffect_))) {
        SetError("Could not expose the GPU timed-text atlas to Direct2D.");
        return FFFResult::DeviceFailure;
    }
    timedTextShadowBlurEffect_->SetInput(0, d2dTimedTextShadowTarget_);
    timedTextShadowBlurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
        D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
    timedTextShadowBlurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
        D2D1_BORDER_MODE_SOFT);
    d2dContext_->SetTarget(d2dAtlasTarget_); d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0, 0));
    const auto end = d2dContext_->EndDraw(); d2dContext_->SetTarget(nullptr);
    if (FAILED(end)) {
        if (end == D2DERR_RECREATE_TARGET)
            RequestDeviceRecovery(end, "Direct2D target recreation");
        return FFFResult::DeviceFailure;
    }
    timedTextAtlasSize_ = size;
    timedTextAtlasHdr_ = hdrLinear;
    timedTextAtlasX_ = timedTextAtlasY_ = timedTextAtlasRowHeight_ = 0;
    timedTextSprites_.clear();
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureTimedTextInstanceCapacity(const std::size_t count) noexcept {
    if (count == 0 || count <= timedTextSpriteInstanceCapacity_) return FFFResult::Success;
    const auto requested = static_cast<std::uint32_t>(std::min<std::size_t>(count, 4096));
    auto capacity = std::max<std::uint32_t>(64, timedTextSpriteInstanceCapacity_);
    while (capacity < requested) capacity = std::min<std::uint32_t>(capacity * 2, 4096);
    if (timedTextSpriteInstanceView_ != nullptr) { timedTextSpriteInstanceView_->Release(); timedTextSpriteInstanceView_ = nullptr; }
    if (timedTextSpriteInstanceBuffer_ != nullptr) { timedTextSpriteInstanceBuffer_->Release(); timedTextSpriteInstanceBuffer_ = nullptr; }
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(TimedTextSpriteInstance) * capacity;
    buffer.Usage = D3D11_USAGE_DYNAMIC; buffer.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    buffer.StructureByteStride = sizeof(TimedTextSpriteInstance);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN; view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.NumElements = capacity;
    if (FAILED(device_->CreateBuffer(&buffer, nullptr, &timedTextSpriteInstanceBuffer_)) ||
        FAILED(device_->CreateShaderResourceView(timedTextSpriteInstanceBuffer_, &view,
            &timedTextSpriteInstanceView_))) {
        SetError("Could not create the timed-text sprite instance buffer.");
        return FFFResult::DeviceFailure;
    }
    timedTextSpriteInstanceCapacity_ = capacity;
    timedTextSpriteInstances_.reserve(capacity);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::DrawTimedText(const TimedTextLayerSlot slot) noexcept {
    const auto slotIndex = static_cast<std::size_t>(slot);
    if (slotIndex >= ARRAYSIZE(timedTextLayers_)) return FFFResult::InvalidArgument;
    std::shared_ptr<const TimedTextRenderLayer> layer;
    {
        std::lock_guard lock(timedTextMutex_);
        if (!timedTextLayers_[slotIndex] ||
            timedTextLayers_[slotIndex]->sequence == timedTextRenderedSequences_[slotIndex])
            return FFFResult::Success;
        layer = timedTextLayers_[slotIndex];
    }
    if (layer->commands.empty()) {
        std::lock_guard lock(timedTextMutex_);
        timedTextRenderedSequences_[slotIndex] = layer->sequence;
        timedTextRenderedCommandCounts_[slotIndex] = 0;
        timedTextRenderedHdrHighlights_[slotIndex] = false;
        timedTextWidths_[slotIndex] = swapWidth_; timedTextHeights_[slotIndex] = swapHeight_;
        ReleaseTimedTextSlotResources(slot);
        return FFFResult::Success;
    }
    const auto resourceResult = EnsureTimedTextResources(slot);
    if (resourceResult != FFFResult::Success) return resourceResult;
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2dContext_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    d2dContext_->SetTextRenderingParams(timedTextRenderingParams_);
    const bool hdrLinear = actualMode_ == FFF3FPColorMode::MapToHdr;
    const auto scaleX = layer->canvasWidth == 0 ? 1.0f : static_cast<float>(swapWidth_) / layer->canvasWidth;
    const auto scaleY = layer->canvasHeight == 0 ? 1.0f : static_cast<float>(swapHeight_) / layer->canvasHeight;
    if (timedTextBrushes_.size() >= MaximumTimedTextBrushes) {
        for (auto& [color, brush] : timedTextBrushes_)
            if (brush != nullptr) brush->Release();
        timedTextBrushes_.clear();
    }
    const auto getBrush = [this, hdrLinear](const std::uint32_t argb) noexcept -> ID2D1SolidColorBrush* {
        const auto existing = timedTextBrushes_.find(argb);
        if (existing != timedTextBrushes_.end()) return existing->second;
        ID2D1SolidColorBrush* brush = nullptr;
        if (FAILED(d2dContext_->CreateSolidColorBrush(ToD2dColor(argb, hdrLinear), &brush))) return nullptr;
        timedTextBrushes_.emplace(argb, brush);
        return brush;
    };
    const auto getLayout = [this](const TimedTextRenderCommand& command,
        const D2D1_RECT_F& destination, const float fontSize) noexcept -> IDWriteTextLayout* {
        const auto layoutKey = TimedTextLayoutKey(command, destination, fontSize);
        const auto existing = timedTextLayouts_.find(layoutKey);
        if (existing != timedTextLayouts_.end()) return existing->second;
        ComPtr<IDWriteTextLayout> layout;
        if (!command.content || FAILED(CreateTimedTextLayout(writeFactory_, command.content->text,
            command.content->fontFamily, fontSize, command.flags, command.horizontalAlignment,
            command.verticalAlignment,
            std::max(destination.right - destination.left, 1.0f),
            std::max(destination.bottom - destination.top, 1.0f), &layout))) return nullptr;
        constexpr std::size_t MaximumCachedLayouts = 512;
        while (timedTextLayoutOrder_.size() >= MaximumCachedLayouts) {
            const auto oldest = timedTextLayoutOrder_.front();
            timedTextLayoutOrder_.pop_front();
            const auto entry = timedTextLayouts_.find(oldest);
            if (entry != timedTextLayouts_.end()) {
                entry->second->Release();
                timedTextLayouts_.erase(entry);
            }
        }
        auto* retained = layout.Detach();
        timedTextLayouts_.emplace(layoutKey, retained);
        timedTextLayoutOrder_.push_back(layoutKey);
        return retained;
    };
    const auto usesSoftShadow = [](const TimedTextRenderCommand& command) noexcept {
        return (static_cast<std::uint32_t>(command.flags) &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::SoftShadow)) != 0;
    };
    const auto softShadowDepth = [](const float shadowX, const float shadowY) noexcept {
        return std::max(std::abs(shadowX), std::abs(shadowY));
    };
    const auto drawLayout = [&getBrush, this](const TimedTextRenderCommand& command,
        IDWriteTextLayout* layout, const D2D1_POINT_2F origin, const float outline,
        const float shadowX, const float shadowY) noexcept {
        if (layout == nullptr) return;
        const auto softShadow = (static_cast<std::uint32_t>(command.flags) &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::SoftShadow)) != 0;
        auto* outlineBrush = outline > 0.0f && (command.outlineArgb >> 24) != 0
            ? getBrush(command.outlineArgb) : nullptr;
        auto* shadowBrush = !softShadow && (command.shadowArgb >> 24) != 0
            ? getBrush(command.shadowArgb) : nullptr;
        if (outlineBrush != nullptr || shadowBrush != nullptr) {
            auto* effects = new (std::nothrow) TimedTextEffectRenderer(d2dFactory_, d2dContext_,
                outlineBrush, shadowBrush, outline, shadowX, shadowY);
            if (effects != nullptr) {
                layout->Draw(nullptr, effects, origin.x, origin.y);
                effects->Release();
            }
        }
        if ((command.foregroundArgb >> 24) != 0) {
            if (auto* foreground = getBrush(command.foregroundArgb); foreground != nullptr)
                d2dContext_->DrawTextLayout(origin, layout, foreground,
                    D2D1_DRAW_TEXT_OPTIONS_NO_SNAP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
    };
    const auto drawSoftShadowMask = [&getBrush, this](const TimedTextRenderCommand& command,
        IDWriteTextLayout* layout, const D2D1_POINT_2F origin, const float outline) noexcept {
        if (layout == nullptr || (command.shadowArgb >> 24) == 0) return;
        auto* shadowBrush = getBrush(command.shadowArgb);
        if (shadowBrush == nullptr) return;
        auto* effects = new (std::nothrow) TimedTextEffectRenderer(d2dFactory_, d2dContext_,
            nullptr, shadowBrush, outline, 0.0f, 0.0f);
        if (effects != nullptr) {
            layout->Draw(nullptr, effects, origin.x, origin.y);
            effects->Release();
        }
    };

    auto& pendingSprites = timedTextPendingSprites_;
    pendingSprites.reserve(layer->commands.size());
    const auto clearAtlas = [this]() noexcept -> bool {
        timedTextSprites_.clear();
        timedTextAtlasX_ = timedTextAtlasY_ = timedTextAtlasRowHeight_ = 0;
        d2dContext_->SetTarget(d2dAtlasTarget_);
        d2dContext_->BeginDraw();
        d2dContext_->Clear(D2D1::ColorF(0, 0));
        const auto result = d2dContext_->EndDraw();
        d2dContext_->SetTarget(nullptr);
        return SUCCEEDED(result);
    };
    const auto buildPendingSprites = [&](const bool stopWhenFull) noexcept -> bool {
        pendingSprites.clear();
        for (std::size_t commandIndex = 0; commandIndex < layer->commands.size(); ++commandIndex) {
            const auto& command = layer->commands[commandIndex];
            if (command.type != FFF3FPTimedTextCommandType::Text || command.contentId == 0 ||
                command.horizontalAlignment != FFF3FPTimedTextAlignment::Near ||
                command.verticalAlignment != FFF3FPTimedTextAlignment::Near) continue;
            const auto destination = D2D1::RectF(command.x * scaleX, command.y * scaleY,
                (command.x + command.width) * scaleX, (command.y + command.height) * scaleY);
            const auto fontSize = std::max(command.fontSize * scaleY, 1.0f);
            const auto outline = std::max(command.outlineWidth * (scaleX + scaleY) * 0.5f, 0.0f);
            const auto shadowX = command.shadowOffsetX * scaleX;
            const auto shadowY = command.shadowOffsetY * scaleY;
            const auto key = TimedTextSpriteKey(command, destination, fontSize, outline,
                shadowX, shadowY);
            if (timedTextSprites_.contains(key)) continue;
            if (timedTextSprites_.size() + pendingSprites.size() >= MaximumTimedTextSprites)
                return !stopWhenFull;
            auto* layout = getLayout(command, destination, fontSize);
            if (layout == nullptr) continue;
            const auto extents = DescribeTimedTextEffects(outline, shadowX, shadowY,
                (command.shadowArgb >> 24) != 0, usesSoftShadow(command));
            const auto padding = static_cast<float>(std::ceil(std::max(
                {extents.left, extents.top, extents.right, extents.bottom})) + 2.0f);
            const auto width = std::max(1u, static_cast<std::uint32_t>(std::ceil(
                destination.right - destination.left + padding * 2.0f)));
            const auto height = std::max(1u, static_cast<std::uint32_t>(std::ceil(
                destination.bottom - destination.top + padding * 2.0f)));
            if (width > timedTextAtlasSize_ || height > timedTextAtlasSize_) continue;
            if (timedTextAtlasX_ + width > timedTextAtlasSize_) {
                timedTextAtlasX_ = 0;
                timedTextAtlasY_ += timedTextAtlasRowHeight_;
                timedTextAtlasRowHeight_ = 0;
            }
            if (timedTextAtlasY_ + height > timedTextAtlasSize_)
                return !stopWhenFull;
            TimedTextSprite sprite{static_cast<float>(timedTextAtlasX_),
                static_cast<float>(timedTextAtlasY_), padding,
                static_cast<float>(width), static_cast<float>(height)};
            timedTextAtlasX_ += width;
            timedTextAtlasRowHeight_ = std::max(timedTextAtlasRowHeight_, height);
            pendingSprites.push_back(PendingTimedTextSprite{commandIndex, layout, key, sprite, outline,
                shadowX, shadowY});
        }
        return true;
    };

    // Rasterize new strings into one GPU atlas. If the bounded atlas fills, it
    // is rebuilt from the currently visible set; old off-screen content never
    // forces unbounded GPU memory growth.
    auto pendingFit = buildPendingSprites(true);
    while (!pendingFit && timedTextAtlasSize_ < MaximumTimedTextAtlasSize) {
        const auto growResult = EnsureTimedTextAtlas(std::min(
            timedTextAtlasSize_ * 2, MaximumTimedTextAtlasSize));
        if (growResult != FFFResult::Success) return growResult;
        pendingFit = buildPendingSprites(true);
    }
    if (!pendingFit) {
        if (!clearAtlas()) {
            SetError("Direct2D could not reset the timed-text sprite atlas.");
            return FFFResult::DeviceFailure;
        }
        buildPendingSprites(false);
    }
    if (!pendingSprites.empty()) {
        std::vector<std::uint32_t> softShadowDepths;
        for (const auto& pending : pendingSprites) {
            const auto& command = layer->commands[pending.commandIndex];
            if (!usesSoftShadow(command) || (command.shadowArgb >> 24) == 0) continue;
            const auto depth = softShadowDepth(pending.shadowX, pending.shadowY);
            if (depth <= 0.0f) continue;
            const auto bits = std::bit_cast<std::uint32_t>(depth);
            if (std::find(softShadowDepths.begin(), softShadowDepths.end(), bits) ==
                softShadowDepths.end()) softShadowDepths.push_back(bits);
        }
        for (const auto depthBits : softShadowDepths) {
            const auto depth = std::bit_cast<float>(depthBits);
            d2dContext_->SetTarget(d2dTimedTextShadowTarget_);
            d2dContext_->BeginDraw();
            d2dContext_->Clear(D2D1::ColorF(0, 0));
            for (const auto& pending : pendingSprites) {
                const auto& command = layer->commands[pending.commandIndex];
                if (!usesSoftShadow(command) || (command.shadowArgb >> 24) == 0 ||
                    std::bit_cast<std::uint32_t>(softShadowDepth(
                        pending.shadowX, pending.shadowY)) != depthBits) continue;
                const auto& sprite = pending.sprite;
                const auto clip = D2D1::RectF(sprite.atlasX, sprite.atlasY,
                    sprite.atlasX + sprite.width, sprite.atlasY + sprite.height);
                d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
                drawSoftShadowMask(command, pending.layout,
                    D2D1::Point2F(sprite.atlasX + sprite.padding,
                        sprite.atlasY + sprite.padding), pending.outline);
                d2dContext_->PopAxisAlignedClip();
            }
            const auto maskEnd = d2dContext_->EndDraw();
            d2dContext_->SetTarget(nullptr);
            if (FAILED(maskEnd)) {
                SetError("Direct2D could not rasterize the timed-text soft-shadow mask.");
                return FFFResult::DeviceFailure;
            }
            timedTextShadowBlurEffect_->SetValue(
                D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, depth);
            d2dContext_->SetTarget(d2dAtlasTarget_);
            d2dContext_->BeginDraw();
            d2dContext_->DrawImage(timedTextShadowBlurEffect_);
            const auto blurEnd = d2dContext_->EndDraw();
            d2dContext_->SetTarget(nullptr);
            if (FAILED(blurEnd)) {
                SetError("Direct2D could not blur the timed-text soft shadow.");
                return FFFResult::DeviceFailure;
            }
        }
        d2dContext_->SetTarget(d2dAtlasTarget_);
        d2dContext_->BeginDraw();
        for (const auto& pending : pendingSprites) {
            const auto& command = layer->commands[pending.commandIndex];
            const auto& sprite = pending.sprite;
            const auto clip = D2D1::RectF(sprite.atlasX, sprite.atlasY,
                sprite.atlasX + sprite.width, sprite.atlasY + sprite.height);
            d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
            drawLayout(command, pending.layout,
                D2D1::Point2F(sprite.atlasX + sprite.padding,
                    sprite.atlasY + sprite.padding), pending.outline,
                pending.shadowX, pending.shadowY);
            d2dContext_->PopAxisAlignedClip();
        }
        const auto atlasEnd = d2dContext_->EndDraw();
        d2dContext_->SetTarget(nullptr);
        if (FAILED(atlasEnd)) {
            SetError("Direct2D could not rasterize the timed-text sprite atlas.");
            return FFFResult::DeviceFailure;
        }
        for (const auto& pending : pendingSprites) {
            timedTextSprites_[pending.key] = pending.sprite;
            ++timedTextSpriteCacheMisses_;
        }
    }

    timedTextSpriteInstances_.clear();
    d2dContext_->SetTarget(d2dTargets_[slotIndex]);
    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0, 0));
    for (const auto& command : layer->commands) {
        const auto destination = D2D1::RectF(command.x * scaleX, command.y * scaleY,
            (command.x + command.width) * scaleX, (command.y + command.height) * scaleY);
        if (command.type == FFF3FPTimedTextCommandType::Bitmap) {
            D2D1_BITMAP_PROPERTIES properties{};
            // Bitmap subtitle frames are premultiplied sRGB.  HDR timed-text
            // targets are linear FP16, so let Direct2D perform the source
            // transfer conversion at this boundary instead of decoding the
            // same bytes again in the fullscreen shader.
            properties.pixelFormat = D2D1::PixelFormat(
                hdrLinear ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED);
            properties.dpiX = properties.dpiY = 96.0f;
            ComPtr<ID2D1Bitmap> bitmap;
            if (SUCCEEDED(d2dContext_->CreateBitmap(D2D1::SizeU(command.bitmapWidth, command.bitmapHeight),
                command.bitmap.data(), command.bitmapStride, &properties, &bitmap)))
                d2dContext_->DrawBitmap(bitmap.Get(), destination, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            continue;
        }
        const auto fontSize = std::max(command.fontSize * scaleY, 1.0f);
        const auto outline = std::max(command.outlineWidth * (scaleX + scaleY) * 0.5f, 0.0f);
        const auto shadowX = command.shadowOffsetX * scaleX;
        const auto shadowY = command.shadowOffsetY * scaleY;
        if (command.contentId != 0 &&
            command.horizontalAlignment == FFF3FPTimedTextAlignment::Near &&
            command.verticalAlignment == FFF3FPTimedTextAlignment::Near) {
            const auto spriteKey = TimedTextSpriteKey(command, destination, fontSize, outline,
                shadowX, shadowY);
            const auto sprite = timedTextSprites_.find(spriteKey);
            if (sprite != timedTextSprites_.end()) {
                ++timedTextSpriteCacheHits_;
                const auto left = destination.left - sprite->second.padding;
                const auto top = destination.top - sprite->second.padding;
                const auto right = left + sprite->second.width;
                const auto bottom = top + sprite->second.height;
                TimedTextSpriteInstance instance{};
                instance.destination[0] = left * 2.0f / swapWidth_ - 1.0f;
                instance.destination[1] = 1.0f - top * 2.0f / swapHeight_;
                instance.destination[2] = right * 2.0f / swapWidth_ - 1.0f;
                instance.destination[3] = 1.0f - bottom * 2.0f / swapHeight_;
                instance.uv[0] = sprite->second.atlasX / timedTextAtlasSize_;
                instance.uv[1] = sprite->second.atlasY / timedTextAtlasSize_;
                instance.uv[2] = (sprite->second.atlasX + sprite->second.width) / timedTextAtlasSize_;
                instance.uv[3] = (sprite->second.atlasY + sprite->second.height) / timedTextAtlasSize_;
                timedTextSpriteInstances_.push_back(instance);
                continue;
            }
        }
        auto* layout = getLayout(command, destination, fontSize);
        drawLayout(command, layout, D2D1::Point2F(destination.left, destination.top),
            outline, shadowX, shadowY);
    }
    const auto end = d2dContext_->EndDraw();
    d2dContext_->SetTarget(nullptr);
    if (FAILED(end)) {
        if (end == D2DERR_RECREATE_TARGET)
            RequestDeviceRecovery(end, "Direct2D timed-text rendering");
        SetError("Direct2D could not render the timed-text layer.");
        return FFFResult::DeviceFailure;
    }
    if (!timedTextSpriteInstances_.empty()) {
        const auto capacityResult = EnsureTimedTextInstanceCapacity(
            timedTextSpriteInstances_.size());
        if (capacityResult != FFFResult::Success) return capacityResult;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(timedTextSpriteInstanceBuffer_, 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            SetError("Could not update the timed-text sprite instances.");
            return FFFResult::DeviceFailure;
        }
        std::memcpy(mapped.pData, timedTextSpriteInstances_.data(),
            timedTextSpriteInstances_.size() * sizeof(TimedTextSpriteInstance));
        context_->Unmap(timedTextSpriteInstanceBuffer_, 0);
        constexpr float blendFactor[] = {0, 0, 0, 0};
        D3D11_VIEWPORT viewport{0, 0, static_cast<float>(swapWidth_),
            static_cast<float>(swapHeight_), 0, 1};
        context_->OMSetRenderTargets(1, &timedTextTargets_[slotIndex], nullptr);
        context_->OMSetBlendState(timedTextBlend_, blendFactor, UINT_MAX);
        context_->RSSetViewports(1, &viewport);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(timedTextSpriteVertexShader_, nullptr, 0);
        context_->VSSetShaderResources(1, 1, &timedTextSpriteInstanceView_);
        context_->PSSetShader(timedTextSpritePixelShader_, nullptr, 0);
        context_->PSSetShaderResources(0, 1, &timedTextAtlasView_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->DrawInstanced(6, static_cast<UINT>(timedTextSpriteInstances_.size()), 0, 0);
        ID3D11ShaderResourceView* nullView = nullptr;
        context_->VSSetShaderResources(1, 1, &nullView);
        context_->PSSetShaderResources(0, 1, &nullView);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        context_->OMSetBlendState(nullptr, blendFactor, UINT_MAX);
    }
    {
        std::lock_guard lock(timedTextMutex_);
        timedTextRenderedSequences_[slotIndex] = layer->sequence;
        timedTextRenderedCommandCounts_[slotIndex] = static_cast<std::uint32_t>(layer->commands.size());
        timedTextRenderedHdrHighlights_[slotIndex] = std::all_of(layer->commands.begin(),
            layer->commands.end(), [](const TimedTextRenderCommand& command) noexcept {
                return command.type == FFF3FPTimedTextCommandType::Bitmap &&
                    (static_cast<std::uint32_t>(command.flags) &
                     static_cast<std::uint32_t>(FFF3FPTimedTextFlags::HdrHighlightBitmap)) != 0;
            });
    }
    return FFFResult::Success;
}

void PlayerVideoRenderer::CompositeTimedText(ID3D11RenderTargetView* target,
    const TimedTextLayerSlot slot) noexcept {
    const auto slotIndex = static_cast<std::size_t>(slot);
    if (slotIndex >= ARRAYSIZE(timedTextViews_) || timedTextViews_[slotIndex] == nullptr ||
        timedTextRenderedCommandCounts_[slotIndex] == 0) return;
    ID3D11ShaderResourceView* views[] = {timedTextViews_[slotIndex], nullptr, nullptr};
    constexpr float blendFactor[] = {0, 0, 0, 0};
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(swapWidth_),
        static_cast<float>(swapHeight_), 0, 1};
    // This is a complete pipeline boundary, not a continuation of the layer
    // redraw above. Danmaku sprite batching leaves an instanced vertex shader
    // bound; using that shader after its instance SRV is detached produces no
    // valid fullscreen triangle and makes both overlay layers disappear until
    // some unrelated video draw happens to restore the old state.
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->OMSetBlendState(timedTextBlend_, blendFactor, UINT_MAX);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(timedTextPixelShader_, nullptr, 0);
    auto overlaySettings = cachedVideoSettings_;
    overlaySettings.colorMode = static_cast<std::uint32_t>(actualMode_);
    overlaySettings.reserved = (timedTextRenderedHdrHighlights_[slotIndex] ? 1u : 0u) |
        (actualMode_ == FFF3FPColorMode::MapToHdr ? 2u : 0u);
    context_->UpdateSubresource(constants_, 0, nullptr, &overlaySettings, 0, 0);
    context_->PSSetConstantBuffers(0, 1, &constants_);
    context_->PSSetShaderResources(0, ARRAYSIZE(views), views);
    context_->PSSetSamplers(0, 1, &sampler_);
    auto measurePipeline = false;
    if (timedTextPipelineQueries_[slotIndex] != nullptr) {
        if (!timedTextPipelineQueryInFlight_[slotIndex]) {
            measurePipeline = true;
        } else {
            D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};
            if (context_->GetData(timedTextPipelineQueries_[slotIndex], &statistics,
                    sizeof(statistics), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
                timedTextCompositePixelInvocations_[slotIndex] += statistics.PSInvocations;
                timedTextPipelineQueryInFlight_[slotIndex] = false;
                measurePipeline = true;
            }
        }
        if (measurePipeline) context_->Begin(timedTextPipelineQueries_[slotIndex]);
    }
    context_->Draw(3, 0);
    if (measurePipeline) {
        context_->End(timedTextPipelineQueries_[slotIndex]);
        timedTextPipelineQueryInFlight_[slotIndex] = true;
    }
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    context_->OMSetBlendState(nullptr, blendFactor, UINT_MAX);
}

void PlayerVideoRenderer::ReleaseTimedTextSlotResources(
    const TimedTextLayerSlot slot) noexcept {
    const auto index = static_cast<std::size_t>(slot);
    if (index >= ARRAYSIZE(timedTextTextures_)) return;
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    if (d2dTargets_[index] != nullptr) { d2dTargets_[index]->Release(); d2dTargets_[index] = nullptr; }
    if (timedTextPipelineQueries_[index] != nullptr) {
        timedTextPipelineQueries_[index]->Release(); timedTextPipelineQueries_[index] = nullptr;
    }
    if (timedTextViews_[index] != nullptr) { timedTextViews_[index]->Release(); timedTextViews_[index] = nullptr; }
    if (timedTextTargets_[index] != nullptr) { timedTextTargets_[index]->Release(); timedTextTargets_[index] = nullptr; }
    if (timedTextTextures_[index] != nullptr) { timedTextTextures_[index]->Release(); timedTextTextures_[index] = nullptr; }
    timedTextWidths_[index] = timedTextHeights_[index] = 0;
    timedTextRenderedHdrHighlights_[index] = false;
    timedTextPipelineQueryInFlight_[index] = false;
}

void PlayerVideoRenderer::ReleaseTimedTextResources(const bool resetRenderedState) noexcept {
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    for (auto& [key, layout] : timedTextLayouts_) if (layout != nullptr) layout->Release();
    timedTextLayouts_.clear();
    timedTextLayoutOrder_.clear();
    for (auto& [color, brush] : timedTextBrushes_) if (brush != nullptr) brush->Release();
    timedTextBrushes_.clear();
    timedTextSprites_.clear();
    timedTextSpriteInstances_.clear();
    timedTextAtlasX_ = timedTextAtlasY_ = timedTextAtlasRowHeight_ = 0;
    timedTextAtlasSize_ = 0;
    timedTextSpriteInstanceCapacity_ = 0;
    timedTextSpriteCacheHits_ = timedTextSpriteCacheMisses_ = 0;
    if (timedTextRenderingParams_ != nullptr) {
        timedTextRenderingParams_->Release(); timedTextRenderingParams_ = nullptr;
    }
    if (timedTextShadowBlurEffect_ != nullptr) {
        timedTextShadowBlurEffect_->SetInput(0, nullptr);
        timedTextShadowBlurEffect_->Release(); timedTextShadowBlurEffect_ = nullptr;
    }
    if (d2dTimedTextShadowTarget_ != nullptr) {
        d2dTimedTextShadowTarget_->Release(); d2dTimedTextShadowTarget_ = nullptr;
    }
    if (d2dAtlasTarget_ != nullptr) { d2dAtlasTarget_->Release(); d2dAtlasTarget_ = nullptr; }
    for (std::size_t index = 0; index < ARRAYSIZE(timedTextTextures_); ++index)
        ReleaseTimedTextSlotResources(static_cast<TimedTextLayerSlot>(index));
    if (d2dCoverBackdropSource_ == nullptr && d2dCoverBackdropTarget_ == nullptr &&
        coverBackdropBlurEffect_ == nullptr) {
        if (d2dContext_ != nullptr) { d2dContext_->Release(); d2dContext_ = nullptr; }
        if (d2dDevice_ != nullptr) { d2dDevice_->Release(); d2dDevice_ = nullptr; }
    }
    if (timedTextSpriteInstanceView_ != nullptr) { timedTextSpriteInstanceView_->Release(); timedTextSpriteInstanceView_ = nullptr; }
    if (timedTextSpriteInstanceBuffer_ != nullptr) { timedTextSpriteInstanceBuffer_->Release(); timedTextSpriteInstanceBuffer_ = nullptr; }
    if (timedTextSpritePixelShader_ != nullptr) { timedTextSpritePixelShader_->Release(); timedTextSpritePixelShader_ = nullptr; }
    if (timedTextSpriteVertexShader_ != nullptr) { timedTextSpriteVertexShader_->Release(); timedTextSpriteVertexShader_ = nullptr; }
    if (timedTextAtlasView_ != nullptr) { timedTextAtlasView_->Release(); timedTextAtlasView_ = nullptr; }
    if (timedTextAtlasTexture_ != nullptr) { timedTextAtlasTexture_->Release(); timedTextAtlasTexture_ = nullptr; }
    timedTextAtlasHdr_ = false;
    timedTextResourcesHdr_ = false;
    if (timedTextBlend_ != nullptr) { timedTextBlend_->Release(); timedTextBlend_ = nullptr; }
    {
        std::lock_guard lock(timedTextMutex_);
        for (std::size_t index = 0; index < ARRAYSIZE(timedTextLayers_); ++index) {
            timedTextWidths_[index] = timedTextHeights_[index] = 0;
            if (resetRenderedState) {
                timedTextRenderedSequences_[index] = 0;
                timedTextRenderedCommandCounts_[index] = 0;
                timedTextRenderedHdrHighlights_[index] = false;
            }
            timedTextPipelineQueryInFlight_[index] = false;
            timedTextCompositePixelInvocations_[index] = 0;
        }
    }
}

void PlayerVideoRenderer::SetHdrMetadata() noexcept {
    if (swapChain_ == nullptr || !swapHdr_) return;
    DXGI_HDR_METADATA_HDR10 metadata{};
    hdrProcessor_.BuildDxgiHdr10Metadata(metadata);
    swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
}

FFFResult PlayerVideoRenderer::Render(const AVFrame* frame, const bool limitToNativeSize,
    const bool coverArt) noexcept {
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return FFFResult::InvalidArgument;
    struct PlaybackWorkGuard final {
        std::atomic<std::uint32_t>& pending;
        explicit PlaybackWorkGuard(std::atomic<std::uint32_t>& value) noexcept : pending(value) {
            pending.fetch_add(1, std::memory_order_acq_rel);
        }
        ~PlaybackWorkGuard() { pending.fetch_sub(1, std::memory_order_acq_rel); }
    } playbackWorkGuard(playbackWorkPending_);
    const auto width = static_cast<std::uint32_t>(frame->width);
    const auto height = static_cast<std::uint32_t>(frame->height);
    const auto hdrState = hdrProcessor_.ProcessFrame(
        frame, hdrPeakNits_, paperWhiteNits_);
    const auto source2020 = hdrState.format != FFF3FPHdrFormat::Sdr || IsRec2020(frame);
    auto input = DescribeInput(static_cast<AVPixelFormat>(frame->format));
    const auto d3d11Frame = frame->format == AV_PIX_FMT_D3D11;
    if (d3d11Frame && frame->hw_frames_ctx != nullptr) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        input = DescribeInput(frames->sw_format);
    }
    const auto directYuv = input.layout != 0;
    if (!directYuv) {
        const auto* sourceDescriptor = av_pix_fmt_desc_get(
            static_cast<AVPixelFormat>(frame->format));
        if (sourceDescriptor != nullptr && sourceDescriptor->nb_components > 0)
            input.bitDepth = std::max(8, sourceDescriptor->comp[0].depth);
        const auto conversionStart = std::chrono::steady_clock::now();
        // CPU pixel conversion does not touch D3D state. Keeping it outside the
        // immediate-context critical section lets the 60 Hz overlay presenter
        // continue moving cached text while a 4K software frame is converted.
        const auto convertedFormat = input.bitDepth <= 8 ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA64LE;
        const auto bytesPerPixel = input.bitDepth <= 8 ? 4u : 8u;
        scaler_ = sws_getCachedContext(scaler_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, convertedFormat,
            SWS_BILINEAR | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
        if (scaler_ == nullptr) { SetError("FFmpeg could not create the video conversion context."); return FFFResult::FfmpegFailure; }
        const auto* sourceCoefficients = sws_getCoefficients(ToSwsColorSpace(frame, source2020));
        const auto* destinationCoefficients = sws_getCoefficients(SWS_CS_ITU709);
        if (sourceCoefficients == nullptr || destinationCoefficients == nullptr ||
            sws_setColorspaceDetails(scaler_, sourceCoefficients, IsFullRange(frame) ? 1 : 0,
                destinationCoefficients, 1, 0, 1 << 16, 1 << 16) < 0) {
            SetError("FFmpeg could not configure the frame color matrix and range.");
            return FFFResult::FfmpegFailure;
        }
        ResizeVideoConversionBuffer(convertedRgb_,
            static_cast<std::size_t>(width) * height * bytesPerPixel);
        std::uint8_t* outputData[] = { convertedRgb_.data(), nullptr, nullptr, nullptr };
        int outputLines[] = { static_cast<int>(width * bytesPerPixel), 0, 0, 0 };
        if (sws_scale(scaler_, frame->data, frame->linesize, 0, frame->height, outputData, outputLines) <= 0) {
            SetError("FFmpeg could not convert the decoded video frame."); return FFFResult::FfmpegFailure;
        }
        softwareConvert100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - conversionStart).count() / 100));
    }
    const auto deviceWaitStart = std::chrono::steady_clock::now();
    std::unique_lock deviceLock(deviceMutex_);
    deviceLockWait100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - deviceWaitStart).count() / 100));
    // Keep the logical render counter useful for headless/clip-mode sessions;
    // swapChainPresents remains the separate counter for real DXGI presents.
    if (window_ == nullptr) {
        ++presentedVideoFrames_;
        return FFFResult::Success;
    }
    const auto chainResult = EnsureSwapChain(frame->width, frame->height, input.bitDepth);
    if (chainResult != FFFResult::Success) return chainResult;
    if (swapHdr_) SetHdrMetadata();
    const auto pipelineResult = EnsurePipeline(width, height, input.layout, input.bitDepth,
        input.chromaWidthShift, input.chromaHeightShift, d3d11Frame);
    if (pipelineResult != FFFResult::Success) return pipelineResult;
    if (d3d11Frame) {
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
        const auto slice = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
        if (texture == nullptr || input.layout != 2) {
            SetError("The shared D3D11 decoder produced an unsupported surface.");
            return FFFResult::NotSupported;
        }
        // Decoder array slices are transient and are reused as soon as the AVFrame
        // is released. Retain only one shader-readable GPU surface and copy the
        // selected slice; this remains GPU-to-GPU and removes the full CPU transfer.
        context_->CopySubresourceRegion(sourceTextures_[0], 0, 0, 0, 0, texture, slice, nullptr);
    } else if (directYuv) {
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, frame->data[0], frame->linesize[0], 0);
        context_->UpdateSubresource(sourceTextures_[1], 0, nullptr, frame->data[1], frame->linesize[1], 0);
        if (input.layout == 1)
            context_->UpdateSubresource(sourceTextures_[2], 0, nullptr, frame->data[2], frame->linesize[2], 0);
    } else {
        const auto bytesPerPixel = input.bitDepth <= 8 ? 4u : 8u;
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, convertedRgb_.data(),
            width * bytesPerPixel, 0);
    }
    ShaderSettings settings{};
    settings.colorMode = static_cast<std::uint32_t>(actualMode_);
    settings.reserved = 0;
    const auto hlgCompatibility = static_cast<std::uint32_t>(FFF3FPHdrCompatibility::Hlg);
    settings.transfer = hdrState.format == FFF3FPHdrFormat::Hlg ||
        (hdrState.format == FFF3FPHdrFormat::DolbyVision &&
         (hdrState.compatibility & hlgCompatibility) != 0) ? 2u :
        (hdrState.format != FFF3FPHdrFormat::Sdr ? 1u : 0u);
    settings.source2020 = source2020 ? 1u : 0u;
    settings.sdrPeak = sdrPeakNits_;
    settings.hdrPeak = settings.transfer == 0 ? 100.0f : hdrState.sourcePeakNits;
    sourcePeakNits_ = settings.hdrPeak;
    settings.paperWhite = paperWhiteNits_;
    settings.targetPeak = hdrState.targetPeakNits;
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
        settings.cOffset = FullRangeChromaOffset(input.bitDepth);
        settings.cScale = 1.0f;
    }
    YuvCoefficients(frame, source2020, settings.kr, settings.kb);
    // DXGI exposes NV12/P010 processor color spaces with left chroma siting.
    // Hardware-decoded surfaces without bitstream siting metadata follow that
    // API convention so VP and shader A/B paths sample the same phase.
    const auto chromaLocation = d3d11Frame && frame->chroma_location == AVCHROMA_LOC_UNSPECIFIED
        ? AVCHROMA_LOC_LEFT : frame->chroma_location;
    ResolveChromaOffset(frame, input, chromaLocation,
        settings.chromaOffsetX, settings.chromaOffsetY);
    sourceColorSpace_ = frame->colorspace;
    sourceChromaLocation_ = chromaLocation;
    sourceFullRange_ = IsFullRange(frame);
    sourceInterlaced_ = (frame->flags & AV_FRAME_FLAG_INTERLACED) != 0;
    static_assert(sizeof(CachedVideoSettings) == sizeof(ShaderSettings));
    std::memcpy(&cachedVideoSettings_, &settings, sizeof(settings));
    sourceLimitedToNativeSize_ = limitToNativeSize;
    sourceCoverArt_ = coverArt;
    hasCachedVideo_ = true;
    videoGeneration_.fetch_add(1);
    if (coverArt) RequestCoverBackdropRender();
    {
        std::lock_guard lock(timedTextMutex_);
        if (!timedTextThread_.joinable()) {
            timedTextThreadStop_ = false;
            timedTextThreadRunning_ = true;
            timedTextThread_ = std::thread(&PlayerVideoRenderer::TimedTextThread, this);
        } else {
            timedTextThreadRunning_ = true;
        }
        ++presentationGeneration_;
    }
    deviceLock.unlock();
    ++presentedVideoFrames_;
    timedTextCondition_.notify_one();
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::Redraw() noexcept {
    {
        std::lock_guard deviceLock(deviceMutex_);
        if (!hasCachedVideo_ || window_ == nullptr) return FFFResult::Success;
        const auto chainResult = EnsureSwapChain(sourceWidth_, sourceHeight_, sourceBitDepth_);
        if (chainResult != FFFResult::Success) return chainResult;
    }
    {
        std::lock_guard lock(timedTextMutex_);
        if (!timedTextThread_.joinable()) {
            timedTextThreadStop_ = false; timedTextThreadRunning_ = true;
            timedTextThread_ = std::thread(&PlayerVideoRenderer::TimedTextThread, this);
        }
        ++presentationGeneration_;
    }
    timedTextCondition_.notify_one();
    return FFFResult::Success;
}

void PlayerVideoRenderer::ReleaseCoverBackdropResources() noexcept {
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    if (coverBackdropBlurEffect_ != nullptr) {
        coverBackdropBlurEffect_->SetInput(0, nullptr);
        coverBackdropBlurEffect_->Release(); coverBackdropBlurEffect_ = nullptr;
    }
    if (d2dCoverBackdropTarget_ != nullptr) {
        d2dCoverBackdropTarget_->Release(); d2dCoverBackdropTarget_ = nullptr;
    }
    if (d2dCoverBackdropSource_ != nullptr) {
        d2dCoverBackdropSource_->Release(); d2dCoverBackdropSource_ = nullptr;
    }
    if (coverBackdropSourceTarget_ != nullptr) {
        coverBackdropSourceTarget_->Release(); coverBackdropSourceTarget_ = nullptr;
    }
    if (coverBackdropSourceTexture_ != nullptr) {
        coverBackdropSourceTexture_->Release(); coverBackdropSourceTexture_ = nullptr;
    }
    if (coverBackdropView_ != nullptr) {
        coverBackdropView_->Release(); coverBackdropView_ = nullptr;
    }
    if (coverBackdropTexture_ != nullptr) {
        coverBackdropTexture_->Release(); coverBackdropTexture_ = nullptr;
    }
    coverBackdropWidth_ = coverBackdropHeight_ = 0;
    coverBackdropVideoGeneration_ = 0;
    coverBackdropAppliedBlurSettingsGeneration_ = 0;
}

FFFResult PlayerVideoRenderer::EnsureCoverBackdropResources() noexcept {
    if (device_ == nullptr || sourceWidth_ == 0 || sourceHeight_ == 0)
        return FFFResult::InvalidState;
    const auto blurSettingsGeneration =
        coverBackdropBlurSettingsGeneration_.load(std::memory_order_acquire);
    const auto downsampleFactor = std::max(1u,
        coverBackdropDownsampleFactor_.load(std::memory_order_acquire));
    const auto cacheSize = CalculateCoverBackdropCacheSize(
        sourceWidth_, sourceHeight_, downsampleFactor);
    const auto width = cacheSize.width;
    const auto height = cacheSize.height;
    if (width == 0 || height == 0) return FFFResult::InvalidState;
    // The cache receives the main shader's output at a fixed source-relative
    // size. FP16 is required here because scRGB values above 1.0 represent
    // real HDR luminance and must survive the blur passes without UNORM
    // clamping. DrawCoverBackdrop() linearly scales this completed texture to
    // the current output region; swap-chain dimensions never enter this key.
    const auto format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (coverBackdropTexture_ != nullptr && width == coverBackdropWidth_ &&
        height == coverBackdropHeight_ &&
        blurSettingsGeneration == coverBackdropAppliedBlurSettingsGeneration_)
        return FFFResult::Success;

    ReleaseCoverBackdropResources();
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width; description.Height = height;
    description.MipLevels = description.ArraySize = 1;
    description.Format = format; description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&description, nullptr,
            &coverBackdropSourceTexture_)) ||
        FAILED(device_->CreateRenderTargetView(coverBackdropSourceTexture_, nullptr,
            &coverBackdropSourceTarget_)) ||
        FAILED(device_->CreateTexture2D(&description, nullptr, &coverBackdropTexture_)) ||
        FAILED(device_->CreateShaderResourceView(coverBackdropTexture_, nullptr,
            &coverBackdropView_))) {
        ReleaseCoverBackdropResources();
        SetError("Could not create the blurred cover backdrop resources.");
        return FFFResult::DeviceFailure;
    }
    const auto d2dResult = EnsureD2DContext();
    if (d2dResult != FFFResult::Success) {
        ReleaseCoverBackdropResources();
        return d2dResult;
    }
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(format, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    ComPtr<IDXGISurface> sourceSurface;
    ComPtr<IDXGISurface> targetSurface;
    if (FAILED(coverBackdropSourceTexture_->QueryInterface(IID_PPV_ARGS(&sourceSurface))) ||
        FAILED(coverBackdropTexture_->QueryInterface(IID_PPV_ARGS(&targetSurface))) ||
        FAILED(d2dContext_->CreateBitmapFromDxgiSurface(sourceSurface.Get(), &properties,
            &d2dCoverBackdropSource_)) ||
        FAILED(d2dContext_->CreateBitmapFromDxgiSurface(targetSurface.Get(), &properties,
            &d2dCoverBackdropTarget_)) ||
        FAILED(d2dContext_->CreateEffect(CLSID_D2D1GaussianBlur,
            &coverBackdropBlurEffect_))) {
        ReleaseCoverBackdropResources();
        SetError("Could not create the LakeUI Gaussian cover backdrop resources.");
        return FFFResult::DeviceFailure;
    }
    coverBackdropBlurEffect_->SetInput(0, d2dCoverBackdropSource_);
    // LakeUI converts repeated box-blur radius to a single Gaussian sigma.
    const auto configuredRadius = std::bit_cast<float>(
        coverBackdropBlurRadiusBits_.load(std::memory_order_acquire));
    const auto configuredPasses =
        coverBackdropBlurPasses_.load(std::memory_order_acquire);
    const auto cacheScale = std::max(
        static_cast<float>(sourceWidth_) / static_cast<float>(width),
        static_cast<float>(sourceHeight_) / static_cast<float>(height));
    const auto radius = configuredRadius / std::max(cacheScale, 1.0f);
    const auto sigma = std::sqrt(static_cast<float>(std::max(1u, configuredPasses))) *
        radius / std::sqrt(3.0f);
    coverBackdropBlurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
        std::max(0.1f, sigma));
    coverBackdropBlurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
        D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
    coverBackdropBlurEffect_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
        D2D1_BORDER_MODE_HARD);
    coverBackdropWidth_ = width; coverBackdropHeight_ = height;
    coverBackdropAppliedBlurSettingsGeneration_ = blurSettingsGeneration;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::RenderCoverBackdropCache() noexcept {
    if (!sourceCoverArt_ || !hasCachedVideo_) return FFFResult::Success;
    const auto resourceResult = EnsureCoverBackdropResources();
    if (resourceResult != FFFResult::Success) return resourceResult;
    const auto videoGeneration = videoGeneration_.load(std::memory_order_acquire);
    if (coverBackdropVideoGeneration_ == videoGeneration) return FFFResult::Success;
    const auto sourceResult = DrawWithShader(coverBackdropSourceTarget_, 0.0f, 0.0f,
        static_cast<float>(coverBackdropWidth_), static_cast<float>(coverBackdropHeight_),
        CoverBackdropEffect);
    if (sourceResult != FFFResult::Success) return sourceResult;

    d2dContext_->SetTarget(d2dCoverBackdropTarget_);
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0, 0));
    if (coverBackdropBlurPasses_.load(std::memory_order_acquire) > 0)
        d2dContext_->DrawImage(coverBackdropBlurEffect_);
    else
        d2dContext_->DrawImage(d2dCoverBackdropSource_);
    const auto blurResult = d2dContext_->EndDraw();
    d2dContext_->SetTarget(nullptr);
    if (FAILED(blurResult)) {
        SetError("LakeUI Gaussian cover backdrop rendering failed.");
        return FFFResult::DeviceFailure;
    }
    coverBackdropVideoGeneration_ = videoGeneration;
    return FFFResult::Success;
}

PlayerVideoRenderer::CoverBackdropRenderResult
PlayerVideoRenderer::TryRenderCoverBackdropCache() noexcept {
    if (playbackWorkPending_.load(std::memory_order_acquire) != 0)
        return CoverBackdropRenderResult::Deferred;
    std::unique_lock deviceLock(deviceMutex_, std::try_to_lock);
    if (!deviceLock.owns_lock()) return CoverBackdropRenderResult::Deferred;
    if (playbackWorkPending_.load(std::memory_order_acquire) != 0)
        return CoverBackdropRenderResult::Deferred;
    const auto wasReady = coverBackdropTexture_ != nullptr &&
        coverBackdropVideoGeneration_ == videoGeneration_.load(std::memory_order_acquire) &&
        coverBackdropAppliedBlurSettingsGeneration_ ==
            coverBackdropBlurSettingsGeneration_.load(std::memory_order_acquire);
    const auto result = RenderCoverBackdropCache();
    if (result != FFFResult::Success) return CoverBackdropRenderResult::Failed;
    const auto ready = sourceCoverArt_ && coverBackdropTexture_ != nullptr &&
        coverBackdropVideoGeneration_ == videoGeneration_.load(std::memory_order_acquire);
    deviceLock.unlock();
    if (ready && !wasReady) {
        {
            std::lock_guard lock(timedTextMutex_);
            ++presentationGeneration_;
        }
        timedTextCondition_.notify_one();
    }
    return CoverBackdropRenderResult::Complete;
}

void PlayerVideoRenderer::RequestCoverBackdropRender(const bool force) noexcept {
    try {
        {
            std::lock_guard lock(coverBackdropThreadMutex_);
            if (coverBackdropThreadStop_) return;
            if (coverBackdropRequestPending_ && !force) return;
            coverBackdropRequestPending_ = true;
            ++coverBackdropRequestGeneration_;
            if (!coverBackdropThread_.joinable()) {
                coverBackdropThreadStop_ = false;
                coverBackdropThread_ = std::thread(
                    &PlayerVideoRenderer::CoverBackdropThread, this);
            }
        }
        coverBackdropCondition_.notify_one();
    } catch (...) {
        std::lock_guard lock(coverBackdropThreadMutex_);
        coverBackdropRequestPending_ = false;
        SetError("Could not start the deferred cover backdrop renderer.");
    }
}

void PlayerVideoRenderer::CoverBackdropThread() noexcept {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::uint64_t observedRequest = 0;
    for (;;) {
        std::uint64_t request = 0;
        {
            std::unique_lock lock(coverBackdropThreadMutex_);
            coverBackdropCondition_.wait(lock, [this, &observedRequest] {
                return coverBackdropThreadStop_ ||
                    coverBackdropRequestGeneration_ != observedRequest;
            });
            if (coverBackdropThreadStop_) return;
            request = coverBackdropRequestGeneration_;
            const auto changed = coverBackdropCondition_.wait_for(lock,
                std::chrono::milliseconds(120), [this, request] {
                    return coverBackdropThreadStop_ ||
                        coverBackdropRequestGeneration_ != request;
                });
            if (coverBackdropThreadStop_) return;
            if (changed) continue;
        }
        for (;;) {
            const auto result = TryRenderCoverBackdropCache();
            if (result != CoverBackdropRenderResult::Deferred) {
                {
                    std::lock_guard lock(coverBackdropThreadMutex_);
                    observedRequest = request;
                    if (coverBackdropRequestGeneration_ == request)
                        coverBackdropRequestPending_ = false;
                }
                if (result == CoverBackdropRenderResult::Failed)
                    RequestRecoveryIfDeviceLost();
                break;
            }
            std::unique_lock lock(coverBackdropThreadMutex_);
            const auto changed = coverBackdropCondition_.wait_for(lock,
                std::chrono::milliseconds(16), [this, request] {
                    return coverBackdropThreadStop_ ||
                        coverBackdropRequestGeneration_ != request;
                });
            if (coverBackdropThreadStop_) return;
            if (changed) break;
        }
    }
}

void PlayerVideoRenderer::StopCoverBackdropThread() noexcept {
    {
        std::lock_guard lock(coverBackdropThreadMutex_);
        coverBackdropThreadStop_ = true;
    }
    coverBackdropCondition_.notify_all();
    if (coverBackdropThread_.joinable()) coverBackdropThread_.join();
    std::lock_guard lock(coverBackdropThreadMutex_);
    coverBackdropThreadStop_ = false;
    coverBackdropRequestPending_ = false;
}

FFFResult PlayerVideoRenderer::DrawCoverBackdrop(ID3D11RenderTargetView* target) noexcept {
    if (target == nullptr || context_ == nullptr) return FFFResult::InvalidArgument;
    // Keep the last completed cache visible while a newer frame or blur
    // configuration is being rendered in the deferred worker. The video
    // frame must never flash to a plain black backdrop just because the newer
    // cache has not finished yet.
    if (coverBackdropTexture_ == nullptr || coverBackdropView_ == nullptr ||
        coverBackdropWidth_ == 0 || coverBackdropHeight_ == 0) {
        RequestCoverBackdropRender();
        return FFFResult::Success;
    }

    constexpr float blendFactor[] = {0, 0, 0, 0};
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->OMSetBlendState(nullptr, blendFactor, UINT_MAX);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(swapWidth_),
        static_cast<float>(swapHeight_), 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(coverBackdropPixelShader_, nullptr, 0);
    cachedVideoSettings_.colorMode = static_cast<std::uint32_t>(actualMode_);
    cachedVideoSettings_.reserved =
        coverBackdropTintArgb_.load(std::memory_order_acquire);
    context_->UpdateSubresource(constants_, 0, nullptr, &cachedVideoSettings_, 0, 0);
    context_->PSSetConstantBuffers(0, 1, &constants_);
    context_->PSSetSamplers(0, 1, &sampler_);
    ID3D11ShaderResourceView* views[] = {coverBackdropView_, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(views), views);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::DrawCachedVideo(ID3D11RenderTargetView* target) noexcept {
    if (!hasCachedVideo_ || target == nullptr) return FFFResult::InvalidState;
    VideoDestination destination{};
    if (lyricsLayoutEnabled_.load(std::memory_order_acquire) && sourceLimitedToNativeSize_) {
        destination = CalculateLyricsCoverDestination(sourceWidth_, sourceHeight_,
            swapWidth_, swapHeight_,
            std::bit_cast<float>(coverRegionWidthPercentageBits_.load(
                std::memory_order_acquire)),
            std::bit_cast<float>(lyricsRegionWidthPercentageBits_.load(
                std::memory_order_acquire)),
            std::bit_cast<float>(coverLeftPaddingPercentageBits_.load(
                std::memory_order_acquire)),
            std::bit_cast<float>(coverRightPaddingPercentageBits_.load(
                std::memory_order_acquire)),
            std::bit_cast<float>(coverVerticalPaddingPercentageBits_.load(
                std::memory_order_acquire)));
    } else {
        destination = CalculateVideoDestination(sourceWidth_, sourceHeight_, swapWidth_,
            swapHeight_, sourceLimitedToNativeSize_);
    }
    // Apply the view transform (zoom + pan) around the destination center.
    // Zoom scales the fitted video box; pan offsets are normalized to the
    // unzoomed box and clamped so the zoomed view always covers the fitted box.
    const auto zoom = std::bit_cast<float>(viewZoomBits_.load(std::memory_order_acquire));
    const auto panX = std::bit_cast<float>(viewPanXBits_.load(std::memory_order_acquire));
    const auto panY = std::bit_cast<float>(viewPanYBits_.load(std::memory_order_acquire));
    if (zoom > 1.0001f) {
        const float zoomedWidth = destination.width * zoom;
        const float zoomedHeight = destination.height * zoom;
        const float maxPanX = (zoomedWidth - destination.width) / (2.0f * destination.width);
        const float maxPanY = (zoomedHeight - destination.height) / (2.0f * destination.height);
        const float offsetX = panX * std::max(maxPanX, 0.0f) * destination.width;
        const float offsetY = panY * std::max(maxPanY, 0.0f) * destination.height;
        destination.x = static_cast<std::uint32_t>(
            std::max(0.0f, static_cast<float>(destination.x) +
                (destination.width - zoomedWidth) / 2.0f - offsetX));
        destination.y = static_cast<std::uint32_t>(
            std::max(0.0f, static_cast<float>(destination.y) +
                (destination.height - zoomedHeight) / 2.0f - offsetY));
        destination.width = static_cast<std::uint32_t>(zoomedWidth);
        destination.height = static_cast<std::uint32_t>(zoomedHeight);
    }
    ID3D11ShaderResourceView* presentationViews[3]{};
    const auto scaleResult = PrepareScaledVideo(destination.width, destination.height,
        presentationViews);
    if (scaleResult != FFFResult::Success) return scaleResult;
    constexpr float black[] = {0, 0, 0, 1};
    context_->ClearRenderTargetView(target, black);
    if (sourceCoverArt_) {
        const auto backdropResult = DrawCoverBackdrop(target);
        if (backdropResult != FFFResult::Success) return backdropResult;
    }
    // Rendering all inputs through the same shader removes the CPU/GPU
    // scaler discrepancy.  SDR still writes ordinary Rec.709 code values to
    // the implicit SDR swap-chain contract, so DWM remains the sole owner of
    // the Windows HDR SDR-white adjustment.
    const auto result = DrawWithShader(target, static_cast<float>(destination.x),
        static_cast<float>(destination.y), static_cast<float>(destination.width),
        static_cast<float>(destination.height), 0, presentationViews);
    if (result == FFFResult::Success)
        actualVideoScalingMode_.store(FFF3FPVideoScalingMode::Shader);
    return result;
}

FFFResult PlayerVideoRenderer::ReadPixel(FFF3FPVideoPixelProbe& probe) noexcept {
    if (probe.version != 1 || probe.size < sizeof(FFF3FPVideoPixelProbe))
        return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    if (!hasCachedVideo_ || swapChain_ == nullptr || device_ == nullptr || context_ == nullptr ||
        probe.x >= swapWidth_ || probe.y >= swapHeight_)
        return FFFResult::InvalidState;
    std::lock_guard presentLock(presentMutex_);
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> target;
    const auto targetResult = AcquireBackBufferTarget(
        backBuffer.GetAddressOf(), target.GetAddressOf());
    if (targetResult != FFFResult::Success) return targetResult;
    const auto drawResult = DrawCachedVideo(target.Get());
    if (drawResult != FFFResult::Success) return drawResult;

    D3D11_TEXTURE2D_DESC description{};
    backBuffer->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        description.Format != DXGI_FORMAT_R10G10B10A2_UNORM &&
        description.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
        return FFFResult::NotSupported;
    description.Width = description.Height = 1;
    description.MipLevels = description.ArraySize = 1;
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &staging)))
        return FFFResult::DeviceFailure;
    const D3D11_BOX source{probe.x, probe.y, 0, probe.x + 1, probe.y + 1, 1};
    context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, backBuffer.Get(), 0, &source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return FFFResult::DeviceFailure;
    if (description.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        const auto* bgra = static_cast<const std::uint8_t*>(mapped.pData);
        constexpr float scale = 1.0f / 255.0f;
        probe.red = bgra[2] * scale;
        probe.green = bgra[1] * scale;
        probe.blue = bgra[0] * scale;
        probe.alpha = bgra[3] * scale;
    } else if (description.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        std::uint32_t packed = 0;
        std::memcpy(&packed, mapped.pData, sizeof(packed));
        constexpr float rgbScale = 1.0f / 1023.0f;
        probe.red = static_cast<float>(packed & 0x3ffu) * rgbScale;
        probe.green = static_cast<float>((packed >> 10) & 0x3ffu) * rgbScale;
        probe.blue = static_cast<float>((packed >> 20) & 0x3ffu) * rgbScale;
        probe.alpha = static_cast<float>((packed >> 30) & 0x3u) / 3.0f;
    } else if (description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        // scRGB linear output (1.0 = 80 nits); report raw linear values.
        const auto* rgba = static_cast<const float*>(mapped.pData);
        probe.red = rgba[0];
        probe.green = rgba[1];
        probe.blue = rgba[2];
        probe.alpha = rgba[3];
    }
    context_->Unmap(staging.Get(), 0);
    probe.scalingMode = actualVideoScalingMode_.load();
    probe.outputBitDepth = swapOutputBits_;
    probe.colorMode = actualMode_;
    probe.reserved = 0;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::PresentCurrentFrame(IDXGISwapChain4* chain,
    const std::uint64_t renderedVideoGeneration) noexcept {
    if (chain == nullptr) return FFFResult::InvalidState;
    const auto start = std::chrono::steady_clock::now();
    const auto present = chain->Present(1, 0);
    presentWait100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count() / 100));
    if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET ||
        present == DXGI_ERROR_DEVICE_HUNG || present == DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
        RequestDeviceRecovery(present, "DXGI presentation");
        return FFFResult::DeviceFailure;
    }
    if (FAILED(present)) {
        std::ostringstream message;
        message << "Could not present the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(present) << ").";
        SetError(message.str());
        return FFFResult::DeviceFailure;
    }
    ++swapChainPresents_;
    const auto previous = presentedVideoGeneration_.exchange(renderedVideoGeneration);
    if (previous != 0 && renderedVideoGeneration > previous + 1)
        coalescedVideoFrames_.fetch_add(renderedVideoGeneration - previous - 1);
    std::lock_guard lock(timedTextMutex_);
    for (std::size_t index = 0; index < ARRAYSIZE(timedTextPresentCounts_); ++index)
        if (timedTextRenderedCommandCounts_[index] != 0) ++timedTextPresentCounts_[index];
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::PresentTimedText() noexcept {
    std::unique_lock deviceLock(deviceMutex_);
    const auto lyricsLayout = lyricsLayoutEnabled_.load(std::memory_order_acquire);
    if (window_ == nullptr || (!hasCachedVideo_ && swapChain_ == nullptr && !lyricsLayout))
        return FFFResult::Success;
    const auto chainResult = EnsureSwapChain(hasCachedVideo_ ? sourceWidth_ : 1,
        hasCachedVideo_ ? sourceHeight_ : 1, hasCachedVideo_ ? sourceBitDepth_ : 8);
    if (chainResult != FFFResult::Success || swapChain_ == nullptr) return chainResult;
    if (!hasCachedVideo_) {
        const auto pipelineResult = EnsurePipeline(1, 1, 0, 8, 0, 0, false);
        if (pipelineResult != FFFResult::Success) return pipelineResult;
        cachedVideoSettings_ = {};
        cachedVideoSettings_.colorMode = static_cast<std::uint32_t>(actualMode_);
        cachedVideoSettings_.sdrPeak = sdrPeakNits_;
        cachedVideoSettings_.hdrPeak = sdrPeakNits_;
        cachedVideoSettings_.paperWhite = paperWhiteNits_;
        cachedVideoSettings_.targetPeak = hdrProcessor_.State().targetPeakNits;
    }
    std::unique_lock presentLock(presentMutex_);
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> backBufferTarget;
    const auto targetResult = AcquireBackBufferTarget(
        backBuffer.GetAddressOf(), backBufferTarget.GetAddressOf());
    if (targetResult != FFFResult::Success) return targetResult;
    if (hasCachedVideo_) {
        const auto drawResult = DrawCachedVideo(backBufferTarget.Get());
        if (drawResult != FFFResult::Success) return drawResult;
    } else {
        constexpr float black[] = {0, 0, 0, 1};
        context_->ClearRenderTargetView(backBufferTarget.Get(), black);
    }
    const auto danmakuResult = DrawTimedText(TimedTextLayerSlot::Danmaku);
    if (danmakuResult != FFFResult::Success) return danmakuResult;
    const auto subtitleResult = DrawTimedText(TimedTextLayerSlot::Subtitle);
    if (subtitleResult != FFFResult::Success) return subtitleResult;
    const auto lyricsResult = DrawTimedText(TimedTextLayerSlot::Lyrics);
    if (lyricsResult != FFFResult::Success) return lyricsResult;
    const auto informationResult = DrawTimedText(TimedTextLayerSlot::PlayerInformation);
    if (informationResult != FFFResult::Success) return informationResult;
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Danmaku);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Subtitle);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Lyrics);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::PlayerInformation);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    const auto generation = videoGeneration_.load();
    ComPtr<IDXGISwapChain4> retainedChain = swapChain_;
    deviceLock.unlock();
    const auto result = PresentCurrentFrame(retainedChain.Get(), generation);
    if (result != FFFResult::Success) return result;
    // A format/color-space switch destroys and recreates the flip-model chain.
    // Drop every reference to the old chain and its back buffer before handing
    // presentMutex_ to the reconfiguration path. Otherwise CreateSwapChainForHwnd
    // can wait for these references while this thread waits for deviceMutex_.
    backBufferTarget.Reset();
    backBuffer.Reset();
    retainedChain.Reset();
    // Empty layers no longer need full-size GPU surfaces. Release them after the
    // clearing composite reached the display, preserving the submitted sequence.
    presentLock.unlock();
    deviceLock.lock();
    auto allEmpty = true;
    for (std::size_t index = 0; index < ARRAYSIZE(timedTextLayers_); ++index) {
        bool empty = false;
        {
            std::lock_guard lock(timedTextMutex_);
            empty = timedTextLayers_[index] == nullptr || timedTextLayers_[index]->commands.empty();
        }
        if (empty) ReleaseTimedTextSlotResources(static_cast<TimedTextLayerSlot>(index));
        else allEmpty = false;
    }
    if (allEmpty && timedTextAtlasTexture_ != nullptr) ReleaseTimedTextResources(false);
    return FFFResult::Success;
}

void PlayerVideoRenderer::ClearSurface() noexcept {
    if (context_ != nullptr && device_ != nullptr && swapChain_ != nullptr) {
        ComPtr<ID3D11Texture2D> backBuffer;
        ComPtr<ID3D11RenderTargetView> backBufferTarget;
        if (AcquireBackBufferTarget(backBuffer.GetAddressOf(),
                backBufferTarget.GetAddressOf()) == FFFResult::Success) {
            constexpr float black[] = {0, 0, 0, 1};
            context_->OMSetRenderTargets(1, backBufferTarget.GetAddressOf(), nullptr);
            context_->ClearRenderTargetView(backBufferTarget.Get(), black);
            context_->OMSetRenderTargets(0, nullptr, nullptr);
            context_->Flush();
            std::lock_guard presentLock(presentMutex_);
            swapChain_->Present(0, 0);
        }
    }
    if (window_ != nullptr && IsWindow(window_))
        InvalidateRect(window_, nullptr, TRUE);
}

bool PlayerVideoRenderer::DeviceRecoveryRequested() const noexcept {
    return deviceRecoveryRequested_.load(std::memory_order_acquire);
}

void PlayerVideoRenderer::RequestDeviceRecovery(const long result,
    const char* operation) noexcept {
    try {
        std::ostringstream message;
        message << (operation == nullptr ? "The playback graphics device failed" : operation)
                << " requested graphics resource reconstruction (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(result) << ").";
        SetError(message.str());
        if (!deviceRecoveryRequested_.exchange(true, std::memory_order_acq_rel) && recoveryCallback_)
            recoveryCallback_();
    } catch (...) {
        deviceRecoveryRequested_.store(true, std::memory_order_release);
        try { if (recoveryCallback_) recoveryCallback_(); } catch (...) {}
    }
}

bool PlayerVideoRenderer::RequestRecoveryIfDeviceLost() noexcept {
    if (DeviceRecoveryRequested()) return true;
    HRESULT reason = S_OK;
    {
        std::lock_guard deviceLock(deviceMutex_);
        if (device_ == nullptr) return false;
        reason = device_->GetDeviceRemovedReason();
    }
    if (SUCCEEDED(reason)) return false;
    RequestDeviceRecovery(reason, "D3D11 device removal");
    return true;
}

void PlayerVideoRenderer::ReleaseDeviceObjects() noexcept {
    ReleaseVideoProcessor();
    ReleaseVideoProcessorInputSurface();
    ReleaseCoverBackdropResources();
    ReleaseTimedTextResources();
    ReleaseScaleResources();
    if (context_ != nullptr &&
        (device_ == nullptr || SUCCEEDED(device_->GetDeviceRemovedReason()))) {
        context_->ClearState();
        context_->Flush();
    }
    if (swapChain_ != nullptr) {
        std::lock_guard presentLock(presentMutex_);
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        if (sourceViews_[plane] != nullptr) { sourceViews_[plane]->Release(); sourceViews_[plane] = nullptr; }
        if (sourceTextures_[plane] != nullptr) { sourceTextures_[plane]->Release(); sourceTextures_[plane] = nullptr; }
    }
    if (constants_ != nullptr) { constants_->Release(); constants_ = nullptr; }
    if (scaleConstants_ != nullptr) { scaleConstants_->Release(); scaleConstants_ = nullptr; }
    if (pointSampler_ != nullptr) { pointSampler_->Release(); pointSampler_ = nullptr; }
    if (sampler_ != nullptr) { sampler_->Release(); sampler_ = nullptr; }
    if (pixelShader_ != nullptr) { pixelShader_->Release(); pixelShader_ = nullptr; }
    if (scalePixelShader_ != nullptr) { scalePixelShader_->Release(); scalePixelShader_ = nullptr; }
    if (coverBackdropPixelShader_ != nullptr) {
        coverBackdropPixelShader_->Release(); coverBackdropPixelShader_ = nullptr;
    }
    if (timedTextPixelShader_ != nullptr) { timedTextPixelShader_->Release(); timedTextPixelShader_ = nullptr; }
    if (vertexShader_ != nullptr) { vertexShader_->Release(); vertexShader_ = nullptr; }
    if (context_ != nullptr) { context_->Release(); context_ = nullptr; }
    if (device_ != nullptr) { device_->Release(); device_ = nullptr; }
    swapWidth_ = swapHeight_ = sourceWidth_ = sourceHeight_ = 0;
    swapHdr_ = false;
    swapOutputBits_ = 8;
    sourceInputLayout_ = UINT32_MAX;
    sourceBitDepth_ = 0;
    sourceChromaWidthShift_ = sourceChromaHeightShift_ = 0;
    sourceExternal_ = false;
    sourceLimitedToNativeSize_ = false;
    sourceCoverArt_ = false;
    hasCachedVideo_ = false;
    hdrMonitor_ = nullptr;
    hdrSupportValid_ = false;
    hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
    hdrSwapChainRejected_ = false;
}

FFFResult PlayerVideoRenderer::RecreateDeviceResources() noexcept {
    StopCoverBackdropThread();
    StopTimedTextThread();
    std::lock_guard deviceLock(deviceMutex_);
    ReleaseDeviceObjects();
    const auto result = EnsureDevice();
    if (result == FFFResult::Success)
        deviceRecoveryRequested_.store(false, std::memory_order_release);
    return result;
}

void PlayerVideoRenderer::ResetMedia() noexcept {
    StopCoverBackdropThread();
    StopTimedTextThread();
    std::lock_guard deviceLock(deviceMutex_);
    ClearSurface();
    if (scaler_ != nullptr) { sws_freeContext(scaler_); scaler_ = nullptr; }
    ReleaseVideoProcessor();
    ReleaseVideoProcessorInputSurface();
    ReleaseCoverBackdropResources();
    ReleaseScaleResources();
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        if (sourceViews_[plane] != nullptr) { sourceViews_[plane]->Release(); sourceViews_[plane] = nullptr; }
        if (sourceTextures_[plane] != nullptr) { sourceTextures_[plane]->Release(); sourceTextures_[plane] = nullptr; }
    }
    sourceWidth_ = sourceHeight_ = 0;
    sourceInputLayout_ = UINT32_MAX;
    sourceBitDepth_ = 0;
    sourceChromaWidthShift_ = sourceChromaHeightShift_ = 0;
    sourceColorSpace_ = AVCOL_SPC_UNSPECIFIED;
    sourceChromaLocation_ = AVCHROMA_LOC_UNSPECIFIED;
    sourceFullRange_ = sourceInterlaced_ = false;
    sourcePeakNits_ = 100.0f;
    hdrProcessor_.Reset();
    // A previous 8K/RGBA64 source can leave hundreds of MiB in this staging
    // vector. Media replacement is already a pipeline boundary, so release it.
    std::vector<std::uint8_t>().swap(convertedRgb_);
    hasCachedVideo_ = false; sourceExternal_ = false; sourceLimitedToNativeSize_ = false;
    sourceCoverArt_ = false;
    lyricsLayoutEnabled_.store(false, std::memory_order_release);
    actualVideoScalingMode_.store(FFF3FPVideoScalingMode::D3D11VideoProcessor);
    videoGeneration_.store(0); presentedVideoGeneration_.store(0);
    presentedVideoFrames_.store(0); coalescedVideoFrames_.store(0);
    swapChainPresents_.store(0); presentWait100ns_.store(0);
    deviceLockWait100ns_.store(0); softwareConvert100ns_.store(0);
    {
        std::lock_guard lock(timedTextMutex_);
        ++presentationGeneration_;
        for (std::size_t index = 0; index < ARRAYSIZE(timedTextLayers_); ++index) {
            timedTextLayers_[index].reset();
            timedTextRenderedSequences_[index] = 0;
            timedTextRenderedCommandCounts_[index] = 0;
            timedTextRenderedHdrHighlights_[index] = false;
            timedTextPresentCounts_[index] = 0;
        }
    }
    timedTextCondition_.notify_one();
}

void PlayerVideoRenderer::Close() noexcept {
    // Join before taking deviceMutex_: the presenter may already be waiting in
    // PresentTimedText and must be allowed to leave that critical section.
    StopCoverBackdropThread();
    StopTimedTextThread();
    std::lock_guard deviceLock(deviceMutex_);
    ClearSurface();
    if (scaler_ != nullptr) { sws_freeContext(scaler_); scaler_ = nullptr; }
    ReleaseDeviceObjects();
    if (writeFactory_ != nullptr) { writeFactory_->Release(); writeFactory_ = nullptr; }
    if (d2dFactory_ != nullptr) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
    {
        std::lock_guard lock(timedTextMutex_);
        for (std::size_t index = 0; index < ARRAYSIZE(timedTextLayers_); ++index) {
            timedTextLayers_[index].reset();
            timedTextPresentCounts_[index] = 0;
        }
    }
    std::vector<std::uint8_t>().swap(convertedRgb_);
    deviceRecoveryRequested_.store(false, std::memory_order_release);
}

FFF3FPColorMode PlayerVideoRenderer::ActualColorMode() const noexcept { return actualMode_; }
float PlayerVideoRenderer::SourcePeakNits() const noexcept { return sourcePeakNits_; }
HdrFrameState PlayerVideoRenderer::HdrState() const noexcept { return hdrProcessor_.State(); }
std::uint64_t PlayerVideoRenderer::PresentedVideoFrames() const noexcept { return presentedVideoFrames_.load(); }
std::uint64_t PlayerVideoRenderer::CoalescedVideoFrames() const noexcept { return coalescedVideoFrames_.load(); }
std::uint64_t PlayerVideoRenderer::SwapChainPresents() const noexcept { return swapChainPresents_.load(); }
std::uint64_t PlayerVideoRenderer::SubmittedVideoGeneration() const noexcept { return videoGeneration_.load(); }
std::uint64_t PlayerVideoRenderer::PresentedVideoGeneration() const noexcept { return presentedVideoGeneration_.load(); }
bool PlayerVideoRenderer::HasOutputWindow() const noexcept {
    std::lock_guard lock(deviceMutex_);
    return window_ != nullptr && IsWindow(window_);
}
std::uint64_t PlayerVideoRenderer::PresentWait100ns() const noexcept { return presentWait100ns_.load(); }
std::uint64_t PlayerVideoRenderer::DeviceLockWait100ns() const noexcept { return deviceLockWait100ns_.load(); }
std::uint64_t PlayerVideoRenderer::SoftwareConvert100ns() const noexcept { return softwareConvert100ns_.load(); }
std::uint32_t PlayerVideoRenderer::OutputBitDepth() const noexcept {
    std::lock_guard lock(deviceMutex_);
    return swapOutputBits_;
}
FFF3FPVideoScalingMode PlayerVideoRenderer::ActualVideoScalingMode() const noexcept {
    return actualVideoScalingMode_.load();
}
std::string PlayerVideoRenderer::FallbackReason() const { return fallbackReason_; }
std::string PlayerVideoRenderer::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
void PlayerVideoRenderer::SetError(std::string message) noexcept {
    try { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); } catch (...) {}
}
