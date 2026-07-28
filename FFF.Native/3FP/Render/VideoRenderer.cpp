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
#include <d2d1helper.h>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>

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

float ToneToPeakNits(const float nits, const float peak) noexcept {
    const auto knee = peak * 0.75f;
    if (nits <= knee) return nits;
    return knee + (peak - knee) *
        (1.0f - std::exp(-(nits - knee) / std::max(peak - knee, 1.0f)));
}

float ReinhardHdrToSdrNits(const float nits, const float sourcePeak,
    const float paperWhite, const float targetPeak) noexcept {
    const auto reference = std::max(paperWhite, 1.0f);
    const auto white = std::max(sourcePeak / reference, 1.0f);
    const auto normalized = std::max(nits, 0.0f) / reference;
    return targetPeak * normalized * (1.0f + normalized / (white * white)) /
        (1.0f + normalized);
}

struct Float3 { float r, g, b; };

Float3 ScaleToPeak(const Float3 value, const float peak) noexcept {
    const auto maximum = std::max({value.r, value.g, value.b});
    if (maximum <= 1.0e-6f) return value;
    const auto scale = ToneToPeakNits(maximum, peak) / maximum;
    return {value.r * scale, value.g * scale, value.b * scale};
}

Float3 MapHdrToSdr(const Float3 value, const float sourcePeak,
    const float paperWhite, const float targetPeak) noexcept {
    const auto maximum = std::max({value.r, value.g, value.b});
    if (maximum <= 1.0e-6f) return value;
    const auto scale = ReinhardHdrToSdrNits(maximum, sourcePeak, paperWhite, targetPeak) / maximum;
    return {value.r * scale, value.g * scale, value.b * scale};
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
float3 ToneToPeak(float3 nits,float peak) {
    float maximum=max(max(nits.r,nits.g),nits.b);
    return maximum<=0.000001?nits:nits*(ToneOne(maximum,peak)/maximum);
}
float ReinhardHdrToSdrOne(float value,float sourcePeak,float paperWhite,float targetPeak) {
    float reference=max(paperWhite,1.0);
    float white=max(sourcePeak/reference,1.0);
    float normalized=max(value,0.0)/reference;
    return targetPeak*normalized*(1.0+normalized/(white*white))/(1.0+normalized);
}
float3 ToneHdrToSdr(float3 nits,float sourcePeak,float paperWhite,float targetPeak) {
    float maximum=max(max(nits.r,nits.g),nits.b);
    return maximum<=0.000001?nits:nits*(ReinhardHdrToSdrOne(maximum,sourcePeak,paperWhite,targetPeak)/maximum);
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
    if(ColorMode==2){ if(Source2020==0)nits=To2020(nits); return float4(NitsToPq(ToneToPeak(nits,1000.0)),1); }
    if(Source2020!=0)nits=To709(nits);
    // HDR is display-referred.  Compress it before the SDR OETF instead of
    // scaling HDR diffuse white directly to clipping white; that old scaling
    // lifted most mid-tones and left almost no highlight headroom.
    return float4(ToBt709(ToneHdrToSdr(nits,HdrPeak,PaperWhite,SdrPeak)/SdrPeak),1);
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
constexpr std::uint32_t MaximumTimedTextAtlasSize = 4096;
constexpr std::uint32_t MaximumTimedTextSprites = 512;

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
        return {2, 10, 1.0f, 1, 1};
    case AV_PIX_FMT_P012LE:
        return {2, 12, 1.0f, 1, 1};
    case AV_PIX_FMT_P016LE:
        return {2, 16, 1.0f, 1, 1};
    case AV_PIX_FMT_P210LE:
        return {2, 10, 1.0f, 1, 0};
    case AV_PIX_FMT_P212LE:
        return {2, 12, 1.0f, 1, 0};
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

float ResolveSourcePeakNits(const AVFrame* frame, const float fallback, const float paperWhite) noexcept {
    auto peak = fallback;
    const auto* lightSideData = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (lightSideData != nullptr && lightSideData->size >= sizeof(AVContentLightMetadata)) {
        const auto* light = reinterpret_cast<const AVContentLightMetadata*>(lightSideData->data);
        if (light->MaxCLL > 0) peak = static_cast<float>(light->MaxCLL);
    } else {
        const auto* masteringSideData = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        if (masteringSideData != nullptr && masteringSideData->size >= sizeof(AVMasteringDisplayMetadata)) {
            const auto* mastering = reinterpret_cast<const AVMasteringDisplayMetadata*>(masteringSideData->data);
            if (mastering->has_luminance) {
                const auto masteringPeak = static_cast<float>(av_q2d(mastering->max_luminance));
                if (std::isfinite(masteringPeak) && masteringPeak > 0.0f) peak = masteringPeak;
            }
        }
    }
    return std::clamp(peak, std::max(paperWhite, 1.0f), 10000.0f);
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
    HashTimedText(hash, command.contentId);
    if (command.content) HashTimedText(hash, command.content->identity);
    HashTimedText(hash, std::bit_cast<std::uint32_t>(fontSize));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(destination.right - destination.left));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(destination.bottom - destination.top));
    HashTimedText(hash, static_cast<std::uint32_t>(command.flags));
    HashTimedText(hash, static_cast<std::uint32_t>(command.horizontalAlignment));
    HashTimedText(hash, static_cast<std::uint32_t>(command.verticalAlignment));
    return hash == 0 ? 1 : hash;
}

std::uint64_t TimedTextSpriteKey(const TimedTextRenderCommand& command,
    const D2D1_RECT_F& destination, const float fontSize, const float outline) noexcept {
    auto hash = TimedTextLayoutKey(command, destination, fontSize);
    HashTimedText(hash, command.foregroundArgb);
    HashTimedText(hash, command.outlineArgb);
    HashTimedText(hash, std::bit_cast<std::uint32_t>(outline));
    return hash == 0 ? 1 : hash;
}
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
                if (transform.source2020 == 0) Convert709To2020(nits.r, nits.g, nits.b);
                nits = ScaleToPeak(nits, TrueHdrOutputPeakNits);
                rgb = {NitsToPq(nits.r), NitsToPq(nits.g), NitsToPq(nits.b)};
            } else {
                if (transform.source2020 != 0) Convert2020To709(nits.r, nits.g, nits.b);
                nits = MapHdrToSdr(nits, transform.sourcePeakNits,
                    transform.paperWhiteNits, transform.sdrPeakNits);
                rgb = {LinearToBt709(nits.r / transform.sdrPeakNits),
                    LinearToBt709(nits.g / transform.sdrPeakNits),
                    LinearToBt709(nits.b / transform.sdrPeakNits)};
            }
        }
    }
    transform.outputRed = Clamp01(rgb.r);
    transform.outputGreen = Clamp01(rgb.g);
    transform.outputBlue = Clamp01(rgb.b);
    return FFFResult::Success;
}

PlayerVideoRenderer::PlayerVideoRenderer() noexcept
    : window_(nullptr), device_(nullptr), context_(nullptr), swapChain_(nullptr),
      vertexShader_(nullptr), pixelShader_(nullptr), timedTextPixelShader_(nullptr), sampler_(nullptr), constants_(nullptr),
      sourceTextures_{nullptr, nullptr, nullptr}, sourceViews_{nullptr, nullptr, nullptr},
      timedTextTextures_{nullptr, nullptr}, timedTextTargets_{nullptr, nullptr},
      timedTextViews_{nullptr, nullptr}, timedTextPipelineQueries_{nullptr, nullptr},
      timedTextBlend_(nullptr),
      timedTextAtlasTexture_(nullptr), timedTextAtlasView_(nullptr),
      timedTextSpriteVertexShader_(nullptr), timedTextSpritePixelShader_(nullptr),
      timedTextSpriteInstanceBuffer_(nullptr), timedTextSpriteInstanceView_(nullptr),
      d2dFactory_(nullptr), d2dDevice_(nullptr), d2dContext_(nullptr), d2dTargets_{nullptr, nullptr},
      d2dAtlasTarget_(nullptr),
      writeFactory_(nullptr), scaler_(nullptr),
      swapWidth_(0), swapHeight_(0), swapHdr_(false), sourceWidth_(0), sourceHeight_(0),
      sourceInputLayout_(UINT32_MAX), sourceBitDepth_(0),
      sourceChromaWidthShift_(0), sourceChromaHeightShift_(0),
      sourceExternal_(false),
      requestedMode_(FFF3FPColorMode::MapToSdr), actualMode_(FFF3FPColorMode::MapToSdr),
      sdrPeakNits_(100.0f), hdrPeakNits_(TrueHdrOutputPeakNits),
      paperWhiteNits_(203.0f), sourcePeakNits_(100.0f),
      timedTextThreadStop_(false), timedTextThreadRunning_(false),
      presentationGeneration_(0), presentationFrameRate_(60.0f),
      timedTextRenderedSequences_{0, 0}, timedTextRenderedCommandCounts_{0, 0},
      timedTextWidths_{0, 0}, timedTextHeights_{0, 0}, timedTextPresentCounts_{0, 0},
      backBufferAcquisitionCount_(0),
      timedTextPipelineQueryInFlight_{false, false},
      timedTextCompositePixelInvocations_{0, 0},
      hasCachedVideo_(false), videoGeneration_(0), presentedVideoGeneration_(0),
      presentedVideoFrames_(0), coalescedVideoFrames_(0), swapChainPresents_(0),
      presentWait100ns_(0), deviceLockWait100ns_(0), softwareConvert100ns_(0),
      hdrMonitor_(nullptr), hdrSupportValid_(false), hdrSupported_(false),
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
    hdrSupportValid_ = false; hdrMonitor_ = nullptr;
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
        const auto result = ReconfigureSwapChain(actualMode_ == FFF3FPColorMode::MapToHdr);
        if (result != FFFResult::Success) return result;
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::ForceSdrOutputForSdrSource() noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    // Commit the public mode only after DXGI has returned the retained swap
    // chain to BGRA/BT.709 and removed HDR10 metadata. Reporting SDR while an
    // old PQ chain is still active would make the next SDR frame invalid.
    if (swapChain_ != nullptr && swapHdr_) {
        const auto result = ReconfigureSwapChain(false);
        if (result != FFFResult::Success) return result;
    }
    requestedMode_ = FFF3FPColorMode::MapToSdr;
    actualMode_ = FFF3FPColorMode::MapToSdr;
    fallbackReason_.clear();
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureDevice() noexcept {
    if (device_ != nullptr) return FFFResult::Success;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
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
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &selected, &context_);
    if (FAILED(result)) { SetError("Could not create the D3D11 playback device."); return FFFResult::DeviceFailure; }
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&multithread)))) multithread->SetMultithreadProtected(TRUE);
    return FFFResult::Success;
}

bool PlayerVideoRenderer::OutputSupportsHdr() noexcept {
    if (window_ == nullptr || !IsWindow(window_)) return false;
    const auto monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    if (hdrSupportValid_ && monitor == hdrMonitor_) return hdrSupported_;
    hdrMonitor_ = monitor;
    hdrSupportValid_ = true;
    hdrSupported_ = false;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;
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
            hdrSupported_ = description1.BitsPerColor >= 10 &&
                (description1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                 description1.ColorSpace == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020);
            return hdrSupported_;
        }
    }
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

FFFResult PlayerVideoRenderer::EnsureSwapChain(std::uint32_t width, std::uint32_t height) noexcept {
    if (window_ == nullptr) return FFFResult::Success;
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        const auto nextMode = OutputSupportsHdr() ? FFF3FPColorMode::MapToHdr : FFF3FPColorMode::MapToSdr;
        if (nextMode != actualMode_) {
            actualMode_ = nextMode;
            fallbackReason_ = nextMode == requestedMode_ ? std::string{} :
                "The target display or Windows Advanced Color mode does not support true HDR output.";
            if (swapChain_ != nullptr) {
                const auto modeResult = ReconfigureSwapChain(nextMode == FFF3FPColorMode::MapToHdr);
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
    if (swapChain_ != nullptr && hdr != swapHdr_) {
        const auto modeResult = ReconfigureSwapChain(hdr);
        if (modeResult != FFFResult::Success) return modeResult;
    }
    if (swapChain_ != nullptr && width == swapWidth_ && height == swapHeight_ && hdr == swapHdr_) return FFFResult::Success;
    if (swapChain_ != nullptr && hdr == swapHdr_) {
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
        std::ostringstream message;
        message << "Could not create the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(result) << ").";
        SetError(message.str()); return FFFResult::DeviceFailure;
    }
    swapWidth_ = width; swapHeight_ = height; swapHdr_ = hdr;
    // Keep at most one complete composite queued. The presentation thread may
    // wait here, but decode and managed layer production retain only their
    // latest state instead of building latency or exposing partial frames.
    swapChain_->SetMaximumFrameLatency(1);
    ReleaseTimedTextResources();
    if (hdr) {
        UINT support = 0;
        if (FAILED(swapChain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support)) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020))) {
            fallbackReason_ = "The swap chain rejected the Rec.2020 PQ color space.";
            actualMode_ = FFF3FPColorMode::MapToSdr;
            const auto fallbackResult = ReconfigureSwapChain(false);
            return fallbackResult;
        }
        SetHdrMetadata();
    } else {
        swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::ReconfigureSwapChain(const bool hdr) noexcept {
    if (swapChain_ == nullptr || hdr == swapHdr_) return FFFResult::Success;
    if (context_ != nullptr) { context_->ClearState(); context_->Flush(); }
    ReleaseTimedTextResources();
    const auto format = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
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
    swapHdr_ = hdr;
    if (hdr) {
        UINT support = 0;
        if (FAILED(swapChain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support)) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020))) {
            // HDR capability can change while a window moves between monitors.
            // Keep the same HWND/flip chain, but atomically fall back to the SDR
            // format instead of leaving an R10 buffer with an ambiguous space.
            fallbackReason_ = "The reconfigured swap chain rejected the Rec.2020 PQ color space.";
            actualMode_ = FFF3FPColorMode::MapToSdr;
            presentLock.unlock();
            return ReconfigureSwapChain(false);
        }
        SetHdrMetadata();
    } else {
        // ResizeBuffers does not define application HDR metadata lifetime. Clear
        // it explicitly whenever the retained chain becomes SDR, including a
        // HDR-to-SDR media switch on the same HWND.
        swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        if (FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709))) {
            SetError("The reconfigured swap chain rejected the Rec.709 SDR color space.");
            return FFFResult::DeviceFailure;
        }
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
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&texture, nullptr, &sourceTextures_[0]))) {
            SetError("Could not create the retained hardware-decoded video surface.");
            return FFFResult::NotSupported;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC luma{};
        luma.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        luma.Texture2D.MipLevels = 1;
        luma.Format = bitDepth > 8 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
        auto chroma = luma;
        chroma.Format = bitDepth > 8 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(device_->CreateShaderResourceView(sourceTextures_[0], &luma, &sourceViews_[0])) ||
            FAILED(device_->CreateShaderResourceView(sourceTextures_[0], &chroma, &sourceViews_[1]))) {
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
    sourceExternal_ = externalSource;
    sourceChromaWidthShift_ = chromaWidthShift;
    sourceChromaHeightShift_ = chromaHeightShift;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetTimedTextLayer(TimedTextRenderLayer layer,
    const TimedTextLayerSlot slot) noexcept {
    try {
        const auto slotIndex = static_cast<std::size_t>(slot);
        if (slotIndex >= ARRAYSIZE(timedTextLayers_)) return FFFResult::InvalidArgument;
        auto retained = std::make_shared<TimedTextRenderLayer>(std::move(layer));
        {
            std::lock_guard lock(timedTextMutex_);
            if (retained->sequence == 0)
                retained->sequence = timedTextLayers_[slotIndex]
                    ? timedTextLayers_[slotIndex]->sequence + 1 : 1;
            if (slot == TimedTextLayerSlot::Danmaku ||
                timedTextLayers_[static_cast<std::size_t>(TimedTextLayerSlot::Danmaku)] == nullptr)
                presentationFrameRate_ = std::clamp(retained->targetFrameRate, 1.0f, 240.0f);
            timedTextLayers_[slotIndex] = std::move(retained);
            const auto hasVisibleLayer = std::any_of(std::begin(timedTextLayers_),
                std::end(timedTextLayers_), [](const auto& item) {
                    return item != nullptr && !item->commands.empty();
                });
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
        {
            std::unique_lock lock(timedTextMutex_);
            timedTextCondition_.wait(lock, [this, &observedPresentationGeneration,
                    &observedVideoGeneration] {
                return timedTextThreadStop_ ||
                    presentationGeneration_ != observedPresentationGeneration ||
                    (timedTextThreadRunning_ &&
                        videoGeneration_.load() != observedVideoGeneration);
            });
            if (timedTextThreadStop_) return;
            if (!timedTextThreadRunning_) {
                observedPresentationGeneration = presentationGeneration_;
                observedVideoGeneration = videoGeneration_.load();
                continue;
            }
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
        const auto presentationStart = std::chrono::steady_clock::now();
        const auto result = PresentTimedText();
        if (result != FFFResult::Success)
            SetError("The independent timed-text presenter could not compose the latest layer.");
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

FFFResult PlayerVideoRenderer::EnsureTimedTextResources(const TimedTextLayerSlot slot) noexcept {
    const auto slotIndex = static_cast<std::size_t>(slot);
    if (slotIndex >= ARRAYSIZE(timedTextTextures_)) return FFFResult::InvalidArgument;
    if (swapWidth_ == 0 || swapHeight_ == 0) return FFFResult::Success;
    if (timedTextTextures_[slotIndex] != nullptr &&
        timedTextWidths_[slotIndex] == swapWidth_ && timedTextHeights_[slotIndex] == swapHeight_)
        return FFFResult::Success;
    ReleaseTimedTextSlotResources(slot);
    if (d2dFactory_ == nullptr && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        IID_PPV_ARGS(&d2dFactory_)))) {
        SetError("Could not create the Direct2D timed-text factory."); return FFFResult::DeviceFailure;
    }
    if (writeFactory_ == nullptr && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory_)))) {
        SetError("Could not create the DirectWrite timed-text factory."); return FFFResult::DeviceFailure;
    }
    if (d2dContext_ == nullptr) {
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
            FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_)) ||
            FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_))) {
            SetError("Could not bind Direct2D to the D3D11 playback device."); return FFFResult::DeviceFailure;
        }
    }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = swapWidth_; texture.Height = swapHeight_;
    texture.MipLevels = texture.ArraySize = 1; texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
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
    timedTextWidths_[slotIndex] = swapWidth_;
    timedTextHeights_[slotIndex] = swapHeight_;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureTimedTextAtlas(const std::uint32_t requestedSize) noexcept {
    const auto size = std::clamp(requestedSize, InitialTimedTextAtlasSize,
        MaximumTimedTextAtlasSize);
    if (timedTextAtlasTexture_ != nullptr && timedTextAtlasSize_ >= size)
        return FFFResult::Success;
    if (d2dContext_ == nullptr || device_ == nullptr) return FFFResult::InvalidState;
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    if (d2dAtlasTarget_ != nullptr) { d2dAtlasTarget_->Release(); d2dAtlasTarget_ = nullptr; }
    if (timedTextAtlasView_ != nullptr) { timedTextAtlasView_->Release(); timedTextAtlasView_ = nullptr; }
    if (timedTextAtlasTexture_ != nullptr) { timedTextAtlasTexture_->Release(); timedTextAtlasTexture_ = nullptr; }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = size; texture.MipLevels = texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM; texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&texture, nullptr, &timedTextAtlasTexture_)) ||
        FAILED(device_->CreateShaderResourceView(timedTextAtlasTexture_, nullptr, &timedTextAtlasView_))) {
        SetError("Could not create the GPU timed-text sprite atlas.");
        return FFFResult::DeviceFailure;
    }
    ComPtr<IDXGISurface> surface;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (FAILED(timedTextAtlasTexture_->QueryInterface(IID_PPV_ARGS(&surface))) ||
        FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &d2dAtlasTarget_))) {
        SetError("Could not expose the GPU timed-text atlas to Direct2D.");
        return FFFResult::DeviceFailure;
    }
    d2dContext_->SetTarget(d2dAtlasTarget_); d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0, 0));
    const auto end = d2dContext_->EndDraw(); d2dContext_->SetTarget(nullptr);
    if (FAILED(end)) return FFFResult::DeviceFailure;
    timedTextAtlasSize_ = size;
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
        timedTextWidths_[slotIndex] = swapWidth_; timedTextHeights_[slotIndex] = swapHeight_;
        ReleaseTimedTextSlotResources(slot);
        return FFFResult::Success;
    }
    const auto resourceResult = EnsureTimedTextResources(slot);
    if (resourceResult != FFFResult::Success) return resourceResult;
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2dContext_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    const auto scaleX = layer->canvasWidth == 0 ? 1.0f : static_cast<float>(swapWidth_) / layer->canvasWidth;
    const auto scaleY = layer->canvasHeight == 0 ? 1.0f : static_cast<float>(swapHeight_) / layer->canvasHeight;
    const auto getBrush = [this](const std::uint32_t argb) noexcept -> ID2D1SolidColorBrush* {
        const auto existing = timedTextBrushes_.find(argb);
        if (existing != timedTextBrushes_.end()) return existing->second;
        ID2D1SolidColorBrush* brush = nullptr;
        if (FAILED(d2dContext_->CreateSolidColorBrush(ToD2dColor(argb), &brush))) return nullptr;
        timedTextBrushes_.emplace(argb, brush);
        return brush;
    };
    const auto getLayout = [this](const TimedTextRenderCommand& command,
        const D2D1_RECT_F& destination, const float fontSize) noexcept -> IDWriteTextLayout* {
        const auto weight = (static_cast<std::uint32_t>(command.flags) &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Bold)) != 0
            ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        const auto style = (static_cast<std::uint32_t>(command.flags) &
            static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Italic)) != 0
            ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        const auto layoutKey = TimedTextLayoutKey(command, destination, fontSize);
        const auto existing = timedTextLayouts_.find(layoutKey);
        if (existing != timedTextLayouts_.end()) return existing->second;
        ComPtr<IDWriteTextFormat> format;
        if (!command.content || FAILED(writeFactory_->CreateTextFormat(command.content->fontFamily.c_str(), nullptr, weight, style,
            DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &format))) return nullptr;
        format->SetTextAlignment(ToTextAlignment(command.horizontalAlignment));
        format->SetParagraphAlignment(ToParagraphAlignment(command.verticalAlignment));
        // The managed scheduler already measured the run. A second wrapping
        // decision in DirectWrite can intermittently move/clip the last glyph.
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(writeFactory_->CreateTextLayout(command.content->text.c_str(),
            static_cast<UINT32>(command.content->text.size()), format.Get(),
            std::max(destination.right - destination.left, 1.0f),
            std::max(destination.bottom - destination.top, 1.0f), &layout))) return nullptr;
        if ((static_cast<std::uint32_t>(command.flags) & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Underline)) != 0)
            layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(command.content->text.size())});
        if ((static_cast<std::uint32_t>(command.flags) & static_cast<std::uint32_t>(FFF3FPTimedTextFlags::Strikeout)) != 0)
            layout->SetStrikethrough(TRUE, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(command.content->text.size())});
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
    const auto drawLayout = [&getBrush, this](const TimedTextRenderCommand& command,
        IDWriteTextLayout* layout, const D2D1_POINT_2F origin, const float outline) noexcept {
        if (layout == nullptr) return;
        constexpr auto textOptions = D2D1_DRAW_TEXT_OPTIONS_NO_SNAP;
        if (outline > 0 && (command.outlineArgb >> 24) != 0) {
            if (auto* outlineBrush = getBrush(command.outlineArgb); outlineBrush != nullptr) {
                const auto radius = std::max(1, static_cast<int>(std::ceil(outline)));
                constexpr int directions[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                for (const auto& direction : directions)
                    d2dContext_->DrawTextLayout(D2D1::Point2F(origin.x + direction[0] * radius,
                        origin.y + direction[1] * radius), layout, outlineBrush, textOptions);
            }
        }
        if ((command.foregroundArgb >> 24) != 0) {
            if (auto* foreground = getBrush(command.foregroundArgb); foreground != nullptr)
                d2dContext_->DrawTextLayout(origin, layout, foreground, textOptions);
        }
    };

    struct PendingSprite {
        const TimedTextRenderCommand* command = nullptr;
        IDWriteTextLayout* layout = nullptr;
        std::uint64_t key = 0;
        TimedTextSprite sprite{};
        float outline = 0;
    };
    std::vector<PendingSprite> pendingSprites;
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
        for (const auto& command : layer->commands) {
            if (command.type != FFF3FPTimedTextCommandType::Text || command.contentId == 0 ||
                command.horizontalAlignment != FFF3FPTimedTextAlignment::Near ||
                command.verticalAlignment != FFF3FPTimedTextAlignment::Near) continue;
            const auto destination = D2D1::RectF(command.x * scaleX, command.y * scaleY,
                (command.x + command.width) * scaleX, (command.y + command.height) * scaleY);
            const auto fontSize = std::max(command.fontSize * scaleY, 1.0f);
            const auto outline = std::max(command.outlineWidth * (scaleX + scaleY) * 0.5f, 0.0f);
            const auto key = TimedTextSpriteKey(command, destination, fontSize, outline);
            if (timedTextSprites_.contains(key)) continue;
            if (timedTextSprites_.size() + pendingSprites.size() >= MaximumTimedTextSprites)
                return !stopWhenFull;
            auto* layout = getLayout(command, destination, fontSize);
            if (layout == nullptr) continue;
            const auto padding = static_cast<float>(std::ceil(outline) + 4.0f);
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
            pendingSprites.push_back(PendingSprite{&command, layout, key, sprite, outline});
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
        d2dContext_->SetTarget(d2dAtlasTarget_);
        d2dContext_->BeginDraw();
        for (const auto& pending : pendingSprites) {
            const auto& sprite = pending.sprite;
            const auto clip = D2D1::RectF(sprite.atlasX, sprite.atlasY,
                sprite.atlasX + sprite.width, sprite.atlasY + sprite.height);
            d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
            drawLayout(*pending.command, pending.layout,
                D2D1::Point2F(sprite.atlasX + sprite.padding,
                    sprite.atlasY + sprite.padding), pending.outline);
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
        const auto fontSize = std::max(command.fontSize * scaleY, 1.0f);
        const auto outline = std::max(command.outlineWidth * (scaleX + scaleY) * 0.5f, 0.0f);
        if (command.contentId != 0 &&
            command.horizontalAlignment == FFF3FPTimedTextAlignment::Near &&
            command.verticalAlignment == FFF3FPTimedTextAlignment::Near) {
            const auto spriteKey = TimedTextSpriteKey(command, destination, fontSize, outline);
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
        drawLayout(command, layout, D2D1::Point2F(destination.left, destination.top), outline);
    }
    const auto end = d2dContext_->EndDraw();
    d2dContext_->SetTarget(nullptr);
    if (FAILED(end)) { SetError("Direct2D could not render the timed-text layer."); return FFFResult::DeviceFailure; }
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
    if (d2dAtlasTarget_ != nullptr) { d2dAtlasTarget_->Release(); d2dAtlasTarget_ = nullptr; }
    for (std::size_t index = 0; index < ARRAYSIZE(timedTextTextures_); ++index)
        ReleaseTimedTextSlotResources(static_cast<TimedTextLayerSlot>(index));
    if (d2dContext_ != nullptr) { d2dContext_->Release(); d2dContext_ = nullptr; }
    if (d2dDevice_ != nullptr) { d2dDevice_->Release(); d2dDevice_ = nullptr; }
    if (timedTextSpriteInstanceView_ != nullptr) { timedTextSpriteInstanceView_->Release(); timedTextSpriteInstanceView_ = nullptr; }
    if (timedTextSpriteInstanceBuffer_ != nullptr) { timedTextSpriteInstanceBuffer_->Release(); timedTextSpriteInstanceBuffer_ = nullptr; }
    if (timedTextSpritePixelShader_ != nullptr) { timedTextSpritePixelShader_->Release(); timedTextSpritePixelShader_ = nullptr; }
    if (timedTextSpriteVertexShader_ != nullptr) { timedTextSpriteVertexShader_->Release(); timedTextSpriteVertexShader_ = nullptr; }
    if (timedTextAtlasView_ != nullptr) { timedTextAtlasView_->Release(); timedTextAtlasView_ = nullptr; }
    if (timedTextAtlasTexture_ != nullptr) { timedTextAtlasTexture_->Release(); timedTextAtlasTexture_ = nullptr; }
    if (timedTextBlend_ != nullptr) { timedTextBlend_->Release(); timedTextBlend_ = nullptr; }
    {
        std::lock_guard lock(timedTextMutex_);
        for (std::size_t index = 0; index < ARRAYSIZE(timedTextLayers_); ++index) {
            timedTextWidths_[index] = timedTextHeights_[index] = 0;
            if (resetRenderedState) {
                timedTextRenderedSequences_[index] = 0;
                timedTextRenderedCommandCounts_[index] = 0;
            }
            timedTextPipelineQueryInFlight_[index] = false;
            timedTextCompositePixelInvocations_[index] = 0;
        }
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
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return FFFResult::InvalidArgument;
    const auto width = static_cast<std::uint32_t>(frame->width);
    const auto height = static_cast<std::uint32_t>(frame->height);
    auto input = DescribeInput(static_cast<AVPixelFormat>(frame->format));
    const auto d3d11Frame = frame->format == AV_PIX_FMT_D3D11;
    if (d3d11Frame && frame->hw_frames_ctx != nullptr) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        input = DescribeInput(frames->sw_format);
    }
    const auto directYuv = input.layout != 0;
    if (!directYuv) {
        const auto conversionStart = std::chrono::steady_clock::now();
        // CPU pixel conversion does not touch D3D state. Keeping it outside the
        // immediate-context critical section lets the 60 Hz overlay presenter
        // continue moving cached text while a 4K software frame is converted.
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
    const auto chainResult = EnsureSwapChain(frame->width, frame->height);
    if (chainResult != FFFResult::Success) return chainResult;
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
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, rgba64_.data(), width * 8, 0);
    }
    ShaderSettings settings{};
    settings.colorMode = static_cast<std::uint32_t>(actualMode_);
    settings.transfer = frame->color_trc == AVCOL_TRC_SMPTE2084 ? 1u : (frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ? 2u : 0u);
    settings.source2020 = IsRec2020(frame) ? 1u : 0u;
    settings.sdrPeak = sdrPeakNits_;
    settings.hdrPeak = settings.transfer == 0 ? sdrPeakNits_ :
        ResolveSourcePeakNits(frame, hdrPeakNits_, paperWhiteNits_);
    sourcePeakNits_ = settings.hdrPeak;
    settings.paperWhite = paperWhiteNits_;
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
    static_assert(sizeof(CachedVideoSettings) == sizeof(ShaderSettings));
    std::memcpy(&cachedVideoSettings_, &settings, sizeof(settings));
    hasCachedVideo_ = true;
    const auto currentVideoGeneration = videoGeneration_.fetch_add(1) + 1;
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
    (void)currentVideoGeneration;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::Redraw() noexcept {
    {
        std::lock_guard deviceLock(deviceMutex_);
        if (!hasCachedVideo_ || window_ == nullptr) return FFFResult::Success;
        const auto chainResult = EnsureSwapChain(sourceWidth_, sourceHeight_);
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

FFFResult PlayerVideoRenderer::DrawCachedVideo(ID3D11RenderTargetView* target) noexcept {
    if (!hasCachedVideo_ || target == nullptr) return FFFResult::InvalidState;
    cachedVideoSettings_.colorMode = static_cast<std::uint32_t>(actualMode_);
    cachedVideoSettings_.outputWidth = static_cast<float>(swapWidth_);
    cachedVideoSettings_.outputHeight = static_cast<float>(swapHeight_);
    context_->UpdateSubresource(constants_, 0, nullptr, &cachedVideoSettings_, 0, 0);
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(swapWidth_), static_cast<float>(swapHeight_), 0, 1};
    context_->OMSetRenderTargets(1, &target, nullptr); context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0); context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &constants_); context_->PSSetSamplers(0, 1, &sampler_);
    context_->PSSetShaderResources(0, ARRAYSIZE(sourceViews_), sourceViews_); context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::PresentCurrentFrame(IDXGISwapChain4* chain,
    const std::uint64_t renderedVideoGeneration) noexcept {
    if (chain == nullptr) return FFFResult::InvalidState;
    const auto start = std::chrono::steady_clock::now();
    const auto present = chain->Present(1, 0);
    presentWait100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count() / 100));
    if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
        SetError("The D3D11 playback device was removed."); return FFFResult::DeviceFailure;
    }
    if (FAILED(present)) return FFFResult::DeviceFailure;
    ++swapChainPresents_;
    const auto previous = presentedVideoGeneration_.exchange(renderedVideoGeneration);
    if (renderedVideoGeneration > previous + 1)
        coalescedVideoFrames_.fetch_add(renderedVideoGeneration - previous - 1);
    std::lock_guard lock(timedTextMutex_);
    for (std::size_t index = 0; index < ARRAYSIZE(timedTextPresentCounts_); ++index)
        if (timedTextRenderedCommandCounts_[index] != 0) ++timedTextPresentCounts_[index];
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::PresentTimedText() noexcept {
    std::unique_lock deviceLock(deviceMutex_);
    if (swapChain_ == nullptr || !hasCachedVideo_) return FFFResult::Success;
    std::unique_lock presentLock(presentMutex_);
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> backBufferTarget;
    const auto targetResult = AcquireBackBufferTarget(
        backBuffer.GetAddressOf(), backBufferTarget.GetAddressOf());
    if (targetResult != FFFResult::Success) return targetResult;
    const auto drawResult = DrawCachedVideo(backBufferTarget.Get());
    if (drawResult != FFFResult::Success) return drawResult;
    const auto danmakuResult = DrawTimedText(TimedTextLayerSlot::Danmaku);
    if (danmakuResult != FFFResult::Success) return danmakuResult;
    const auto subtitleResult = DrawTimedText(TimedTextLayerSlot::Subtitle);
    if (subtitleResult != FFFResult::Success) return subtitleResult;
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Danmaku);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Subtitle);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    const auto generation = videoGeneration_.load();
    ComPtr<IDXGISwapChain4> retainedChain = swapChain_;
    deviceLock.unlock();
    const auto result = PresentCurrentFrame(retainedChain.Get(), generation);
    if (result != FFFResult::Success) return result;
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

void PlayerVideoRenderer::ResetMedia() noexcept {
    StopTimedTextThread();
    std::lock_guard deviceLock(deviceMutex_);
    ClearSurface();
    if (scaler_ != nullptr) { sws_freeContext(scaler_); scaler_ = nullptr; }
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        if (sourceViews_[plane] != nullptr) { sourceViews_[plane]->Release(); sourceViews_[plane] = nullptr; }
        if (sourceTextures_[plane] != nullptr) { sourceTextures_[plane]->Release(); sourceTextures_[plane] = nullptr; }
    }
    sourceWidth_ = sourceHeight_ = 0;
    sourceInputLayout_ = UINT32_MAX;
    sourceBitDepth_ = 0;
    sourceChromaWidthShift_ = sourceChromaHeightShift_ = 0;
    sourcePeakNits_ = sdrPeakNits_;
    rgba64_.clear(); rgba64_.shrink_to_fit();
    hasCachedVideo_ = false; sourceExternal_ = false;
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
            timedTextPresentCounts_[index] = 0;
        }
    }
    timedTextCondition_.notify_one();
}

void PlayerVideoRenderer::Close() noexcept {
    // Join before taking deviceMutex_: the presenter may already be waiting in
    // PresentTimedText and must be allowed to leave that critical section.
    StopTimedTextThread();
    std::lock_guard deviceLock(deviceMutex_);
    ClearSurface();
    if (scaler_ != nullptr) { sws_freeContext(scaler_); scaler_ = nullptr; }
    ReleaseTimedTextResources();
    if (writeFactory_ != nullptr) { writeFactory_->Release(); writeFactory_ = nullptr; }
    if (d2dFactory_ != nullptr) { d2dFactory_->Release(); d2dFactory_ = nullptr; }
    {
        std::lock_guard lock(timedTextMutex_);
        for (std::size_t index = 0; index < ARRAYSIZE(timedTextLayers_); ++index) {
            timedTextLayers_[index].reset();
            timedTextPresentCounts_[index] = 0;
        }
    }
    // Flip-model swap chains are exclusive per HWND. Unbind and submit every
    // outstanding D3D reference before releasing the chain, otherwise an
    // immediate same-window media reopen can fail CreateSwapChainForHwnd.
    if (context_ != nullptr) { context_->ClearState(); context_->Flush(); }
    if (swapChain_ != nullptr) {
        std::lock_guard presentLock(presentMutex_);
        swapChain_->Release(); swapChain_ = nullptr;
    }
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
    sourceChromaWidthShift_ = sourceChromaHeightShift_ = 0;
}

FFF3FPColorMode PlayerVideoRenderer::ActualColorMode() const noexcept { return actualMode_; }
float PlayerVideoRenderer::SourcePeakNits() const noexcept { return sourcePeakNits_; }
std::uint64_t PlayerVideoRenderer::PresentedVideoFrames() const noexcept { return presentedVideoFrames_.load(); }
std::uint64_t PlayerVideoRenderer::CoalescedVideoFrames() const noexcept { return coalescedVideoFrames_.load(); }
std::uint64_t PlayerVideoRenderer::SwapChainPresents() const noexcept { return swapChainPresents_.load(); }
std::uint64_t PlayerVideoRenderer::PresentWait100ns() const noexcept { return presentWait100ns_.load(); }
std::uint64_t PlayerVideoRenderer::DeviceLockWait100ns() const noexcept { return deviceLockWait100ns_.load(); }
std::uint64_t PlayerVideoRenderer::SoftwareConvert100ns() const noexcept { return softwareConvert100ns_.load(); }
std::string PlayerVideoRenderer::FallbackReason() const { return fallbackReason_; }
std::string PlayerVideoRenderer::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
void PlayerVideoRenderer::SetError(std::string message) noexcept {
    try { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); } catch (...) {}
}
