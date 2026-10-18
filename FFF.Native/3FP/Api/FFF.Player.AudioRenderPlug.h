// Cross-PE audio render plug ABI. Additive surface: this header adds no
// FFF3FPSnapshot field, no offset pin, and does not bump PlayerApiVersion --
// new exports follow the existing convention (PlayerApi.cpp:148, :328, :386).
//
// Why a plug at all: rendering a channel bed to binaural output is an
// implementation that needs authorization to write, so it does not belong in
// the body. The body keeps only this neutral seam plus the matrix fold-down it
// already runs today. The host loads the external module, authenticates it, and
// hands the kernel a function table; PCM never leaves the kernel's own process
// boundary and never crosses the managed/native line.
//
// Everything here is plain-old-data. No AVFrame, no AVChannelLayout, no FFmpeg
// type or symbol may appear, because the module deliberately links no FFmpeg
// library (its import table is KERNEL32 + CRYPT32 + bcrypt, measured 2026-10-18
// and gated in its README section 4). Channel positions are FFmpeg AVChannel
// values passed as int32, and AV_CHAN_NONE (-1, libavutil/channel_layout.h:49)
// means "no such slot" -- the module maps them into its own layout tables.

#pragma once

#include "3FP/Api/FFF.Player.Api.h"   // FFFResult, FFF3FPHandle

#include <cstdint>

#define FFF3FP_AUDIO_PLUG_VERSION 1u

// Bed inputs wider than this must be declined by the plug. 12 channels covers
// 7.1.4; the renderer the plug replaces never addressed wider beds.
#define FFF3FP_AUDIO_PLUG_MAX_CHANNELS 16u

// What the kernel actually asks about (measured against the implementation in
// Audio/WasapiRenderer.cpp, so a module author can rely on it):
//   * it asks only when a plug is installed AND the endpoint is a 2-channel one;
//   * mono IS in scope -- one channel is a point source at a real azimuth, which
//     the body's fold-down collapses to the same signal twice with no spatial cue;
//   * a 2.0 frame is never handed over (the device target already is stereo);
//   * beds wider than FFF3FP_AUDIO_PLUG_MAX_CHANNELS are declined by the kernel and
//     never reach supports();
//   * 3..FFF3FP_AUDIO_PLUG_MAX_CHANNELS channels are offered as described.
//
// Counter meaning, as implemented: framesDeclined counts the bed-level "no"
// (supports() answering 0). A per-frame render() bail is not counted anywhere --
// the contract only promises a fold-down for that frame, so the visible trace of
// a bailing plug is the gap between framesRendered and the decoded frame count.
//
// Describes the bed once, when the kernel first sees a given format. The plug
// answers whether it can place this bed in azimuth; "no" is a normal answer and
// costs nothing but a decline counter.
struct FFF3FPAudioBedDescription {
    std::uint32_t size;         // sizeof(FFF3FPAudioBedDescription) as compiled
    std::uint32_t version;      // FFF3FP_AUDIO_PLUG_VERSION
    std::uint32_t sampleRate;
    std::uint32_t channelCount;             // <= FFF3FP_AUDIO_PLUG_MAX_CHANNELS
    std::int32_t positions[FFF3FP_AUDIO_PLUG_MAX_CHANNELS];  // AVChannel, -1 = unused
    std::uint32_t lfeIndex;                 // index into positions, or -1u if none
};

// One unit of work. Samples are always planar float32: converting the codec's
// native format to float is the kernel's job, so the plug never needs
// av_sample_fmt handling. `channels` is valid only for the duration of the call.
struct FFF3FPAudioBedFrame {
    std::uint32_t size;
    std::uint32_t version;
    const float* const* channels;   // channelCount planar arrays, frameCount each
    std::uint32_t channelCount;
    std::uint32_t frameCount;
    std::int64_t position100ns;     // media position; -1 when the frame is untimed
};

// The plug's entry table. All pointers must be non-null except destroy.
struct FFF3FPAudioRenderPlug {
    std::uint32_t size;      // sizeof(FFF3FPAudioRenderPlug) as compiled
    std::uint32_t version;   // FFF3FP_AUDIO_PLUG_VERSION
    void* context;           // opaque to the kernel, passed back verbatim

    // UTF-8, for the host to display which renderer is in charge.
    const char* name;

    // Called on the first frame of a format and on every format change.
    // Non-zero => this bed is accepted. Zero => matrix fold-down, no error.
    int32_t(*supports)(void* context, const FFF3FPAudioBedDescription* bed) noexcept;

    // Renders one frame into two caller-owned planar buffers of `outCapacity`
    // frames. Returns the number of frames written (<= outCapacity), or <= 0 to
    // decline this frame; a decline makes the kernel run its own fold-down for
    // exactly this frame, so a plug can bail out mid-stream without breaking
    // playback. Must not block: it runs on the endpoint's event thread.
    int32_t(*render)(void* context, const FFF3FPAudioBedFrame* frame,
        float* outLeft, float* outRight, std::uint32_t outCapacity) noexcept;

    // Format change, seek, device restart. Drop any internal phase/pipeline state.
    void(*reset)(void* context) noexcept;

    // Called when the kernel releases the plug (cleared, session teardown, or a
    // replacement install). May be null.
    void(*destroy)(void* context) noexcept;
};

#ifdef FFFNATIVE_EXPORTS
#define FFF3FP_API extern "C" __declspec(dllexport)
#else
#define FFF3FP_API extern "C" __declspec(dllimport)
#endif

// Installs or replaces the plug. Pass null to clear it and return to the matrix
// fold-down. Ownership of `plug->context` transfers to the kernel until the next
// install or teardown; the plug struct itself is copied, so the caller may free
// it immediately. Fails only on a bad size/version, which leaves the previous
// plug in place.
FFF3FP_API FFFResult FFF3FP_SetAudioRenderPlug(FFF3FPHandle player,
    const FFF3FPAudioRenderPlug* plug) noexcept;

// Reports whether a plug is currently installed (1) or the fold-down owns the
// bed (0). Both the plug path and the fold-down path write FFF3FPSnapshot's
// existing `audioBinaural`, which had no writer until this seam existed.
FFF3FP_API FFFResult FFF3FP_GetAudioRenderPlugState(FFF3FPHandle player,
    std::uint32_t* active, char* name, std::uint32_t nameCapacity,
    std::uint64_t* framesRendered, std::uint64_t* framesDeclined) noexcept;
