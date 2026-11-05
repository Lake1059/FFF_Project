// Cross-PE ASR audio tap ABI. Additive surface: this header adds no
// FFF3FPSnapshot field, no offset pin, and does not bump PlayerApiVersion --
// new exports follow the existing convention (PlayerApi.cpp tail-append, same
// as the render plug entries before them).
//
// Why a tap at all: real-time speech recognition needs the decoded audio bed
// *before* any bed route, fold-down, resample or gain touches it, and the
// recognition engine deliberately lives outside the player process (a separate
// worker, per docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md). This is the
// neutral socket that carries raw frames across that boundary: the kernel keeps
// a bounded ring, the host pulls blocks on its own thread, and the audio path
// pays one pointer load in Enqueue() while the tap is off.
//
// Everything here is plain-old-data. No AVFrame, no AVChannelLayout, no FFmpeg
// type or symbol may appear, for the same reason the render plug header keeps
// them out: the consumer is another process, not another C++ module. Channel
// positions are FFmpeg AVChannel values passed as int32 (-1 = no such slot),
// sampleFormat is an AVSampleFormat value, and position100ns is media time in
// 100 ns units (INT64_MIN = the frame carries no timestamp). The worker maps
// all three itself.

#pragma once

#include "3FP/Api/FFF.Player.Api.h"   // FFFResult, FFF3FPHandle, FFF3FP_API

#include <cstdint>

#define FFF3FP_AUDIO_TAP_VERSION 1u

// 24 aligns PlayerAudioRuntimeState::MaximumChannels (WasapiRenderer.h): the
// IAMF 7.1.4 render target is 12 channels and system H renders 24 through the
// same path, so a block has to be able to describe them all. A wider bed is not
// an error -- the frame is skipped and counted, and playback never blocks.
#define FFF3FP_AUDIO_TAP_MAX_CHANNELS 24u

// Ring sizing. 32 MiB holds ~29 s of 48 kHz 5.1 float32, which is orders of
// magnitude more than a pump thread that drains every millisecond needs; the
// ring only has to absorb pump stalls. Clamped to [MIN, MAX] and rounded up to
// a power of two by the kernel (the ring indexes with a mask).
#define FFF3FP_AUDIO_TAP_DEFAULT_RING_BYTES (32u * 1024u * 1024u)
#define FFF3FP_AUDIO_TAP_MIN_RING_BYTES (1u * 1024u * 1024u)
#define FFF3FP_AUDIO_TAP_MAX_RING_BYTES (256u * 1024u * 1024u)

// Optional; pass null for defaults. A struct that is not this ABI's
// size/version is refused and leaves any previously installed tap in place.
struct FFF3FPAudioTapConfig {
    std::uint32_t size;              // sizeof(FFF3FPAudioTapConfig) as compiled
    std::uint32_t version;           // FFF3FP_AUDIO_TAP_VERSION
    std::uint32_t ringCapacityBytes; // 0 = default; clamped to [MIN, MAX]
    std::uint32_t flags;             // v1: all zero, reserved
};

// One ring block's metadata plus the PCM that follows it in the caller's
// buffer. `bytesWritten` is the number of PCM bytes actually copied; when the
// buffer is too small the call fails with BufferTooSmall, `requiredBytes`
// reports the needed size and the block is NOT consumed (a bigger call gets
// the same block). PCM layout is the decoded frame's own: channel-major
// (plane by plane) for planar sample formats, interleaved for packed ones.
struct FFF3FPAudioTapBlock {
    std::uint32_t size;              // sizeof(FFF3FPAudioTapBlock) as compiled
    std::uint32_t version;           // FFF3FP_AUDIO_TAP_VERSION
    std::uint32_t bytesWritten;      // PCM bytes copied into the caller's buffer
    std::uint32_t requiredBytes;     // needed PCM bytes when BufferTooSmall
    std::uint64_t generation;        // session timeline generation (seek stamp)
    std::int64_t position100ns;      // media position of the first sample
    std::uint32_t frames;            // sample frames in this block
    std::uint32_t sampleRate;
    std::uint32_t channels;
    std::int32_t positions[FFF3FP_AUDIO_TAP_MAX_CHANNELS];  // AVChannel, -1 = none
    std::int32_t sampleFormat;       // AVSampleFormat
};

// Monotonic counters since the tap was installed. blocksDropped counts blocks
// that never reached a consumer: evicted to make room for newer ones (the ring
// is drop-oldest), or refused outright (a bed wider than the ABI describes).
// A non-zero value means the consumer is not keeping up -- a diagnostic, not
// an error, and never a reason to disturb playback.
struct FFF3FPAudioTapStats {
    std::uint32_t size;
    std::uint32_t version;
    std::uint64_t blocksProduced;
    std::uint64_t blocksDropped;
    std::uint64_t bufferedBytes;
};

// Creates and installs the tap, re-sizing the ring when one is already
// installed (the previous ring's contents do not survive). Pass null for
// defaults. Fails with InvalidArgument on a bad size/version, leaving any
// previous tap untouched, and with NativeFailure when the ring cannot be
// allocated.
FFF3FP_API FFFResult FFF3FP_EnableAudioTap(FFF3FPHandle player,
    const FFF3FPAudioTapConfig* config) noexcept;

// Uninstalls the tap and frees the ring. Safe to call when none is installed;
// after it, reads report an empty ring and stats report zeros rather than an
// error, so a pump loop that is still draining exits quietly.
FFF3FP_API FFFResult FFF3FP_DisableAudioTap(FFF3FPHandle player) noexcept;

// Non-blocking pull of at most one block: metadata into `block` (mandatory),
// PCM into `dst` (up to `capacity` bytes). Empty ring => Success with
// bytesWritten = 0. dst may be null when capacity is 0 (the resize probe).
FFF3FP_API FFFResult FFF3FP_ReadAudioTap(FFF3FPHandle player,
    void* dst, std::uint32_t capacity, FFF3FPAudioTapBlock* block) noexcept;

FFF3FP_API FFFResult FFF3FP_GetAudioTapStats(FFF3FPHandle player,
    FFF3FPAudioTapStats* stats) noexcept;
