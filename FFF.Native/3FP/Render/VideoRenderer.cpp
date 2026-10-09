#include "pch.h"
#include "3FP/Render/VideoRenderer.h"
#include "3FP/Render/ShaderBytecode.h"
#include "3FP/Render/SdrShaderBytecode.h"
#include "3FP/Render/ColorExtension.h"
#include "3FP/Hdr/HdrToneCurve.h"
#include "3FP/Render/NvidiaVideoExtensions.h"

extern "C" {
#include <libavcodec/codec_par.h>
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <d2d1effects.h>
#include <d2d1helper.h>
#include <DirectXPackedVector.h>
#include <roapi.h>
#include <windows.graphics.display.h>
#include <windows.graphics.display.interop.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace {
template<class T> void ReleaseCom(T*& resource) noexcept {
    if (resource != nullptr) { resource->Release(); resource = nullptr; }
}

const AVFrame* EnhancementFrame(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->opaque_ref == nullptr ||
        frame->opaque_ref->data == nullptr ||
        frame->opaque_ref->size < sizeof(FFFColorExtensionEnhancementFrameReference))
        return nullptr;
    const auto* reference = reinterpret_cast<const FFFColorExtensionEnhancementFrameReference*>(
        frame->opaque_ref->data);
    if (reference->magic != FFFColorExtensionEnhancementReferenceMagic ||
        reference->frame == nullptr)
        return nullptr;
    return static_cast<const AVFrame*>(reference->frame);
}

float DetectDisplayRefreshRate(const HWND window) noexcept {
    // Pace camera redraws to the monitor hosting the playback window.  A
    // 120 Hz ceiling keeps high-refresh displays responsive without creating
    // an unbounded presentation queue on faster panels.
    if (window == nullptr) return 60.0f;
    const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr) return 60.0f;
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return 60.0f;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0) ||
        mode.dmDisplayFrequency < 2) return 60.0f;
    return std::clamp(static_cast<float>(mode.dmDisplayFrequency), 60.0f, 120.0f);
}

// DISPLAYCONFIG SDR white = SDRWhiteLevel / 1000 * 80 nits.
// Use 72-byte path entries, QDC_DATABASE_CURRENT and the target id for the white-level query.
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
        float sdrWhite = 0.0f;
        if (FAILED(color->get_MinLuminanceInNits(&minimum)) ||
            FAILED(color->get_MaxLuminanceInNits(&maximum)) ||
            FAILED(color->get_MaxAverageFullFrameLuminanceInNits(&fullFrame))) break;
        if (!std::isfinite(maximum) || maximum <= 0.0f) break;
        capabilities.maximumNits = maximum;
        if (std::isfinite(minimum) && minimum >= 0.0f)
            capabilities.minimumNits = minimum;
        if (std::isfinite(fullFrame) && fullFrame > 0.0f)
            capabilities.maximumFullFrameNits = fullFrame;
        // Optional: older builds and non-Advanced-Color desktops report 0.
        // Failure here must not invalidate the luminance read above.
        if (SUCCEEDED(color->get_SdrWhiteLevelInNits(&sdrWhite)) &&
            std::isfinite(sdrWhite) && sdrWhite > 0.0f)
            capabilities.sdrWhiteLevelNits = sdrWhite;
        read = true;
    } while (false);
    if (shouldUninitialize) RoUninitialize();
    return read;
}

// SDK 28000 moves size into DISPLAYCONFIG_DEVICE_INFO_HEADER; older SDKs use
// the two-argument API. requires/if constexpr selects the installed SDK layout.
template <typename Packet>
bool QueryDisplayConfigInfo(Packet& packet) noexcept {
    if constexpr (requires { packet.header.size; }) {
        packet.header.size = sizeof(Packet);
        return DisplayConfigGetDeviceInfo(&packet.header) == ERROR_SUCCESS;
    } else {
        return DisplayConfigGetDeviceInfo(&packet.header, sizeof(Packet)) == ERROR_SUCCESS;
    }
}

// Same system value as AdvancedColorInfo::SdrWhiteLevelInNits, read through
// DISPLAYCONFIG instead of WinRT: no RoInitialize, works where the WinRT call
// briefly fails. Heeds the pitfalls recorded above (QDC_DATABASE_CURRENT only;
// GET_SDR_WHITE_LEVEL's header.id is the TARGET id).
bool ReadSdrWhiteLevelDisplayConfig(const HMONITOR monitor, float& nits) noexcept {
    nits = 0.0f;
    if (monitor == nullptr) return false;
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_DATABASE_CURRENT, &pathCount, &modeCount) != ERROR_SUCCESS)
        return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_DATABASE_CURRENT, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS)
        return false;
    for (UINT32 index = 0; index < pathCount; ++index) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.adapterId = paths[index].sourceInfo.adapterId;
        source.header.id = paths[index].sourceInfo.id;
        if (!QueryDisplayConfigInfo(source)) continue;
        if (_wcsicmp(source.viewGdiDeviceName, monitorInfo.szDevice) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL sdr{};
        sdr.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        sdr.header.adapterId = paths[index].sourceInfo.adapterId;
        sdr.header.id = paths[index].targetInfo.id;
        if (!QueryDisplayConfigInfo(sdr)) return false;
        const auto value = static_cast<float>(sdr.SDRWhiteLevel) / 1000.0f * 80.0f;
        if (std::isfinite(value) && value > 0.0f) {
            nits = value;
            return true;
        }
        return false;
    }
    return false;
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

bool IsP3Primaries(const AVFrame* frame) noexcept {
    return frame->color_primaries == AVCOL_PRI_SMPTE431 ||
        frame->color_primaries == AVCOL_PRI_SMPTE432;
}

// Regenerate ShaderBytecode.h with tools/generate_shader_bytecode.py after HLSL changes.
// Gamut is tri-state (709/2020/P3); source and bytecode must stay in sync.

// Gamut: 0=709, 1=2020, 2=P3. Test primaries before matrix tags:
// P3 video commonly uses BT2020_NCL matrix coefficients.
std::uint32_t ResolveSourceGamut(const AVFrame* frame, const bool hdrSource) noexcept {
    if (IsP3Primaries(frame)) return 2u;
    if (hdrSource || IsRec2020(frame)) return 1u;
    return 0u;
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

// Display P3 / DCI-P3 (D65 white point) to linear Rec.709. A pure matrix like
// Convert2020To709: P3 red sits outside Rec.709 and produces a negative green
// and blue, which is exactly what the FP16 scRGB swap chain can carry, so
// nothing is clamped here.
void ConvertP3To709(float& r, float& g, float& b) noexcept {
    const auto nr = 1.224810f * r - 0.224970f * g - 0.000025f * b;
    const auto ng = -0.042043f * r + 1.042084f * g + 0.000018f * b;
    const auto nb = -0.019642f * r - 0.078649f * g + 1.098527f * b;
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
    // Gamut: 0 = Rec.709, 1 = Rec.2020, 2 = P3 (DCI/Display).
    uint ColorMode; uint Transfer; uint Gamut; uint Reserved;
    float SdrPeak; float HdrPeak; float PaperWhite; float TargetPeak;
    float SourceWidth; float SourceHeight; float OutputWidth; float OutputHeight;
    uint InputLayout; float SampleScale; float YOffset; float YScale;
    float COffset; float CScale; float Kr; float Kb;
    float2 ChromaOffset; float2 ImageMode;
    uint Projection360; float ViewYaw; float ViewPitch; float ViewFovY;
    float ViewAspect; float ViewRotation; float3 ViewPadding;
};
#ifdef FFF_SDR_LAYOUT
// Compile-time specialization only: sampling and color formulas stay identical.
#define InputLayout FFF_SDR_LAYOUT
#define ColorMode 0
#define Transfer 0
#define Gamut 0
#define Reserved 0
#define Projection360 0
#endif
// ST 2094 (HDR10+ / HDR Vivid) dynamic tone mapping, bound at register(b2).
//
// Slot choice matters: register(b1) is already claimed by extensionConstants_
// (the Dolby Vision color-extension path binds it on every draw), so the
// dynamic curve state must not live there or the two would overwrite each
// other whenever a color extension is installed.
//
// This is a separate buffer on purpose: when a stream carries no dynamic
// metadata, DynamicEnabled stays 0 and every branch below falls through to the
// original BT.2390 path byte for byte. Keeping the new state out of the b0
// cbuffer means the existing path cannot be perturbed by layout changes.
//
// CurveLut holds the reconstructed per-window tone curve(s), sampled uniformly
// over x in [0,1]. WindowMin/Max carry the normalised rectangles used to pick
// which curve a pixel uses.
//
// The last row is a single float packed beside WindowMax2 because HLSL does not
// allow two scalars to share a 16-byte register row: writing
// `float2 WindowMin2; float2 WindowMax2; float WindowBlend;` silently widens the
// buffer and shifts every field after it.
cbuffer DynamicSettings : register(b2) {
    uint DynamicEnabled;      // 0 = use the original mapping only
    uint DynamicWindowCount;  // 1 or 3
    uint DynamicFlags;        // reserved, currently 0
    float WindowBlend;        // half-width of the cross-window transition band
    float DynamicSourcePeak;  // content peak for this frame (nits)
    float DynamicTargetPeak;  // the content's targeted display peak (nits)
    float2 DynamicPad0;
    float2 WindowMin0; float2 WindowMax0;
    float2 WindowMin1; float2 WindowMax1;
    float2 WindowMin2; float2 WindowMax2;
};
Texture2D<float4> CurveLut : register(t3);
Texture2D<float4> Source : register(t0);
Texture2D<float4> ChromaU : register(t1);
Texture2D<float4> ChromaV : register(t2);
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
SamplerState PanoramaSampler : register(s2);
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
// Display P3 / DCI-P3 (D65) to linear Rec.709. Not clamped: P3 red maps to a
// negative green and blue, which the FP16 scRGB swap chain carries verbatim.
float3 P3To709(float3 v) { return mul(float3x3(1.224810,-0.224970,-0.000025, -0.042043,1.042084,0.000018, -0.019642,-0.078649,1.098527),v); }
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
// Sample one ST 2094 window curve. The table is a 256-entry 1-D ramp laid out
// along x, holding the reconstructed mapping of the normalised PQ signal.
float SampleCurve(float windowIndex,float normalized) {
    float x=saturate(normalized);
    float texel=(x*255.0+0.5)/256.0;
    // windowIndex is a row number; the table is kHdrMaxProcessingWindows rows tall
    // (a C++ static_assert below pins that to the 3.0 here). Passing the raw index
    // clamps rows 1 and 2 onto the last row, which handed the middle window the
    // third window's curve.
    return CurveLut.SampleLevel(LinearSampler,float2(texel,(windowIndex+0.5)/3.0),0).r;
}
// Resolve the ST 2094 window for this pixel and evaluate its curve.
//
// Windows do not tile the frame seamlessly in practice: adjacent windows carry
// independently mastered parameters, so switching at a hard edge draws a visible
// seam. The weight therefore ramps across a band of width WindowBlend *centred
// on the border*, so that on the border itself both neighbours contribute half.
//
// Centring matters. An earlier version ramped from the window's own edge inward
// (`smoothstep(min, min+blend, uv)`), which meant that at the border the outgoing
// window had already fallen to 0 while the incoming one had not yet risen: the
// weights never overlapped, the total collapsed toward 0 and the picture showed
// a step. Each window's ramp must extend *outside* its own rectangle so the sum
// stays ~1 everywhere.
float DynamicCurveWeight(float2 uv,float2 minimum,float2 maximum,out float weight) {
    // Eight-wide smoothstep, so the ramp is symmetric about each border:
    // the lower edge ramps over [min-blend, min+blend], the upper over
    // [max-blend, max+blend]. Adjacent windows therefore cross-fade exactly.
    const float2 lower=smoothstep(minimum-WindowBlend,minimum+WindowBlend,uv);
    const float2 upper=1.0-smoothstep(maximum-WindowBlend,maximum+WindowBlend,uv);
    weight=saturate(min(lower.x,lower.y)*min(upper.x,upper.y));
    return weight;
}
float EvaluateDynamicCurve(float2 uv,float normalized) {
    if(DynamicWindowCount<=1)
        return SampleCurve(0.0,normalized);
    float weight0,weight1,weight2;
    DynamicCurveWeight(uv,WindowMin0,WindowMax0,weight0);
    DynamicCurveWeight(uv,WindowMin1,WindowMax1,weight1);
    DynamicCurveWeight(uv,WindowMin2,WindowMax2,weight2);
    const float total=weight0+weight1+weight2;
    // A pixel can legitimately fall outside every rectangle once the ramps are
    // symmetric (the corners of a multi-window layout, for instance). Falling
    // back to window 0 there is right: window 0 is the full-frame window in the
    // single-window shape, and in the three-window shape it is the left region,
    // which is no worse than the static mapping.
    if(total<=0.000001)
        return SampleCurve(0.0,normalized);
    const float c0=SampleCurve(0.0,normalized);
    const float c1=SampleCurve(1.0,normalized);
    const float c2=SampleCurve(2.0,normalized);
    return (c0*weight0+c1*weight1+c2*weight2)/total;
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
int WrapPanoramaX(int coordinate,uint width) {
    int wrapped=coordinate%int(width);
    return wrapped<0?wrapped+int(width):wrapped;
}
float4 LoadPanorama(Texture2D<float4> sourceTexture,int2 coordinate,uint2 dimensions) {
    int2 wrapped=int2(WrapPanoramaX(coordinate.x,dimensions.x),
        clamp(coordinate.y,0,int(dimensions.y)-1));
    return sourceTexture.Load(int3(wrapped,0));
}
float4 SampleLanczos3Panorama(Texture2D<float4> sourceTexture,float2 uv,uint2 dimensions) {
    float2 position=float2(frac(uv.x),saturate(uv.y))*float2(dimensions)-0.5;
    int2 origin=int2(floor(position));
    float2 fraction=position-float2(origin);
    float4 total=0.0;
    float weightTotal=0.0;
    [unroll] for(int y=-2;y<=3;++y) {
        const float wy=Lanczos3Weight(float(y)-fraction.y);
        [unroll] for(int x=-2;x<=3;++x) {
            const float weight=wy*Lanczos3Weight(float(x)-fraction.x);
            total+=LoadPanorama(sourceTexture,origin+int2(x,y),dimensions)*weight;
            weightTotal+=weight;
        }
    }
    const float4 filtered=total/max(abs(weightTotal),0.000001);
    const float4 a=LoadPanorama(sourceTexture,origin,dimensions);
    const float4 b=LoadPanorama(sourceTexture,origin+int2(1,0),dimensions);
    const float4 c=LoadPanorama(sourceTexture,origin+int2(0,1),dimensions);
    const float4 d=LoadPanorama(sourceTexture,origin+int2(1,1),dimensions);
    return clamp(filtered,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
}
float4 SamplePanorama(Texture2D<float4> sourceTexture,float2 uv) {
    uint width,height;
    sourceTexture.GetDimensions(width,height);
    return SampleLanczos3Panorama(sourceTexture,uv,uint2(width,height));
}
float4 SampleVideo(Texture2D<float4> sourceTexture,float2 uv) {
    uint width,height;
    sourceTexture.GetDimensions(width,height);
    const uint2 dimensions=uint2(width,height);
    if(abs(OutputWidth-float(width))<0.01&&abs(OutputHeight-float(height))<0.01)
        return sourceTexture.SampleLevel(PointSampler,uv,0);
    // Still images enlarged: each source pixel becomes a square block of output
    // pixels, so the picture keeps its own pixel grid and reads as a crisp
    // magnification instead of a smeared interpolation. ImageMode.x carries the flag.
    // Sampling is done with an explicit texel index rather than a point sampler so
    // the block edges land exactly on the source texel centres at any zoom factor.
    if(ImageMode.x>0.5) {
        const int2 texel=int2(floor(uv*float2(dimensions)));
        return sourceTexture.Load(int3(clamp(texel,int2(0,0),int2(dimensions)-1),0));
    }
    return SampleLanczos3(sourceTexture,uv,dimensions);
}
float3 ReadSource(float2 uv) {
    if(InputLayout==0||InputLayout==3)return SampleVideo(Source,uv).rgb;
    float2 chromaUv=uv+ChromaOffset;
    float y=SampleVideo(Source,uv).r*SampleScale;
    float2 chroma=InputLayout==1
        ?float2(SampleVideo(ChromaU,chromaUv).r,SampleVideo(ChromaV,chromaUv).r)*SampleScale
        :SampleVideo(ChromaU,chromaUv).rg*SampleScale;
#ifdef FFF_COLOR_EXTENSION
    if(ExtensionEnabled!=0&&ColorMode!=1)return ExtensionDecode(float3(y,chroma));
#endif
    y=(y-YOffset)*YScale;
    chroma=(chroma-COffset)*CScale;
    float kg=1.0-Kr-Kb;
    return float3(y+(2.0-2.0*Kr)*chroma.y,
        y-Kb*(2.0-2.0*Kb)/kg*chroma.x-Kr*(2.0-2.0*Kr)/kg*chroma.y,
        y+(2.0-2.0*Kb)*chroma.x);
}
float3 ReadSourceLinear(float2 uv) {
    if(InputLayout==0||InputLayout==3)return Source.Sample(LinearSampler,uv).rgb;
    float2 chromaUv=uv+ChromaOffset;
    float y=Source.Sample(LinearSampler,uv).r*SampleScale;
    float2 chroma=InputLayout==1
        ?float2(ChromaU.Sample(LinearSampler,chromaUv).r,
                ChromaV.Sample(LinearSampler,chromaUv).r)*SampleScale
        :ChromaU.Sample(LinearSampler,chromaUv).rg*SampleScale;
#ifdef FFF_COLOR_EXTENSION
    if(ExtensionEnabled!=0&&ColorMode!=1)return ExtensionDecode(float3(y,chroma));
#endif
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
float3 ReadSourcePanorama(float2 uv) {
    if(InputLayout==0||InputLayout==3)return SamplePanorama(Source,uv).rgb;
    float2 chromaUv=uv+ChromaOffset;
    float y=SamplePanorama(Source,uv).r*SampleScale;
    float2 chroma=InputLayout==1
        ?float2(SamplePanorama(ChromaU,chromaUv).r,
                SamplePanorama(ChromaV,chromaUv).r)*SampleScale
        :SamplePanorama(ChromaU,chromaUv).rg*SampleScale;
#ifdef FFF_COLOR_EXTENSION
    if(ExtensionEnabled!=0&&ColorMode!=1)return ExtensionDecode(float3(y,chroma));
#endif
    y=(y-YOffset)*YScale;
    chroma=(chroma-COffset)*CScale;
    float kg=1.0-Kr-Kb;
    return float3(y+(2.0-2.0*Kr)*chroma.y,
        y-Kb*(2.0-2.0*Kb)/kg*chroma.x-Kr*(2.0-2.0*Kr)/kg*chroma.y,
        y+(2.0-2.0*Kb)*chroma.x);
}
float2 EquirectangularUv(float2 uv) {
    const float radiansPerDegree=0.0174532925199433;
    const float tangent=tan(ViewFovY*radiansPerDegree*0.5);
    float3 direction=normalize(float3(
        (uv.x*2.0-1.0)*tangent*ViewAspect,
        (1.0-uv.y*2.0)*tangent,
        1.0));
    const float pitch=ViewPitch*radiansPerDegree;
    const float pitchCos=cos(pitch),pitchSin=sin(pitch);
    direction=float3(direction.x,
        direction.y*pitchCos+direction.z*pitchSin,
        -direction.y*pitchSin+direction.z*pitchCos);
    const float yaw=ViewYaw*radiansPerDegree;
    const float yawCos=cos(yaw),yawSin=sin(yaw);
    direction=float3(direction.x*yawCos+direction.z*yawSin,
        direction.y,-direction.x*yawSin+direction.z*yawCos);
    const float longitude=atan2(direction.x,direction.z);
    const float latitude=asin(clamp(direction.y,-1.0,1.0));
    return float2(frac(longitude/6.283185307179586+0.5),
        saturate(0.5-latitude/3.141592653589793));
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    // ViewRotation is quarter turns clockwise (0..3), applied by rotating the
    // sampling coordinate. Rotating the lookup rather than the quad keeps the
    // destination rect and the chroma layout untouched: every downstream stage
    // (chroma offsets, 360 projection, tone mapping) still sees a normal 0..1 uv.
    // The rotated value goes in a local: an HLSL pixel-shader input parameter
    // cannot be written (inout would turn TEXCOORD0 into an output semantic).
    float2 sampleUv=uv;
    if(ViewRotation>0.5){
        if(ViewRotation<1.5)      sampleUv=float2(1.0-uv.y,uv.x);      // 90 cw
        else if(ViewRotation<2.5) sampleUv=float2(1.0-uv.x,1.0-uv.y);  // 180
        else                      sampleUv=float2(uv.y,1.0-uv.x);      // 270 cw
    }
    float3 rgb=Reserved==1?ReadCoverBackdrop(sampleUv):
        (Projection360!=0?ReadSourcePanorama(EquirectangularUv(sampleUv)):ReadSource(sampleUv));    if(ColorMode==1)return float4(rgb,1);
    // Transfer==3: the source is already linear light (JPEG XR decodes to scRGB, whose
    // gamma is 1.0 -- see jxrlib JXRGlue.h, "scRGB formats. Gamma is 1.0").
    // Running the sRGB decode below would both apply the wrong curve and re-clamp the
    // negatives/super-whites through saturate(), so pass the value through untouched.
    // The SDR chain below still folds Rec.2020 and scales by PaperWhite, which is the
    // correct treatment for linear input.
    if(Transfer==3){
        float3 alreadyLinear=rgb;
        if(ColorMode==0){
            if(Gamut==1)alreadyLinear=ToBt709(To709(alreadyLinear));
            // No saturate() here on purpose: this is the SDR chain, and its surface is
            // UNORM -- R10G10B10A2 for sources deeper than 8-bit, B8G8R8A8 for 8-bit
            // ones (OutputBitDepthForSource never hands SDR a float surface). Neither
            // can hold >1.0 or <0.0, so let that conversion clamp: the limit stays a
            // visible property of the surface, not a hidden edit to the source.
            return float4(alreadyLinear,1);
        }
    }
    if(ColorMode==0&&Transfer==0){
        // Rec.2020 is folded into Rec.709 here because the SDR chain cannot hold
        // it. P3 keeps its historical Rec.709 passthrough on this path.
        if(Gamut==1)rgb=ToBt709(To709(ToLinear709(rgb)));
        return float4(rgb,1);
    }
    float3 nits=Transfer==1?PqToNits(rgb):(Transfer==2?HlgToNits(rgb):(Transfer==3?rgb:ToLinear709(rgb)*PaperWhite));
    if(ColorMode==2){
        // scRGB swap-chain contract: linear Rec.709 primaries, 1.0 = 80 nits.
        // Tone mapping is delegated to the display via the HDR metadata.
        // Gamut 2 (P3) uses its own matrix; both are pure linear maps whose
        // negative components FP16 carries as-is.
        float3 rec709Nits=Gamut==0?nits:(Gamut==1?To709(nits):P3To709(nits));
        return float4(rec709Nits/80.0,1);
    }
    if(Gamut==0)nits=To2020(nits);
    // BT.2390 operates on IPT intensity before Rec.2020-to-Rec.709 gamut
    // conversion. Chroma follows the reduced IPT gamut hull.
    if(DynamicEnabled!=0) {
        // ST 2094 dynamic metadata path, composed in two stages.
        //
        //   1. The reconstructed curve maps the *content* signal to what the
        //      content's own targeted display would show. This is the part that
        //      varies per frame (and per window).
        //   2. That targeted display is still not this display, so the result is
        //      compressed onto our SDR peak with the same BT.2390 mapping the
        //      static path uses.
        //
        // Applying the curve on its own (skipping step 2) would leave the source
        // range uncompressed and clip highlights to white; using the static
        // source peak alone (skipping step 1) is the behaviour this feature
        // exists to replace.
        //
        // Domain note: in this renderer `ipt.x` (the IPT intensity of the nits
        // vector) is the quantity Bt2390HdrToSdrPq consumes, exactly as the
        // static path does via ToneHdrToSdr. The curve is defined on that same
        // normalised signal, so multiplying back by sourcePq before the BT.2390
        // call keeps both stages in one domain.
        float3 ipt=Linear2020NitsToIpt(nits);
        const float originalIntensity=ipt.x;
        const float sourcePeak=max(DynamicSourcePeak,1.0);
        const float sourcePq=NitsToPq(sourcePeak.xxx).r;
        float mapped=originalIntensity;
        if(sourcePq>0.000001) {
            const float normalized=saturate(originalIntensity/sourcePq);
            // Step 1: the per-window curve, in the normalised signal domain.
            mapped=EvaluateDynamicCurve(uv,normalized)*sourcePq;
            // Step 2: compress the content's targeted range onto this display.
            mapped=Bt2390HdrToSdrPq(mapped,DynamicTargetPeak,SdrPeak);
        }
        ipt.x=mapped;
        if(originalIntensity<=0.000001||mapped<=0.000001) {
            ipt.yz=0.0;
        } else {
            float2 hull=float2(IptChromaHull(originalIntensity),IptChromaHull(mapped));
            float chromaScale=saturate(min(mapped/originalIntensity,
                hull.y/max(hull.x,0.000001)));
            ipt.yz*=chromaScale;
        }
        float3 mapped2020=IptToLinear2020Nits(ipt);
        float3 sdr=ToBt709(To709(mapped2020)/SdrPeak);
        return float4(sdr,1);
    }
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
    float SourceWidth; float SourceHeight; float OutputWidth; float OutputHeight;
};
Texture2D<float4> Backdrop : register(t0);
SamplerState LinearSampler : register(s0);
float LinearOne(float v) { v=saturate(v); return v<0.081 ? v/4.5 : pow((v+0.099)/1.099,1.0/0.45); }
float3 ToLinear709(float3 v) { return float3(LinearOne(v.r),LinearOne(v.g),LinearOne(v.b)); }
float2 CoverFillUv(float2 uv) {
    const float sourceAspect=SourceWidth/max(SourceHeight,1.0);
    const float outputAspect=OutputWidth/max(OutputHeight,1.0);
    if(sourceAspect>outputAspect)
        uv.x=(uv.x-0.5)*(outputAspect/sourceAspect)+0.5;
    else
        uv.y=(uv.y-0.5)*(sourceAspect/outputAspect)+0.5;
    return uv;
}
float4 main(float4 position:SV_Position,float2 uv:TEXCOORD0):SV_Target {
    float4 color=Backdrop.Sample(LinearSampler,CoverFillUv(uv));
    float4 tint=float4(float3((TintArgb>>16)&255u,(TintArgb>>8)&255u,TintArgb&255u),
                       float((TintArgb>>24)&255u))/255.0;
    if(ColorMode==2){
        // The FP16 backdrop is already scRGB (linear 709, 1.0 = 80 nits).
        // Convert only the sRGB tint before blending.
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
// How long a *swap chain* that refused scRGB stays refused, even though the display
// capability probe says yes. Must be much longer than the probe cache: the two answer
// different questions, and re-asking a chain that already answered "no" costs a full
// chain re-creation each time (see OutputSupportsHdr).
constexpr auto ScRgbChainRejectionBackoff = std::chrono::seconds(10);

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
    // gamut: 0 = Rec.709, 1 = Rec.2020, 2 = P3 (DCI/Display).
    std::uint32_t colorMode, transfer, gamut, reserved;
    float sdrPeak, hdrPeak, paperWhite, targetPeak;
    float sourceWidth, sourceHeight, outputWidth, outputHeight;
    std::uint32_t inputLayout;
    float sampleScale, yOffset, yScale;
    float cOffset, cScale, kr, kb;
    // imageModeX drives the still-image pixel-block enlargement in SampleVideo;
    // padding2 stays reserved. Both reuse what the HLSL cbuffer calls ImageMode.
    float chromaOffsetX, chromaOffsetY, imageModeX, padding2;
    std::uint32_t projection360;
    float viewYaw, viewPitch, viewFovY;
    // viewRotation holds quarter turns clockwise (0..3). It reuses what used to be
    // padding, so the constant buffer layout (and the HLSL cbuffer above) is
    // unchanged in size -- only the meaning of one slot moved.
    float viewAspect, viewRotation, padding4, padding5;
};

struct ScaleShaderSettings {
    float sourceWidth, sourceHeight, destinationWidth, destinationHeight;
    std::uint32_t axis, filter;
    float padding1, padding2;
};
static_assert(sizeof(ScaleShaderSettings) == 32);

// Curve table width and the b2 cbuffer mirror now live in VideoRenderer.h so
// the class method signature can name the settings type.

// True when any HDR10 metadata field moved enough to be worth re-submitting.
//
// The luminance fields are the ones ST 2094 varies per scene. A threshold of
// ~2% keeps genuinely different scenes apart while ignoring the last-bit
// wigggle of a statically graded stream, so DWM is not asked to re-tone-map on
// every frame.
bool HdrMetadataChanged(const DXGI_HDR_METADATA_HDR10& previous,
    const DXGI_HDR_METADATA_HDR10& next) noexcept {
    const auto moved = [](const std::uint32_t a, const std::uint32_t b) noexcept {
        const auto high = std::max(a, b);
        if (high == 0) return false;
        const auto low = std::min(a, b);
        return (high - low) * 100u > high * 2u;
    };
    if (moved(previous.MaxContentLightLevel, next.MaxContentLightLevel)) return true;
    if (moved(previous.MaxFrameAverageLightLevel, next.MaxFrameAverageLightLevel)) return true;
    if (moved(previous.MaxMasteringLuminance, next.MaxMasteringLuminance)) return true;
    if (previous.MinMasteringLuminance != next.MinMasteringLuminance) return true;
    for (int index = 0; index < 2; ++index) {
        if (previous.RedPrimary[index] != next.RedPrimary[index] ||
            previous.GreenPrimary[index] != next.GreenPrimary[index] ||
            previous.BluePrimary[index] != next.BluePrimary[index] ||
            previous.WhitePoint[index] != next.WhitePoint[index])
            return true;
    }
    return false;
}

struct VideoDestination {
    // Zoom/pan may place the origin outside the back buffer. Keep it signed.
    std::int32_t x, y;
    std::uint32_t width, height;
};

constexpr VideoDestination CalculateVideoDestination(const std::uint32_t sourceWidth,
    const std::uint32_t sourceHeight, const std::uint32_t outputWidth,
    const std::uint32_t outputHeight, const bool limitToNativeSize = false) noexcept {
    if (sourceWidth == 0 || sourceHeight == 0 || outputWidth == 0 || outputHeight == 0)
        return {0, 0, 1, 1};
    if (limitToNativeSize && sourceWidth <= outputWidth && sourceHeight <= outputHeight)
        return {static_cast<std::int32_t>((outputWidth - sourceWidth) / 2),
            static_cast<std::int32_t>((outputHeight - sourceHeight) / 2),
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
    return {static_cast<std::int32_t>((outputWidth - width) / 2),
        static_cast<std::int32_t>((outputHeight - height) / 2), width, height};
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
    return {static_cast<std::int32_t>(regionWidth - rightPadding - inner.width),
        static_cast<std::int32_t>(verticalPadding) + inner.y,
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

// Filter identifiers understood by ScalePixelShader's ScaleWeight().
enum class ScaleKernel : std::uint32_t {
    Cubic = 0,      // Catmull-Rom cubic, radius 1 -- sharp, cheapest
    Lanczos3 = 1,   // radius 3 -- sharpest reconstruction, best anti-aliasing
    Block = 2,      // radius 0.5 -- enlarges by pixel blocks
};

constexpr std::uint32_t ToShaderFilter(const ScaleKernel kernel) noexcept {
    return static_cast<std::uint32_t>(kernel);
}

// Planar/semi-planar CPU uploads switch from Map(WRITE_DISCARD) to
// UpdateSubresource above this size. Map's write-combined memory is fine for
// small frames but degrades as rows widen: at 8K the same copy costs 22.27 ms
// against 15.21 ms, while at 1080p the two are within 6%. 4K is the crossover --
// it already favours UpdateSubresource (2.886 vs 3.293 ms), so the threshold sits
// there rather than higher.
constexpr std::uint32_t DefaultUploadWidthThreshold = 3840;
constexpr std::uint32_t DefaultUploadHeightThreshold = 2160;

constexpr bool UseDefaultUploadForSize(const std::uint32_t width,
    const std::uint32_t height) noexcept {
    return width >= DefaultUploadWidthThreshold && height >= DefaultUploadHeightThreshold;
}

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
    // Float RGB (JPEG XR decodes to rgbaf16le). This gets its OWN layout value
    // rather than only a bitDepth, because downstream picks the shader-visible
    // texture format from `layout`: layout 0 would give R16G16B16A16_UNORM, which
    // cannot represent the >1.0 and <0.0 components that float RGB legitimately
    // carries (measured saturation 2.79%-10.73% across the sample set).
    case AV_PIX_FMT_RGBAF16LE:
        return {3, 16, 1.0f, 0, 0};
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
    const float width, const float height, const float fontSize) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    // The managed producer may assign a new content id while a scrolling item
    // is rebuilt. Cache the immutable glyph inputs instead of that transport id;
    // otherwise every animation tick rasterizes the same text again.
    if (command.content) {
        for (const auto value : command.content->text) HashTimedText(hash, value);
        for (const auto value : command.content->fontFamily) HashTimedText(hash, value);
    }
    HashTimedText(hash, std::bit_cast<std::uint32_t>(fontSize));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(width));
    HashTimedText(hash, std::bit_cast<std::uint32_t>(height));
    HashTimedText(hash, static_cast<std::uint32_t>(command.flags));
    HashTimedText(hash, static_cast<std::uint32_t>(command.horizontalAlignment));
    HashTimedText(hash, static_cast<std::uint32_t>(command.verticalAlignment));
    return hash == 0 ? 1 : hash;
}

std::uint64_t TimedTextSpriteKey(const TimedTextRenderCommand& command,
    const float width, const float height, const float fontSize, const float outline,
    const float shadowX, const float shadowY) noexcept {
    auto hash = TimedTextLayoutKey(command, width, height, fontSize);
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
        const float shadowX, const float shadowY, D2D1_RECT_F* inkBounds = nullptr) noexcept
        : factory_(factory), context_(context), outlineBrush_(outlineBrush),
          shadowBrush_(shadowBrush), outline_(outline), shadowX_(shadowX), shadowY_(shadowY),
          inkBounds_(inkBounds) {}

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
        if (inkBounds_ != nullptr) {
            D2D1_RECT_F bounds{};
            const auto result = path->GetWidenedBounds(outline_ * 2.0f, nullptr,
                D2D1::Matrix3x2F::Translation(baselineX, baselineY), &bounds);
            if (FAILED(result)) return result;
            inkBounds_->left = std::min(inkBounds_->left, bounds.left);
            inkBounds_->top = std::min(inkBounds_->top, bounds.top);
            inkBounds_->right = std::max(inkBounds_->right, bounds.right);
            inkBounds_->bottom = std::max(inkBounds_->bottom, bounds.bottom);
            return S_OK;
        }
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
    D2D1_RECT_F* inkBounds_;
};
}

FFFResult EvaluateVideoColorTransform(FFF3FPColorTransform& transform) noexcept {
    if (transform.size < sizeof(transform) || transform.version != 1 ||
        transform.colorMode > FFF3FPColorMode::MapToHdr ||
        // source2020 is a gamut switch: 0 = Rec.709, 1 = Rec.2020, 2 = P3.
        transform.transfer > FFF3FPColorTransfer::Hlg || transform.source2020 > 2 ||
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
            // Only Rec.2020 is folded here. A P3 source keeps its current
            // Rec.709 passthrough on the SDR chain: folding P3 into Rec.709 is a
            // separate decision, not something this switch silently enables.
            if (transform.source2020 == 1) {
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
                if (transform.source2020 == 1)
                    Convert2020To709(nits.r, nits.g, nits.b);
                else if (transform.source2020 == 2)
                    ConvertP3To709(nits.r, nits.g, nits.b);
                // No clamping - same contract as the production shader: P3
                // sources legitimately produce negative components and the FP16
                // scRGB swap chain carries them.
                rgb = {nits.r / 80.0f, nits.g / 80.0f, nits.b / 80.0f};
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

// Process log sink, defined in 3FP/Api/PlayerApi.cpp and forwarded to the callback
// installed through FFF3FP_SetLogCallback. Declared here rather than included so the
// renderer keeps no dependency on the API translation unit; it is a plain C++ symbol
// instead of FFF3FP_API, so referencing it adds nothing to the DLL's export table.
void FFF3FP_KernelLogImpl(const char* utf8Line) noexcept;

PlayerVideoRenderer::PlayerVideoRenderer(std::function<void()> recoveryCallback) noexcept
    : window_(nullptr), device_(nullptr), context_(nullptr), swapChain_(nullptr),
      vertexShader_(nullptr), pixelShader_(nullptr), coverBackdropPixelShader_(nullptr),
      timedTextPixelShader_(nullptr), scalePixelShader_(nullptr), sampler_(nullptr),
      pointSampler_(nullptr), panoramaSampler_(nullptr), constants_(nullptr), scaleConstants_(nullptr),
      sourceTextures_{nullptr, nullptr, nullptr}, sourceViews_{nullptr, nullptr, nullptr},
      scaledVideoGeneration_(UINT64_MAX), scaledOutputWidth_(0), scaledOutputHeight_(0),
      scaledVideoSuperResolution_(false),
      scaledSourceViews_{nullptr, nullptr, nullptr},
      videoDevice_(nullptr), videoContext_(nullptr), videoProcessorEnumerator_(nullptr),
      videoProcessor_(nullptr), videoProcessorRenderTexture_(nullptr),
      superResolutionTexture_(nullptr), superResolutionViews_{nullptr, nullptr, nullptr},
      effectiveSourceViews_{nullptr, nullptr, nullptr},
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
      swapWidth_(0), swapHeight_(0), swapHdr_(false), swapAllowTearing_(false), swapOutputBits_(8), sourceWidth_(0), sourceHeight_(0),
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
      paperWhiteNits_(203.0f), sdrWhiteLevelNits_(0.0f), sourcePeakNits_(100.0f),
      viewZoomBits_(std::bit_cast<float>(1.0f)),
      sourceWideGamut_(false),
      viewPanXBits_(std::bit_cast<float>(0.0f)),
      viewPanYBits_(std::bit_cast<float>(0.0f)),
      projection360Enabled_(0),
      view360RedrawPending_(false),
      view360YawBits_(std::bit_cast<float>(0.0f)),
      view360PitchBits_(std::bit_cast<float>(0.0f)),
      view360FovYBits_(std::bit_cast<float>(90.0f)),
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
      hasCachedVideo_(false), videoGeneration_(0), presentedVideoGeneration_(0), countedVideoGeneration_(0),
      presentedVideoFrames_(0), coalescedVideoFrames_(0), swapChainPresents_(0),
      presentWait100ns_(0), deviceLockWait100ns_(0), softwareConvert100ns_(0), upload100ns_(0),
      playbackWorkPending_(0),
      interactiveMove_(false),
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
      forceHdrOutput_(false), sdrScRgbMode_(0),
      hdrSupportCheckedAt_(std::chrono::steady_clock::time_point::min()),
      hdrSwapChainRejected_(false),
      timedTextAtlasX_(0), timedTextAtlasY_(0), timedTextAtlasRowHeight_(0),
      timedTextAtlasSize_(0), timedTextSpriteInstanceCapacity_(0),
      timedTextSpriteCacheHits_(0), timedTextSpriteCacheMisses_(0) {}

PlayerVideoRenderer::~PlayerVideoRenderer() { Close(); }

FFFResult PlayerVideoRenderer::SetWindow(const HWND window) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (window != nullptr && !IsWindow(window)) return FFFResult::InvalidArgument;
    if (window == window_) {
        // The same HWND can move between monitors while the player remains
        // open. Refresh the pacing contract and wake the presenter so a
        // 120 Hz monitor is picked up without rebuilding the media session.
        {
            std::lock_guard lock(timedTextMutex_);
            presentationFrameRate_ = DetectDisplayRefreshRate(window_);
            ++presentationGeneration_;
        }
        timedTextCondition_.notify_one();
        return FFFResult::Success;
    }
    if (swapChain_ != nullptr) {
        std::lock_guard presentLock(presentMutex_);
        swapChain_->Release(); swapChain_ = nullptr;
    }
    window_ = window;
    {
        std::lock_guard lock(timedTextMutex_);
        presentationFrameRate_ = DetectDisplayRefreshRate(window_);
    }
    // A generation presented to the previous HWND says nothing about the new
    // swap chain. Reset only the acknowledgement; submitted video remains
    // cached and will be presented again by Redraw.
    presentedVideoGeneration_.store(0, std::memory_order_release);
    hdrSupportValid_ = false; hdrMonitor_ = nullptr;
    hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
    hdrSwapChainRejected_ = false;
    swapWidth_ = swapHeight_ = 0;
    swapHdr_ = false; swapOutputBits_ = 8;
    swapAllowTearing_ = false;
    // Probe output capability lazily on the worker. HDR, wide gamut and
    // high-bit-depth Auto SDR use the same gate as SetColorMode/EnsureSwapChain.
    if (requestedMode_ == FFF3FPColorMode::MapToHdr) {
        const auto wantsHdrPath = WantsScRgbPresentationPath(sourceBitDepth_);
        actualMode_ = wantsHdrPath ? FFF3FPColorMode::MapToHdr :
            FFF3FPColorMode::MapToSdr;
        try { std::lock_guard fallbackLock(fallbackMutex_);
            fallbackReason_ = wantsHdrPath ? std::string{} :
                "True HDR output is only available for HDR or wide-gamut sources.";
        } catch (...) {}
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetPreferredAdapterIndex(const std::int32_t index) noexcept {
    if (index < -1 || index > 15) return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    preferredAdapterIndex_.store(index, std::memory_order_release);
    // Adapter preference takes effect at the next device creation.
    return FFFResult::Success;
}

void PlayerVideoRenderer::SetInteractiveMove(const bool enabled) noexcept {
    interactiveMove_.store(enabled, std::memory_order_release);
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

FFFResult PlayerVideoRenderer::WarmDevice() noexcept {
    // Same entry point the first Render uses, so nothing here can put the renderer into
    // a state Render would not have reached on its own.
    std::lock_guard deviceLock(deviceMutex_);
    return EnsureDevice();
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

void PlayerVideoRenderer::ViewTransform(float& zoom, float& panX, float& panY) const noexcept {
    // Read back in one place so a host that zooms through ZoomViewAt can keep its own
    // cached zoom in step without a second API to query the anchor result.
    zoom = std::bit_cast<float>(viewZoomBits_.load(std::memory_order_relaxed));
    panX = std::bit_cast<float>(viewPanXBits_.load(std::memory_order_relaxed));
    panY = std::bit_cast<float>(viewPanYBits_.load(std::memory_order_relaxed));
}

FFFResult PlayerVideoRenderer::ZoomViewAt(const float factor,
    const float anchorX, const float anchorY, float* const resultingZoom) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0f ||
        !std::isfinite(anchorX) || !std::isfinite(anchorY) ||
        anchorX < 0.0f || anchorX > 1.0f || anchorY < 0.0f || anchorY > 1.0f)
        return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);

    // Keep the point under the cursor fixed while the scale changes. A host doing this
    // itself would have to reconstruct the fitted box, the zoomed box and the pan
    // mapping, all of which live here -- and getting the arithmetic subtly wrong is what
    // makes wheel zoom feel like it drifts. The work is the same computation the draw
    // path already performs, so the result stays consistent with what is on screen.
    const auto previousZoom = std::bit_cast<float>(viewZoomBits_.load(std::memory_order_relaxed));
    const auto previousPanX = std::bit_cast<float>(viewPanXBits_.load(std::memory_order_relaxed));
    const auto previousPanY = std::bit_cast<float>(viewPanYBits_.load(std::memory_order_relaxed));
    const auto nextZoom = std::clamp(previousZoom * factor, 0.05f, 64.0f);
    if (resultingZoom != nullptr) *resultingZoom = previousZoom;
    if (nextZoom == previousZoom) return FFFResult::Success;

    // The anchor is expressed in viewport space; convert it to a point in the source
    // picture using the geometry the last frame was drawn with.
    const auto swapWidth = std::max(1u, swapWidth_);
    const auto swapHeight = std::max(1u, swapHeight_);
    const auto layoutWidth = (viewRotation_.load(std::memory_order_acquire) & 1u) != 0
        ? sourceHeight_ : sourceWidth_;
    const auto layoutHeight = (viewRotation_.load(std::memory_order_acquire) & 1u) != 0
        ? sourceWidth_ : sourceHeight_;
    if (resultingZoom != nullptr) *resultingZoom = nextZoom;
    if (layoutWidth == 0 || layoutHeight == 0) return FFFResult::Success;
    const auto fitted = CalculateVideoDestination(
        layoutWidth, layoutHeight, swapWidth, swapHeight,
        sourceLimitedToNativeSize_ || fitLimitToNative_.load(std::memory_order_acquire));
    const auto fittedWidth = static_cast<float>(fitted.width);
    const auto fittedHeight = static_cast<float>(fitted.height);
    if (resultingZoom != nullptr) *resultingZoom = nextZoom;
    if (fittedWidth <= 0.0f || fittedHeight <= 0.0f) return FFFResult::Success;

    const auto zoomedWidth = std::max(1.0f, fittedWidth * previousZoom);
    const auto zoomedHeight = std::max(1.0f, fittedHeight * previousZoom);
    const auto travelX = std::abs(zoomedWidth - fittedWidth);
    const auto travelY = std::abs(zoomedHeight - fittedHeight);
    if (travelX < 0.5f && travelY < 0.5f) {
        // Nothing to anchor against: the box does not move at this size.
        viewZoomBits_.store(std::bit_cast<float>(nextZoom), std::memory_order_relaxed);
        if (resultingZoom != nullptr) *resultingZoom = nextZoom;
        return FFFResult::Success;
    }

    // Where the anchor sits relative to the box centre *as currently drawn*. The box is
    // not at its fitted centre once pan is applied, so measuring from the fitted centre
    // alone misses the existing pan offset and the anchor slides on every step.
    const auto currentOffsetX = previousPanX * travelX * 0.5f;
    const auto currentOffsetY = previousPanY * travelY * 0.5f;
    const auto anchorPixelsX = anchorX * static_cast<float>(swapWidth);
    const auto anchorPixelsY = anchorY * static_cast<float>(swapHeight);
    const auto boxCentreX = static_cast<float>(fitted.x) + fittedWidth * 0.5f;
    const auto boxCentreY = static_cast<float>(fitted.y) + fittedHeight * 0.5f;
    // Displacement of the cursor from the drawn box centre, in screen pixels.
    const auto cursorFromDrawnCentreX = anchorPixelsX - (boxCentreX + currentOffsetX);
    const auto cursorFromDrawnCentreY = anchorPixelsY - (boxCentreY + currentOffsetY);

    // In the drawn box's own coordinates that same point is cursorFromDrawnCentre
    // regardless of scale, so after scaling the box by nextZoom / previousZoom the
    // required new pan follows directly.
    const auto scaleRatio = nextZoom / previousZoom;
    const auto nextZoomedWidth = std::max(1.0f, fittedWidth * nextZoom);
    const auto nextZoomedHeight = std::max(1.0f, fittedHeight * nextZoom);
    const auto nextTravelX = std::abs(nextZoomedWidth - fittedWidth);
    const auto nextTravelY = std::abs(nextZoomedHeight - fittedHeight);

    auto nextPanX = previousPanX;
    auto nextPanY = previousPanY;
    // Solve pan so that centre + pan * nextTravel / 2 lands on the same content point.
    if (nextTravelX > 0.5f)
        nextPanX = std::clamp((currentOffsetX + cursorFromDrawnCentreX * (scaleRatio - 1.0f)) /
            (nextTravelX * 0.5f), -1.0f, 1.0f);
    if (nextTravelY > 0.5f)
        nextPanY = std::clamp((currentOffsetY + cursorFromDrawnCentreY * (scaleRatio - 1.0f)) /
            (nextTravelY * 0.5f), -1.0f, 1.0f);

    viewZoomBits_.store(std::bit_cast<float>(nextZoom), std::memory_order_relaxed);
    viewPanXBits_.store(std::bit_cast<float>(nextPanX), std::memory_order_relaxed);
    viewPanYBits_.store(std::bit_cast<float>(nextPanY), std::memory_order_relaxed);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetViewRotation(
    const std::uint32_t quarterTurnsClockwise) noexcept {
    if (quarterTurnsClockwise > 3) return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    if (viewRotation_.exchange(quarterTurnsClockwise, std::memory_order_acq_rel) ==
        quarterTurnsClockwise)
        return FFFResult::Success;
    // The fit box depends on the rotated aspect ratio, so the scaled-video cache
    // keyed on the old output size must be invalidated.
    scaledVideoGeneration_ = UINT64_MAX;
    return FFFResult::Success;
}

std::uint32_t PlayerVideoRenderer::ViewRotation() const noexcept {
    return viewRotation_.load(std::memory_order_acquire);
}

FFFResult PlayerVideoRenderer::SetFitLimitToNative(const bool enable) noexcept {
    // Opt in to "the fit box never exceeds the source's native size". With it on,
    // zoom == 1 is pixel-exact 1:1 and the zoom factor *is* the screen:video pixel
    // ratio; by default the picture is fitted to the window, so zoom is relative to
    // that box and carries no absolute meaning.
    // Separate from Render()'s per-frame limitToNativeSize on purpose: that one is
    // only refreshed by the decode thread, so a paused session would keep the old
    // geometry until the next frame. This flips on the next present.
    std::lock_guard deviceLock(deviceMutex_);
    fitLimitToNative_.store(enable, std::memory_order_release);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetSdrScRgbMode(const std::uint32_t mode) noexcept {
    if (mode > 1) return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    // Changing the policy re-evaluates the gate on the next present: the mode is
    // recomputed by EnsureSwapChain, so no swap chain is torn down here.
    sdrScRgbMode_ = mode;
    hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
    hdrSwapChainRejected_ = false;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::SetColorMode(const FFF3FPColorMode mode, const float sdrPeakNits,
    const float hdrPeakNits, const float paperWhiteNits, const bool forceHdrOutput) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdrPeakNits) || sdrPeakNits <= 0.0f ||
        !std::isfinite(hdrPeakNits) || hdrPeakNits < 0.0f || hdrPeakNits > 10000.0f ||
        !std::isfinite(paperWhiteNits) || paperWhiteNits <= 0.0f) return FFFResult::InvalidArgument;
    requestedMode_ = mode;
    ReleaseScaleResources();
    sdrPeakNits_ = sdrPeakNits;
    // Zero selects the current display's reported peak. Positive values are a
    // future user setting and intentionally override the monitor descriptor.
    hdrPeakNits_ = hdrPeakNits;
    paperWhiteNits_ = paperWhiteNits;
    forceHdrOutput_ = forceHdrOutput;
    try { std::lock_guard fallbackLock(fallbackMutex_); fallbackReason_.clear(); } catch (...) {}
    actualMode_ = requestedMode_;
    hdrSupportCheckedAt_ = std::chrono::steady_clock::time_point::min();
    hdrSwapChainRejected_ = false;
    // Probe capability at swap-chain creation. HDR, wide gamut and high-bit-depth
    // Auto SDR need scRGB; the SDR swap chain cannot retain their range.
    if (requestedMode_ == FFF3FPColorMode::MapToHdr && !WantsScRgbPresentationPath(sourceBitDepth_)) {
        actualMode_ = FFF3FPColorMode::MapToSdr;
        try { std::lock_guard fallbackLock(fallbackMutex_);
            fallbackReason_ = "True HDR output is only available for HDR or wide-gamut sources.";
        } catch (...) {}
    }
    // hdrPeakNits_ is an output-display override, never the source mastering
    // peak. SDR callers pass zero; source peak metadata is configured per frame.
    hdrProcessor_.SetTargetPeakOverride(hdrPeakNits_);
    if (hasCachedVideo_) {
        cachedVideoSettings_.sdrPeak = sdrPeakNits_;
        cachedVideoSettings_.paperWhite = EffectivePaperWhiteNits();
        cachedVideoSettings_.targetPeak = hdrProcessor_.State().targetPeakNits;
    }
    const bool hdr = actualMode_ == FFF3FPColorMode::MapToHdr;
    const auto outputBits = PreferredOutputBitDepth(sourceBitDepth_, hdr);
    if (swapChain_ != nullptr && (swapHdr_ != hdr || swapOutputBits_ != outputBits)) {
        // Present and swap-chain reconfiguration must never overlap; hold
        // presentMutex_ across the rewrite like the render paths do.
        std::lock_guard presentLock(presentMutex_);
        const auto result = ReconfigureSwapChain(hdr, outputBits, sourceBitDepth_);
        if (result != FFFResult::Success) return result;
    }
    if (swapHdr_) SetHdrMetadata();
    return FFFResult::Success;
}

void PlayerVideoRenderer::ConfigureHdrStream(const AVCodecParameters* parameters) noexcept {
    // Widened primaries are a property of the stream, not of the transfer
    // function, and they decide whether an SDR swap chain can hold the picture
    // at all. Record them here: the colour mode may be selected before any
    // frame has been decoded.
    const auto primaries = parameters != nullptr ?
        parameters->color_primaries : AVCOL_PRI_UNSPECIFIED;
    sourceWideGamut_.store(primaries == AVCOL_PRI_BT2020 ||
        primaries == AVCOL_PRI_SMPTE431 || primaries == AVCOL_PRI_SMPTE432,
        std::memory_order_release);
    hdrProcessor_.ConfigureStream(parameters);
    hdrProcessor_.SetExtensionAvailability(IsColorExtensionAuthorized());
    // These two flags are read under deviceMutex_ in EnsurePipeline; clear them
    // under the same lock or a concurrent pipeline build can re-enter the
    // extension-shader creation block and overwrite (leak) the live shader.
    { std::lock_guard deviceLock(deviceMutex_);
        extensionAttempted_ = false;
        extensionEligible_ = false;
    }
}

FFFResult PlayerVideoRenderer::ForceSdrOutputForSdrSource() noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    requestedMode_ = FFF3FPColorMode::MapToSdr;
    actualMode_ = FFF3FPColorMode::MapToSdr;
    if (swapChain_ != nullptr && swapHdr_) {
        // Same invariant as SetColorMode: hold presentMutex_ across the rewrite.
        std::lock_guard presentLock(presentMutex_);
        const auto result = ReconfigureSwapChain(false,
            PreferredOutputBitDepth(sourceBitDepth_, false), sourceBitDepth_);
        if (result != FFFResult::Success) return result;
    }
    try { std::lock_guard fallbackLock(fallbackMutex_); fallbackReason_.clear(); } catch (...) {}
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsureDevice() noexcept {
    if (device_ != nullptr) return FFFResult::Success;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selected{};
    ComPtr<IDXGIAdapter1> selectedAdapter;
    // An explicitly requested adapter outranks the
    // monitor match. Any enumeration failure (bad index, no factory) leaves
    // selectedAdapter null so the built-in policy below still runs — the caller
    // never sees a hard failure just because a GPU preference could not be honoured.
    const auto preferredAdapterIndex =
        preferredAdapterIndex_.load(std::memory_order_acquire);
    if (preferredAdapterIndex >= 0) {
        ComPtr<IDXGIFactory6> preferredFactory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&preferredFactory)))) {
            preferredFactory->EnumAdapters1(
                static_cast<UINT>(preferredAdapterIndex), &selectedAdapter);
        }
    }
    if (!selectedAdapter && window_ != nullptr && IsWindow(window_)) {
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

    // Report the actual adapter to the debugger and public log sink.
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                // RTX Video Super Resolution is a vendor-private NVIDIA
                // extension; probe once per device so the status report can say
                // "not an NVIDIA adapter" instead of silently doing nothing.
                nvidiaAdapter_ =
                    desc.VendorId == FFF3FP::NvidiaVideo::NvidiaVendorId;
                // Fixed-size stack buffer: EnsureDevice() is noexcept, so a
                // std::string/std::to_string allocation here would turn an
                // OutOfMemory into std::terminate instead of a returned error.
                char line[192]{};
                _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "FFF.Native: device adapter requested=%d vendor=0x%04x device=0x%04x luid=%ld:%lu",
                    preferredAdapterIndex, desc.VendorId, desc.DeviceId,
                    static_cast<long>(desc.AdapterLuid.HighPart),
                    static_cast<unsigned long>(desc.AdapterLuid.LowPart));
                FFF3FP_KernelLogImpl(line);
                OutputDebugStringA(line);
                // The sink takes bare lines (the host adds its own framing), so the
                // terminator is emitted separately to keep debugger output one-per-line.
                OutputDebugStringA("\n");
            }
        }
    }

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
    const auto cachedUsable = [this](const HdrDisplayCapabilities&) noexcept {
        return forceHdrOutput_ || hdrSupported_;
    };
    if (!preservePrevious) hdrSwapChainRejected_ = false;
    // Display capability is re-probed periodically. Swap-chain rejection uses
    // longer backoff to avoid repeated rebuilds; policy/device/monitor changes reset it.
    if (preservePrevious && hdrSwapChainRejected_ &&
        now - hdrSupportCheckedAt_ < ScRgbChainRejectionBackoff)
        return false;
    hdrSwapChainRejected_ = false;
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
        return forceHdrOutput_;
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
                return forceHdrOutput_;
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
                capabilities.sdrWhiteLevelNits = previousCapabilities.sdrWhiteLevelNits;
            }
            // Partial success: WinRT luminance read fine but the SDR white level
            // came back 0/failed (driver transient). Keep the last known value
            // instead of dropping EffectivePaperWhiteNits() to the 80-nit
            // fallback mid-playback (visible brightness jump).
            if (capabilities.sdrWhiteLevelNits <= 0.0f && preservePrevious)
                capabilities.sdrWhiteLevelNits = previousCapabilities.sdrWhiteLevelNits;
            hdrProcessor_.SetDisplayCapabilities(capabilities);
            // SDR content brightness anchors an SDR picture presented on the
            // scRGB chain, so keep it in step with the capability probe.
            sdrWhiteLevelNits_ = capabilities.sdrWhiteLevelNits;
            if (sdrWhiteLevelNits_ <= 0.0f) {
                // The WinRT path did not report it (older Windows, brief
                // re-init): DISPLAYCONFIG carries the same value.
                float displayConfigNits = 0.0f;
                if (ReadSdrWhiteLevelDisplayConfig(monitor, displayConfigNits))
                    sdrWhiteLevelNits_ = displayConfigNits;
            }
            // Several TVs correctly expose the active 10-bit/PQ desktop but
            // leave every luminance field at zero. The HDR processor already
            // owns a conservative 1000-nit fallback for that case, so missing
            // luminance must not block the actual scRGB swap-chain attempt.
            return forceHdrOutput_ || hdrSupported_;
        }
    }
    if (preservePrevious) {
        hdrSupported_ = previousCapabilities.supported;
        hdrProcessor_.SetDisplayCapabilities(previousCapabilities);
        return cachedUsable(previousCapabilities);
    }
    hdrProcessor_.SetDisplayCapabilities({});
    return forceHdrOutput_;
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
        // Wide-gamut SDR also needs scRGB to retain colors outside Rec.709.
        const auto wantsHdrPath = WantsScRgbPresentationPath(sourceBitDepth);
        const auto nextMode = wantsHdrPath && OutputSupportsHdr() ?
            FFF3FPColorMode::MapToHdr : FFF3FPColorMode::MapToSdr;
        // Distinguish "this source does not need scRGB" from "the display cannot
        // give it scRGB": with the Auto SDR policy a 10-bit SDR source reaches
        // the second branch without being HDR or wide gamut at all.
        const auto reason = !wantsHdrPath ?
            "True HDR output is only available for HDR or wide-gamut sources." :
            (hdrProcessor_.IsHdrSource() || IsWideGamutSource() ?
                "The target display or Windows Advanced Color mode does not support true HDR output." :
                "Advanced Color output is not active, so this SDR source stays on the classic SDR swap chain.");
        try { std::lock_guard fallbackLock(fallbackMutex_);
            fallbackReason_ = nextMode == requestedMode_ ? std::string{} : reason;
        } catch (...) {}
        if (nextMode != actualMode_) {
            actualMode_ = nextMode;
            if (swapChain_ != nullptr) {
                const auto modeResult = ReconfigureSwapChain(
                    nextMode == FFF3FPColorMode::MapToHdr,
                    PreferredOutputBitDepth(sourceBitDepth, nextMode == FFF3FPColorMode::MapToHdr),
                    sourceBitDepth);
                if (modeResult != FFFResult::Success) return modeResult;
            }
        }
    }
    const auto deviceResult = EnsureDevice();
    if (deviceResult != FFFResult::Success) return deviceResult;
    RECT client{};
    if (!GetClientRect(window_, &client)) return FFFResult::DeviceFailure;
    const auto clientWidth = client.right - client.left;
    const auto clientHeight = client.bottom - client.top;
    // A minimized window and some WM_SIZE/WM_WINDOWPOSCHANGED transitions
    // temporarily expose a zero-sized client area. Flip-model swap chains do
    // not need a 1x1 resize here; defer it until the window has a real size.
    if (clientWidth <= 0 || clientHeight <= 0) return FFFResult::Success;
    width = static_cast<std::uint32_t>(clientWidth);
    height = static_cast<std::uint32_t>(clientHeight);
    const bool hdr = actualMode_ == FFF3FPColorMode::MapToHdr;
    const auto outputBits = PreferredOutputBitDepth(sourceBitDepth, hdr);
    if (swapChain_ != nullptr && (hdr != swapHdr_ || outputBits != swapOutputBits_)) {
        const auto modeResult = ReconfigureSwapChain(hdr, outputBits, sourceBitDepth);
        if (modeResult != FFFResult::Success) return modeResult;
    }
    if (swapChain_ != nullptr && width == swapWidth_ && height == swapHeight_ &&
        hdr == swapHdr_ && outputBits == swapOutputBits_) return FFFResult::Success;
    if (swapChain_ != nullptr && hdr == swapHdr_ && outputBits == swapOutputBits_) {
        context_->ClearState();
        const auto resizeFlags = swapAllowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
        const auto resize = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, resizeFlags);
        if (SUCCEEDED(resize)) {
            swapWidth_ = width; swapHeight_ = height;
            ReleaseTimedTextResources();
            return FFFResult::Success;
        }
        if (RequestRecoveryIfDeviceLostLocked()) return FFFResult::DeviceFailure;
        std::ostringstream message;
        message << "Could not resize the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(resize) << ").";
        SetError(message.str());
        return FFFResult::DeviceFailure;
    }
    return CreateSwapChain(width, height, hdr, outputBits, sourceBitDepth);
}

FFFResult PlayerVideoRenderer::CreateSwapChain(const std::uint32_t width,
    const std::uint32_t height, const bool hdr, const std::uint32_t outputBits,
    const std::uint32_t sourceBitDepth) noexcept {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        if (RequestRecoveryIfDeviceLostLocked()) return FFFResult::DeviceFailure;
        SetError("Could not obtain the DXGI playback factory."); return FFFResult::DeviceFailure;
    }
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5)))
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &allowTearing, sizeof(allowTearing));
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width; description.Height = height;
    description.Format = outputBits >= 16 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
        (outputBits >= 10 ? DXGI_FORMAT_R10G10B10A2_UNORM :
            DXGI_FORMAT_B8G8R8A8_UNORM);
    description.SampleDesc.Count = 1; description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 3; description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE; description.Scaling = DXGI_SCALING_NONE;
    description.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> chain1;
    const auto result = factory->CreateSwapChainForHwnd(device_, window_, &description, nullptr, nullptr, &chain1);
    if (FAILED(result) || FAILED(chain1->QueryInterface(IID_PPV_ARGS(&swapChain_)))) {
        if (RequestRecoveryIfDeviceLostLocked()) return FFFResult::DeviceFailure;
        std::ostringstream message;
        message << "Could not create the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(result) << ").";
        SetError(message.str()); return FFFResult::DeviceFailure;
    }
    swapWidth_ = width; swapHeight_ = height; swapHdr_ = hdr; swapOutputBits_ = outputBits;
    InvalidateHdrMetadataCache();
    swapAllowTearing_ = allowTearing != FALSE;
    ReleaseTimedTextResources();
    if (hdr) {
        UINT support = 0;
        const auto supportResult = swapChain_->CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support);
        if ((!forceHdrOutput_ && (FAILED(supportResult) ||
             (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)) ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))) {
            try { std::lock_guard fallbackLock(fallbackMutex_);
                fallbackReason_ = "The swap chain rejected the scRGB color space.";
            } catch (...) {}
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
                PreferredOutputBitDepth(sourceBitDepth, false), sourceBitDepth);
        }
        hdrSwapChainRejected_ = false;
        SetHdrMetadata();
    } else {
        // Keep a newly-created SDR swap chain on DXGI's default SDR contract.
        // Do not call SetColorSpace1 or SetHDRMetaData, even with NONE: either
        // call opts the window into an explicit Advanced Color presentation
        // contract instead of the ordinary SDR desktop path.
    }
    // This chain has no FRAME_LATENCY_WAITABLE_OBJECT flag, so IDXGISwapChain2's
    // latency API is invalid here (that call was silently failing before). The
    // device-level API has no such precondition.
    //
    // The value is 2 rather than the previous *intended* 1 or DXGI's default 3:
    // two permits one frame rendering while another waits on Present, which is the
    // overlap this path wants, and it stays strictly below the 3-deep buffer count
    // so the queue cannot outrun the chain. Measured via the queue-depth effect on
    // dropped frames.
    ComPtr<IDXGIDevice1> latencyDevice;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&latencyDevice))))
        latencyDevice->SetMaximumFrameLatency(2);
    {
        std::lock_guard lock(timedTextMutex_);
        presentationFrameRate_ = DetectDisplayRefreshRate(window_);
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::ReconfigureSwapChain(const bool hdr,
    const std::uint32_t outputBits, const std::uint32_t sourceBitDepth) noexcept {
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
        // The caller holds presentMutex_ for the entire rewrite (see
        // EnsureSwapChain/SetColorMode); do not relock it here.
        swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        swapChain_->Release();
        swapChain_ = nullptr;
        swapWidth_ = swapHeight_ = 0;
        swapHdr_ = false;
        swapOutputBits_ = 8;
        return CreateSwapChain(width, height, hdr, formatBits, sourceBitDepth);
    }
    const auto format = formatBits >= 16 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
        (formatBits >= 10 ? DXGI_FORMAT_R10G10B10A2_UNORM :
            DXGI_FORMAT_B8G8R8A8_UNORM);
    const auto resizeFlags = swapAllowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    const auto resize = swapChain_->ResizeBuffers(0, std::max(1u, swapWidth_),
        std::max(1u, swapHeight_), format, resizeFlags);
    if (FAILED(resize)) {
        if (RequestRecoveryIfDeviceLostLocked()) return FFFResult::DeviceFailure;
        std::ostringstream message;
        message << "Could not reconfigure the playback swap chain (HRESULT 0x" << std::hex
                << static_cast<std::uint32_t>(resize) << ").";
        SetError(message.str());
        return FFFResult::DeviceFailure;
    }
    swapHdr_ = hdr; swapOutputBits_ = formatBits;
    InvalidateHdrMetadataCache();
    if (hdr) {
        UINT support = 0;
        const auto supportResult = swapChain_->CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support);
        if ((!forceHdrOutput_ && (FAILED(supportResult) ||
             (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0)) ||
            FAILED(swapChain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))) {
            try { std::lock_guard fallbackLock(fallbackMutex_);
                fallbackReason_ = "The reconfigured swap chain rejected the scRGB color space.";
            } catch (...) {}
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
            return CreateSwapChain(width, height, false,
                PreferredOutputBitDepth(sourceBitDepth, false), sourceBitDepth);
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

void PlayerVideoRenderer::ReleaseOffscreenTarget() noexcept {
    if (offscreenTarget_ != nullptr) { offscreenTarget_->Release(); offscreenTarget_ = nullptr; }
    if (offscreenTexture_ != nullptr) { offscreenTexture_->Release(); offscreenTexture_ = nullptr; }
    offscreenWidth_ = offscreenHeight_ = offscreenFormat_ = 0;
}

FFFResult PlayerVideoRenderer::AcquireOffscreenTarget(const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t format,
    ID3D11Texture2D** texture, ID3D11RenderTargetView** target) noexcept {
    if (texture == nullptr || target == nullptr || device_ == nullptr)
        return FFFResult::InvalidArgument;
    *texture = nullptr; *target = nullptr;
    if (width == 0 || height == 0) return FFFResult::InvalidArgument;
    // 0 = BGRA8 (SDR), 1 = RGBA16F (linear scRGB, for HDR and wide-gamut SDR).
    const auto dxgiFormat = format == 1 ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                        : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (offscreenTexture_ != nullptr && offscreenWidth_ == width &&
        offscreenHeight_ == height && offscreenFormat_ == format) {
        *texture = offscreenTexture_;
        *target = offscreenTarget_;
        return FFFResult::Success;
    }
    ReleaseOffscreenTarget();
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = description.ArraySize = 1;
    description.Format = dxgiFormat;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&description, nullptr,
            &offscreenTexture_)) ||
        FAILED(device_->CreateRenderTargetView(offscreenTexture_, nullptr,
            &offscreenTarget_))) {
        ReleaseOffscreenTarget();
        SetError("Could not create the screenshot readback surface.");
        return FFFResult::DeviceFailure;
    }
    offscreenWidth_ = width;
    offscreenHeight_ = height;
    offscreenFormat_ = format;
    *texture = offscreenTexture_;
    *target = offscreenTarget_;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::ReadbackTexture(ID3D11Texture2D* const source,
    void* const pixels, const std::uint32_t capacity, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t format) noexcept {
    if (source == nullptr || pixels == nullptr || device_ == nullptr || context_ == nullptr)
        return FFFResult::InvalidArgument;
    // BGRA8 is 4 bytes per pixel, RGBA16F is 8. The byte count is what the host
    // sized its buffer with, so it is the contract that matters here.
    const std::uint64_t bytesPerPixel = format == 1 ? 8u : 4u;
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(width) * height * bytesPerPixel;
    if (bytes > capacity) return FFFResult::BufferTooSmall;

    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &staging)))
        return FFFResult::DeviceFailure;
    context_->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return FFFResult::DeviceFailure;
    const auto* rows = static_cast<const std::uint8_t*>(mapped.pData);
    auto* out = static_cast<std::uint8_t*>(pixels);
    if (format == 1) {
        // Source is DXGI_FORMAT_R16G16B16A16_FLOAT: HALF per channel, already in the
        // scRGB linear contract (1.0 = 80 nits), so this is a pure copy with the
        // RowPitch removed. Callers must NOT treat these as 8-bit code values.
        const auto rowBytes = static_cast<std::size_t>(width) * 8u;
        for (std::uint32_t row = 0; row < height; ++row)
            std::memcpy(out + static_cast<std::size_t>(row) * rowBytes,
                rows + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);
    } else {
        // Source is DXGI_FORMAT_B8G8R8A8_UNORM. Emit BGRA byte order so the caller
        // can wrap it as 32bpp with no per-pixel work (Windows bitmaps are BGRA).
        const auto rowBytes = static_cast<std::size_t>(width) * 4u;
        for (std::uint32_t row = 0; row < height; ++row)
            std::memcpy(out + static_cast<std::size_t>(row) * rowBytes,
                rows + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);
    }
    context_->Unmap(staging.Get(), 0);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::CopyFrame(void* const pixels,
    const std::uint32_t capacity, std::uint32_t& width, std::uint32_t& height,
    const std::uint32_t layout, const std::uint32_t format) noexcept {
    if (layout > 1 || format > 1) return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    if (!hasCachedVideo_ || device_ == nullptr || context_ == nullptr)
        return FFFResult::InvalidState;
    std::lock_guard presentLock(presentMutex_);

    // Source resolution honours the view rotation: an odd quarter turn presents the
    // picture with its axes swapped, and a screenshot of a rotated photo must come
    // out portrait rather than letterboxed into a landscape frame.
    const auto rotated = (viewRotation_.load(std::memory_order_acquire) & 1u) != 0;
    const auto sourceLayoutWidth = rotated ? sourceHeight_ : sourceWidth_;
    const auto sourceLayoutHeight = rotated ? sourceWidth_ : sourceHeight_;
    const auto targetWidth = layout == 1 ? swapWidth_ : sourceLayoutWidth;
    const auto targetHeight = layout == 1 ? swapHeight_ : sourceLayoutHeight;
    if (targetWidth == 0 || targetHeight == 0) return FFFResult::InvalidState;

    width = targetWidth;
    height = targetHeight;
    if (pixels == nullptr)
        return FFFResult::BufferTooSmall;   // size query: caller re-calls with a buffer

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    const auto acquired = AcquireOffscreenTarget(targetWidth, targetHeight, format,
        texture.GetAddressOf(), target.GetAddressOf());
    if (acquired != FFFResult::Success) return acquired;
    // AcquireOffscreenTarget hands back *borrowed* references to members it owns, and
    // those ComPtr locals would release them on scope exit -- dropping the refcount to
    // zero and leaving dangling members for the next call (measured as an access
    // violation on the second screenshot). Take our own reference for the duration.
    texture->AddRef();
    target->AddRef();

    constexpr float black[] = {0, 0, 0, 1};
    context_->ClearRenderTargetView(target.Get(), black);
    const auto drawn = DrawCachedVideo(target.Get(), targetWidth, targetHeight);
    if (drawn != FFFResult::Success) return drawn;
    // Subtitle/danmaku/lyrics layers are composited in the same order the presenter
    // uses, so the screenshot matches what the user sees rather than video only.
    const TimedTextLayerSlot slots[] = {TimedTextLayerSlot::Danmaku,
        TimedTextLayerSlot::Subtitle, TimedTextLayerSlot::Lyrics,
        TimedTextLayerSlot::Disc, TimedTextLayerSlot::PlayerInformation};
    for (const auto slot : slots) {
        const auto layerDrawn = DrawTimedText(slot);
        if (layerDrawn != FFFResult::Success) return layerDrawn;
        CompositeTimedText(target.Get(), slot);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);

    const auto read = ReadbackTexture(texture.Get(), pixels, capacity, targetWidth,
        targetHeight, format);
    if (read != FFFResult::Success) return read;
    finalReadbackFormat_ = format == 1 ? 16u : 8u;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::EnsurePipeline(const std::uint32_t sourceWidth,
    const std::uint32_t sourceHeight, const std::uint32_t inputLayout,
    const std::uint32_t bitDepth, const std::uint32_t chromaWidthShift,
    const std::uint32_t chromaHeightShift, const bool externalSource) noexcept {
    if (vertexShader_ == nullptr || pixelShader_ == nullptr || scalePixelShader_ == nullptr ||
        coverBackdropPixelShader_ == nullptr || timedTextPixelShader_ == nullptr ||
        sampler_ == nullptr || pointSampler_ == nullptr || panoramaSampler_ == nullptr ||
        constants_ == nullptr || scaleConstants_ == nullptr) {
        if (FAILED(device_->CreateVertexShader(FFFVertexShaderBytecode,
                sizeof(FFFVertexShaderBytecode), nullptr, &vertexShader_)) ||
            FAILED(device_->CreatePixelShader(FFFPixelShaderBytecode,
                sizeof(FFFPixelShaderBytecode), nullptr, &pixelShader_)) ||
            FAILED(device_->CreatePixelShader(FFFScalePixelShaderBytecode,
                sizeof(FFFScalePixelShaderBytecode), nullptr, &scalePixelShader_)) ||
            FAILED(device_->CreatePixelShader(FFFCoverBackdropPixelShaderBytecode,
                sizeof(FFFCoverBackdropPixelShaderBytecode), nullptr,
                &coverBackdropPixelShader_)) ||
            FAILED(device_->CreatePixelShader(FFFTimedTextPixelShaderBytecode,
                sizeof(FFFTimedTextPixelShaderBytecode), nullptr,
                &timedTextPixelShader_))) {
            SetError("Could not create the precompiled playback presentation shaders.");
            return FFFResult::DeviceFailure;
        }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        D3D11_SAMPLER_DESC pointSampler = sampler;
        pointSampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        D3D11_SAMPLER_DESC panoramaSampler = sampler;
        panoramaSampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(ShaderSettings); buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_BUFFER_DESC scaleBuffer = buffer;
        scaleBuffer.ByteWidth = sizeof(ScaleShaderSettings);
        if (FAILED(device_->CreateSamplerState(&sampler, &sampler_)) ||
            FAILED(device_->CreateSamplerState(&pointSampler, &pointSampler_)) ||
            FAILED(device_->CreateSamplerState(&panoramaSampler, &panoramaSampler_)) ||
            FAILED(device_->CreateBuffer(&buffer, nullptr, &constants_)) ||
            FAILED(device_->CreateBuffer(&scaleBuffer, nullptr, &scaleConstants_))) {
            SetError("Could not create the presentation shader resources."); return FFFResult::DeviceFailure;
        }
        // The ST 2094 resources are optional: if either fails, dynamicCurveBuilt_
        // stays false and the shader takes its original path. Creating them is
        // therefore deliberately outside the hard-failure check above.
        D3D11_BUFFER_DESC dynamicBuffer = buffer;
        dynamicBuffer.ByteWidth = sizeof(DynamicShaderSettings);
        if (SUCCEEDED(device_->CreateBuffer(&dynamicBuffer, nullptr, &dynamicConstants_))) {
            D3D11_TEXTURE2D_DESC curve{};
            curve.Width = kDynamicCurveLutWidth;
            curve.Height = kHdrMaxProcessingWindows;
            curve.MipLevels = curve.ArraySize = 1;
            curve.Format = DXGI_FORMAT_R32_FLOAT;
            curve.SampleDesc.Count = 1;
            curve.Usage = D3D11_USAGE_DEFAULT;
            curve.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (SUCCEEDED(device_->CreateTexture2D(&curve, nullptr, &dynamicCurveTexture_)) &&
                SUCCEEDED(device_->CreateShaderResourceView(
                    dynamicCurveTexture_, nullptr, &dynamicCurveView_))) {
                dynamicCurveBuilt_ = true;
            }
        }
    }
    // Do not make ordinary SDR playback pay for private test shader setup.
    // Stream classification is already updated by ProcessFrame before the
    // pipeline is requested, and late RPU side data will retry on that frame.
    if (!extensionAttempted_ && extensionEligible_) {
        extensionAttempted_ = true;
        if (const auto* api = GetColorExtension()) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = api->constantsSize;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const unsigned char zero[FFFColorExtensionCapacity]{};
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = zero;
            if (FAILED(device_->CreatePixelShader(api->shaderBytecode,
                    api->shaderBytecodeSize, nullptr, &extensionShader_)) ||
                FAILED(device_->CreateBuffer(&desc, &initial, &extensionConstants_))) {
                    ReleaseCom(extensionShader_);
            }
        }
    }
    if (sourceTextures_[0] != nullptr && sourceExternal_ == externalSource &&
        sourceWidth_ == sourceWidth && sourceHeight_ == sourceHeight &&
        sourceInputLayout_ == inputLayout && sourceBitDepth_ == bitDepth &&
        sourceChromaWidthShift_ == chromaWidthShift &&
        sourceChromaHeightShift_ == chromaHeightShift)
        return FFFResult::Success;
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        ReleaseCom(sourceViews_[plane]);
        ReleaseCom(sourceTextures_[plane]);
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
                        ReleaseCom(sourceViews_[1]);
                        ReleaseCom(sourceViews_[0]);
                        ReleaseCom(sourceTextures_[0]);
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
        if (inputLayout == 3)
            // Fully general floating-point range: negatives and super-whites survive.
            // Half-float is bit-compatible with rgba f16, so no swscale is involved.
            texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        else if (inputLayout == 0) texture.Format = bitDepth <= 8 ?
            DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
        else if (inputLayout == 2 && plane == 1)
            texture.Format = bitDepth > 8 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
        else texture.Format = bitDepth > 8 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Usage = D3D11_USAGE_DEFAULT; texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        // Small planar/semi-planar frames upload through Map(WRITE_DISCARD), which
        // needs a CPU-writable dynamic texture. Large ones must NOT: Map hands back
        // write-combined memory, and copying into it row by row loses badly once the
        // rows get wide. Measured (31 interleaved rounds, 10-bit):
        //   1920x1080  Map 0.582 ms vs UpdateSubresource 0.546 ms  (1.06x)
        //   3840x2160  Map 3.293 ms vs 2.886 ms                    (1.14x)
        //   7680x4320  Map 22.266 ms vs 15.213 ms                  (1.46x)
        // So only the large case is worth a second code path, and it needs a DEFAULT
        // texture because UpdateSubresource cannot target a DYNAMIC one.
        if ((inputLayout == 1 || inputLayout == 2) && !UseDefaultUploadForSize(sourceWidth, sourceHeight)) {
            texture.Usage = D3D11_USAGE_DYNAMIC;
            texture.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
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

bool PlayerVideoRenderer::CanUseVideoProcessorForUpscale() const noexcept {
    // A dedicated, colour-agnostic precondition test for the VP upscale step.
    // It intentionally drops four tests that the older CanUseDirectVideoProcessor
    // applied, each for a measured reason:
    //   * sourceExternal_    - software decode uploaded to a D3D11 texture feeds
    //                          a video processor just as well (Chromium uploads a
    //                          staging texture this way).
    //   * transfer / MapToSdr - VSR is spatial; it enhances HDR sources too
    //                          (NVIDIA: "VSR now also upscales HDR video").
    //   * swapOutputBits_     - unrelated to VSR, and gating on it would wrongly
    //                          disable VSR on the 16-bit scRGB path.
    // Chroma location is relaxed to accept UNSPECIFIED, which most real files use.
    if (sourceInputLayout_ != 2 || sourceTextures_[0] == nullptr) return false;
    if (sourceInterlaced_ || sourceBitDepth_ > 10) return false;
    if (sourceCoverArt_) return false;
    if (projection360Enabled_.load(std::memory_order_acquire) != 0) return false;
    if (sourceChromaLocation_ != AVCHROMA_LOC_LEFT &&
        sourceChromaLocation_ != AVCHROMA_LOC_UNSPECIFIED) return false;
    D3D11_TEXTURE2D_DESC inputDescription{};
    sourceTextures_[0]->GetDesc(&inputDescription);
    return (inputDescription.BindFlags & D3D11_BIND_DECODER) != 0;
}

bool PlayerVideoRenderer::ShouldEnableNvidiaSuperResolution(
    const std::uint32_t targetWidth, const std::uint32_t targetHeight) const noexcept {
    if (requestedVideoSuperResolution_.load(std::memory_order_acquire) !=
        FFF3FPVideoSuperResolution::Auto)
        return false;
    if (videoSuperResolutionRejected_) return false;
    if (!nvidiaAdapter_) return false;
    if (!CanUseVideoProcessorForUpscale()) return false;
    // NVIDIA: "will only be enabled if the video requires upscaling".
    if (targetWidth <= sourceWidth_ && targetHeight <= sourceHeight_) return false;
    // NVIDIA documents an input range of 360p..1440p. Judged on height; the
    // documented wording is resolution-class based and says nothing about
    // unusually wide aspect ratios.
    if (sourceHeight_ < 360 || sourceHeight_ > 1440) return false;
    return true;
}

bool PlayerVideoRenderer::CreateYuvPlaneViews(ID3D11Texture2D* const texture,
    const std::uint32_t format, ID3D11ShaderResourceView** const views) noexcept {
    if (texture == nullptr || views == nullptr || device_ == nullptr) return false;
    // P010 carries 10-bit samples; NV12 carries 8-bit. The plane view formats
    // MUST match EnsurePipeline's (R16/R16G16 vs R8/R8G8), otherwise the shader
    // reads the wrong width and the picture shifts.
    const bool tenBit = format == DXGI_FORMAT_P010;
    D3D11_SHADER_RESOURCE_VIEW_DESC luma{};
    luma.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    luma.Texture2D.MipLevels = 1;
    luma.Format = tenBit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
    auto chroma = luma;
    chroma.Format = tenBit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
    // One resource, two views: NV12/P010 interleave chroma as a second plane.
    if (FAILED(device_->CreateShaderResourceView(texture, &luma, &views[0])) ||
        FAILED(device_->CreateShaderResourceView(texture, &chroma, &views[1]))) {
        if (views[0] != nullptr) { views[0]->Release(); views[0] = nullptr; }
        if (views[1] != nullptr) { views[1]->Release(); views[1] = nullptr; }
        return false;
    }
    views[2] = nullptr;
    return true;
}

void PlayerVideoRenderer::ReleaseSuperResolutionResources() noexcept {
    for (std::size_t plane = 0; plane < ARRAYSIZE(superResolutionViews_); ++plane) {
        if (superResolutionViews_[plane] != nullptr) {
            superResolutionViews_[plane]->Release();
            superResolutionViews_[plane] = nullptr;
        }
    }
    if (superResolutionTexture_ != nullptr) {
        superResolutionTexture_->Release();
        superResolutionTexture_ = nullptr;
    }
    videoSuperResolutionSourceWidth_ = videoSuperResolutionSourceHeight_ = 0;
    videoSuperResolutionTargetWidth_ = videoSuperResolutionTargetHeight_ = 0;
    videoSuperResolutionActive_.store(false, std::memory_order_release);
}

FFFResult PlayerVideoRenderer::ApplySuperResolution(const std::uint32_t targetWidth,
    const std::uint32_t targetHeight) noexcept {
    // Latch the feature off permanently on any driver-side failure: the
    // extension has no capability query, so a failed attempt is the only
    // evidence available (Chromium does the same, and mpv/VLC behave likewise).
    const auto reject = [this](const FFF3FPVideoSuperResolutionReason reason) noexcept {
        videoSuperResolutionRejected_ = true;
        videoSuperResolutionActive_.store(false, std::memory_order_release);
        videoSuperResolutionReason_.store(reason, std::memory_order_release);
        SetError("NVIDIA RTX Super Resolution was rejected; using shader scaling.");
        return FFFResult::NotSupported;
    };
    // Same latch, but records which step failed. Kept separate so the caller can
    // tell "the driver has no such feature" apart from "our view setup is wrong"
    // -- both used to collapse into one opaque DriverRejected.
    const auto rejectAt = [this](const char* stage) noexcept {
        videoSuperResolutionRejected_ = true;
        videoSuperResolutionActive_.store(false, std::memory_order_release);
        videoSuperResolutionReason_.store(FFF3FPVideoSuperResolutionReason::DriverRejected,
            std::memory_order_release);
        std::string message = "NVIDIA RTX Super Resolution step failed: ";
        message += stage;
        SetError(message);
        // Also surface it through the process log: the status block only says
        // "DriverRejected", which cannot distinguish an unsupported driver from a
        // bug in our own view setup.
        FFF3FP_KernelLogImpl(message.c_str());
        return FFFResult::NotSupported;
    };

    // Every early exit must clear `active`, otherwise a frame that ran with VSR
    // leaves the flag latched and the status block would claim VSR is on while
    // the shader path is actually in use.
    const auto skip = [this](const FFF3FPVideoSuperResolutionReason reason) noexcept {
        videoSuperResolutionActive_.store(false, std::memory_order_release);
        videoSuperResolutionReason_.store(reason, std::memory_order_release);
        return FFFResult::NotSupported;
    };

    if (requestedVideoSuperResolution_.load(std::memory_order_acquire) ==
        FFF3FPVideoSuperResolution::Off)
        return skip(FFF3FPVideoSuperResolutionReason::NotRequested);

    if (!nvidiaAdapter_)
        return skip(FFF3FPVideoSuperResolutionReason::NotNvidiaAdapter);
    if (!CanUseVideoProcessorForUpscale())
        return skip(FFF3FPVideoSuperResolutionReason::UnsupportedSourceFormat);
    if (targetWidth <= sourceWidth_ && targetHeight <= sourceHeight_)
        return skip(FFF3FPVideoSuperResolutionReason::NotUpscaling);
    if (sourceHeight_ < 360 || sourceHeight_ > 1440)
        return skip(FFF3FPVideoSuperResolutionReason::SourceResolutionOutOfRange);
    if (videoSuperResolutionRejected_)
        return skip(FFF3FPVideoSuperResolutionReason::DriverRejected);

    // NV12/P010 are 4:2:0 formats: a video processor cannot write an odd-sized
    // surface. The destination rect can be odd (it is a fit-to-window box), so
    // round the intermediate surface up to even and keep the rect separate.
    const auto surfaceWidth = (targetWidth + 1u) & ~1u;
    const auto surfaceHeight = (targetHeight + 1u) & ~1u;

    // A video processor is configured for a fixed in/out geometry, so the
    // cached surface is rebuilt whenever the geometry or the decoded format
    // changes. Compare against the *cached* values before overwriting them.
    D3D11_TEXTURE2D_DESC sourceDescription{};
    sourceTextures_[0]->GetDesc(&sourceDescription);
    const auto sourceFormat = static_cast<std::uint32_t>(sourceDescription.Format);
    const bool sameGeometry = superResolutionTexture_ != nullptr &&
        videoSuperResolutionSourceWidth_ == sourceWidth_ &&
        videoSuperResolutionSourceHeight_ == sourceHeight_ &&
        videoSuperResolutionTargetWidth_ == surfaceWidth &&
        videoSuperResolutionTargetHeight_ == surfaceHeight &&
        superResolutionSourceFormat_ == sourceFormat;
    if (!sameGeometry) {
        ReleaseSuperResolutionResources();
        videoSuperResolutionSourceWidth_ = sourceWidth_;
        videoSuperResolutionSourceHeight_ = sourceHeight_;
        videoSuperResolutionTargetWidth_ = surfaceWidth;
        videoSuperResolutionTargetHeight_ = surfaceHeight;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = surfaceWidth;
        description.Height = surfaceHeight;
        description.MipLevels = description.ArraySize = 1;
        // Same format in and out: the video processor must not perform the
        // YUV->RGB conversion, because that would bypass this renderer's entire
        // colour pipeline (tone mapping, scRGB, wide gamut).
        description.Format = sourceDescription.Format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device_->CreateTexture2D(&description, nullptr,
                &superResolutionTexture_)) ||
            !CreateYuvPlaneViews(superResolutionTexture_, sourceFormat,
                superResolutionViews_)) {
            ReleaseSuperResolutionResources();
            superResolutionSourceFormat_ = 0;
            return rejectAt("CreateTexture2D/plane views for the upscaled surface");
        }
        superResolutionSourceFormat_ = sourceFormat;
    }

    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&videoDevice))) ||
        FAILED(context_->QueryInterface(IID_PPV_ARGS(&videoContext))))
        return rejectAt("QueryInterface: no ID3D11VideoDevice/ID3D11VideoContext");

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = { 60, 1 };
    content.InputWidth = sourceWidth_;
    content.InputHeight = sourceHeight_;
    content.OutputFrameRate = content.InputFrameRate;
    // The processor writes the even-sized surface, not the possibly-odd rect.
    content.OutputWidth = surfaceWidth;
    content.OutputHeight = surfaceHeight;
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&content, &enumerator)) ||
        FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor)))
        return rejectAt("CreateVideoProcessorEnumerator/Processor");

    UINT inputSupport = 0;
    UINT outputSupport = 0;
    if (FAILED(enumerator->CheckVideoProcessorFormat(sourceDescription.Format, &inputSupport)) ||
        FAILED(enumerator->CheckVideoProcessorFormat(sourceDescription.Format, &outputSupport)) ||
        (inputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
        (outputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0)
        return reject(FFF3FPVideoSuperResolutionReason::UnsupportedSourceFormat);

    videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(), 0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    // Keep the driver's implicit enhancements off for a reproducible result;
    // VSR itself is requested explicitly below and is unaffected by this flag
    // (mpv and VLC both disable auto-processing and still use VSR).
    videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
    const RECT source{ 0, 0, static_cast<LONG>(sourceWidth_),
        static_cast<LONG>(sourceHeight_) };
    videoContext->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &source);
    // Fill the whole even-sized surface; the renderer then samples it into the
    // real destination rect, so the extra row/column is never shown.
    const RECT destination{ 0, 0, static_cast<LONG>(surfaceWidth),
        static_cast<LONG>(surfaceHeight) };
    videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &destination);

    // The one call that turns the driver's AI upscaler on.
    if (FAILED(FFF3FP::NvidiaVideo::SetSuperResolution(videoContext.Get(),
            processor.Get(), true)))
        return rejectAt("SetStreamExtension(NVIDIA PPE) rejected");

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDescription{};
    inputDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    if (FAILED(videoDevice->CreateVideoProcessorInputView(sourceTextures_[0],
            enumerator.Get(), &inputDescription, &inputView)))
        return rejectAt("CreateVideoProcessorInputView");

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDescription{};
    outputDescription.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    if (FAILED(videoDevice->CreateVideoProcessorOutputView(superResolutionTexture_,
            enumerator.Get(), &outputDescription, &outputView)))
        return rejectAt("CreateVideoProcessorOutputView");

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.pInputSurface = inputView.Get();
    if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1,
            &stream)))
        return rejectAt("VideoProcessorBlt");

    videoSuperResolutionReason_.store(FFF3FPVideoSuperResolutionReason::None,
        std::memory_order_release);
    videoSuperResolutionActive_.store(true, std::memory_order_release);
    return FFFResult::Success;
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
    // An injected ST 2094 table is applied on the present path rather than from the API
    // call: this is the one place that pushes the constant buffers immediately before a
    // draw, so refreshing here is serialised with the draw by construction and never
    // races the presentation pump against the same immediate context.
    if (hdrInputsDirty_.exchange(false)) RefreshHdrShaderInputs();
    cachedVideoSettings_.colorMode = static_cast<std::uint32_t>(actualMode_);
    const bool reconstructed = extensionReconstructed_ && actualMode_ != FFF3FPColorMode::RawHdrAsSdr;
    cachedVideoSettings_.inputLayout = reconstructed ? 1u : cachedOriginalInputLayout_;
    cachedVideoSettings_.sampleScale = reconstructed ? 65535.0f / 1023.0f : cachedOriginalSampleScale_;
    cachedVideoSettings_.reserved = effect;
    cachedVideoSettings_.outputWidth = width;
    cachedVideoSettings_.outputHeight = height;
    const auto projection360 = effect == 0 ?
        projection360Enabled_.load(std::memory_order_acquire) : 0u;
    cachedVideoSettings_.projection360 = projection360;
    cachedVideoSettings_.viewYaw = std::bit_cast<float>(
        view360YawBits_.load(std::memory_order_acquire));
    cachedVideoSettings_.viewPitch = std::bit_cast<float>(
        view360PitchBits_.load(std::memory_order_acquire));
    cachedVideoSettings_.viewFovY = std::bit_cast<float>(
        view360FovYBits_.load(std::memory_order_acquire));
    cachedVideoSettings_.viewAspect = width / height;
    cachedVideoSettings_.viewRotation = static_cast<float>(
        viewRotation_.load(std::memory_order_acquire));
    // Image pixel-block sampling applies only while enlarging. The destination rect
    // passed in is the final fitted box, so comparing it with the cached source size
    // is the authoritative test -- the earlier size-based checks cannot see the fit.
    cachedVideoSettings_.imageModeX = sourceImageMode_ &&
        (width > cachedVideoSettings_.sourceWidth || height > cachedVideoSettings_.sourceHeight)
        ? 1.0f : 0.0f;
    context_->UpdateSubresource(constants_, 0, nullptr, &cachedVideoSettings_, 0, 0);
    context_->OMSetRenderTargets(1, &target, nullptr);
    const D3D11_VIEWPORT viewport{x, y, width, height, 0.0f, 1.0f};
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    auto* shader = extensionShader_ ? extensionShader_ : pixelShader_;
    // SDR layout specialization: the bytecode has InputLayout fixed at compile time
    // and the colour/projection constants forced to zero, so it is only valid when
    // the frame really is plain SDR. `effect` must stay excluded -- effect 1 is the
    // cover-backdrop composite, which shades differently.
    //
    // Measured on the RTX 5080: this changes nothing (median 173 vs 173 frames on 8K
    // hardware decode, 66 vs 66 on 720p software). The branches it removes are all
    // uniform, which modern GPUs effectively execute for free, and every available
    // source is decode- or refresh-bound anyway. Kept because it is already written
    // and passes pixel-equivalence, but it is not an optimization to rely on.
    if (!extensionShader_ && effect == 0 && projection360 == 0 &&
        cachedVideoSettings_.colorMode == 0 && cachedVideoSettings_.transfer == 0 &&
        cachedVideoSettings_.gamut == 0 && cachedVideoSettings_.inputLayout < 3) {
        auto*& specialized = sdrPixelShaders_[cachedVideoSettings_.inputLayout];
        if (specialized == nullptr) {
            const BYTE* bytecode[] = {FFFSdrRgbShaderBytecode,
                FFFSdrPlanarShaderBytecode, FFFSdrSemiPlanarShaderBytecode};
            const SIZE_T sizes[] = {sizeof(FFFSdrRgbShaderBytecode),
                sizeof(FFFSdrPlanarShaderBytecode), sizeof(FFFSdrSemiPlanarShaderBytecode)};
            device_->CreatePixelShader(bytecode[cachedVideoSettings_.inputLayout],
                sizes[cachedVideoSettings_.inputLayout], nullptr, &specialized);
        }
        if (specialized != nullptr) shader = specialized;
    }
    context_->PSSetShader(shader, nullptr, 0);
    context_->PSSetConstantBuffers(1, 1, &extensionConstants_);
    context_->PSSetConstantBuffers(0, 1, &constants_);
    // ST 2094 curve state at b2 / t3. Both are always bound so the shader's
    // declared registers are never left holding a released object; with
    // DynamicEnabled == 0 the values are inert.
    if (dynamicConstants_ != nullptr)
        context_->PSSetConstantBuffers(2, 1, &dynamicConstants_);
    ID3D11SamplerState* samplers[] = {sampler_, pointSampler_, panoramaSampler_};
    context_->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);
    auto* views = sourceViews != nullptr ? sourceViews :
        (reconstructed ? extensionReconstructedViews_ : sourceViews_);
    context_->PSSetShaderResources(0, ARRAYSIZE(sourceViews_), views);
    if (dynamicCurveView_ != nullptr)
        context_->PSSetShaderResources(3, 1, &dynamicCurveView_);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView* nullViews[] = {nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);
    ID3D11ShaderResourceView* nullCurve = nullptr;
    context_->PSSetShaderResources(3, 1, &nullCurve);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    return FFFResult::Success;
}

void PlayerVideoRenderer::ReleaseExtensionEnhancement() noexcept {
    for (std::size_t plane = 0; plane < 3; ++plane) {
        ReleaseCom(extensionEnhancementViews_[plane]);
        ReleaseCom(extensionEnhancementTextures_[plane]);
    }
    extensionEnhancementWidth_ = extensionEnhancementHeight_ = 0;
    ReleaseExtensionReconstruction();
}

void PlayerVideoRenderer::ReleaseExtensionReconstruction() noexcept {
    for (std::size_t plane = 0; plane < 3; ++plane) {
        ReleaseCom(extensionReconstructedOutputs_[plane]);
        ReleaseCom(extensionReconstructedViews_[plane]);
        ReleaseCom(extensionReconstructedTextures_[plane]);
    }
    extensionReconstructedWidth_ = extensionReconstructedHeight_ = 0;
    extensionReconstructed_ = false;
}

bool PlayerVideoRenderer::UploadExtensionEnhancement(const AVFrame* frame) noexcept {
    const bool hardware = frame != nullptr && frame->format == AV_PIX_FMT_D3D11;
    if (!frame || (!hardware && frame->format != AV_PIX_FMT_YUV420P10LE) || frame->width <= 0 || frame->height <= 0)
        return false;
    if (hardware) {
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(frame->data[0]);
        if (!texture || !frame->hw_frames_ctx) return false;
        ComPtr<ID3D11Device> decodeDevice;
        texture->GetDevice(&decodeDevice);
        if (decodeDevice.Get() != device_) return false;
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        if (frames->sw_format != AV_PIX_FMT_P010LE) return false;
    }
    for (int plane = 0; !hardware && plane < 3; ++plane) {
        const auto width = plane == 0 ? frame->width : (frame->width + 1) / 2;
        if (!frame->data[plane] || frame->linesize[plane] < width * 2) return false;
    }
    if (extensionEnhancementWidth_ != frame->width || extensionEnhancementHeight_ != frame->height ||
        extensionEnhancementSemiplanar_ != hardware) {
        ReleaseExtensionEnhancement();
        for (int plane = 0; plane < (hardware ? 1 : 3); ++plane) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = plane == 0 ? frame->width : (frame->width + 1) / 2;
            desc.Height = plane == 0 ? frame->height : (frame->height + 1) / 2;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = hardware ? DXGI_FORMAT_P010 : DXGI_FORMAT_R16_UNORM; desc.SampleDesc.Count = 1;
            if (hardware) {
                D3D11_TEXTURE2D_DESC decoded{};
                reinterpret_cast<ID3D11Texture2D*>(frame->data[0])->GetDesc(&decoded);
                desc.Width = decoded.Width; desc.Height = decoded.Height;
            }
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels = 1;
            view.Format = DXGI_FORMAT_R16_UNORM;
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, &extensionEnhancementTextures_[plane])) ||
                FAILED(device_->CreateShaderResourceView(extensionEnhancementTextures_[plane], hardware ? &view : nullptr,
                    &extensionEnhancementViews_[plane]))) {
                ReleaseExtensionEnhancement();
                return false;
            }
            if (hardware) {
                view.Format = DXGI_FORMAT_R16G16_UNORM;
                if (FAILED(device_->CreateShaderResourceView(extensionEnhancementTextures_[plane], &view,
                    &extensionEnhancementViews_[1]))) {
                    ReleaseExtensionEnhancement(); return false;
                }
            }
        }
        extensionEnhancementWidth_ = frame->width; extensionEnhancementHeight_ = frame->height;
        extensionEnhancementSemiplanar_ = hardware;
    }
    if (hardware) {
        const auto slice = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(frame->data[1]));
        context_->CopySubresourceRegion(extensionEnhancementTextures_[0], 0, 0, 0, 0,
            reinterpret_cast<ID3D11Texture2D*>(frame->data[0]), slice, nullptr);
    } else {
        for (int plane = 0; plane < 3; ++plane)
            context_->UpdateSubresource(extensionEnhancementTextures_[plane], 0, nullptr,
                frame->data[plane], frame->linesize[plane], 0);
    }
    return true;
}

bool PlayerVideoRenderer::ReconstructExtensionEnhancement(const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t layout, const float sampleScale) noexcept {
    const auto* api = GetColorExtension();
    if (!api || !context_ || !extensionConstants_ || (layout != 1 && layout != 2)) return false;
    if (!extensionEnhancementShader_ && FAILED(device_->CreateComputeShader(api->enhancementShaderBytecode,
        api->enhancementShaderBytecodeSize, nullptr, &extensionEnhancementShader_))) return false;
    struct Settings {
        std::uint32_t width, height, plane, layout;
        float codeScale, padding[3];
        std::uint32_t elLayout; float elCodeScale;
        std::uint32_t elWidth, elHeight;
    };
    if (!extensionEnhancementConstants_) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(Settings); desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &extensionEnhancementConstants_))) return false;
    }
    if (extensionReconstructedWidth_ != width || extensionReconstructedHeight_ != height) {
        ReleaseExtensionReconstruction();
        for (int plane = 0; plane < 3; ++plane) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = plane == 0 ? width : (width + 1) / 2;
            desc.Height = plane == 0 ? height : (height + 1) / 2;
            desc.MipLevels = desc.ArraySize = 1; desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R16_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, &extensionReconstructedTextures_[plane])) ||
                FAILED(device_->CreateShaderResourceView(extensionReconstructedTextures_[plane], nullptr,
                    &extensionReconstructedViews_[plane])) ||
                FAILED(device_->CreateUnorderedAccessView(extensionReconstructedTextures_[plane], nullptr,
                    &extensionReconstructedOutputs_[plane]))) {
                ReleaseExtensionReconstruction();
                return false;
            }
        }
        extensionReconstructedWidth_ = width; extensionReconstructedHeight_ = height;
        ReleaseScaleResources();
    }
    ID3D11ShaderResourceView* inputs[]{sourceViews_[0], sourceViews_[1], sourceViews_[2],
        extensionEnhancementViews_[0], extensionEnhancementViews_[1], extensionEnhancementViews_[2]};
    context_->CSSetShader(extensionEnhancementShader_, nullptr, 0);
    ID3D11Buffer* buffers[]{extensionEnhancementConstants_, extensionConstants_};
    context_->CSSetConstantBuffers(0, ARRAYSIZE(buffers), buffers);
    context_->CSSetShaderResources(0, ARRAYSIZE(inputs), inputs);
    for (int plane = 0; plane < 3; ++plane) {
        const Settings settings{plane == 0 ? width : (width + 1) / 2,
            plane == 0 ? height : (height + 1) / 2, static_cast<std::uint32_t>(plane), layout,
            sampleScale * 1023.0f, {}, extensionEnhancementSemiplanar_ ? 2u : 1u,
            extensionEnhancementSemiplanar_ ? 65535.0f / 64.0f : 65535.0f,
            extensionEnhancementWidth_, extensionEnhancementHeight_};
        context_->UpdateSubresource(extensionEnhancementConstants_, 0, nullptr, &settings, 0, 0);
        context_->CSSetUnorderedAccessViews(0, 1, &extensionReconstructedOutputs_[plane], nullptr);
        context_->Dispatch((settings.width + 15) / 16, (settings.height + 15) / 16, 1);
    }
    ID3D11UnorderedAccessView* nullOutput = nullptr;
    context_->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
    ID3D11ShaderResourceView* nullInputs[6]{};
    context_->CSSetShaderResources(0, ARRAYSIZE(nullInputs), nullInputs);
    context_->CSSetShader(nullptr, nullptr, 0);
    return true;
}

void PlayerVideoRenderer::ReleaseScaleResources() noexcept {
    for (auto& chain : planeScaleChains_) {
        for (auto& pass : chain.passes) {
            ReleaseCom(pass.view);
            ReleaseCom(pass.target);
            ReleaseCom(pass.texture);
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

std::uint32_t PlayerVideoRenderer::SelectScaleFilter(const float scaleX,
    const float scaleY) const noexcept {
    const auto upscaling = scaleX > 1.0f || scaleY > 1.0f;
    const auto downscaling = scaleX < 1.0f || scaleY < 1.0f;

    if (sourceImageMode_) {
        // A photograph or drawing is enlarged by whole pixel blocks: each source pixel
        // becomes a square of output pixels, so the picture keeps its own pixel grid
        // and reads as a crisp enlargement rather than a smeared one. Only upscaling
        // can do this -- reducing a picture still has to average detail away, so a
        // downscale keeps the smoothing kernel below.
        if (upscaling && !downscaling) return ToShaderFilter(ScaleKernel::Block);
    } else if (upscaling && !downscaling) {
        // Video upscaling: reconstruct sharply. Lanczos-3 on the high-quality setting,
        // the cubic otherwise, which is the cheaper sharp kernel.
        return ToShaderFilter(scalingQuality_ == FFF3FPVideoScalingQuality::HighQuality
            ? ScaleKernel::Lanczos3 : ScaleKernel::Cubic);
    }

    if (downscaling) {
        // Every downscale uses Lanczos-3. Its support widens with the ratio
        // (support = radius/scale), so it anti-aliases properly at any reduction,
        // including the severe ones that previously fell back to the equal-weight
        // Box. Measured on a stripe sweep it had the lowest residual of the separable
        // kernels, and it costs only ~15% more than bilinear.
        return ToShaderFilter(ScaleKernel::Lanczos3);
    }

    // Pure 1:1 pass (a chain step that changes only one axis): keep it exact.
    return ToShaderFilter(ScaleKernel::Block);
}

void PlayerVideoRenderer::ConfigureAdaptiveDownscale(const bool enabled,
    const std::uint32_t dropPercent) noexcept {
    adaptiveDownscaleEnabled_.store(enabled, std::memory_order_release);
    adaptiveDownscaleDropPercent_.store(std::clamp(dropPercent, 1u, 99u),
        std::memory_order_release);
    if (!enabled) {
        adaptiveDownscaleActive_.store(false, std::memory_order_release);
        adaptiveWindowFrames_ = 0;
        adaptiveWindowDrops_ = 0;
    }
}

void PlayerVideoRenderer::ObserveFrameForAdaptiveDownscale(const bool dropped) noexcept {
    if (!adaptiveDownscaleEnabled_.load(std::memory_order_acquire)) return;
    ++adaptiveWindowFrames_;
    if (dropped) ++adaptiveWindowDrops_;
    // A window long enough that one stall cannot flip the policy, short enough that a
    // genuinely overloaded session switches within a fraction of a second at 60 fps.
    constexpr std::uint64_t WindowFrames = 30;
    if (adaptiveWindowFrames_ < WindowFrames) return;
    const auto percent = static_cast<std::uint32_t>(
        adaptiveWindowDrops_ * 100 / std::max<std::uint64_t>(adaptiveWindowFrames_, 1));
    const auto threshold = adaptiveDownscaleDropPercent_.load(std::memory_order_acquire);
    const auto nowActive = percent >= threshold;
    const auto wasActive = adaptiveDownscaleActive_.exchange(nowActive, std::memory_order_acq_rel);
    adaptiveWindowFrames_ = 0;
    adaptiveWindowDrops_ = 0;
    // Log only the edges: the policy is meant to be invisible until it matters.
    if (nowActive != wasActive)
        std::fprintf(stderr, "[adaptive] downscale-before-upload %s (drops %u%%)\n",
            nowActive ? "engaged" : "released", percent);
}

bool PlayerVideoRenderer::PrepareAdaptiveDownscale(const AVFrame* frame,
    const std::uint32_t width, const std::uint32_t height,
    const std::uint32_t outputWidth, const std::uint32_t outputHeight) noexcept {
    if (frame == nullptr) return false;
    if (!adaptiveDownscaleEnabled_.load(std::memory_order_acquire)) return false;
    if (!adaptiveDownscaleActive_.load(std::memory_order_acquire)) return false;
    if (outputWidth == 0 || outputHeight == 0) return false;
    // Only meaningful when the frame really is larger than its destination.
    if (width <= outputWidth && height <= outputHeight) return false;

    const auto pixelFormat = static_cast<AVPixelFormat>(frame->format);
    adaptiveScaler_ = sws_getCachedContext(adaptiveScaler_, frame->width, frame->height,
        pixelFormat, static_cast<int>(outputWidth), static_cast<int>(outputHeight),
        pixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (adaptiveScaler_ == nullptr) return false;
    // Match the main path's colour handling so the resample does not shift colours.
    const auto* sourceCoefficients = sws_getCoefficients(ToSwsColorSpace(frame, false));
    const auto* destinationCoefficients = sws_getCoefficients(SWS_CS_ITU709);
    if (sourceCoefficients != nullptr && destinationCoefficients != nullptr)
        (void)sws_setColorspaceDetails(adaptiveScaler_, sourceCoefficients,
            IsFullRange(frame) ? 1 : 0, destinationCoefficients, 1, 0, 1 << 16, 1 << 16);

    const auto bytes = static_cast<std::size_t>(av_image_get_buffer_size(pixelFormat,
        static_cast<int>(outputWidth), static_cast<int>(outputHeight), 1));
    if (bytes == 0) return false;
    if (adaptiveBuffer_.size() < bytes) {
        try { adaptiveBuffer_.resize(bytes); }
        catch (...) { return false; }
    }
    std::uint8_t* destinationData[4]{};
    int destinationLines[4]{};
    if (av_image_fill_arrays(destinationData, destinationLines, adaptiveBuffer_.data(),
            pixelFormat, static_cast<int>(outputWidth), static_cast<int>(outputHeight), 1) < 0)
        return false;
    if (sws_scale(adaptiveScaler_, frame->data, frame->linesize, 0, frame->height,
            destinationData, destinationLines) <= 0)
        return false;
    adaptiveSourceWidth_ = outputWidth;
    adaptiveSourceHeight_ = outputHeight;
    // The caller uploads these planes instead of the original frame's.
    for (int plane = 0; plane < 4; ++plane) {
        adaptivePlanes_[plane] = destinationData[plane];
        adaptiveLines_[plane] = destinationLines[plane];
    }
    return true;
}

FFFResult PlayerVideoRenderer::PrepareScaledVideo(const std::uint32_t outputWidth,
    const std::uint32_t outputHeight, ID3D11ShaderResourceView** views) noexcept {
    if (views == nullptr || outputWidth == 0 || outputHeight == 0)
        return FFFResult::InvalidArgument;
    const auto generation = videoGeneration_.load(std::memory_order_acquire);
    // The cache key includes the VSR state: VSR swaps the effective source for an
    // upscaled surface of a different size, and that changes the result even
    // though the decoded generation is unchanged.
    const auto superResolutionActive = videoSuperResolutionActive_.load(std::memory_order_acquire);
    if (scaledVideoGeneration_ == generation && scaledOutputWidth_ == outputWidth &&
        scaledOutputHeight_ == outputHeight &&
        scaledVideoSuperResolution_ == superResolutionActive) {
        std::copy(std::begin(scaledSourceViews_), std::end(scaledSourceViews_), views);
        return FFFResult::Success;
    }

    // Upstream's reconstructed-extension path picks its own plane layout and
    // source array; VSR only ever applies to the ordinary decoded planes, so the
    // two are combined rather than one replacing the other.
    const bool reconstructed = extensionReconstructed_ && actualMode_ != FFF3FPColorMode::RawHdrAsSdr;
    const auto layout = reconstructed ? 1u : sourceInputLayout_;
    auto* inputViews = reconstructed ? extensionReconstructedViews_ : effectiveSourceViews_;
    const auto planeCount = layout == 1 ? 3u : (layout == 2 ? 2u : 1u);
    for (std::size_t plane = 0; plane < ARRAYSIZE(effectiveSourceViews_); ++plane) {
        if (plane >= planeCount || inputViews[plane] == nullptr) {
            scaledSourceViews_[plane] = nullptr;
            continue;
        }
        // With VSR active the effective source is already at the target size, so
        // these denominators describe the upscaled surface, not the decoded one.
        const auto superResolutionPlane =
            !reconstructed && superResolutionActive && superResolutionViews_[plane] != nullptr;
        const auto planeSourceWidth = superResolutionPlane ? outputWidth : sourceWidth_;
        const auto planeSourceHeight = superResolutionPlane ? outputHeight : sourceHeight_;
        const auto planeWidth = plane == 0 ? planeSourceWidth :
            (planeSourceWidth + (1u << sourceChromaWidthShift_) - 1) >> sourceChromaWidthShift_;
        const auto planeHeight = plane == 0 ? planeSourceHeight :
            (planeSourceHeight + (1u << sourceChromaHeightShift_) - 1) >> sourceChromaHeightShift_;
        const auto targetWidth = std::min(planeWidth, outputWidth);
        const auto targetHeight = std::min(planeHeight, outputHeight);
        // Layout 3 (float RGB) is interleaved RGBA in a single plane, exactly like
        // layout 0, so it must use the same 4-channel float format. Without this the
        // `else` arm below degrades it to R16_FLOAT and only the red channel survives.
        const auto format = layout == 0 || layout == 3 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
            (layout == 2 && plane == 1 ? DXGI_FORMAT_R16G16_FLOAT :
                DXGI_FORMAT_R16_FLOAT);
        const auto ensure = EnsurePlaneScaleChain(plane, planeWidth, planeHeight,
            targetWidth, targetHeight, static_cast<std::uint32_t>(format));
        if (ensure != FFFResult::Success) return ensure;

        // Select the filter from the overall ratio, not each halving pass.
        //
        // Upscaling and downscaling want opposite things, so they no longer share one
        // kernel:
        //   - Upscaling wants a sharp reconstruction. Lanczos-3 keeps the most detail;
        //     the Catmull-Rom cubic is the cheaper sharp option.
        //   - Downscaling wants a smooth, wide kernel to suppress aliasing. Lanczos-3
        //     already widens its support with the ratio (support = radius/scale), and
        //     measured best on a stripe sweep, so it is used for every downscale
        //     instead of dropping to the equal-weight Box below 0.25x. That Box
        //     shortcut only equals a true area average at integer ratios, which the
        //     common video sizes happen to be but an arbitrary window size is not.
        //   - Still images enlarge by pixel blocks instead, so a picture keeps its
        //     source pixel grid and reads as a crisp magnification rather than a
        //     smooth interpolation. See SelectScaleFilter.
        const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(planeWidth);
        const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(planeHeight);
        const std::uint32_t filter = SelectScaleFilter(scaleX, scaleY);

        auto* currentView = inputViews[plane];
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
    scaledVideoSuperResolution_ = superResolutionActive;
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
    ReleaseCom(videoProcessor_);
    ReleaseCom(videoProcessorEnumerator_);
    ReleaseCom(videoContext_);
    ReleaseCom(videoDevice_);
    videoProcessorInputFormat_ = videoProcessorOutputFormat_ = DXGI_FORMAT_UNKNOWN;
    videoProcessorInputColorSpace_ = videoProcessorOutputColorSpace_ = DXGI_COLOR_SPACE_CUSTOM;
    videoProcessorInputWidth_ = videoProcessorInputHeight_ = 0;
    videoProcessorOutputWidth_ = videoProcessorOutputHeight_ = 0;
    videoProcessorConfigurationFailed_ = false;
}

void PlayerVideoRenderer::ReleaseVideoProcessorInputSurface() noexcept {
    ReleaseCom(videoProcessorRenderTarget_);
    ReleaseCom(videoProcessorRenderTexture_);
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
            presentationFrameRate_ = DetectDisplayRefreshRate(window_);
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
                const auto cameraLive = projection360Enabled_.load(std::memory_order_acquire) != 0;
                // Decode and 360-view updates present immediately; overlay-only
                // updates obey their cadence. The swap chain paces the display.
                if (const auto now = std::chrono::steady_clock::now();
                    !videoChanged && !cameraLive &&
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
    D3D11_TEXTURE2D_DESC description{};
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
        status.backBufferAcquisitionCount = backBufferAcquisitionCount_.load(
            std::memory_order_acquire);
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
        if (FAILED(device_->CreateVertexShader(FFFTimedTextSpriteVertexShaderBytecode,
                sizeof(FFFTimedTextSpriteVertexShaderBytecode), nullptr,
                &timedTextSpriteVertexShader_)) ||
            FAILED(device_->CreatePixelShader(FFFTimedTextSpritePixelShaderBytecode,
                sizeof(FFFTimedTextSpritePixelShaderBytecode), nullptr,
                &timedTextSpritePixelShader_))) {
            SetError("Could not create the precompiled timed-text sprite shaders.");
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
    ReleaseCom(d2dTimedTextShadowTarget_);
    ReleaseCom(d2dAtlasTarget_);
    ReleaseCom(timedTextAtlasView_);
    ReleaseCom(timedTextAtlasTexture_);
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
    ReleaseCom(timedTextSpriteInstanceView_);
    ReleaseCom(timedTextSpriteInstanceBuffer_);
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
    const auto getLayout = [this, scaleX, scaleY](const TimedTextRenderCommand& command,
        const float fontSize) noexcept -> IDWriteTextLayout* {
        const auto width = std::max(command.width * scaleX, 1.0f);
        const auto height = std::max(command.height * scaleY, 1.0f);
        const auto layoutKey = TimedTextLayoutKey(command, width, height, fontSize);
        const auto existing = timedTextLayouts_.find(layoutKey);
        if (existing != timedTextLayouts_.end()) return existing->second;
        ComPtr<IDWriteTextLayout> layout;
        if (!command.content || FAILED(CreateTimedTextLayout(writeFactory_, command.content->text,
            command.content->fontFamily, fontSize, command.flags, command.horizontalAlignment,
            command.verticalAlignment,
            width, height, &layout))) return nullptr;
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
        std::unordered_set<std::uint64_t> pendingKeys;
        pendingKeys.reserve(std::min(layer->commands.size(),
            static_cast<std::size_t>(MaximumTimedTextSprites)));
        for (std::size_t commandIndex = 0; commandIndex < layer->commands.size(); ++commandIndex) {
            const auto& command = layer->commands[commandIndex];
            if (command.type != FFF3FPTimedTextCommandType::Text || command.contentId == 0 ||
                command.horizontalAlignment != FFF3FPTimedTextAlignment::Near ||
                command.verticalAlignment != FFF3FPTimedTextAlignment::Near) continue;
            const auto fontSize = std::max(command.fontSize * scaleY, 1.0f);
            const auto outline = std::max(command.outlineWidth * (scaleX + scaleY) * 0.5f, 0.0f);
            const auto shadowX = command.shadowOffsetX * scaleX;
            const auto shadowY = command.shadowOffsetY * scaleY;
            const auto key = TimedTextSpriteKey(command, std::max(command.width * scaleX, 1.0f),
                std::max(command.height * scaleY, 1.0f), fontSize, outline, shadowX, shadowY);
            if (timedTextSprites_.contains(key) || !pendingKeys.insert(key).second) continue;
            if (timedTextSprites_.size() + pendingSprites.size() >= MaximumTimedTextSprites)
                return !stopWhenFull;
            auto* layout = getLayout(command, fontSize);
            if (layout == nullptr) continue;
            DWRITE_OVERHANG_METRICS overhang{};
            if (FAILED(layout->GetOverhangMetrics(&overhang))) continue;
            auto inkBounds = D2D1::RectF(-overhang.left, -overhang.top,
                layout->GetMaxWidth() + overhang.right, layout->GetMaxHeight() + overhang.bottom);
            if (outline > 0.0f && ((command.outlineArgb | command.shadowArgb) >> 24) != 0) {
                auto* boundsRenderer = new (std::nothrow) TimedTextEffectRenderer(
                    d2dFactory_, d2dContext_, nullptr, nullptr, outline, 0.0f, 0.0f, &inkBounds);
                if (boundsRenderer == nullptr) continue;
                const auto boundsResult = layout->Draw(nullptr, boundsRenderer, 0.0f, 0.0f);
                boundsRenderer->Release();
                if (FAILED(boundsResult)) continue;
            }
            const auto extents = DescribeTimedTextEffects(0.0f, shadowX, shadowY,
                (command.shadowArgb >> 24) != 0, usesSoftShadow(command));
            const auto left = std::floor(inkBounds.left - extents.left) - 2.0f;
            const auto top = std::floor(inkBounds.top - extents.top) - 2.0f;
            const auto right = std::ceil(inkBounds.right + extents.right) + 2.0f;
            const auto bottom = std::ceil(inkBounds.bottom + extents.bottom) + 2.0f;
            const auto measuredWidth = right - left;
            const auto measuredHeight = bottom - top;
            if (!std::isfinite(measuredWidth) || !std::isfinite(measuredHeight) ||
                measuredWidth <= 0.0f || measuredHeight <= 0.0f ||
                measuredWidth > MaximumTimedTextAtlasSize || measuredHeight > MaximumTimedTextAtlasSize) continue;
            if (measuredWidth > timedTextAtlasSize_ || measuredHeight > timedTextAtlasSize_)
                return !stopWhenFull;
            const auto width = static_cast<std::uint32_t>(measuredWidth);
            const auto height = static_cast<std::uint32_t>(measuredHeight);
            if (timedTextAtlasX_ + width > timedTextAtlasSize_) {
                timedTextAtlasX_ = 0;
                timedTextAtlasY_ += timedTextAtlasRowHeight_;
                timedTextAtlasRowHeight_ = 0;
            }
            if (timedTextAtlasY_ + height > timedTextAtlasSize_)
                return !stopWhenFull;
            TimedTextSprite sprite{static_cast<float>(timedTextAtlasX_),
                static_cast<float>(timedTextAtlasY_), left, top,
                static_cast<float>(width), static_cast<float>(height)};
            timedTextAtlasX_ += width;
            timedTextAtlasRowHeight_ = std::max(timedTextAtlasRowHeight_, height);
            layout->AddRef();
            pendingSprites.push_back(PendingTimedTextSprite{commandIndex,
                std::shared_ptr<IDWriteTextLayout>(layout, [](IDWriteTextLayout* retained) {
                    retained->Release();
                }), key, sprite, outline, shadowX, shadowY});
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
            d2dContext_->PushAxisAlignedClip(D2D1::RectF(sprite.atlasX, sprite.atlasY,
                sprite.atlasX + sprite.width, sprite.atlasY + sprite.height),
                D2D1_ANTIALIAS_MODE_ALIASED);
            d2dContext_->Clear(D2D1::ColorF(0, 0));
            d2dContext_->PopAxisAlignedClip();
        }
        const auto clearEnd = d2dContext_->EndDraw();
        d2dContext_->SetTarget(nullptr);
        if (FAILED(clearEnd)) return FFFResult::DeviceFailure;
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
                drawSoftShadowMask(command, pending.layout.get(),
                    D2D1::Point2F(sprite.atlasX - sprite.offsetX,
                        sprite.atlasY - sprite.offsetY), pending.outline);
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
            for (const auto& pending : pendingSprites) {
                const auto& command = layer->commands[pending.commandIndex];
                if (!usesSoftShadow(command) || (command.shadowArgb >> 24) == 0 ||
                    std::bit_cast<std::uint32_t>(softShadowDepth(
                        pending.shadowX, pending.shadowY)) != depthBits) continue;
                const auto& sprite = pending.sprite;
                const auto clip = D2D1::RectF(sprite.atlasX + 1.0f, sprite.atlasY + 1.0f,
                    sprite.atlasX + sprite.width - 1.0f, sprite.atlasY + sprite.height - 1.0f);
                d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
                d2dContext_->DrawImage(timedTextShadowBlurEffect_,
                    D2D1::Point2F(clip.left, clip.top), clip);
                d2dContext_->PopAxisAlignedClip();
            }
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
            const auto clip = D2D1::RectF(sprite.atlasX + 1.0f, sprite.atlasY + 1.0f,
                sprite.atlasX + sprite.width - 1.0f, sprite.atlasY + sprite.height - 1.0f);
            d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
            drawLayout(command, pending.layout.get(),
                D2D1::Point2F(sprite.atlasX - sprite.offsetX,
                    sprite.atlasY - sprite.offsetY), pending.outline,
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
        pendingSprites.clear();
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
            const auto spriteKey = TimedTextSpriteKey(command, std::max(command.width * scaleX, 1.0f),
                std::max(command.height * scaleY, 1.0f), fontSize, outline, shadowX, shadowY);
            const auto sprite = timedTextSprites_.find(spriteKey);
            if (sprite != timedTextSprites_.end()) {
                ++timedTextSpriteCacheHits_;
                const auto left = destination.left + sprite->second.offsetX;
                const auto top = destination.top + sprite->second.offsetY;
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
        auto* layout = getLayout(command, fontSize);
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
    if (slot == TimedTextLayerSlot::Disc) {
        const auto aspect = discAspect_.load();
        const auto rect = CalculateVideoDestination(aspect > 0 ? static_cast<unsigned>(aspect * 10000) : sourceWidth_,
            aspect > 0 ? 10000u : sourceHeight_, swapWidth_, swapHeight_);
        viewport.TopLeftX = static_cast<float>(rect.x); viewport.TopLeftY = static_cast<float>(rect.y);
        viewport.Width = static_cast<float>(rect.width); viewport.Height = static_cast<float>(rect.height);
    }
    // Restore the complete fullscreen pipeline after instanced danmaku drawing.
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
    ReleaseCom(d2dTargets_[index]);
    ReleaseCom(timedTextPipelineQueries_[index]);
    ReleaseCom(timedTextViews_[index]);
    ReleaseCom(timedTextTargets_[index]);
    ReleaseCom(timedTextTextures_[index]);
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
    ReleaseCom(timedTextRenderingParams_);
    if (timedTextShadowBlurEffect_ != nullptr) {
        timedTextShadowBlurEffect_->SetInput(0, nullptr);
        timedTextShadowBlurEffect_->Release(); timedTextShadowBlurEffect_ = nullptr;
    }
    ReleaseCom(d2dTimedTextShadowTarget_);
    ReleaseCom(d2dAtlasTarget_);
    for (std::size_t index = 0; index < ARRAYSIZE(timedTextTextures_); ++index)
        ReleaseTimedTextSlotResources(static_cast<TimedTextLayerSlot>(index));
    if (d2dCoverBackdropSource_ == nullptr && d2dCoverBackdropTarget_ == nullptr &&
        coverBackdropBlurEffect_ == nullptr) {
        ReleaseCom(d2dContext_);
        ReleaseCom(d2dDevice_);
    }
    ReleaseCom(timedTextSpriteInstanceView_);
    ReleaseCom(timedTextSpriteInstanceBuffer_);
    ReleaseCom(timedTextSpritePixelShader_);
    ReleaseCom(timedTextSpriteVertexShader_);
    ReleaseCom(timedTextAtlasView_);
    ReleaseCom(timedTextAtlasTexture_);
    timedTextAtlasHdr_ = false;
    timedTextResourcesHdr_ = false;
    ReleaseCom(timedTextBlend_);
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

// Build the b2 constant buffer and the curve LUT for this frame.
//
// Returns false whenever the dynamic path must not be taken, which includes:
// no metadata, a window with no usable curve, or missing GPU resources. Every
// one of those cases leaves the shader's DynamicEnabled at 0, i.e. the original
// mapping, so the failure mode is "as before" rather than "wrong picture".
bool PlayerVideoRenderer::BuildDynamicToneSettings(const HdrDynamicMetadata& metadata,
    DynamicShaderSettings& settings, std::array<float, kDynamicCurveLutWidth *
        kHdrMaxProcessingWindows>& curves) noexcept {
    settings = DynamicShaderSettings{};
    if (!dynamicCurveBuilt_ || dynamicConstants_ == nullptr ||
        dynamicCurveView_ == nullptr || metadata.windowCount == 0 ||
        metadata.kind == HdrMetadataKind::None) {
        return false;
    }
    // A stream whose metadata was rejected carries no usable curve; stay on the
    // static path rather than applying a degenerate mapping.
    if (IsBlockingDegrade(metadata.degrade)) return false;

    std::uint32_t built = 0;
    for (std::uint32_t index = 0; index < metadata.windowCount &&
        index < kHdrMaxProcessingWindows; ++index) {
        const auto& window = metadata.windows[index];
        HdrToneCurve curve{};
        if (!HdrToneCurveBuild::BuildFromWindow(window, curve)) break;
        for (std::uint32_t sample = 0; sample < kDynamicCurveLutWidth; ++sample)
            curves[index * kDynamicCurveLutWidth + sample] = curve.lut[sample];
        ++built;
    }
    // Require every declared window to have produced a curve: applying a curve
    // to only part of the frame would make the mapped region visibly different
    // from the rest, which is worse than the static mapping.
    if (built == 0 || built != metadata.windowCount) return false;

    settings.enabled = 1;
    settings.windowCount = built;
    // Stage-2 compression runs from the content's own targeted display onto this
    // display. When the stream names no targeted display, fall back to the
    // frame's content peak so the mapping stays bounded.
    settings.sourcePeak = std::clamp(metadata.windows[0].signalPeakNits, 1.0f, 10000.0f);
    settings.targetPeak = metadata.hasTargetedDisplay && metadata.targetedDisplayNits > 0.0f
        ? std::clamp(metadata.targetedDisplayNits, 1.0f, 10000.0f)
        : std::max(settings.sourcePeak, 1.0f);
    // A transition band keeps adjacent windows from meeting at a hard edge. The
    // value is a fraction of the normalised frame, so it is resolution
    // independent. Single-window streams never consult it.
    settings.windowBlend = built > 1 ? kDynamicWindowBlend : 0.0f;
    const auto write = [](float (&minimum)[2], float (&maximum)[2],
        const HdrProcessingWindow& window) noexcept {
        minimum[0] = window.upperLeftX; minimum[1] = window.upperLeftY;
        maximum[0] = window.lowerRightX; maximum[1] = window.lowerRightY;
    };
    // Unused slots have to be parked one by one. A slot left at the zero-initialised
    // rectangle is not "no area": the blend ramps are symmetric about each border, so
    // a degenerate box at the origin still weighs in -- and its row in the curve LUT
    // was never filled, because only `built` rows are written into a zeroed array. A
    // two-window table therefore blended an all-zero curve into the whole picture.
    write(settings.windowMin0, settings.windowMax0, metadata.windows[0]);
    if (built > 1) {
        write(settings.windowMin1, settings.windowMax1, metadata.windows[1]);
    } else {
        settings.windowMin1[0] = settings.windowMin1[1] = 2.0f;
        settings.windowMax1[0] = settings.windowMax1[1] = 3.0f;
    }
    if (built > 2) {
        write(settings.windowMin2, settings.windowMax2, metadata.windows[2]);
    } else {
        settings.windowMin2[0] = settings.windowMin2[1] = 2.0f;
        settings.windowMax2[0] = settings.windowMax2[1] = 3.0f;
    }
    return true;
}

FFFResult PlayerVideoRenderer::SetInjectedHdrMetadata(
    const FFF3FPHdrDynamicMetadataEntry* entries, const std::uint32_t count) noexcept {
    const auto result = hdrProcessor_.SetInjectedMetadata(entries, count);
    // Flag before redrawing: the pump's next draw refreshes the shader inputs on its own
    // call stack, so a table installed while paused or at end-of-stream reaches the
    // picture, not only the diagnostics.
    if (result == FFFResult::Success) {
        hdrInputsDirty_.store(true);
        (void)Redraw();
    }
    return result;
}

void PlayerVideoRenderer::ClearInjectedHdrMetadata() noexcept {
    hdrProcessor_.ClearInjectedMetadata();
    hdrInputsDirty_.store(true);
    (void)Redraw();
}

std::uint32_t PlayerVideoRenderer::HdrMetadataSource() const noexcept {
    return hdrProcessor_.InjectedMetadataActive()
        ? static_cast<std::uint32_t>(FFF3FPHdrMetadataSource::Injected)
        : static_cast<std::uint32_t>(FFF3FPHdrMetadataSource::Bitstream);
}

void PlayerVideoRenderer::InvalidateHdrMetadataCache() noexcept {
    lastHdrMetadata_ = {};
    hdrMetadataValid_ = false;
}

void PlayerVideoRenderer::SetHdrMetadata() noexcept {
    if (swapChain_ == nullptr || !swapHdr_) return;
    // An SDR source presented on the scRGB chain has no mastering display and no
    // content light level. The fallback HDR10 block would still declare Rec.2020
    // primaries and a 100-nit peak the source never claimed, which is exactly the
    // kind of hint that makes a display tone map the picture. Declare "none" and
    // let DWM apply its default mapping to the linear values we hand over.
    if (hdrProcessor_.State().format == FFF3FPHdrFormat::Sdr) {
        swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        lastHdrMetadata_ = {};
        hdrMetadataValid_ = false;
        return;
    }
    DXGI_HDR_METADATA_HDR10 metadata{};
    hdrProcessor_.BuildDxgiHdr10Metadata(metadata);
    // With ST 2094 metadata present these fields now change per scene, and DWM
    // does not take kindly to a new block on every frame: pushing one per frame
    // makes the display's tone mapping visibly hunt. Only submit when a field
    // moved by more than a perceptual threshold.
    if (hdrMetadataValid_ && !HdrMetadataChanged(lastHdrMetadata_, metadata)) return;
    lastHdrMetadata_ = metadata;
    hdrMetadataValid_ = true;
    swapChain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
}

FFFResult PlayerVideoRenderer::Render(const AVFrame* frame, const std::int64_t frameIndex,
    const bool limitToNativeSize,
    const bool coverArt, const bool prepareOnly, const bool imageMode) noexcept {
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return FFFResult::InvalidArgument;
    const auto* extensionEnhancementFrame = EnhancementFrame(frame);
    const auto hdrState = hdrProcessor_.ProcessFrame(
        frame, hdrPeakNits_, paperWhiteNits_, frameIndex);
    struct PlaybackWorkGuard final {
        std::atomic<std::uint32_t>& pending;
        explicit PlaybackWorkGuard(std::atomic<std::uint32_t>& value) noexcept : pending(value) {
            pending.fetch_add(1, std::memory_order_acq_rel);
        }
        ~PlaybackWorkGuard() { pending.fetch_sub(1, std::memory_order_acq_rel); }
    } playbackWorkGuard(playbackWorkPending_);
    const auto width = static_cast<std::uint32_t>(frame->width);
    const auto height = static_cast<std::uint32_t>(frame->height);
    // Captured here, before any early return: the refresh path re-resolves the
    // transfer from these, and pairing this frame's format with the previous
    // frame's trc is exactly what the frame's own declaration is there to prevent.
    const bool frameDeclaresTrc = frame != nullptr && frame->color_trc != AVCOL_TRC_UNSPECIFIED;
    auto transferFromFrame = 0u;
    if (frameDeclaresTrc) {
        transferFromFrame = frame->color_trc == AVCOL_TRC_ARIB_STD_B67 ? 2u :
            frame->color_trc == AVCOL_TRC_SMPTE2084 ? 1u : 0u;
    }
    lastFrameDeclaresTrc_ = frameDeclaresTrc;
    lastTransferFromFrame_ = transferFromFrame;
    // YUV matrix selection stays a property of the transfer/colorspace pair;
    // the shader's gamut switch is driven by the primaries instead.
    const auto source2020 = hdrState.format != FFF3FPHdrFormat::Sdr || IsRec2020(frame);
    const auto gamut = ResolveSourceGamut(frame, hdrState.format != FFF3FPHdrFormat::Sdr);
    auto input = DescribeInput(static_cast<AVPixelFormat>(frame->format));
    const auto d3d11Frame = frame->format == AV_PIX_FMT_D3D11;
    if (d3d11Frame && frame->hw_frames_ctx != nullptr) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        input = DescribeInput(frames->sw_format);
    }
    // Layout 3 is interleaved half-float RGBA (JPEG XR's rgbaf16le): like layout 0 it
    // is sampled directly as RGB, so it must NOT take the planar/semi-planar upload
    // path. Unlike layout 0 it needs no CPU conversion at all -- the decoded buffer is
    // already half-float, which is bit-compatible with DXGI_FORMAT_R16G16B16A16_FLOAT,
    // so sws_scale would only re-quantise it into an integer range that cannot hold the
    // negatives and super-whites float RGB legitimately carries.
    const auto floatRgb = input.layout == 3;
    const auto directYuv = input.layout != 0 && !floatRgb;
    // The size that gets uploaded has to be settled before the CPU conversion below,
    // because the layout-0 arm folds an engaged downscale into that conversion (see the
    // fold inside it). The planar arms and float RGB keep deciding at the pipeline, where
    // the resample has always happened, so their timing is unchanged.
    auto uploadWidth = width;
    auto uploadHeight = height;
    auto uploadPlanes = frame->data;
    auto uploadLines = frame->linesize;
    if (!directYuv && !floatRgb) {
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
        // Same size in and out unless the adaptive policy folded a downscale into this
        // pass (uploadWidth/uploadHeight above), so this is normally a pure format
        // conversion (rgb24 -> BGRA) and sws already takes its unscaled path. Measured on
        // a 4K rgb24 frame: 4.73 ms with BILINEAR against 4.76 ms with POINT, i.e.
        // identical -- the cost is memory bandwidth (~58 MB moved at ~12 GB/s), not
        // filtering. The flags are therefore not worth changing; a real win would need the
        // conversion itself to move to the GPU.
        // The fold: when the policy is engaged this pass resamples to the destination size
        // instead of the frame size, so the buffer, the texture EnsurePipeline creates and
        // the row pitch of the upload all describe the same picture. Converting at full
        // frame size and pouring that into the smaller texture is what used to leave a
        // magnified top-left corner of the frame on screen.
        const auto fittedWidth = lastDestWidth_.load(std::memory_order_relaxed);
        const auto fittedHeight = lastDestHeight_.load(std::memory_order_relaxed);
        // Deliberately the same guards PrepareAdaptiveDownscale applies, minus its
        // resample: that one produces planes in the *decoded* format (rgb24 and friends),
        // which this arm cannot upload into a BGRA / RGBA64LE texture. Keep the two in
        // step if those guards ever change.
        if (adaptiveDownscaleEnabled_.load(std::memory_order_acquire) &&
            adaptiveDownscaleActive_.load(std::memory_order_acquire) &&
            fittedWidth != 0 && fittedHeight != 0 &&
            (width > fittedWidth || height > fittedHeight)) {
            uploadWidth = fittedWidth;
            uploadHeight = fittedHeight;
        }
        scaler_ = sws_getCachedContext(scaler_, frame->width, frame->height,
            static_cast<AVPixelFormat>(frame->format), static_cast<int>(uploadWidth),
            static_cast<int>(uploadHeight), convertedFormat,
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
            static_cast<std::size_t>(uploadWidth) * uploadHeight * bytesPerPixel);
        std::uint8_t* outputData[] = { convertedRgb_.data(), nullptr, nullptr, nullptr };
        int outputLines[] = { static_cast<int>(uploadWidth * bytesPerPixel), 0, 0, 0 };
        if (sws_scale(scaler_, frame->data, frame->linesize, 0, frame->height, outputData, outputLines) <= 0) {
            SetError("FFmpeg could not convert the decoded video frame."); return FFFResult::FfmpegFailure;
        }
        softwareConvert100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - conversionStart).count() / 100));
    }
    const auto deviceWaitStart = std::chrono::steady_clock::now();
    const auto interactiveMove = interactiveMove_.load(std::memory_order_acquire);
    std::unique_lock deviceLock(deviceMutex_, std::defer_lock);
    if (interactiveMove) (void)deviceLock.try_lock();
    else deviceLock.lock();
    deviceLockWait100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - deviceWaitStart).count() / 100));
    if (!deviceLock.owns_lock()) {
        // The timed-text/presentation thread owns the immediate context. Do not
        // make the playback worker wait behind it; the next decoded frame will
        // supersede this one and audio can continue on schedule.
        coalescedVideoFrames_.fetch_add(1, std::memory_order_relaxed);
        return FFFResult::Success;
    }
    // Publish extension eligibility under deviceMutex_ before EnsurePipeline.
    extensionEligible_ = hdrState.format == FFF3FPHdrFormat::DolbyVision &&
        av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA) != nullptr;
    // Keep the logical render counter useful for headless/clip-mode sessions;
    // swapChainPresents remains the separate counter for real DXGI presents.
    if (window_ == nullptr) {
        ++presentedVideoFrames_;
        return FFFResult::Success;
    }
    // Ordinary SDR uploads do not access the back buffer. Avoid waiting for a
    // blocking Present when the chain's size/precision/color contract is stable.
    // All resize, HDR and output reconfiguration still take both locks.
    RECT client{};
    const bool stableSdrChain = swapChain_ != nullptr && !swapHdr_ &&
        requestedMode_ == FFF3FPColorMode::MapToSdr &&
        actualMode_ == FFF3FPColorMode::MapToSdr &&
        PreferredOutputBitDepth(input.bitDepth, false) == swapOutputBits_ &&
        GetClientRect(window_, &client) && client.right > 0 && client.bottom > 0 &&
        static_cast<std::uint32_t>(client.right) == swapWidth_ &&
        static_cast<std::uint32_t>(client.bottom) == swapHeight_;
    if (!stableSdrChain) {
        std::unique_lock presentLock(presentMutex_, std::defer_lock);
        if (interactiveMove) (void)presentLock.try_lock();
        else presentLock.lock();
        if (!presentLock.owns_lock()) {
            coalescedVideoFrames_.fetch_add(1, std::memory_order_relaxed);
            return FFFResult::Success;
        }
        const auto chainResult = EnsureSwapChain(frame->width, frame->height, input.bitDepth);
        if (chainResult != FFFResult::Success) return chainResult;
        if (swapHdr_) SetHdrMetadata();
    }
    // Adaptive downscale must be decided BEFORE the pipeline is created: the source
    // textures are sized here, and uploading a resampled frame into full-size textures
    // would save nothing while also mismatching the source stride. When the policy is
    // engaged the frame is resampled to the display size and the pipeline is built for
    // that smaller size instead. Layout 0 settled its share above, inside the colour
    // conversion, so the planes resampled here only ever feed the planar arms and float.
    const auto destWidth = lastDestWidth_.load(std::memory_order_relaxed);
    const auto destHeight = lastDestHeight_.load(std::memory_order_relaxed);
    if ((directYuv || floatRgb) &&
        PrepareAdaptiveDownscale(frame, width, height, destWidth, destHeight)) {
        uploadWidth = adaptiveSourceWidth_;
        uploadHeight = adaptiveSourceHeight_;
        uploadPlanes = AdaptivePlanes();
        uploadLines = AdaptiveLines();
    }
    const auto pipelineResult = EnsurePipeline(uploadWidth, uploadHeight, input.layout, input.bitDepth,
        input.chromaWidthShift, input.chromaHeightShift, d3d11Frame);
    if (pipelineResult != FFFResult::Success) return pipelineResult;
    extensionReconstructed_ = false;
    bool reconstructEnhancement = false;
    if (extensionShader_ && extensionConstants_) {
        FFFColorExtensionOutput output{};
        output.size = sizeof(output);
        bool applied = false;
        const auto* metadata = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        if (directYuv && metadata && actualMode_ != FFF3FPColorMode::RawHdrAsSdr) {
            const FFFColorExtensionInput request{sizeof(FFFColorExtensionInput),
                FFFColorExtensionVersion, avutil_version(), hdrState.dolbyVisionProfile,
                metadata->data, metadata->size, extensionEnhancementFrame,
                extensionEnhancementFrame == nullptr ? 0u : static_cast<std::uint32_t>(extensionEnhancementFrame->width),
                extensionEnhancementFrame == nullptr ? 0u : static_cast<std::uint32_t>(extensionEnhancementFrame->height),
                extensionEnhancementFrame == nullptr ? 0u : static_cast<std::uint32_t>(extensionEnhancementFrame->format)};
            applied = GetColorExtension()->prepare(&request, &output) != 0;
            if (applied && (output.flags & FFFColorExtensionGpuEnhancement) != 0)
                applied = UploadExtensionEnhancement(extensionEnhancementFrame);
            reconstructEnhancement = applied && (output.flags & FFFColorExtensionGpuEnhancement) != 0;
        }
        if (!applied) std::memset(output.constants, 0, sizeof(output.constants));
        context_->UpdateSubresource(extensionConstants_, 0, nullptr, output.constants, 0, 0);
        hdrProcessor_.SetExtensionProcessing(output.sourcePeakNits, applied);
        if (applied) {
            if (swapHdr_) SetHdrMetadata();
        }
    } else {
        hdrProcessor_.SetExtensionProcessing(0.0f, false);
    }
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
        // Planar (1) and semi-planar (2) frames arrive as CPU planes and must be
        // copied into shader-visible textures. Two upload strategies, picked by size:
        //
        //  - Small frames: Map(WRITE_DISCARD) into a DYNAMIC texture. The driver can
        //    rename storage rather than waiting for the previous frame to stop
        //    sampling the same DEFAULT texture, which is why this path exists.
        //  - Large frames (>= 4K): UpdateSubresource into a DEFAULT texture. Map
        //    returns write-combined memory and copying into it row by row loses badly
        //    as rows widen -- measured at 8K, 22.27 ms against 15.21 ms for the same
        //    bytes, while at 1080p the two are within 6%. EnsurePipeline creates the
        //    matching texture usage, so these two decisions must stay in step.
        const auto uploadStart = std::chrono::steady_clock::now();
        // uploadPlanes/uploadLines already point at the resampled copy when the
        // adaptive policy engaged, and uploadWidth/uploadHeight match the textures
        // EnsurePipeline just created for that size.
        const auto largeFrame = UseDefaultUploadForSize(uploadWidth, uploadHeight);
        if (input.layout == 1 || input.layout == 2) {
            const auto planeCount = input.layout == 1 ? 3u : 2u;
            if (largeFrame) {
                for (unsigned plane = 0; plane < planeCount; ++plane)
                    context_->UpdateSubresource(sourceTextures_[plane], 0, nullptr,
                        uploadPlanes[plane], uploadLines[plane], 0);
            } else {
                for (unsigned plane = 0; plane < planeCount; ++plane) {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (FAILED(context_->Map(sourceTextures_[plane], 0,
                        D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                        SetError("Could not map the decoded frame plane.");
                        return FFFResult::DeviceFailure;
                    }
                    const auto xShift = plane == 0 ? 0u : input.chromaWidthShift;
                    const auto yShift = plane == 0 ? 0u : input.chromaHeightShift;
                    const auto rows = (uploadHeight + (1u << yShift) - 1) >> yShift;
                    const auto rowBytes = ((uploadWidth + (1u << xShift) - 1) >> xShift) *
                        (input.bitDepth > 8 ? 2u : 1u);
                    for (unsigned row = 0; row < rows; ++row)
                        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) +
                            static_cast<std::size_t>(row) * mapped.RowPitch,
                            uploadPlanes[plane] + static_cast<std::ptrdiff_t>(row) *
                                uploadLines[plane], rowBytes);
                    context_->Unmap(sourceTextures_[plane], 0);
                }
            }
        } else {
            context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, uploadPlanes[0],
                uploadLines[0], 0);
        }
        upload100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - uploadStart).count() / 100));
    } else if (floatRgb) {
        // Verbatim (no quantisation) so negatives and super-whites survive; taken from uploadPlanes/
        // uploadLines, like directYuv above, so an engaged downscale cannot leave a top-left crop.
        const auto floatUploadStart = std::chrono::steady_clock::now();
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, uploadPlanes[0], uploadLines[0], 0);
        upload100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - floatUploadStart).count() / 100));
    } else {
        // Layout 0: convertedRgb_ already holds the frame at uploadWidth x uploadHeight,
        // so the row pitch has to be the uploaded width. A decoded-width pitch inside a
        // destination-sized texture makes UpdateSubresource read only the top-left corner
        // of the conversion and drop the rest of the frame.
        const auto bytesPerPixel = input.bitDepth <= 8 ? 4u : 8u;
        const auto rgbUploadStart = std::chrono::steady_clock::now();
        context_->UpdateSubresource(sourceTextures_[0], 0, nullptr, convertedRgb_.data(),
            uploadWidth * bytesPerPixel, 0);
        upload100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now() - rgbUploadStart).count() / 100));
    }
    if (reconstructEnhancement) {
        extensionReconstructed_ = ReconstructExtensionEnhancement(width, height, input.layout, input.sampleScale);
        if (!extensionReconstructed_) {
            const unsigned char zero[FFFColorExtensionCapacity]{};
            context_->UpdateSubresource(extensionConstants_, 0, nullptr, zero, 0, 0);
            hdrProcessor_.SetExtensionProcessing(0, false);
        }
    }
    ShaderSettings settings{};
    settings.colorMode = static_cast<std::uint32_t>(actualMode_);
    settings.reserved = 0;
    // The decode transfer function must follow the *pixels*, not the metadata
    // classification. ST 2094 dynamic metadata (HDR10+, HDR Vivid) is defined
    // for streams that keep their own encoding: metadata arriving later must
    // not change how the samples are decoded. A "HLG signal + HDR Vivid
    // metadata" stream decoded as PQ over-brightens its highlights by an order
    // of magnitude, and would flip transfer mid-playback.
    // The decision itself lives in ResolveDecodeTransfer, shared with the
    // injection refresh path.
    settings.transfer = ResolveDecodeTransfer(hdrState);
    settings.gamut = gamut;
    // Half-float RGB (JPEG XR's rgbaf16le) is linear scRGB by construction: jxrlib
    // documents these formats as "scRGB formats. Gamma is 1.0", and the decoder sets
    // no colour tags at all, so every metadata-driven guess above would be wrong --
    // it would land on transfer 0 and sRGB-decode already-linear pixels, clipping the
    // negatives and super-whites the FP16 texture was chosen to preserve.
    //
    // Keyed on the PIXEL FORMAT rather than a measured dynamic range: WIC's rule is
    // that integer RGB is sRGB and float RGB is scRGB, independent of how wide the
    // values happen to be (FH4-HDR.jxr peaks at only 4.56, so a ">1.0 means linear"
    // heuristic would misfire). Transfer 3 means "already linear, do not decode".
    // Gamut is forced to 709 after `gamut` is assigned, not before -- scRGB is defined
    // on Rec.709 primaries, and assigning it earlier would be silently overwritten.
    if (floatRgb) {
        settings.transfer = 3u;
        settings.gamut = 0u;
    }
    settings.sdrPeak = sdrPeakNits_;
    settings.hdrPeak = settings.transfer == 0 ? 100.0f : hdrProcessor_.State().sourcePeakNits;
    sourcePeakNits_ = settings.hdrPeak;
    // ST 2094 dynamic tone mapping owns the SDR mapping path only. The HDR
    // (scRGB) path hands linear light to the display unchanged, so a curve there
    // would double-map; mapToHdr uses per-frame DXGI metadata instead
    // (SetHdrMetadata).
    DynamicShaderSettings dynamicSettings{};
    std::array<float, kDynamicCurveLutWidth * kHdrMaxProcessingWindows> dynamicCurves{};
    const bool useDynamicCurve = !prepareOnly && settings.transfer != 0 &&
        actualMode_ != FFF3FPColorMode::MapToHdr &&
        actualMode_ != FFF3FPColorMode::RawHdrAsSdr &&
        BuildDynamicToneSettings(hdrState.dynamic, dynamicSettings, dynamicCurves);
    // Upload unconditionally so the GPU state never keeps a previous frame's
    // curve when this frame has none: the buffer and texture are bound on every
    // draw, and a stale table with enabled==0 would still be sampled if the
    // shader were ever changed to ignore the gate.
    if (dynamicConstants_ != nullptr) {
        context_->UpdateSubresource(dynamicConstants_, 0, nullptr, &dynamicSettings, 0, 0);
        if (dynamicCurveTexture_ != nullptr) {
            context_->UpdateSubresource(dynamicCurveTexture_, 0, nullptr,
                dynamicCurves.data(), kDynamicCurveLutWidth * sizeof(float), 0);
        }
    }
    settings.paperWhite = EffectivePaperWhiteNits();
    settings.targetPeak = hdrState.targetPeakNits;
    // Report the size actually uploaded: when adaptive downscale engaged this is the
    // resampled size, and the shader scales from that. Using the original frame size
    // here would make the shader sample a texture it believes is larger than it is.
    settings.sourceWidth = static_cast<float>(uploadWidth);
    settings.sourceHeight = static_cast<float>(uploadHeight);
    settings.outputWidth = static_cast<float>(swapWidth_); settings.outputHeight = static_cast<float>(swapHeight_);
    settings.inputLayout = extensionReconstructed_ ? 1u : input.layout;
    settings.sampleScale = extensionReconstructed_ ? 65535.0f / 1023.0f : input.sampleScale;
    cachedOriginalInputLayout_ = input.layout;
    cachedOriginalSampleScale_ = input.sampleScale;
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
    // A still image that is being enlarged (its fitted destination is larger than the
    // source) switches SampleVideo to pixel-block sampling so the picture keeps its
    // own pixel grid. Shrinking one still needs the smoothing kernel, so the flag is
    // only set when the destination actually exceeds the source.
    settings.imageModeX = 0.0f;
    sourceColorSpace_ = frame->colorspace;
    sourceChromaLocation_ = chromaLocation;
    sourceFullRange_ = IsFullRange(frame);
    sourceInterlaced_ = (frame->flags & AV_FRAME_FLAG_INTERLACED) != 0;
    static_assert(sizeof(CachedVideoSettings) == sizeof(ShaderSettings));
    std::memcpy(&cachedVideoSettings_, &settings, sizeof(settings));
    sourceLimitedToNativeSize_ = limitToNativeSize;
    sourceCoverArt_ = coverArt;
    sourceImageMode_ = imageMode;
    if (prepareOnly) return FFFResult::Success;
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
    timedTextCondition_.notify_one();
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::Redraw() noexcept {
    {
        std::lock_guard deviceLock(deviceMutex_);
        if (!hasCachedVideo_ || window_ == nullptr) return FFFResult::Success;
        std::lock_guard presentLock(presentMutex_);
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

std::uint32_t PlayerVideoRenderer::ResolveDecodeTransfer(
    const HdrFrameState& hdrState) const noexcept {
    //   1) Dolby Vision keeps its compatibility-layer decision: the RPU
    //      remapping has its own semantics that a container trc cannot express.
    //   2) Otherwise follow the frame's own color_trc when it declares one.
    //   3) Fall back to the format-based inference for frames that declare
    //      nothing (previous behaviour).
    const auto hlgCompatibility = static_cast<std::uint32_t>(FFF3FPHdrCompatibility::Hlg);
    if (hdrState.format == FFF3FPHdrFormat::DolbyVision)
        return (hdrState.compatibility & hlgCompatibility) != 0 ? 2u : 1u;
    if (hdrState.format != FFF3FPHdrFormat::Sdr && lastFrameDeclaresTrc_ &&
        lastTransferFromFrame_ != 0u)
        return lastTransferFromFrame_;
    return hdrState.format == FFF3FPHdrFormat::Hlg ? 2u :
        (hdrState.format != FFF3FPHdrFormat::Sdr ? 1u : 0u);
}

void PlayerVideoRenderer::RefreshHdrShaderInputs() noexcept {
    // Present-path only (called from DrawWithShader under the device/present locks):
    // cachedVideoSettings_ and the constant buffers belong to that lock, not to the
    // thread that received FFF3FP_SetHdrDynamicMetadata.
    if (!hasCachedVideo_ || context_ == nullptr) return;
    const auto hdrState = hdrProcessor_.State();
    cachedVideoSettings_.transfer = ResolveDecodeTransfer(hdrState);
    cachedVideoSettings_.hdrPeak = cachedVideoSettings_.transfer == 0 ?
        100.0f : hdrState.sourcePeakNits;
    sourcePeakNits_ = cachedVideoSettings_.hdrPeak;
    cachedVideoSettings_.paperWhite = EffectivePaperWhiteNits();
    cachedVideoSettings_.targetPeak = hdrState.targetPeakNits;
    // Same gate as the per-frame path, so a refresh cannot enable a curve that the
    // frame path would have refused.
    DynamicShaderSettings dynamicSettings{};
    std::array<float, kDynamicCurveLutWidth * kHdrMaxProcessingWindows> dynamicCurves{};
    const bool useDynamicCurve = cachedVideoSettings_.transfer != 0 &&
        actualMode_ != FFF3FPColorMode::MapToHdr &&
        actualMode_ != FFF3FPColorMode::RawHdrAsSdr &&
        BuildDynamicToneSettings(hdrState.dynamic, dynamicSettings, dynamicCurves);
    if (!useDynamicCurve) {
        // Guarded rather than left to short-circuit order: whatever the gate decides,
        // the buffer that reaches the shader has to agree with it.
        dynamicSettings = DynamicShaderSettings{};
        dynamicCurves = {};
    }
    // Uploaded unconditionally, exactly like the per-frame path: a stale table with
    // enabled == 0 must never survive into the next draw.
    if (dynamicConstants_ != nullptr) {
        context_->UpdateSubresource(dynamicConstants_, 0, nullptr, &dynamicSettings, 0, 0);
        if (dynamicCurveTexture_ != nullptr) {
            context_->UpdateSubresource(dynamicCurveTexture_, 0, nullptr,
                dynamicCurves.data(), kDynamicCurveLutWidth * sizeof(float), 0);
        }
    }
}

void PlayerVideoRenderer::ReleaseCoverBackdropResources() noexcept {
    if (d2dContext_ != nullptr) d2dContext_->SetTarget(nullptr);
    if (coverBackdropBlurEffect_ != nullptr) {
        coverBackdropBlurEffect_->SetInput(0, nullptr);
        coverBackdropBlurEffect_->Release(); coverBackdropBlurEffect_ = nullptr;
    }
    ReleaseCom(d2dCoverBackdropTarget_);
    ReleaseCom(d2dCoverBackdropSource_);
    ReleaseCom(coverBackdropSourceTarget_);
    ReleaseCom(coverBackdropSourceTexture_);
    ReleaseCom(coverBackdropView_);
    ReleaseCom(coverBackdropTexture_);
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
    // Keep source-relative cache sizing and FP16 values above 1.0 for HDR blur.
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
    const auto sourceWidth = cachedVideoSettings_.sourceWidth;
    const auto sourceHeight = cachedVideoSettings_.sourceHeight;
    const auto outputWidth = cachedVideoSettings_.outputWidth;
    const auto outputHeight = cachedVideoSettings_.outputHeight;
    cachedVideoSettings_.sourceWidth = static_cast<float>(coverBackdropWidth_);
    cachedVideoSettings_.sourceHeight = static_cast<float>(coverBackdropHeight_);
    cachedVideoSettings_.outputWidth = static_cast<float>(swapWidth_);
    cachedVideoSettings_.outputHeight = static_cast<float>(swapHeight_);
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
    cachedVideoSettings_.sourceWidth = sourceWidth;
    cachedVideoSettings_.sourceHeight = sourceHeight;
    cachedVideoSettings_.outputWidth = outputWidth;
    cachedVideoSettings_.outputHeight = outputHeight;
    return FFFResult::Success;
}

// ---- Presentation-policy toggle (exported as FFF3FP_SetPresentConfig) ----
FFFResult PlayerVideoRenderer::SetPresentConfig(const bool enableTearing) noexcept {
    // Preference only: the chain already carries DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
    // when the adapter supports it, so no chain recreation is needed, and the request
    // outlives the next creation because it is stored apart from that capability.
    // Takes effect on the next Present.
    tearingRequested_.store(enableTearing, std::memory_order_release);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::GetRenderTargetInfo(RenderTargetInfo& info) noexcept {
    std::lock_guard lock(deviceMutex_);
    info = {};
    // Report "not ready" instead of an all-zero Success. A zeroed struct is
    // indistinguishable from a real 0x0 target, and callers use these numbers to
    // map pixel-probe coordinates, so a stale/zero swap size would silently
    // produce wrong probe results rather than a diagnosable failure.
    if (swapChain_ == nullptr || device_ == nullptr || context_ == nullptr)
        return FFFResult::InvalidState;
    info.swapWidth = swapWidth_;
    info.swapHeight = swapHeight_;
    info.outputBitDepth = swapOutputBits_.load(std::memory_order_relaxed);
    info.hdr = swapHdr_;
    if (window_ != nullptr && IsWindow(window_)) {
        RECT client{};
        if (GetClientRect(window_, &client)) {
            info.clientWidth = static_cast<std::uint32_t>(client.right - client.left);
            info.clientHeight = static_cast<std::uint32_t>(client.bottom - client.top);
        }
    }
    info.destX = lastDestX_.load(std::memory_order_relaxed);
    info.destY = lastDestY_.load(std::memory_order_relaxed);
    info.destWidth = lastDestWidth_.load(std::memory_order_relaxed);
    info.destHeight = lastDestHeight_.load(std::memory_order_relaxed);
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::Set360View(const bool enabled, const float yaw,
    const float pitch, const float fovY) noexcept {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(fovY) || fovY <= 0.0f)
        return FFFResult::InvalidArgument;
    view360YawBits_.store(std::bit_cast<float>(std::remainder(yaw, 360.0f)),
        std::memory_order_relaxed);
    view360PitchBits_.store(std::bit_cast<float>(std::clamp(pitch, -89.0f, 89.0f)),
        std::memory_order_relaxed);
    view360FovYBits_.store(std::bit_cast<float>(std::clamp(fovY, 30.0f, 90.0f)),
        std::memory_order_relaxed);
    // Publish the enable flag last so an acquire load observes this complete
    // yaw/pitch/FOV tuple rather than a partially updated camera state.
    projection360Enabled_.store(enabled ? 1u : 0u, std::memory_order_release);
    // View updates wake the presenter directly and coalesce to the latest camera tuple.
    if (!view360RedrawPending_.exchange(true, std::memory_order_acq_rel)) {
        {
            std::lock_guard lock(timedTextMutex_);
            ++presentationGeneration_;
        }
        timedTextCondition_.notify_one();
    }
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::DrawCachedVideo(ID3D11RenderTargetView* target,
    const std::uint32_t outputWidth, const std::uint32_t outputHeight) noexcept {
    if (!hasCachedVideo_ || target == nullptr) return FFFResult::InvalidState;
    if (outputWidth == 0 || outputHeight == 0) return FFFResult::InvalidArgument;
    const auto projection360 = projection360Enabled_.load(std::memory_order_acquire) != 0;
    VideoDestination destination{};
    if (lyricsLayoutEnabled_.load(std::memory_order_acquire) && sourceLimitedToNativeSize_) {
        destination = CalculateLyricsCoverDestination(sourceWidth_, sourceHeight_,
            outputWidth, outputHeight,
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
    } else if (projection360) {
        destination = {0, 0, outputWidth, outputHeight};
    } else {
        const auto aspect = discAspect_.load();
        // A rotated view presents the picture with its axes swapped, so the fit
        // box must be computed from the swapped dimensions; otherwise a portrait
        // photo keeps a landscape box and is letterboxed into a sliver.
        const auto rotation = viewRotation_.load(std::memory_order_acquire);
        const auto swapAxes = (rotation & 1u) != 0;
        const auto layoutWidth = swapAxes ? sourceHeight_ : sourceWidth_;
        const auto layoutHeight = swapAxes ? sourceWidth_ : sourceHeight_;
        destination = CalculateVideoDestination(
            aspect > 0 ? static_cast<unsigned>(layoutHeight * aspect + 0.5f) : layoutWidth,
            layoutHeight, outputWidth, outputHeight,
            sourceLimitedToNativeSize_ || fitLimitToNative_.load(std::memory_order_acquire));
    }
    // Apply the view transform (zoom + pan) around the destination center.
    // Zoom scales the fitted video box; pan offsets are normalized to the
    // unzoomed box and clamped to the interval the transformed box can actually
    // occupy: z > 1 slides a magnified box that still covers the fitted box,
    // z < 1 slides a shrunken box inside it (letterbox around the picture).
    const auto zoom = std::bit_cast<float>(viewZoomBits_.load(std::memory_order_acquire));
    const auto panX = std::bit_cast<float>(viewPanXBits_.load(std::memory_order_acquire));
    const auto panY = std::bit_cast<float>(viewPanYBits_.load(std::memory_order_acquire));
    if (!projection360 && std::abs(zoom - 1.0f) > 1e-4f) {
        const float fittedWidth = static_cast<float>(destination.width);
        const float fittedHeight = static_cast<float>(destination.height);
        const float zoomedWidth = std::max(1.0f, fittedWidth * zoom);
        const float zoomedHeight = std::max(1.0f, fittedHeight * zoom);
        // min/max keeps clamp bounds ordered for both zoom-in and zoom-out.
        const float edgeX0 = static_cast<float>(destination.x);
        const float edgeX1 = static_cast<float>(destination.x) + fittedWidth - zoomedWidth;
        const float minX = std::min(edgeX0, edgeX1), maxX = std::max(edgeX0, edgeX1);
        const float edgeY0 = static_cast<float>(destination.y);
        const float edgeY1 = static_cast<float>(destination.y) + fittedHeight - zoomedHeight;
        const float minY = std::min(edgeY0, edgeY1), maxY = std::max(edgeY0, edgeY1);
        // Travel range = half the interval, and it must be an **absolute** value: the
        // old max(maxPan, 0) form swallowed it to zero for z < 1, which killed panning
        // entirely in the downscaled state. The mapping stays continuous through z = 1.
        //
        // pan is positive = the picture moves right/down, i.e. it follows the pointer.
        // The sign here was the opposite, which made a drag push the image away from the
        // cursor -- measured with pan_direction_probe: panX = +0.5 moved the content 316
        // px left, and the usual convention (lakeUI's PixelPictureBox, and scroll views
        // generally) is that dragging right reveals what is to the left, so the image
        // travels with the pointer. Adding the offset instead of subtracting matches that.
        const float offsetX = panX * std::abs(zoomedWidth - fittedWidth) / 2.0f;
        const float offsetY = panY * std::abs(zoomedHeight - fittedHeight) / 2.0f;
        destination.x = static_cast<std::int32_t>(std::lround(
            std::clamp((minX + maxX) * 0.5f + offsetX, minX, maxX)));
        destination.y = static_cast<std::int32_t>(std::lround(
            std::clamp((minY + maxY) * 0.5f + offsetY, minY, maxY)));
        destination.width = static_cast<std::uint32_t>(zoomedWidth);
        destination.height = static_cast<std::uint32_t>(zoomedHeight);
    }
    ID3D11ShaderResourceView* presentationViews[3]{};
    if (projection360) {
        // The panorama projection is view-dependent. Scaling the equirectangular
        // image to the window first throws away source detail (and can distort
        // its 2:1 aspect ratio), especially in small windows. Sample the native
        // source planes directly in the projection shader instead.
        videoSuperResolutionActive_.store(false, std::memory_order_release);
        std::copy(std::begin(sourceViews_), std::end(sourceViews_), presentationViews);
    } else {
        // Rebuild the per-frame source views from the pipeline's decoded planes,
        // then optionally redirect them at the RTX VSR upscale result. The
        // pipeline-owned sourceViews_ array is never mutated, so switching VSR
        // on or off mid-stream cannot corrupt the next frame.
        for (std::size_t plane = 0; plane < ARRAYSIZE(effectiveSourceViews_); ++plane)
            effectiveSourceViews_[plane] = sourceViews_[plane];
        if (ApplySuperResolution(destination.width, destination.height) ==
                FFFResult::Success &&
            videoSuperResolutionActive_.load(std::memory_order_acquire)) {
            for (std::size_t plane = 0; plane < ARRAYSIZE(effectiveSourceViews_); ++plane)
                effectiveSourceViews_[plane] = superResolutionViews_[plane];
        }
        const auto scaleResult = PrepareScaledVideo(destination.width, destination.height,
            presentationViews);
        if (scaleResult != FFFResult::Success) return scaleResult;
    }
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
    if (result == FFFResult::Success) {
        // Report the path that actually produced the frame. A VP-upscaled frame
        // leaves PrepareScaledVideo with zero passes (source already equals the
        // target), so this is the only place that knows VSR ran.
        actualVideoScalingMode_.store(
            videoSuperResolutionActive_.load(std::memory_order_acquire)
                ? FFF3FPVideoScalingMode::D3D11VideoProcessor
                : FFF3FPVideoScalingMode::Shader);
        // Record the drawn rect for GetRenderTargetInfo.
        lastDestX_.store(destination.x, std::memory_order_relaxed);
        lastDestY_.store(destination.y, std::memory_order_relaxed);
        lastDestWidth_.store(destination.width, std::memory_order_relaxed);
        lastDestHeight_.store(destination.height, std::memory_order_relaxed);
    }
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
        const auto* rgba = static_cast<const DirectX::PackedVector::HALF*>(mapped.pData);
        probe.red = DirectX::PackedVector::XMConvertHalfToFloat(rgba[0]);
        probe.green = DirectX::PackedVector::XMConvertHalfToFloat(rgba[1]);
        probe.blue = DirectX::PackedVector::XMConvertHalfToFloat(rgba[2]);
        probe.alpha = DirectX::PackedVector::XMConvertHalfToFloat(rgba[3]);
    }
    context_->Unmap(staging.Get(), 0);
    probe.scalingMode = actualVideoScalingMode_.load();
    probe.outputBitDepth = swapOutputBits_;
    probe.colorMode = actualMode_;
    probe.reserved = 0;
    return FFFResult::Success;
}

// Batch pixel readback. One staging copy + one Map
// replaces the per-pixel GPU round trip used by CapturePixelSampled (~14,400
// serialized flushes for a 320px thumbnail). Returns normalized RGBA floats
// (range [0,1], scRGB linear for 16-bit output) in row-major order.
FFFResult PlayerVideoRenderer::ReadPixelRegion(const std::uint32_t x, const std::uint32_t y,
    const std::uint32_t width, const std::uint32_t height, float* dst,
    const std::uint32_t dstFloatCount, std::uint32_t* outputBitDepth) noexcept {
    // Compare in 64-bit: width * height * 4 wraps around for large requests,
    // which would otherwise let an undersized dst buffer pass this check.
    if (dst == nullptr || width == 0 || height == 0 ||
        dstFloatCount < static_cast<std::uint64_t>(width) * height * 4u)
        return FFFResult::InvalidArgument;
    std::lock_guard deviceLock(deviceMutex_);
    if (!hasCachedVideo_ || swapChain_ == nullptr || device_ == nullptr || context_ == nullptr ||
        x >= swapWidth_ || y >= swapHeight_)
        return FFFResult::InvalidState;
    // The header documents dst as a width x height row-major block. Truncating
    // to the swapchain here used to return Success with a compact row pitch and
    // untouched tail samples, which the caller cannot detect. Reject instead.
    // Safe from underflow: x < swapWidth_ and y < swapHeight_ just above.
    if (width > swapWidth_ - x || height > swapHeight_ - y)
        return FFFResult::InvalidArgument;
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
    const auto sourceFormat = description.Format;
    if (sourceFormat != DXGI_FORMAT_B8G8R8A8_UNORM &&
        sourceFormat != DXGI_FORMAT_R10G10B10A2_UNORM &&
        sourceFormat != DXGI_FORMAT_R16G16B16A16_FLOAT)
        return FFFResult::NotSupported;
    // Occasional readback uses a per-call staging texture, sized to the checked region.
    const auto copyWidth = width;
    const auto copyHeight = height;
    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = copyWidth;
    stagingDesc.Height = copyHeight;
    stagingDesc.MipLevels = stagingDesc.ArraySize = 1;
    stagingDesc.Format = sourceFormat;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, &staging)))
        return FFFResult::DeviceFailure;
    const D3D11_BOX source{x, y, 0, x + copyWidth, y + copyHeight, 1};
    context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, backBuffer.Get(), 0, &source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return FFFResult::DeviceFailure;
    const auto* srcBytes = static_cast<const std::uint8_t*>(mapped.pData);
    auto* out = dst;
    for (std::uint32_t row = 0; row < copyHeight; ++row) {
        const auto* rowPtr = srcBytes + static_cast<std::size_t>(row) * mapped.RowPitch;
        if (sourceFormat == DXGI_FORMAT_B8G8R8A8_UNORM) {
            const auto* bgra = rowPtr;
            for (std::uint32_t col = 0; col < copyWidth; ++col) {
                constexpr float scale = 1.0f / 255.0f;
                out[0] = bgra[2] * scale;
                out[1] = bgra[1] * scale;
                out[2] = bgra[0] * scale;
                out[3] = bgra[3] * scale;
                bgra += 4; out += 4;
            }
        } else if (sourceFormat == DXGI_FORMAT_R10G10B10A2_UNORM) {
            const auto* packed = reinterpret_cast<const std::uint32_t*>(rowPtr);
            constexpr float rgbScale = 1.0f / 1023.0f;
            for (std::uint32_t col = 0; col < copyWidth; ++col) {
                const auto p = packed[col];
                out[0] = static_cast<float>(p & 0x3ffu) * rgbScale;
                out[1] = static_cast<float>((p >> 10) & 0x3ffu) * rgbScale;
                out[2] = static_cast<float>((p >> 20) & 0x3ffu) * rgbScale;
                out[3] = static_cast<float>((p >> 30) & 0x3u) / 3.0f;
                out += 4;
            }
        } else { // R16G16B16A16_FLOAT: each channel is HALF (2 bytes), NOT float.
            // Must convert like the single-pixel ReadPixel does. The previous code
            // reinterpret_cast to float* and memcpy'd copyWidth*4*sizeof(float) bytes,
            // which is 2x the real row size => wrong values AND out-of-bounds read.
            const auto* rgba = reinterpret_cast<const DirectX::PackedVector::HALF*>(rowPtr);
            for (std::uint32_t col = 0; col < copyWidth; ++col) {
                out[0] = DirectX::PackedVector::XMConvertHalfToFloat(rgba[col * 4u + 0]);
                out[1] = DirectX::PackedVector::XMConvertHalfToFloat(rgba[col * 4u + 1]);
                out[2] = DirectX::PackedVector::XMConvertHalfToFloat(rgba[col * 4u + 2]);
                out[3] = DirectX::PackedVector::XMConvertHalfToFloat(rgba[col * 4u + 3]);
                out += 4;
            }
        }
    }
    context_->Unmap(staging.Get(), 0);
    if (outputBitDepth != nullptr) *outputBitDepth = swapOutputBits_;
    return FFFResult::Success;
}

FFFResult PlayerVideoRenderer::PresentCurrentFrame(IDXGISwapChain4* chain,
    const std::uint64_t renderedVideoGeneration) noexcept {
    if (chain == nullptr) return FFFResult::InvalidState;
    const auto start = std::chrono::steady_clock::now();
    // Normal playback is synchronized to the display. During the native
    // window-move loop, Present must not wait for DWM: that loop owns the UI
    // thread's message pump and a blocking Present can starve audio and decode.
    const auto interactiveMove = interactiveMove_.load(std::memory_order_acquire);
    // Tearing needs the host's request *and* a chain created with the ALLOW_TEARING
    // flag: passing DXGI_PRESENT_ALLOW_TEARING to a chain without it is
    // DXGI_ERROR_INVALID_CALL, which would take playback down on the first frame.
    const auto tearing = swapAllowTearing_ &&
        tearingRequested_.load(std::memory_order_acquire);
    const auto present = interactiveMove
        ? chain->Present(0, swapAllowTearing_ ?
            (DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT) : DXGI_PRESENT_DO_NOT_WAIT)
        : tearing ? chain->Present(0, DXGI_PRESENT_ALLOW_TEARING) : chain->Present(1, 0);
    presentWait100ns_.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count() / 100));
    if (present == DXGI_ERROR_WAS_STILL_DRAWING)
        return FFFResult::Success;
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
    presentedVideoGeneration_.store(renderedVideoGeneration);
    const auto previous = countedVideoGeneration_.exchange(renderedVideoGeneration);
    if (renderedVideoGeneration != 0 && renderedVideoGeneration != previous)
        ++presentedVideoFrames_;
    if (renderedVideoGeneration > previous + 1)
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
    // Hold both locks across EnsureSwapChain: it may resize or recreate the
    // swap chain, and Present must never overlap that rewrite (the same
    // invariant the main render path enforces).
    std::unique_lock presentLock(presentMutex_);
    const auto chainResult = EnsureSwapChain(hasCachedVideo_ ? sourceWidth_ : 1,
        hasCachedVideo_ ? sourceHeight_ : 1, hasCachedVideo_ ? sourceBitDepth_ : 8);
    if (chainResult != FFFResult::Success || swapChain_ == nullptr) return chainResult;
    // Clear before drawing so an input arriving during this frame can publish a
    // fresh generation instead of being hidden by the current presentation.
    view360RedrawPending_.store(false, std::memory_order_release);
    if (!hasCachedVideo_) {
        const auto pipelineResult = EnsurePipeline(1, 1, 0, 8, 0, 0, false);
        if (pipelineResult != FFFResult::Success) return pipelineResult;
        cachedVideoSettings_ = {};
        cachedVideoSettings_.colorMode = static_cast<std::uint32_t>(actualMode_);
        cachedVideoSettings_.sdrPeak = sdrPeakNits_;
        cachedVideoSettings_.hdrPeak = sdrPeakNits_;
        cachedVideoSettings_.paperWhite = EffectivePaperWhiteNits();
        cachedVideoSettings_.targetPeak = hdrProcessor_.State().targetPeakNits;
    }
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
    const auto discResult = DrawTimedText(TimedTextLayerSlot::Disc);
    if (discResult != FFFResult::Success) return discResult;
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Danmaku);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Subtitle);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Lyrics);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::Disc);
    CompositeTimedText(backBufferTarget.Get(), TimedTextLayerSlot::PlayerInformation);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    const auto generation = videoGeneration_.load();
    // presentMutex_ protects the chain/back buffer from reconfiguration, and it is
    // still held across Present. deviceMutex_ guards the immediate context, and is
    // released for the duration so the playback worker can upload the next frame
    // instead of stalling behind a synchronised Present.
    //
    // This is safe but not free, and the cost is worth naming: the D3D11 immediate
    // context accepts commands from any thread, so an upload landing inside this
    // window is legal, but its commands interleave with the composite that is
    // already waiting on the GPU. That makes submission order across the two threads
    // non-deterministic and is the most likely source of the present-wait jitter we
    // measured (2.1-5.9 ms spread run to run, wider than the effect of any scheduling
    // change we tested). Reconfiguration cannot slip in here -- it needs presentMutex_,
    // which is still held -- so this does not risk tearing down the live chain.
    //
    // If the jitter ever becomes a problem, the clean fix is a deferred context for
    // uploads rather than re-taking this lock, which would reintroduce the stall.
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
    // Precondition: the caller holds deviceMutex_. Presenting here only needs
    // presentMutex_; swapChain_ was already validated under deviceMutex_.
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
    std::lock_guard deviceLock(deviceMutex_);
    return RequestRecoveryIfDeviceLostLocked();
}

bool PlayerVideoRenderer::RequestRecoveryIfDeviceLostLocked() noexcept {
    if (deviceRecoveryRequested_.load(std::memory_order_acquire)) return true;
    if (device_ == nullptr) return false;
    const auto reason = device_->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) return false;
    RequestDeviceRecovery(reason, "D3D11 device removal");
    return true;
}

void PlayerVideoRenderer::ReleaseDeviceObjects() noexcept {
    ReleaseVideoProcessor();
    ReleaseVideoProcessorInputSurface();
    ReleaseSuperResolutionResources();
    ReleaseOffscreenTarget();
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
        ReleaseCom(sourceViews_[plane]);
        ReleaseCom(sourceTextures_[plane]);
    }
    ReleaseCom(constants_);
    ReleaseCom(scaleConstants_);
    ReleaseCom(pointSampler_);
    ReleaseCom(panoramaSampler_);
    ReleaseCom(sampler_);
    ReleaseCom(pixelShader_);
    for (auto*& shader : sdrPixelShaders_) ReleaseCom(shader);
    ReleaseCom(extensionShader_);
    ReleaseCom(extensionConstants_);
    ReleaseExtensionEnhancement();
    ReleaseCom(extensionEnhancementShader_);
    ReleaseCom(extensionEnhancementConstants_);
    ReleaseCom(dynamicCurveView_);
    ReleaseCom(dynamicCurveTexture_);
    ReleaseCom(dynamicConstants_);
    dynamicCurveBuilt_ = false;
    extensionAttempted_ = false;
    extensionEligible_ = false;
    ReleaseCom(scalePixelShader_);
    ReleaseCom(coverBackdropPixelShader_);
    ReleaseCom(timedTextPixelShader_);
    ReleaseCom(vertexShader_);
    ReleaseCom(context_);
    ReleaseCom(device_);
    swapWidth_ = swapHeight_ = sourceWidth_ = sourceHeight_ = 0;
    swapHdr_ = false;
    swapOutputBits_ = 8;
    sourceInputLayout_ = UINT32_MAX;
    sourceBitDepth_ = 0;
    sourceChromaWidthShift_ = sourceChromaHeightShift_ = 0;
    sourceExternal_ = false;
    sourceLimitedToNativeSize_ = false;
    sourceCoverArt_ = false;
    sourceImageMode_ = false;
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
    if (adaptiveScaler_ != nullptr) { sws_freeContext(adaptiveScaler_); adaptiveScaler_ = nullptr; }
    ReleaseVideoProcessor();
    ReleaseVideoProcessorInputSurface();
    ReleaseSuperResolutionResources();
    ReleaseOffscreenTarget();
    ReleaseCoverBackdropResources();
    ReleaseScaleResources();
    for (std::size_t plane = 0; plane < ARRAYSIZE(sourceTextures_); ++plane) {
        ReleaseCom(sourceViews_[plane]);
        ReleaseCom(sourceTextures_[plane]);
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
    sourceImageMode_ = false;
    projection360Enabled_.store(0, std::memory_order_release);
    view360YawBits_.store(std::bit_cast<float>(0.0f), std::memory_order_relaxed);
    view360PitchBits_.store(std::bit_cast<float>(0.0f), std::memory_order_relaxed);
    view360FovYBits_.store(std::bit_cast<float>(90.0f), std::memory_order_relaxed);
    lyricsLayoutEnabled_.store(false, std::memory_order_release);
    actualVideoScalingMode_.store(FFF3FPVideoScalingMode::D3D11VideoProcessor);
    videoGeneration_.store(0); presentedVideoGeneration_.store(0);
    countedVideoGeneration_.store(0);
    presentedVideoFrames_.store(0); coalescedVideoFrames_.store(0);
    swapChainPresents_.store(0); presentWait100ns_.store(0);
    deviceLockWait100ns_.store(0); softwareConvert100ns_.store(0); upload100ns_.store(0);
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
    if (adaptiveScaler_ != nullptr) { sws_freeContext(adaptiveScaler_); adaptiveScaler_ = nullptr; }
    ReleaseDeviceObjects();
    ReleaseCom(writeFactory_);
    ReleaseCom(d2dFactory_);
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
bool PlayerVideoRenderer::IsWideGamutSource() const noexcept {
    return sourceWideGamut_.load(std::memory_order_acquire);
}
float PlayerVideoRenderer::EffectivePaperWhiteNits() const noexcept {
    // SDR on scRGB anchors white at Windows SDR content brightness (1.0 = 80 nits).
    // HDR keeps the configured paper white.
    if (actualMode_ == FFF3FPColorMode::MapToHdr && !hdrProcessor_.IsHdrSource()) {
        // Reported whenever Windows HDR is active. If the platform does not
        // report it, fall back to scRGB 1.0 (80 nits) rather than the 200-nit
        // graphics white: 80 is the contract reference and sits much closer to
        // what an ordinary SDR window looks like.
        return sdrWhiteLevelNits_ > 0.0f ? sdrWhiteLevelNits_ : 80.0f;
    }
    return paperWhiteNits_;
}

bool PlayerVideoRenderer::WantsScRgbPresentationPath(const std::uint32_t bitDepth) const noexcept {
    // HDR and wide gamut always need scRGB; Auto SDR also admits >8-bit sources.
    // Use the supplied depth because sourceBitDepth_ is unset before the first frame.
    if (hdrProcessor_.IsHdrSource() || IsWideGamutSource()) return true;
    return sdrScRgbMode_ != 0 && bitDepth > 8;
}
float PlayerVideoRenderer::SourcePeakNits() const noexcept { return sourcePeakNits_; }
HdrFrameState PlayerVideoRenderer::HdrState() const noexcept { return hdrProcessor_.State(); }
std::uint64_t PlayerVideoRenderer::PresentedVideoFrames() const noexcept { return presentedVideoFrames_.load(); }
std::uint64_t PlayerVideoRenderer::CoalescedVideoFrames() const noexcept { return coalescedVideoFrames_.load(); }
std::uint64_t PlayerVideoRenderer::SwapChainPresents() const noexcept { return swapChainPresents_.load(); }
std::uint64_t PlayerVideoRenderer::SubmittedVideoGeneration() const noexcept { return videoGeneration_.load(); }
std::uint64_t PlayerVideoRenderer::PresentedVideoGeneration() const noexcept { return presentedVideoGeneration_.load(); }
bool PlayerVideoRenderer::HasPendingVideoPresentation() const noexcept {
    return !interactiveMove_.load(std::memory_order_acquire) &&
        videoGeneration_.load() > presentedVideoGeneration_.load() && HasOutputWindow();
}

FFFResult PlayerVideoRenderer::CopySdrFrame(void* pixels, std::uint32_t capacity,
    std::uint32_t& width, std::uint32_t& height, bool discOnly) noexcept {
    std::lock_guard deviceLock(deviceMutex_);
    if (!hasCachedVideo_ || !swapChain_ || !device_ || !context_) return FFFResult::InvalidState;
    std::lock_guard presentLock(presentMutex_);
    width = swapWidth_; height = swapHeight_;
    const auto bytes = static_cast<std::uint64_t>(width) * height * 4;
    if (!pixels || bytes > capacity) return FFFResult::BufferTooSmall;
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> target;
    auto result = AcquireBackBufferTarget(backBuffer.GetAddressOf(), target.GetAddressOf());
    if (result != FFFResult::Success) return result;
    D3D11_TEXTURE2D_DESC description{}; backBuffer->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return FFFResult::NotSupported;
    if (discOnly) { constexpr float clear[4]{}; context_->ClearRenderTargetView(target.Get(), clear); }
    else { result = DrawCachedVideo(target.Get()); if (result != FFFResult::Success) return result; }
    const TimedTextLayerSlot slots[] = {TimedTextLayerSlot::Danmaku, TimedTextLayerSlot::Subtitle,
        TimedTextLayerSlot::Lyrics, TimedTextLayerSlot::Disc, TimedTextLayerSlot::PlayerInformation};
    for (auto slot : slots) {
        if (discOnly && slot != TimedTextLayerSlot::Disc) continue;
        result = DrawTimedText(slot); if (result != FFFResult::Success) return result;
        CompositeTimedText(target.Get(), slot);
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &staging))) return FFFResult::DeviceFailure;
    context_->CopyResource(staging.Get(), backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return FFFResult::DeviceFailure;
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(static_cast<uint8_t*>(pixels) + static_cast<size_t>(y) * width * 4,
            static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, width * 4);
    context_->Unmap(staging.Get(), 0);
    return FFFResult::Success;
}
bool PlayerVideoRenderer::HasOutputWindow() const noexcept {
    std::lock_guard lock(deviceMutex_);
    return window_ != nullptr && IsWindow(window_);
}
std::uint64_t PlayerVideoRenderer::PresentWait100ns() const noexcept { return presentWait100ns_.load(); }
std::uint64_t PlayerVideoRenderer::DeviceLockWait100ns() const noexcept { return deviceLockWait100ns_.load(); }
std::uint64_t PlayerVideoRenderer::SoftwareConvert100ns() const noexcept { return softwareConvert100ns_.load(); }
std::uint64_t PlayerVideoRenderer::Upload100ns() const noexcept { return upload100ns_.load(); }
std::uint32_t PlayerVideoRenderer::OutputBitDepth() const noexcept {
    return swapOutputBits_.load(std::memory_order_acquire);
}
FFF3FPVideoScalingMode PlayerVideoRenderer::ActualVideoScalingMode() const noexcept {
    return actualVideoScalingMode_.load();
}

FFFResult PlayerVideoRenderer::SetVideoSuperResolution(
    const FFF3FPVideoSuperResolution mode) noexcept {
    if (mode != FFF3FPVideoSuperResolution::Off &&
        mode != FFF3FPVideoSuperResolution::Auto)
        return FFFResult::InvalidArgument;
    // A new request clears a previous driver rejection: the user may have just
    // turned the feature on in the NVIDIA control panel, and re-probing costs
    // one extension call per frame at worst until the next blit settles it.
    if (mode == FFF3FPVideoSuperResolution::Auto && videoSuperResolutionRejected_)
        videoSuperResolutionRejected_ = false;
    requestedVideoSuperResolution_.store(mode, std::memory_order_release);
    if (mode == FFF3FPVideoSuperResolution::Off) {
        videoSuperResolutionActive_.store(false, std::memory_order_release);
        videoSuperResolutionReason_.store(FFF3FPVideoSuperResolutionReason::NotRequested,
            std::memory_order_release);
    }
    return FFFResult::Success;
}

FFF3FPVideoSuperResolution PlayerVideoRenderer::RequestedVideoSuperResolution() const noexcept {
    return requestedVideoSuperResolution_.load(std::memory_order_acquire);
}

bool PlayerVideoRenderer::VideoSuperResolutionActive() const noexcept {
    return videoSuperResolutionActive_.load(std::memory_order_acquire);
}

void PlayerVideoRenderer::FillVideoSuperResolutionStatus(
    FFF3FPVideoSuperResolutionStatus& status) const noexcept {
    status.requested = requestedVideoSuperResolution_.load(std::memory_order_acquire);
    status.nvidiaAdapter = nvidiaAdapter_ ? 1u : 0u;
    status.active = videoSuperResolutionActive_.load(std::memory_order_acquire) ? 1u : 0u;
    status.driverRejected = videoSuperResolutionRejected_ ? 1u : 0u;
    status.reason = status.active
        ? FFF3FPVideoSuperResolutionReason::None
        : videoSuperResolutionReason_.load(std::memory_order_acquire);
    status.sourceWidth = videoSuperResolutionSourceWidth_;
    status.sourceHeight = videoSuperResolutionSourceHeight_;
    status.targetWidth = videoSuperResolutionTargetWidth_;
    status.targetHeight = videoSuperResolutionTargetHeight_;
}
std::string PlayerVideoRenderer::FallbackReason() const {
    try { std::lock_guard fallbackLock(fallbackMutex_); return fallbackReason_; }
    catch (...) { return {}; }
}
std::string PlayerVideoRenderer::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
void PlayerVideoRenderer::SetError(std::string message) noexcept {
    try { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); } catch (...) {}
}
