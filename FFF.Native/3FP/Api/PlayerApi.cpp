#include "pch.h"
#include "3FP/Core/MediaText.h"
#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Core/PlayerSession.h"
#include "3FP/Render/VideoRenderer.h"
#include "3FP/Render/ColorExtension.h"
#include "3FP/Subtitle/SubtitleProbe.h"

#include <cmath>
#include <atomic>

namespace {
// Configuration layout and version must match the public header exactly.
// Bumped 14 -> 15 because FFF3FPConfiguration gained the preferredAdapterIndex
// field. FFF3FP_Create rejects a mismatched version outright, so
// every consumer of this header MUST be rebuilt and bumped in lockstep.
// Bumped 15 -> 16 because FFF3FPConfiguration gained the sdrScRgbMode field.
// Bumped 16 -> 17 because it gained adaptiveDownscaleBeforeUpload and
// adaptiveDownscaleDropPercent. FFF3FP_Create rejects a struct smaller than
// sizeof(FFF3FPConfiguration), so a host built against an older header now fails
// with InvalidArgument instead of reading past the end of its own struct -- rebuild
// against the new header. That rejection is the contract working, not a defect, and
// it is why every probe had to be recompiled after this change.
// VSR and view rotation are deliberately NOT part of this contract: they are
// appended exports, so a host that knows only the current version keeps working
// unchanged.
using PlayerMediaText::CopyUtf8;
// 19: FFF3FPSnapshot gained iamfContentChannels (appended at the end).
//     This is a *snapshot* field, so sizeof(FFF3FPConfiguration) is unchanged and a host
//     would still create a session structurally -- but GetSnapshot validates
//     `size >= sizeof(FFF3FPSnapshot)` strictly, so an un-rebuilt host starts getting
//     InvalidArgument from every snapshot call. Bumping the version turns that into an
//     explicit mismatch, and the release targets in FFF.Player.vbproj refuse to package a
//     GUI and kernel that disagree.
constexpr std::uint32_t PlayerApiVersion = 19;

// Process-wide native log sink, installed through FFF3FP_SetLogCallback.
std::atomic<FFF3FPLogCallback> g_logSink{nullptr};
std::atomic<void*> g_logContext{nullptr};

}

std::uint32_t FFF3FP_GetApiVersion() noexcept {
    if (const auto* api = GetColorExtension()) api->requestAuthorizationPrompt();
    return PlayerApiVersion;
}

std::int32_t FFF3FP_GetColorExtensionStatus() noexcept {
    const auto* api = GetColorExtension();
    return api == nullptr ? 0 : api->getAuthorizationStatus();
}

const char* FFF3FP_GetColorExtensionStatusText(std::uint32_t state, std::uint32_t variant) noexcept {
    const auto* api = GetColorExtension();
    return api == nullptr ? nullptr : api->getStatusText(state, variant);
}

void FFF3FP_SetColorExtensionAuthorizationPrompt(
    int (__cdecl* callback)(char*, std::uint32_t)) noexcept {
    if (const auto* api = GetColorExtension()) api->setAuthorizationPrompt(callback);
}

FFFResult FFF3FP_AuthenticateColorExtension(const char* codeUtf8) noexcept {
    const auto* api = GetColorExtension();
    if (api == nullptr || codeUtf8 == nullptr)
        return FFFResult::InvalidArgument;
    return api->authenticate(codeUtf8) == 1
        ? FFFResult::Success : FFFResult::NotSupported;
}

// ---- Process-wide native log sink (public surface: FFF3FP_SetLogCallback) ----
void FFF3FP_SetLogCallback(FFF3FPLogCallback callback, void* context) noexcept {
    g_logContext.store(context, std::memory_order_release);
    g_logSink.store(callback, std::memory_order_release);
}

// Internal sink invoker for kernel log lines. Deliberately a plain C++ symbol and not
// FFF3FP_API: it stays out of the export table (the surface the ABI checks walk), and
// in-module callers declare it locally (PlayerSession::Fail, PlayerVideoRenderer).
void FFF3FP_KernelLogImpl(const char* utf8Line) noexcept {
    if (utf8Line == nullptr) return;
    const auto sink = g_logSink.load(std::memory_order_acquire);
    if (sink == nullptr) return;
    const auto ctx = g_logContext.load(std::memory_order_acquire);
    sink(ctx, utf8Line);
}

FFFResult FFF3FP_EvaluateHdrProcessing(FFF3FPHdrProcessingProbe* probe) noexcept {
    return probe == nullptr ? FFFResult::InvalidArgument : HdrProcessor::EvaluateProbe(*probe);
}

FFFResult FFF3FP_Create(const FFF3FPConfiguration* configuration, FFF3FPHandle* player) noexcept {
    if (configuration == nullptr || player == nullptr || configuration->size < sizeof(FFF3FPConfiguration) ||
        configuration->version != PlayerApiVersion || configuration->decodeMode == FFF3FPDecodeMode::Unspecified ||
        configuration->decodeMode > FFF3FPDecodeMode::D3D11 || configuration->colorMode > FFF3FPColorMode::MapToHdr ||
        configuration->videoScalingQuality > FFF3FPVideoScalingQuality::HighQuality ||
        configuration->forceHdrOutput > 1 ||
        configuration->sdrScRgbMode > 1 ||
        // -1 = auto (adapter driving the window's monitor); 0..15 = DXGI index.
        // Callers are expected to clamp to the same range.
        configuration->preferredAdapterIndex < -1 || configuration->preferredAdapterIndex > 15 ||
        !std::isfinite(configuration->sdrPeakNits) || configuration->sdrPeakNits <= 0 ||
        !std::isfinite(configuration->hdrPeakNits) || configuration->hdrPeakNits < 0 ||
        configuration->hdrPeakNits > 10000 || !std::isfinite(configuration->sdrPaperWhiteNits) ||
        configuration->sdrPaperWhiteNits <= 0) return FFFResult::InvalidArgument;
    try { *player = new PlayerSession(*configuration); return FFFResult::Success; }
    catch (...) { *player = nullptr; return FFFResult::NativeFailure; }
}

FFFResult FFF3FP_Open(const FFF3FPHandle player, const char* path) noexcept { return player ? static_cast<PlayerSession*>(player)->Open(path) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_DiscNavigate(FFF3FPHandle player, int command, int value, int y) noexcept { return player ? static_cast<PlayerSession*>(player)->DiscNavigate(command, value, y) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_CopySdrFrame(FFF3FPHandle player, void* pixels, std::uint32_t capacity,
    std::uint32_t* width, std::uint32_t* height, std::uint32_t discOnly) noexcept {
    return player && width && height && discOnly <= 1 ? static_cast<PlayerSession*>(player)->CopySdrFrame(pixels, capacity, *width, *height, discOnly != 0) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetDiscStatus(FFF3FPHandle player, char* output, std::uint32_t size, std::uint32_t* required) noexcept {
    return player ? CopyUtf8(static_cast<PlayerSession*>(player)->DiscStatus(), output, size, required) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_Play(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->Play() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_Pause(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->Pause() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_DiscardAudioOutput(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->DiscardAudioOutput() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_Stop(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->Stop() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_Close(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->Close() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_Seek(const FFF3FPHandle player, const std::int64_t position) noexcept { return player ? static_cast<PlayerSession*>(player)->Seek(position) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SeekKeyframe(const FFF3FPHandle player, const std::int64_t position) noexcept { return player ? static_cast<PlayerSession*>(player)->SeekKeyframe(position) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SeekFrame(const FFF3FPHandle player, const std::int64_t frame) noexcept { return player ? static_cast<PlayerSession*>(player)->SeekFrame(frame) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_StepFrame(const FFF3FPHandle player, const std::int32_t direction) noexcept { return player ? static_cast<PlayerSession*>(player)->StepFrame(direction) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_StepKeyframe(const FFF3FPHandle player, const std::int32_t direction) noexcept { return player ? static_cast<PlayerSession*>(player)->StepKeyframe(direction) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SelectVideoStream(const FFF3FPHandle player, const std::int32_t stream) noexcept { return player ? static_cast<PlayerSession*>(player)->SelectVideoStream(stream) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SelectAudioStream(const FFF3FPHandle player, const std::int32_t stream) noexcept { return player ? static_cast<PlayerSession*>(player)->SelectAudioStream(stream) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_LoadExternalAudio(const FFF3FPHandle player, const char* path, const std::int32_t stream,
    const std::int64_t offset) noexcept { return player ? static_cast<PlayerSession*>(player)->LoadExternalAudio(path, stream, offset) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_ClearExternalAudio(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->ClearExternalAudio() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetExternalAudioOffset(const FFF3FPHandle player, const std::int64_t offset) noexcept { return player ? static_cast<PlayerSession*>(player)->SetExternalAudioOffset(offset) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetColorMode(const FFF3FPHandle player, const FFF3FPColorMode mode, const float sdr,
    const float hdr, const float paper, const std::uint32_t forceHdr) noexcept {
    return player && forceHdr <= 1 ?
        static_cast<PlayerSession*>(player)->SetColorMode(mode, sdr, hdr, paper, forceHdr != 0) :
        FFFResult::InvalidArgument;
}
FFFResult FFF3FP_SetPresentConfig(const FFF3FPHandle player, const std::uint32_t enableTearing) noexcept {
    return player && enableTearing <= 1 ?
        static_cast<PlayerSession*>(player)->SetPresentConfig(enableTearing != 0) :
        FFFResult::InvalidArgument;
}
FFFResult FFF3FP_SetOutputWindow(const FFF3FPHandle player, void* window) noexcept { return player ? static_cast<PlayerSession*>(player)->SetOutputWindow(window) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetInteractiveMove(const FFF3FPHandle player, const std::uint32_t enabled) noexcept {
    return player && enabled <= 1 ? static_cast<PlayerSession*>(player)->SetInteractiveMove(enabled != 0)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_SetViewTransform(const FFF3FPHandle player, const float zoom,
    const float panX, const float panY) noexcept {
    return player ? static_cast<PlayerSession*>(player)->SetViewTransform(zoom, panX, panY)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_ZoomViewAt(const FFF3FPHandle player, const float factor,
    const float anchorX, const float anchorY, float* const resultingZoom) noexcept {
    return player ? static_cast<PlayerSession*>(player)->ZoomViewAt(factor, anchorX, anchorY,
        resultingZoom) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_SetFitLimitToNative(const FFF3FPHandle player,
    const std::uint32_t enable) noexcept {
    if (player == nullptr || enable > 1) return FFFResult::InvalidArgument;
    return static_cast<PlayerSession*>(player)->SetFitLimitToNative(enable != 0);
}
FFFResult FFF3FP_Set360View(const FFF3FPHandle player, const std::uint32_t enabled,
    const float yaw, const float pitch, const float fovY) noexcept {
    return player && enabled <= 1 ?
        static_cast<PlayerSession*>(player)->Set360View(enabled != 0, yaw, pitch, fovY) :
        FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetImageInfo(const FFF3FPHandle player, FFF3FPImageInfo* info) noexcept {
    return player && info ? static_cast<PlayerSession*>(player)->GetImageInfo(*info)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_SetAudioEndpoint(const FFF3FPHandle player, const char* endpoint) noexcept { return player ? static_cast<PlayerSession*>(player)->SetAudioEndpoint(endpoint) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetAudioExclusiveMode(const FFF3FPHandle player, const std::uint32_t exclusive) noexcept {
    return player && exclusive <= 1 ? static_cast<PlayerSession*>(player)->SetAudioExclusiveMode(exclusive != 0)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_SetVolume(const FFF3FPHandle player, const float volume, const std::uint32_t muted) noexcept { return player ? static_cast<PlayerSession*>(player)->SetVolume(volume, muted != 0) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetTimedTextLayer(const FFF3FPHandle player,
    const FFF3FPTimedTextLayer* layer) noexcept {
    return player && layer ? static_cast<PlayerSession*>(player)->SetTimedTextLayer(*layer)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetSnapshot(const FFF3FPHandle player, FFF3FPSnapshot* snapshot) noexcept { return player && snapshot ? static_cast<PlayerSession*>(player)->GetSnapshot(*snapshot) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_ReadVideoPixel(const FFF3FPHandle player,
    FFF3FPVideoPixelProbe* probe) noexcept {
    return player && probe ? static_cast<PlayerSession*>(player)->ReadVideoPixel(*probe) :
        FFFResult::InvalidArgument;
}
// Batch pixel readback (single staging copy + Map).
FFFResult FFF3FP_ReadVideoPixelRegion(const FFF3FPHandle player,
    const std::uint32_t x, const std::uint32_t y, const std::uint32_t width,
    const std::uint32_t height, float* dst, const std::uint32_t dstFloatCount,
    std::uint32_t* outputBitDepth) noexcept {
    return player ? static_cast<PlayerSession*>(player)->ReadVideoPixelRegion(
        x, y, width, height, dst, dstFloatCount, outputBitDepth) :
        FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetAudioPeakLevels(const FFF3FPHandle player,
    FFF3FPAudioPeakLevels* levels) noexcept {
    return player && levels ? static_cast<PlayerSession*>(player)->GetAudioPeakLevels(*levels)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetTimedTextStatus(const FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept {
    return player && status ? static_cast<PlayerSession*>(player)->GetTimedTextStatus(*status)
        : FFFResult::InvalidArgument;
}
// Re-present the last cached frame on the presenter thread (see the header note on
// FFF3FP_Redraw for when the host is expected to call it).
FFFResult FFF3FP_Redraw(const FFF3FPHandle player) noexcept {
    return player ? static_cast<PlayerSession*>(player)->Redraw()
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetDanmakuStatus(const FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept {
    return player && status ? static_cast<PlayerSession*>(player)->GetDanmakuStatus(*status)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetLyricsStatus(const FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept {
    return player && status ? static_cast<PlayerSession*>(player)->GetLyricsStatus(*status)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_EvaluateColorTransform(FFF3FPColorTransform* transform) noexcept {
    return transform ? EvaluateVideoColorTransform(*transform) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_EvaluateTimedTextRasterization(
    FFF3FPTimedTextRasterizationProbe* probe) noexcept {
    return probe ? EvaluateTimedTextRasterization(*probe) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_MeasureTimedText(const char* textUtf8, const char* fontFamilyUtf8,
    const float fontSize, const FFF3FPTimedTextFlags flags, const float maxWidth,
    const float outlineWidth, const float shadowOffsetX, const float shadowOffsetY,
    const std::uint32_t shadowEnabled, FFF3FPTimedTextMeasurement* measurement) noexcept {
    return measurement ? MeasureTimedText(textUtf8, fontFamilyUtf8, fontSize, flags,
        maxWidth, outlineWidth, shadowOffsetX, shadowOffsetY, shadowEnabled != 0,
        *measurement) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_MeasureTimedTextWidth(const char* textUtf8, const char* fontFamilyUtf8,
    const float fontSize, const FFF3FPTimedTextFlags flags, float* width) noexcept {
    return width ? MeasureTimedTextWidth(textUtf8, fontFamilyUtf8, fontSize, flags, *width) :
        FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetMediaInfo(const FFF3FPHandle player, char* output, const std::uint32_t size,
    std::uint32_t* required) noexcept { if (!player) return FFFResult::InvalidArgument; try { return CopyUtf8(static_cast<PlayerSession*>(player)->MediaInfo(), output, size, required); } catch (...) { return FFFResult::NativeFailure; } }
FFFResult FFF3FP_GetLastError(const FFF3FPHandle player, char* output, const std::uint32_t size,
    std::uint32_t* required) noexcept { if (!player) return FFFResult::InvalidArgument; try { return CopyUtf8(static_cast<PlayerSession*>(player)->LastError(), output, size, required); } catch (...) { return FFFResult::NativeFailure; } }
FFFResult FFF3FP_ProbeSubtitleStreams(const char* localPathUtf8, char* output, const std::uint32_t size,
    std::uint32_t* required) noexcept { try { return ProbeSubtitleStreams(localPathUtf8, output, size, required); } catch (...) { return FFFResult::NativeFailure; } }
// Render-target diagnostics
FFFResult FFF3FP_GetRenderTargetInfo(const FFF3FPHandle player,
    FFF3FPRenderTargetInfo* info) noexcept {
    return player && info ? static_cast<PlayerSession*>(player)->GetRenderTargetInfo(*info)
        : FFFResult::InvalidArgument;
}
void FFF3FP_Destroy(const FFF3FPHandle player) noexcept { delete static_cast<PlayerSession*>(player); }

// RTX Video Super Resolution. Appended after the v16 surface on purpose: these
// are additive entry points, so PlayerApiVersion stays at 16 and an existing
// host keeps working against this DLL without a rebuild.
FFFResult FFF3FP_SetVideoSuperResolution(const FFF3FPHandle player,
    const FFF3FPVideoSuperResolution mode) noexcept {
    return player ? static_cast<PlayerSession*>(player)->SetVideoSuperResolution(mode)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetVideoSuperResolutionStatus(const FFF3FPHandle player,
    FFF3FPVideoSuperResolutionStatus* status) noexcept {
    return player && status
        ? static_cast<PlayerSession*>(player)->GetVideoSuperResolutionStatus(*status)
        : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_SetViewRotation(const FFF3FPHandle player,
    const std::uint32_t quarterTurnsClockwise) noexcept {
    return player
        ? static_cast<PlayerSession*>(player)->SetViewRotation(quarterTurnsClockwise)
        : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_GetViewRotation(const FFF3FPHandle player,
    std::uint32_t* quarterTurnsClockwise) noexcept {
    if (player == nullptr || quarterTurnsClockwise == nullptr)
        return FFFResult::InvalidArgument;
    *quarterTurnsClockwise = static_cast<PlayerSession*>(player)->ViewRotation();
    return FFFResult::Success;
}

// Screenshot readback. Appended after the v16 surface, so PlayerApiVersion is
// unchanged; hosts resolve this dynamically.
FFFResult FFF3FP_CopyFrame(const FFF3FPHandle player, void* pixels,
    const std::uint32_t capacity, std::uint32_t* width, std::uint32_t* height,
    const FFF3FPCopyFrameLayout layout, const FFF3FPCopyFrameFormat format) noexcept {
    if (player == nullptr || width == nullptr || height == nullptr)
        return FFFResult::InvalidArgument;
    const auto layoutValue = static_cast<std::uint32_t>(layout);
    const auto formatValue = static_cast<std::uint32_t>(format);
    if (layoutValue > 1 || formatValue > 1) return FFFResult::InvalidArgument;
    std::uint32_t frameWidth = 0;
    std::uint32_t frameHeight = 0;
    const auto result = static_cast<PlayerSession*>(player)->CopyFrame(pixels, capacity,
        frameWidth, frameHeight, layoutValue, formatValue);
    // Publish the size on every path: the two-call contract needs it even when the
    // caller's buffer was too small (that is how it learns what to allocate).
    *width = frameWidth;
    *height = frameHeight;
    return result;
}

FFFResult FFF3FP_GetLastCopyFrameBitDepth(const FFF3FPHandle player,
    std::uint32_t* bitDepth) noexcept {
    if (player == nullptr || bitDepth == nullptr) return FFFResult::InvalidArgument;
    *bitDepth = static_cast<PlayerSession*>(player)->LastCopyFrameBitDepth();
    return FFFResult::Success;
}
