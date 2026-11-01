#include "pch.h"
#include "3FP/Core/MediaText.h"
#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Api/FFF.Player.AudioRenderPlug.h"
#include "3FP/Core/PlayerSession.h"
#include "3FP/Render/VideoRenderer.h"
#include "3FP/Subtitle/SubtitleProbe.h"

#include <cmath>
#include <atomic>
#include <cstring>

namespace {
// Configuration layout and version must match the public header exactly.
// Bumped 14 -> 15 because FFF3FPConfiguration gained the preferredAdapterIndex
// field. FFF3FP_Create rejects a mismatched version outright, so
// every consumer of this header MUST be rebuilt and bumped in lockstep.
// Bumped 15 -> 16 because FFF3FPConfiguration gained the sdrScRgbMode field.
// Bumped 16 -> 17 because FFF3FPSnapshot gained the ST 2094 dynamic metadata
// diagnostic fields at its tail. GetSnapshot rejects a caller whose declared
// size is smaller than sizeof(FFF3FPSnapshot), so every consumer of the
// snapshot struct must be rebuilt in lockstep with this bump.
// Bumped 17 -> 18 because FFF3FPConfiguration gained adaptiveDownscaleBeforeUpload,
// adaptiveDownscaleDropPercent, adaptiveDecoderThreads, minSoftwareDecoderThreads,
// maxSoftwareDecoderThreads, decoderGrowDropPercent and decoderShrinkDropPercent,
// and FFF3FPSnapshot gained iamfActive / iamfChannels / iamfSoundSystem,
// videoUpload100ns and iamfContentChannels at its tail.
// FFF3FP_Create rejects both a struct smaller than sizeof(FFF3FPConfiguration)
// and a version != PlayerApiVersion, so a host built against an older header now
// fails with InvalidArgument instead of reading past the end of its own struct --
// rebuild against this header. That rejection is the contract working, not a
// defect, and it is why every probe had to be recompiled after this change.
// VSR and view rotation are deliberately NOT part of this contract: they are
// appended exports, so a host that knows only the current version keeps working
// unchanged.
using PlayerMediaText::CopyUtf8;
constexpr std::uint32_t PlayerApiVersion = 20;

// Process-wide native log sink, installed through FFF3FP_SetLogCallback.
std::atomic<FFF3FPLogCallback> g_logSink{nullptr};
std::atomic<void*> g_logContext{nullptr};

}

std::uint32_t FFF3FP_GetApiVersion() noexcept {
    return PlayerApiVersion;
}

// The four exports below are deprecated ABI: the private color-extension module
// they used to drive no longer exists (and was never shippable), so the
// capability is permanently absent. The symbols are kept so that existing
// hosts keep resolving them; they now report "absent" / "unsupported".
std::int32_t FFF3FP_GetColorExtensionStatus() noexcept {
    return 0;
}

const char* FFF3FP_GetColorExtensionStatusText(std::uint32_t state, std::uint32_t variant) noexcept {
    (void)state;
    (void)variant;
    return nullptr;
}

void FFF3FP_SetColorExtensionAuthorizationPrompt(
    int (__cdecl* callback)(char*, std::uint32_t)) noexcept {
    (void)callback;
}

FFFResult FFF3FP_AuthenticateColorExtension(const char* codeUtf8) noexcept {
    if (codeUtf8 == nullptr)
        return FFFResult::InvalidArgument;
    return FFFResult::NotSupported;
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
        configuration->version != PlayerApiVersion ||
        // Unspecified is answered, not refused: it means "no preference", and the
        // session then runs GPU-first -- hardware where it works, CPU where it does
        // not (PlayerSession's constructor normalizes it to Gpu). Only values past
        // the end of the enum are rejected.
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
FFFResult FFF3FP_ConfigurePlaybackPerformance(const FFF3FPHandle player,
    const std::int32_t decodeAdapterIndex, const std::uint32_t videoBufferPolicy,
    const std::uint32_t audioBufferPolicy) noexcept {
    return player ? static_cast<PlayerSession*>(player)->ConfigurePlaybackPerformance(
        decodeAdapterIndex, videoBufferPolicy, audioBufferPolicy) : FFFResult::InvalidArgument;
}
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
// Presentation cadence lock. Appended after the v20 surface like the readback and VSR
// entries: additive, so PlayerApiVersion is unchanged and a host built against an older
// header keeps loading this DLL. There is deliberately no setter -- the window's flip-rate
// bound is always min(panel rate, content rate) and is not configurable.
FFFResult FFF3FP_GetPresentPacingStatus(const FFF3FPHandle player,
    FFF3FPPresentPacingStatus* status) noexcept {
    return player && status
        ? static_cast<PlayerSession*>(player)->GetPresentPacingStatus(*status)
        : FFFResult::InvalidArgument;
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
FFFResult FFF3FP_PanView(const FFF3FPHandle player, const float deltaX,
    const float deltaY) noexcept {
    return player ? static_cast<PlayerSession*>(player)->PanView(deltaX, deltaY)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetImageZoom(const FFF3FPHandle player, float* zoom,
    float* minimum, float* maximum) noexcept {
    return player && zoom && minimum && maximum
        ? static_cast<PlayerSession*>(player)->ImageZoom(*zoom, *minimum, *maximum)
        : FFFResult::InvalidArgument;
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
// HDR presentation diagnostics. The ST 2094 curve gate and the per-frame
// display signaling were previously only assertable by reading the shader source; a
// host can now ask which mechanism actually ran for the frames drawn so far.
FFFResult FFF3FP_GetHdrSignalDiagnostics(const FFF3FPHandle player,
    FFF3FPHdrSignalDiagnostics* diagnostics) noexcept {
    return player && diagnostics
        ? static_cast<PlayerSession*>(player)->GetHdrSignalDiagnostics(*diagnostics)
        : FFFResult::InvalidArgument;
}
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
// ST 2094-40 injection. See the header for units and the frame-matching rule.
// A malformed entry is rejected (InvalidArgument) and the previously installed
// table stays live, so a bad call cannot half-apply and change the picture.
FFFResult FFF3FP_SetHdrDynamicMetadata(const FFF3FPHandle player,
    const FFF3FPHdrDynamicMetadataEntry* entries, const std::uint32_t count) noexcept {
    return player ? static_cast<PlayerSession*>(player)
        ->SetInjectedHdrMetadata(entries, count)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_ClearHdrDynamicMetadata(const FFF3FPHandle player) noexcept {
    return player ? static_cast<PlayerSession*>(player)->ClearInjectedHdrMetadata()
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetHdrMetadataSource(const FFF3FPHandle player,
    std::uint32_t* source) noexcept {
    if (player == nullptr || source == nullptr) return FFFResult::InvalidArgument;
    *source = static_cast<PlayerSession*>(player)->HdrMetadataSource();
    return FFFResult::Success;
}

// Generic dynamic-metadata carrier. See the header's carrier contract: same
// table, matching rule and rejection semantics as the ST 2094-40 entry point,
// with a kind tag per packet and NotSupported for kinds this kernel does not
// know (the previous table stays live either way).
FFFResult FFF3FP_SetDynamicMetadata(const FFF3FPHandle player,
    const FFF3FPDynamicMetadataPacket* packets, const std::uint32_t count) noexcept {
    return player ? static_cast<PlayerSession*>(player)->SetDynamicMetadata(packets, count)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_ClearDynamicMetadata(const FFF3FPHandle player,
    const std::uint32_t kind) noexcept {
    return player ? static_cast<PlayerSession*>(player)->ClearDynamicMetadata(kind)
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

// NVIDIA automatic HDR (RTX Video HDR, video-processor private path). Appended
// like the VSR exports above: additive entry points, PlayerApiVersion unchanged,
// and an older kernel simply fails to resolve them (treated as unavailable).
FFFResult FFF3FP_SetVideoAutoHdr(const FFF3FPHandle player,
    const FFF3FPVideoAutoHdr mode) noexcept {
    return player ? static_cast<PlayerSession*>(player)->SetVideoAutoHdr(mode)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetVideoAutoHdrStatus(const FFF3FPHandle player,
    FFF3FPVideoAutoHdrStatus* status) noexcept {
    return player && status
        ? static_cast<PlayerSession*>(player)->GetVideoAutoHdrStatus(*status)
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

// Embedded-track audio delay (lip-sync correction). Appended after the v16 surface
// like the VSR, rotation and readback exports, so PlayerApiVersion is unchanged for
// a host that never resolves this entry point.
FFFResult FFF3FP_SetAudioDelay(const FFF3FPHandle player, const std::int64_t delay) noexcept { return player ? static_cast<PlayerSession*>(player)->SetAudioDelay(delay) : FFFResult::InvalidArgument; }
// Neutral frame side-data ring. Additive like the entry points above, so
// PlayerApiVersion is unchanged and hosts resolve it through GetProcAddress.
// The side data type is a plain number and the payload is copied untouched:
// this layer interprets nothing, which is what lets an external module own the
// format-specific reading.
FFFResult FFF3FP_CopyFrameSideDataAfter(const FFF3FPHandle player,
    const std::uint32_t sideDataType, const std::int64_t fromFrameIndex, void* buffer,
    const std::uint32_t capacity, std::uint32_t* writtenSize,
    std::int64_t* foundFrameIndex) noexcept {
    if (player == nullptr || writtenSize == nullptr || foundFrameIndex == nullptr)
        return FFFResult::InvalidArgument;
    std::uint32_t written = 0;
    std::int64_t found = -1;
    const auto result = static_cast<PlayerSession*>(player)->CopyFrameSideDataAfter(
        sideDataType, fromFrameIndex, buffer, capacity, written, found);
    *writtenSize = written;
    *foundFrameIndex = found;
    return result;
}

// Neutral stream-level side data. Additive like the entry points above, so
// PlayerApiVersion is unchanged and hosts resolve it through GetProcAddress.
// A separate entry point from the frame ring because the stream-level type
// enumeration is a different one from the per-frame enumeration: the two
// collide on raw numbers. The type is carried as a plain number and the
// payload is copied untouched.
FFFResult FFF3FP_CopyStreamSideData(const FFF3FPHandle player,
    const std::uint32_t sideDataType, void* buffer, const std::uint32_t capacity,
    std::uint32_t* writtenSize) noexcept {
    if (player == nullptr || writtenSize == nullptr) return FFFResult::InvalidArgument;
    std::uint32_t written = 0;
    const auto result = static_cast<PlayerSession*>(player)->CopyStreamSideData(
        sideDataType, buffer, capacity, written);
    *writtenSize = written;
    return result;
}

// Optional auxiliary video layer. Additive like the entry points above, so
// PlayerApiVersion is unchanged and a host that never resolves it keeps
// loading this DLL unchanged.
FFFResult FFF3FP_SetAuxiliaryComposition(const FFF3FPHandle player,
    const FFF3FPAuxiliaryComposition* composition) noexcept {
    if (player == nullptr || composition == nullptr)
        return FFFResult::InvalidArgument;
    return static_cast<PlayerSession*>(player)->SetAuxiliaryComposition(*composition);
}

// External bed render plug. Appended after the v20 surface like the readback, VSR,
// rotation and side-data entries before it: additive entry points, so
// PlayerApiVersion stays at 20 and a host that never resolves these two keeps
// loading this DLL unchanged. See FFF.Player.AudioRenderPlug.h for why the seam
// exists at all -- rendering a channel bed to binaural output is an implementation
// that needs authorization to write, so the body keeps the neutral socket and the
// matrix fold-down, and the renderer itself arrives from outside the process's own
// source tree.
FFFResult FFF3FP_SetAudioRenderPlug(const FFF3FPHandle player,
    const FFF3FPAudioRenderPlug* plug) noexcept {
    return player ? static_cast<PlayerSession*>(player)->SetAudioRenderPlug(plug)
        : FFFResult::InvalidArgument;
}

// active: 1 = a plug owns the bed, 0 = the matrix fold-down does. The two frame
// counters are the seam's own accounting (rendered / declined), read back from the
// renderer's published runtime state so this call never touches the renderer
// pointer. Only `active` is mandatory: name, capacity, and both counters are
// optional outputs, and a name that does not fit is truncated rather than failed --
// it is informational, and unlike the string-returning exports there is no
// two-call resize dance to ask for a bigger buffer.
FFFResult FFF3FP_GetAudioRenderPlugState(const FFF3FPHandle player,
    std::uint32_t* active, char* name, const std::uint32_t nameCapacity,
    std::uint64_t* framesRendered, std::uint64_t* framesDeclined) noexcept {
    if (player == nullptr || active == nullptr) return FFFResult::InvalidArgument;
    std::uint32_t activeValue = 0;
    std::string plugName;
    std::uint64_t rendered = 0;
    std::uint64_t declined = 0;
    static_cast<PlayerSession*>(player)->GetAudioRenderPlugState(activeValue, plugName,
        rendered, declined);
    *active = activeValue;
    if (framesRendered != nullptr) *framesRendered = rendered;
    if (framesDeclined != nullptr) *framesDeclined = declined;
    if (name != nullptr && nameCapacity > 0) {
        name[0] = '\0';
        const auto copied = CopyUtf8(plugName, name, nameCapacity, nullptr);
        if (copied != FFFResult::Success && nameCapacity > 1) {
            std::memcpy(name, plugName.c_str(), nameCapacity - 1);
            name[nameCapacity - 1] = '\0';
        }
    }
    return FFFResult::Success;
}

FFFResult FFF3FP_SetPlaybackRate(FFF3FPHandle p, double r) noexcept { return p ? static_cast<PlayerSession*>(p)->SetPlaybackRate(r) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetAudioEffects(FFF3FPHandle p, bool e, const float* g, float w) noexcept { return p ? static_cast<PlayerSession*>(p)->SetAudioEffects(e,g,w) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetScalingMode(FFF3FPHandle p, std::uint32_t m) noexcept { return p ? static_cast<PlayerSession*>(p)->SetScalingMode(m) : FFFResult::InvalidArgument; }
