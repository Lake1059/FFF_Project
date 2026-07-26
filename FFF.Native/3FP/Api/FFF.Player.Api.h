#pragma once

#include "3FR/Api/FFF.Native.Api.h"

#include <cstdint>

enum class FFF3FPDecodeMode : std::uint32_t {
    Unspecified = 0,
    Cpu = 1,
    Gpu = 2,
    D3D11 = Gpu,
};

enum class FFF3FPColorMode : std::uint32_t {
    MapToSdr = 0,
    RawHdrAsSdr = 1,
    MapToHdr = 2,
};

enum class FFF3FPState : std::uint32_t {
    Idle = 0,
    Opening = 1,
    Ready = 2,
    Playing = 3,
    Paused = 4,
    Ended = 5,
    Failed = 6,
    Closed = 7,
};

enum class FFF3FPEvent : std::uint32_t {
    StateChanged = 1,
    OpenCompleted = 2,
    OperationCompleted = 3,
    PlaybackEnded = 4,
    Error = 5,
    ColorModeChanged = 6,
    DeviceChanged = 7,
};

using FFF3FPEventCallback = void(__cdecl*)(void* context, FFF3FPEvent eventType,
    const char* detailJsonUtf8);

struct FFF3FPConfiguration {
    std::uint32_t size;
    std::uint32_t version;
    void* outputWindow;
    FFF3FPDecodeMode decodeMode;
    FFF3FPColorMode colorMode;
    float sdrPeakNits;
    float hdrPeakNits;
    float sdrPaperWhiteNits;
    const char* audioEndpointIdUtf8;
    FFF3FPEventCallback eventCallback;
    void* eventCallbackContext;
};

struct FFF3FPSnapshot {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPState state;
    FFF3FPDecodeMode decodeMode;
    FFF3FPColorMode requestedColorMode;
    FFF3FPColorMode actualColorMode;
    std::int64_t position100ns;
    std::int64_t duration100ns;
    std::int64_t frameIndex;
    std::int64_t framePts;
    std::int32_t frameTimeBaseNumerator;
    std::int32_t frameTimeBaseDenominator;
    std::int32_t selectedVideoStream;
    std::int32_t selectedAudioStream;
    std::uint32_t videoWidth;
    std::uint32_t videoHeight;
    std::uint32_t isHdrSource;
    std::uint32_t isExternalAudio;
    std::int64_t externalAudioOffset100ns;
};

using FFF3FPHandle = void*;
using FFF3FPBitmapSubtitleHandle = void*;

enum class FFF3FPBitmapSubtitleFlags : std::uint32_t {
    None = 0,
    Clear = 1,
    EndOfStream = 2,
    Forced = 4,
};

struct FFF3FPBitmapSubtitleFrame {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPBitmapSubtitleFlags flags;
    std::uint32_t reserved;
    std::int64_t start100ns;
    std::int64_t end100ns;
    std::int32_t canvasWidth;
    std::int32_t canvasHeight;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
    std::int32_t stride;
    std::uint32_t pixelBytes;
    std::int64_t sequence;
};

#ifdef FFFNATIVE_EXPORTS
#define FFF3FP_API extern "C" __declspec(dllexport)
#else
#define FFF3FP_API extern "C" __declspec(dllimport)
#endif

FFF3FP_API std::uint32_t FFF3FP_GetApiVersion() noexcept;
FFF3FP_API FFFResult FFF3FP_Create(const FFF3FPConfiguration* configuration,
    FFF3FPHandle* player) noexcept;
FFF3FP_API FFFResult FFF3FP_Open(FFF3FPHandle player, const char* localPathUtf8) noexcept;
FFF3FP_API FFFResult FFF3FP_Play(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Pause(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Stop(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Close(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Seek(FFF3FPHandle player, std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekFrame(FFF3FPHandle player, std::int64_t frameIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_StepFrame(FFF3FPHandle player, std::int32_t direction) noexcept;
FFF3FP_API FFFResult FFF3FP_SelectVideoStream(FFF3FPHandle player,
    std::int32_t streamIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_SelectAudioStream(FFF3FPHandle player,
    std::int32_t streamIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_LoadExternalAudio(FFF3FPHandle player,
    const char* localPathUtf8, std::int32_t streamIndex, std::int64_t offset100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_ClearExternalAudio(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_SetExternalAudioOffset(FFF3FPHandle player,
    std::int64_t offset100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SetColorMode(FFF3FPHandle player, FFF3FPColorMode mode,
    float sdrPeakNits, float hdrPeakNits, float sdrPaperWhiteNits) noexcept;
FFF3FP_API FFFResult FFF3FP_SetOutputWindow(FFF3FPHandle player, void* outputWindow) noexcept;
FFF3FP_API FFFResult FFF3FP_SetAudioEndpoint(FFF3FPHandle player,
    const char* endpointIdUtf8) noexcept;
FFF3FP_API FFFResult FFF3FP_SetVolume(FFF3FPHandle player, float volume, std::uint32_t muted) noexcept;
FFF3FP_API FFFResult FFF3FP_GetSnapshot(FFF3FPHandle player, FFF3FPSnapshot* snapshot) noexcept;
FFF3FP_API FFFResult FFF3FP_GetMediaInfo(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API FFFResult FFF3FP_GetLastError(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_Destroy(FFF3FPHandle player) noexcept;

FFF3FP_API FFFResult FFF3FP_OpenBitmapSubtitle(const char* localPathUtf8,
    std::int32_t streamIndex, FFF3FPBitmapSubtitleHandle* decoder) noexcept;
FFF3FP_API FFFResult FFF3FP_ReadBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder,
    FFF3FPBitmapSubtitleFrame* frame) noexcept;
FFF3FP_API FFFResult FFF3FP_CopyBitmapSubtitlePixels(FFF3FPBitmapSubtitleHandle decoder,
    void* output, std::uint32_t outputSize) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder,
    std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_GetBitmapSubtitleLastError(FFF3FPBitmapSubtitleHandle decoder,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_DestroyBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder) noexcept;
