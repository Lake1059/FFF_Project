// Passthrough plug ABI. Module: IEC 61937 packing + format semantics. Kernel: seam only.
// ABI contract: POD only; module must not link FFmpeg.

#pragma once

#include "3FP/Api/FFF.Player.Api.h"   // FFFResult, FFF3FPHandle
#include <cstdint>

#define FFF3FP_AUDIO_PASSTHROUGH_VERSION 1u
#define FFF3FP_AUDIO_PASSTHROUGH_BURST_CAPACITY_MIN 61440u

// Kernel-owned codec id; maps AV_CODEC_ID_* at the interception point. The
// family numbering is owned by the module: the kernel only translates FFmpeg's
// public AV_CODEC_ID values onto these numbers and never interprets them.
enum FFF3FPAudioPassthroughCodec : std::uint32_t {
    FFF3FP_PT_CODEC_NONE = 0,
    FFF3FP_PT_CODEC_TYPE1 = 1,   // family 1..5; meaning owned by the module
    FFF3FP_PT_CODEC_TYPE2 = 2,
    FFF3FP_PT_CODEC_TYPE3 = 3,
    FFF3FP_PT_CODEC_TYPE4 = 4,
    FFF3FP_PT_CODEC_TYPE5 = 5,
};

#define FFF3FP_PT_MODE_AUTO 0u    // engage when module accepts and endpoint supports it
#define FFF3FP_PT_MODE_FORCE 1u   // same decision, host treats failures as loud
#define FFF3FP_PT_MODE_OFF 2u

// Module decision flags. Bit meanings are owned by the module; the kernel only
// carries them verbatim into diagnostics and clears flag 1 when its one-step
// carrier degrade retargets the family's generic carrier.
#define FFF3FP_PT_FLAG_1 0x1u
#define FFF3FP_PT_FLAG_2 0x2u
#define FFF3FP_PT_FLAG_3 0x4u

// Audio track description (describe() input).
struct FFF3FPAudioPassthroughTrackInfo {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t codecId;           // FFF3FPAudioPassthroughCodec
    std::uint32_t sampleRate;        // content rate
    std::uint32_t channelCount;
    const std::uint8_t* firstFrame;  // one complete frame, not owned
    std::uint32_t firstFrameSize;
    const char* endpointHint;        // UTF-8 endpoint name, may be null
};

// Format decision (describe() output). subFormat packs the IEC 61937 SubFormat
// GUID as {Data1, Data2 | Data3 << 16, Data4[0..3], Data4[4..7]}, e.g.
// 0000010a-0cea-0010-8000-00aa00389b71 =>
//   {0x0000010A, 0x00100CEA, 0xAA000080, 0x719B3800}
struct FFF3FPAudioPassthroughFormat {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t subFormat[4];
    std::uint32_t linkSampleRate;    // link rate, not content rate
    std::uint32_t linkChannels;      // 2 or 8
    std::uint32_t burstBytes;        // 6144 / 8192 / 24576 / 61440
    std::uint32_t flags;             // FFF3FP_PT_FLAG_*
    const char* displayName;         // UTF-8, static storage
};

// Module entry table. All pointers non-null except destroy.
struct FFF3FPAudioPassthroughApi {
    std::uint32_t size;
    std::uint32_t version;
    void* context;           // opaque to the kernel

    // 0 = accepted and outFormat written; <0 = decline (kernel decodes instead).
    std::int32_t(*describe)(void* context,
        const FFF3FPAudioPassthroughTrackInfo* info,
        FFF3FPAudioPassthroughFormat* outFormat) noexcept;

    // Consume one complete frame, emit at most one burst (s16le ready).
    // burstSize == 0 = buffered (accumulation or pre-sync drop, module's call).
    // When burstSize > 0: burstPts100ns = pts of the first frame folded in,
    // burstDuration100ns = media time the burst covers.
    // 0 = ok, <0 = hard error (kernel falls back for the track).
    std::int32_t(*pack)(void* context,
        const std::uint8_t* frame, std::uint32_t frameSize, std::int64_t framePts100ns,
        std::uint8_t* outBurst, std::uint32_t burstCapacity,
        std::uint32_t* burstSize, std::int64_t* burstPts100ns,
        std::int64_t* burstDuration100ns) noexcept;

    void(*reset)(void* context) noexcept;    // seek / track switch
    void(*destroy)(void* context) noexcept;  // may be null
};

#ifdef FFFNATIVE_EXPORTS
#define FFF3FP_API extern "C" __declspec(dllexport)
#else
#define FFF3FP_API extern "C" __declspec(dllimport)
#endif

// Install/replace the table; null clears it. Context ownership transfers to the
// kernel. Bad size/version leaves the previous table in place.
FFF3FP_API FFFResult FFF3FP_SetAudioPassthroughApi(FFF3FPHandle player,
    const FFF3FPAudioPassthroughApi* api) noexcept;

// FFF3FP_PT_MODE_*. Applies at the next track open.
FFF3FP_API FFFResult FFF3FP_SetAudioPassthroughMode(FFF3FPHandle player,
    std::uint32_t mode) noexcept;

// active=1 while the lane owns the track; name is the module's displayName.
FFF3FP_API FFFResult FFF3FP_GetAudioPassthroughState(FFF3FPHandle player,
    std::uint32_t* active, std::uint32_t* mode, std::uint32_t* codecId,
    char* name, std::uint32_t nameCapacity,
    std::uint64_t* burstsRendered) noexcept;
