// Gating: this whole header sits inside #if FFF_WITH_LIBIAMF, so an IAMF-off
// build sees no BedBinauralRenderer declaration at all. Every consumer must
// include it from inside its own #if FFF_WITH_LIBIAMF block (same macro as the
// Condition on the BedBinauralRenderer.cpp/.h items in FFF.Native.vcxproj).
#pragma once
#if FFF_WITH_LIBIAMF

// Generic channel-bed -> binaural renderer.
//
// Why this exists: spatial audio outside IAMF (channel-based surround beds
// from legacy codecs, any multichannel PCM/FLAC/WAV) only ever got an
// swr matrix downmix on a stereo device: the energy survives, the placement
// does not, because a fixed-coefficient mix carries no azimuth. This class renders
// the bed through OBR instead (libiamf's own binaural renderer, Clear BSD plus
// the AOM patent grant, already linked in for IAMF -- no new library, no new licence).
//
// Key facts, every one of them measured by a dedicated bed-order layout probe:
//   * FFmpeg standard layouts and OBR layouts share the same channel order:
//     per-channel ILD across 5.1/7.1/5.1.2/7.1.4 all matches acoustics (FL=-11.7dB,
//     BL=-12.2dB, SL=-6.9dB, TFL=-9.7dB, TFR=+9.2dB ...);
//   * OBR slot semantics follow the kernel's own ground truth (the bed-order
//     finding in IamfAudioDecoder.cpp: slots 4..7 = BL BR SiL SiR);
//   * OBR does not drop LFE: LFE reaches both ears with ~0.21 rms symmetric energy,
//     so mapping zeroes every LFE slot: a measured implementation decision, not an option;
//   * OBR converts the bed to 3rd-order Ambisonics before binauralising it (DSP log).
//
// Threading contract: not thread-safe, owned exclusively by the caller (the audio
// session worker), under the same contract as PlayerWasapiRenderer::EnsureResampler.

struct AVChannelLayout;
struct AVFrame;
struct SwrContext;

#include <cstdint>
#include <vector>

/// OBR layout slot definitions. The header stays out of avutil/oar detail: positions
/// hold AVChannel values as int, oarLayout holds an oar_layout_t value.
struct BedBinauralLayout {
    int oarLayout;
    const int* positions;
    int count;
};

class BedBinauralRenderer {
public:
    BedBinauralRenderer();
    ~BedBinauralRenderer();
    BedBinauralRenderer(const BedBinauralRenderer&) = delete;
    BedBinauralRenderer& operator=(const BedBinauralRenderer&) = delete;

    /// Whether this FFmpeg layout has an OBR counterpart. When unsupported the caller
    /// must keep using the swr downmix (no worse than not having this class at all).
    static bool SupportsLayout(const AVChannelLayout& layout) noexcept;

    /// Feed one decoded frame (any sample format, planar or interleaved) and receive 0..N
    /// binaural stereo frames (planar float, 2 channels, nb_samples = block size). The
    /// caller owns av_frame_free for the AVFrames placed in out. position100ns is the
    /// absolute first-sample position of the frame, on Enqueue's clock; AV_NOPTS_VALUE if none.
    struct Block {
        AVFrame* frame = nullptr;
        std::int64_t position100ns = 0;
    };
    bool Process(const AVFrame* frame, std::int64_t position100ns,
        std::vector<Block>& out);

    /// Seek or track change: drop the half-filled block but keep the OBR handle (rebuilding costs DSP setup).
    void Reset() noexcept;

    /// Whether the binaural stage is producing (for UI and diagnostics: HRTF or downmix?).
    bool Active() const noexcept { return active_; }

    /// Output channel count (always 2).
    static constexpr int OutputChannels() noexcept { return 2; }

private:
    /// Pick an OBR layout and build the slot -> source gain table (slotSources).
    /// nullptr = no usable layout, and the caller falls back to the swr downmix.
    /// Rule: prefer the most exact matches, then the fewest idle non-LFE slots.
    static const BedBinauralLayout* SelectLayout(
        const AVChannelLayout& layout,
        std::vector<std::vector<std::pair<int, float>>>& slotSources);

    bool EnsurePipeline(const AVFrame* frame);   // build or rebuild the OAR and the internal swr
    void DestroyPipeline() noexcept;
    bool StageSlice(const AVFrame* frame, int firstSample, int sliceSamples);
    bool EmitBlock(std::int64_t position100ns, std::vector<Block>& out);


    void* oar_ = nullptr;              // oar_t* (void keeps this header free of oar.h)
    const BedBinauralLayout* layout_ = nullptr;    // the slot table currently in effect
    // Slot -> source channels (position, coefficient). The position holds an AVChannel
    // value as int; an empty list means the slot is silent (LFE is always empty).
    std::vector<std::vector<std::pair<int, float>>> slotSources_;
    std::uint32_t elementId_ = 1;
    int blockSamples_ = 0;             // samples per block as configured in OBR
    int limiterDelay_ = 0;
    SwrContext* toPlanarFloat_ = nullptr;    // input (any format) -> planar float
    AVChannelLayout* inputLayout_ = nullptr; // cached input layout for the internal swr (heap)
    int inputRate_ = 0;
    int inputFormat_ = 0;              // AVSampleFormat as int, to keep the header light
    bool inputIsPlanar_ = false;

    std::vector<std::vector<float>> planes_;  // accumulation buffers (one plane per OBR slot)
    int staged_ = 0;                          // samples staged so far (< blockSamples_)
    bool active_ = false;
};
#endif // FFF_WITH_LIBIAMF
