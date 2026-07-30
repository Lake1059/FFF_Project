#include "pch.h"
#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Core/PlayerSession.h"
#include "3FP/Render/VideoRenderer.h"

#include <cmath>

namespace {
constexpr std::uint32_t PlayerApiVersion = 6;

FFFResult CopyUtf8(const std::string& value, char* output, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto bytes = value.size() + 1;
    if (bytes > UINT32_MAX) return FFFResult::NativeFailure;
    if (requiredSize != nullptr) *requiredSize = static_cast<std::uint32_t>(bytes);
    if (output == nullptr || outputSize < bytes) return FFFResult::BufferTooSmall;
    std::memcpy(output, value.c_str(), bytes); return FFFResult::Success;
}
}

std::uint32_t FFF3FP_GetApiVersion() noexcept { return PlayerApiVersion; }

FFFResult FFF3FP_Create(const FFF3FPConfiguration* configuration, FFF3FPHandle* player) noexcept {
    if (configuration == nullptr || player == nullptr || configuration->size < sizeof(FFF3FPConfiguration) ||
        configuration->version != PlayerApiVersion || configuration->decodeMode == FFF3FPDecodeMode::Unspecified ||
        configuration->decodeMode > FFF3FPDecodeMode::D3D11 || configuration->colorMode > FFF3FPColorMode::MapToHdr ||
        !std::isfinite(configuration->sdrPeakNits) || configuration->sdrPeakNits <= 0 ||
        !std::isfinite(configuration->hdrPeakNits) || configuration->hdrPeakNits <= 0 ||
        configuration->hdrPeakNits > 10000 || !std::isfinite(configuration->sdrPaperWhiteNits) ||
        configuration->sdrPaperWhiteNits <= 0) return FFFResult::InvalidArgument;
    try { *player = new PlayerSession(*configuration); return FFFResult::Success; }
    catch (...) { *player = nullptr; return FFFResult::NativeFailure; }
}

FFFResult FFF3FP_Open(const FFF3FPHandle player, const char* path) noexcept { return player ? static_cast<PlayerSession*>(player)->Open(path) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_Play(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->Play() : FFFResult::InvalidArgument; }
FFFResult FFF3FP_Pause(const FFF3FPHandle player) noexcept { return player ? static_cast<PlayerSession*>(player)->Pause() : FFFResult::InvalidArgument; }
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
    const float hdr, const float paper) noexcept { return player ? static_cast<PlayerSession*>(player)->SetColorMode(mode, sdr, hdr, paper) : FFFResult::InvalidArgument; }
FFFResult FFF3FP_SetOutputWindow(const FFF3FPHandle player, void* window) noexcept { return player ? static_cast<PlayerSession*>(player)->SetOutputWindow(window) : FFFResult::InvalidArgument; }
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
FFFResult FFF3FP_GetDanmakuStatus(const FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept {
    return player && status ? static_cast<PlayerSession*>(player)->GetDanmakuStatus(*status)
        : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_EvaluateColorTransform(FFF3FPColorTransform* transform) noexcept {
    return transform ? EvaluateVideoColorTransform(*transform) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_EvaluateTimedTextRasterization(
    FFF3FPTimedTextRasterizationProbe* probe) noexcept {
    return probe ? EvaluateTimedTextRasterization(*probe) : FFFResult::InvalidArgument;
}
FFFResult FFF3FP_GetMediaInfo(const FFF3FPHandle player, char* output, const std::uint32_t size,
    std::uint32_t* required) noexcept { if (!player) return FFFResult::InvalidArgument; try { return CopyUtf8(static_cast<PlayerSession*>(player)->MediaInfo(), output, size, required); } catch (...) { return FFFResult::NativeFailure; } }
FFFResult FFF3FP_GetLastError(const FFF3FPHandle player, char* output, const std::uint32_t size,
    std::uint32_t* required) noexcept { if (!player) return FFFResult::InvalidArgument; try { return CopyUtf8(static_cast<PlayerSession*>(player)->LastError(), output, size, required); } catch (...) { return FFFResult::NativeFailure; } }
void FFF3FP_Destroy(const FFF3FPHandle player) noexcept { delete static_cast<PlayerSession*>(player); }
