#pragma once

#include "3FR/Api/FFF.Native.Api.h"
#include "3FP/Api/FFF.Player.AudioRenderPlug.h"
#include "3FP/Api/FFF.Player.AudioPassthrough.h"
#include "3FP/Audio/AsrAudioTap.h"

extern "C" {
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFrame;
struct SwrContext;

struct PlayerAudioRuntimeState final {
    // 24, not 8: the level meters now have to carry the IAMF render target
    // (7.1.4 = 12 channels) plus head-room for wider targets the same probe
    // supports (sound system H renders 24 channels through the same path).
    // 8 silently truncated every IAMF stream to a partial meter. The fixed-size
    // arrays below and both clamp sites (renderer output here, PlayerSession's
    // UpdateInputAudioPeakLevels input side) size themselves from this
    // constant, and the public ABI copy (FFF3FP_GetAudioPeakLevels) stays
    // bounded by its own 8-slot struct -- so raising this only widens the
    // internal storage, not the API contract.
    static constexpr std::uint32_t MaximumChannels = 24;

    PlayerAudioRuntimeState() noexcept { ClearAll(); }

    void SetChannels(const std::uint32_t channels) noexcept {
        for (auto& value : values) value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : inputValues) value.store(0.0f, std::memory_order_relaxed);
        inputChannelCount.store(0, std::memory_order_release);
        channelCount.store(std::min(channels, MaximumChannels), std::memory_order_release);
    }

    void ClearValues() noexcept {
        for (auto& value : values) value.store(0.0f, std::memory_order_relaxed);
        for (auto& value : inputValues) value.store(0.0f, std::memory_order_relaxed);
    }

    void ResetDiagnostics() noexcept {
        buffered100ns.store(0, std::memory_order_relaxed);
        underruns.store(0, std::memory_order_relaxed);
        timestampJitterFrames.store(0, std::memory_order_relaxed);
        discontinuities.store(0, std::memory_order_relaxed);
        insertedSilenceFrames.store(0, std::memory_order_relaxed);
        droppedOverlapFrames.store(0, std::memory_order_relaxed);
        ClearValues();
    }

    void ClearAll() noexcept {
        SetChannels(0);
        ResetDiagnostics();
    }

    std::uint32_t Copy(float* output, const std::uint32_t capacity) const noexcept {
        const auto count = std::min({channelCount.load(std::memory_order_acquire),
            capacity, MaximumChannels});
        for (std::uint32_t index = 0; index < count; ++index)
            output[index] = values[index].load(std::memory_order_relaxed);
        return count;
    }

    std::atomic<std::uint32_t> channelCount{0};
    std::atomic<std::uint32_t> inputChannelCount{0};
    std::array<std::atomic<float>, MaximumChannels> values{};
    std::array<std::atomic<float>, MaximumChannels> inputValues{};
    std::atomic<std::int64_t> buffered100ns{0};
    std::atomic<std::uint64_t> underruns{0};
    std::atomic<std::uint64_t> timestampJitterFrames{0};
    std::atomic<std::uint64_t> discontinuities{0};
    std::atomic<std::uint64_t> insertedSilenceFrames{0};
    std::atomic<std::uint64_t> droppedOverlapFrames{0};
    // External bed render plug seam (FFF.Player.AudioRenderPlug.h). Published here
    // rather than read straight off the renderer because PlayerSession may replace
    // the renderer at any time (device switch, exclusive-mode fallback, restart),
    // and the API thread that answers FFF3FP_GetAudioRenderPlugState must never
    // dereference it. These are deliberately NOT FFF3FPSnapshot fields: the seam
    // adds no snapshot member and moves no offset pin; `audioBinaural` is the only
    // snapshot field it writes, and that one already existed without a writer.
    // Not part of ResetDiagnostics()/ClearAll(): a seek must not erase the evidence
    // of which renderer owned the bed. They restart with the renderer instance.
    std::atomic<std::uint32_t> plugBedActive{0};
    std::atomic<std::uint64_t> plugFramesRendered{0};
    std::atomic<std::uint64_t> plugFramesDeclined{0};
    // Bitstream passthrough seam (FFF.Player.AudioPassthrough.h). Published with
    // the same reasoning as the plug counters above: the API thread that answers
    // FFF3FP_GetAudioPassthroughState must never dereference the renderer, which
    // a device switch may replace at any moment. Bursts rendered by the current
    // renderer instance; a seek deliberately does not clear it (a seek is a
    // re-anchor, not an erasure of what was already played).
    std::atomic<std::uint64_t> ptBurstsRendered{0};
    // Endpoint shape and the binaural route policy, published for the same reason
    // as the counters above: the API thread that answers
    // FFF3FP_GetAudioBinauralStatus must never dereference the renderer, which a
    // device switch may replace at any moment. `endpointFormFactor` is the raw
    // EndpointFormFactor Windows reports for the endpoint the renderer actually
    // opened; it is informational now (the automatic policy keys on the channel
    // count, not the shape -- product decision 2026-11-12). `endpointChannels` is
    // the device mix format's channel count, written once per renderer instance
    // as soon as the device opens, and IS the automatic policy's input.
    // Neither is part of a FFF3FPSnapshot: the seam added no snapshot member and
    // this one deliberately adds none either.
    std::atomic<std::uint32_t> endpointFormFactor{0};
    std::atomic<std::uint32_t> endpointChannels{0};
    std::atomic<std::uint32_t> plugInstalled{0};
};

// Bed-route rule (product decision 2026-11-12, supersedes the old "headphone
// form factor 3/5" rule): the automatic policy defaults binaural ON for any
// 2-channel endpoint and falls back to the matrix fold-down for multichannel
// endpoints. Windows classifies most USB DACs as DigitalAudioDisplayDevice (9)
// even though their mix format is stereo, which under the old rule silently
// disabled the default the product wants; the channel count is the ground truth
// the renderer actually opens with. Forced policies still override: 1 forces
// binaural, 2 forces fold-down. This is the one predicate both the renderer's
// per-frame route decision and the session's read-back use, so they cannot
// disagree about what counts as binaural output.
inline bool BinauralWantedFor(std::uint32_t policy, std::uint32_t endpointChannels) noexcept {
    if (policy == 1u) return true;   // force binaural
    if (policy == 2u) return false;  // force stereo
    return endpointChannels == 2u;   // automatic: stereo endpoint => binaural default
}

class PlayerWasapiRenderer final {
public:
    explicit PlayerWasapiRenderer(std::wstring endpointId, bool exclusive = false,
        PlayerAudioRuntimeState* runtimeState = nullptr,
        std::function<void()> restartCallback = {});
    ~PlayerWasapiRenderer();

    FFFResult Start() noexcept;
    void Stop() noexcept;
    FFFResult Enqueue(const AVFrame* frame, std::int64_t position100ns,
        std::int64_t timestampResolution100ns = 0) noexcept;
    FFFResult Finish() noexcept;
    void SetPaused(bool paused) noexcept;
    void Reset(std::int64_t position100ns) noexcept;
    void SetVolume(float volume, bool muted) noexcept;
    void SetMonoAudio(bool enabled) noexcept;
    // 0 = automatic (2-channel endpoint => binaural default; multichannel =>
    // matrix fold-down), 1 = force binaural, 2 = force stereo.
    // Read per bed frame, so a change lands on the next frame boundary without
    // touching the plug, the device or the renderer instance.
    void SetBinauralPolicy(std::uint32_t policy) noexcept;
    // The policy half of the bed-route decision. Public because the session's
    // read-back has to answer with this exact rule rather than a second copy of it,
    // and inline because that is what lets a probe exercise the shipped rule
    // directly (see 3fp/tools/audio_binaural_probe.cpp).
    static bool BinauralWantedFor(std::uint32_t policy, std::uint32_t endpointChannels) noexcept {
        if (policy == 1u) return true;   // force binaural
        if (policy == 2u) return false;  // force stereo
        return endpointChannels == 2u;   // automatic: stereo endpoint => binaural default
    }
    // Raw EndpointFormFactor read when this instance opened its device.
    std::uint32_t EndpointFormFactor() const noexcept;
    std::int64_t Position100ns() const noexcept;
    std::int64_t TimelineLimit100ns() const noexcept;
    std::int64_t Buffered100ns() const noexcept;
    std::uint64_t UnderrunCount() const noexcept;
    // The buffer duration WASAPI actually granted at Initialize time (both modes,
    // including the exclusive aligned retry), in 100 ns units. 0 before Start.
    std::int64_t DeviceBufferDuration100ns() const noexcept;
    std::uint64_t TimestampJitterCount() const noexcept;
    std::uint64_t DiscontinuityCount() const noexcept;
    std::uint64_t InsertedSilenceFrames() const noexcept;
    std::uint64_t DroppedOverlapFrames() const noexcept;
    std::uint32_t OutputSampleRate() const noexcept;
    std::uint16_t OutputChannels() const noexcept;
    std::uint16_t OutputBitsPerSample() const noexcept;
    std::uint16_t OutputValidBitsPerSample() const noexcept;
    bool OutputIsFloat() const noexcept;
    bool RestartRequested() const noexcept;
    std::string LastError() const;

    // ---- external bed render plug (FFF.Player.AudioRenderPlug.h) ----
    // Install (plug != nullptr) or clear (plug == nullptr) the plug. Called on the
    // session worker -- the same thread that calls Enqueue/Reset -- so the swap can
    // never race a render() call, and a new bed route only takes effect at the next
    // frame boundary. The struct is copied; the `context` pointer is BORROWED:
    // PlayerSession owns the context and is the only code that calls plug->destroy,
    // because this renderer may be replaced while the same context is about to be
    // installed into its successor. Returns false only on a null plug argument that
    // cannot be a clear request, i.e. never; validation lives in PlayerSession.
    void SetAudioRenderPlug(const FFF3FPAudioRenderPlug* plug) noexcept;
    bool AudioRenderPlugInstalled() const noexcept { return plugInstalled_.load(std::memory_order_acquire); }
    std::uint64_t PlugFramesRendered() const noexcept { return plugFramesRendered_.load(std::memory_order_relaxed); }
    std::uint64_t PlugFramesDeclined() const noexcept { return plugFramesDeclined_.load(std::memory_order_relaxed); }
    // True while the most recent bed frame was produced by the plug rather than by
    // the matrix fold-down. This is what writes FFF3FPSnapshot's `audioBinaural`.
    bool BedRenderedByPlug() const noexcept { return bedRenderedByPlug_.load(std::memory_order_relaxed); }

    // ---- ASR audio tap (FFF.Player.AudioTap.h) ----
    // Install (tap != nullptr) or clear (tap == nullptr) the tap. Called on the
    // session worker -- the same thread that calls Enqueue -- so the swap can
    // never race a Produce, exactly like SetAudioRenderPlug. The tap is held by
    // reference for as long as it is installed, which is what lets the session
    // replace or free it across a device switch without a retire list.
    void SetAsrTap(std::shared_ptr<AsrAudioTap> tap) noexcept;

    // ---- bitstream passthrough (FFF.Player.AudioPassthrough.h) ----
    // Arms the non-PCM exclusive route with the module's format decision. Must
    // be called BEFORE Start() (the session recreates the renderer when the lane
    // engages, so this is always true) -- a decision is not retro-fitted onto a
    // live endpoint. `degrade` is the session's one-step GUID fallback for a
    // rejected format probe (variant -> generic carrier, see the lane); it runs on
    // the render thread before the device is initialized and is invoked at most
    // once. Passing a null format with a decision already armed is refused.
    bool SetBitstreamFormat(const FFF3FPAudioPassthroughFormat& format,
        std::function<bool(FFF3FPAudioPassthroughFormat&)> degrade) noexcept;
    // True while the armed decision is in force (device not necessarily open yet).
    bool BitstreamActive() const noexcept { return bitstreamActive_; }
    // One burst of packed IEC 61937 bytes (s16le sample words, already
    // byte-order-prepared by the module). Runs on the session worker next to
    // Enqueue; bytes queue strictly in arrival order and are metered by the
    // burst's own media duration, never by swr/gain/tempo logic.
    FFFResult EnqueueBitstream(const std::uint8_t* burst, std::uint32_t burstSize,
        std::int64_t pts100ns, std::int64_t duration100ns) noexcept;
    // Diagnostics for the session's fallback decisions: the HRESULT the last
    // exclusive Initialize failed with, and whether the endpoint rejected the
    // bitstream *format* itself (as opposed to being busy with another owner).
    std::int64_t LastInitHresult() const noexcept { return lastInitHresult_; }
    bool BitstreamFormatRejected() const noexcept { return bitstreamFormatRejected_; }

private:
    // Which route one decoded frame takes through the seam.
    enum class BedRoute {
        // Outside the seam: no plug, a 2.0 bed (the device is already stereo), or a
        // device that is not a stereo endpoint. Exactly today's code path, and no
        // seam counter moves.
        Native,
        // In the seam, and the plug said no. The frame goes through the matrix
        // fold-down and counts as a decline (see the accounting rule in .cpp).
        Declined,
        // In the seam, and the plug accepted this bed: every frame of it is handed
        // to plug->render() and only rate-converted afterwards.
        Plug,
        // Bitstream passthrough. Never returned by SelectBedRoute:
        // that seam sees decoded PCM frames only. This route is armed explicitly
        // via SetBitstreamFormat and switches the WHOLE renderer to the burst byte
        // queue -- Enqueue() refuses frames while it is active, because mixing
        // packed IEC 61937 bursts with resampled PCM on one endpoint is noise.
        Bitstream,
    };
    struct AudioChunk {
        std::vector<std::uint8_t> bytes;
        std::size_t offset = 0;
        std::size_t silenceBytes = 0;
    };
    void RenderThread() noexcept;
    void ApplyGain(std::vector<std::uint8_t>& converted) noexcept;
    FFFResult EnsureResampler(const AVFrame* frame, const BedRoute route) noexcept;
    // The same swr allocation the non-seam path builds, but kept in its own slot so
    // a plug that bails out mid-stream can fold exactly the way today's code folds
    // (same layout, same rate, same rematrix options) without tearing down the
    // plug-fed slot. Only ever created for a frame the plug declined.
    FFFResult EnsureFoldResampler(const AVFrame* frame) noexcept;
    // Bed in its codec format -> the same bed as planar float32, channel layout and
    // sample rate untouched. Skipped entirely when the frame already is FLTP.
    FFFResult EnsureBedConverter(const AVFrame* frame) noexcept;
    BedRoute SelectBedRoute(const AVFrame* frame) noexcept;
    bool BinauralWanted() const noexcept;
    // Renders one whole bed frame through the plug into plugLeft_/plugRight_.
    // Returns the frame count produced, or <= 0 when the plug declines this frame.
    std::int32_t RenderBedWithPlug(const AVFrame* frame, std::int64_t position100ns) noexcept;
    void ReleaseBedSeamBuffers() noexcept;
    void FreePlugResamplers() noexcept;
    void UpdatePeakLevels(const std::uint8_t* samples, std::uint32_t frames) noexcept;
    void PublishRuntimeDiagnostics() noexcept;
    void RequestRestart(std::string message) noexcept;
    void CloseEvents() noexcept;
    void SetError(std::string message) noexcept;

    std::wstring endpointId_;
    bool exclusive_;
    PlayerAudioRuntimeState* runtimeState_;
    std::function<void()> restartCallback_;
    HANDLE stopEvent_;
    HANDLE sampleEvent_;
    HANDLE controlEvent_;
    std::thread thread_;
    mutable std::mutex mutex_;
    mutable std::mutex clockMutex_;
    mutable std::mutex errorMutex_;
    std::condition_variable initializedCondition_;
    bool initializationFinished_;
    FFFResult initializationResult_;
    // Chunked SPSC queue avoids vector compaction and represents long edit gaps
    // as logical silence instead of allocating several seconds of zero-filled PCM.
    std::deque<AudioChunk> queue_;
    std::deque<std::vector<std::uint8_t>> reusableBuffers_;
    std::size_t queuedBytes_;
    SwrContext* resampler_;
    bool monoAudio_ = false; // Only accessed by the session worker, like resampler_.
    AVChannelLayout inputChannelLayout_;
    std::int32_t inputSampleRate_;
    std::int32_t inputSampleFormat_;
    // Seam slots. foldResampler_ mirrors resampler_'s role for one declined frame;
    // bedConverter_ only ever changes sample format, never layout or rate, because
    // the plug's contract is planar float32 and handing it the codec's native
    // format would push av_sample_fmt knowledge across the module boundary.
    SwrContext* foldResampler_ = nullptr;
    AVChannelLayout foldInputLayout_{};
    std::int32_t foldSampleRate_ = 0;
    std::int32_t foldSampleFormat_ = -1;
    SwrContext* bedConverter_ = nullptr;
    AVChannelLayout bedConverterLayout_{};
    std::int32_t bedConverterRate_ = 0;
    std::int32_t bedConverterFormat_ = -1;
    // Copied plug view (context borrowed, see SetAudioRenderPlug) plus the bed it
    // was last asked about. Touched only on the session worker, like Enqueue.
    FFF3FPAudioRenderPlug plug_{};
    std::atomic<bool> plugInstalled_{false};
    struct BedIdentity final {
        std::int32_t sampleRate = 0;
        std::int32_t channels = 0;
        std::int32_t format = -1;
        // Position map folded into one value: two layouts with the same channel
        // count are different beds and the plug's answer may differ.
        std::uint64_t positions = 0;
        bool operator==(const BedIdentity& other) const noexcept {
            return sampleRate == other.sampleRate && channels == other.channels &&
                format == other.format && positions == other.positions;
        }
    };
    BedIdentity plugBedIdentity_{};
    bool plugBedAccepted_ = false;
    std::atomic<std::uint64_t> plugFramesRendered_{0};
    std::atomic<std::uint64_t> plugFramesDeclined_{0};
    std::atomic<bool> bedRenderedByPlug_{false};
    // ASR tap, held by reference (the session owns the authoritative one, so a
    // device switch re-installs it into the successor instead of inheriting an
    // instance's ownership). Touched only on the session worker, like the plug
    // view above; Enqueue reads it there and nowhere else.
    std::shared_ptr<AsrAudioTap> asrTap_;
    // ---- bitstream passthrough state ----------------------------------------
    // The decision is armed before Start() and never flipped on a live renderer
    // (engagement re-creates the renderer), so the render thread may read
    // bitstreamActive_ without a lock. The burst queue shares mutex_ with the
    // PCM queue on purpose: Reset/consumption must stay atomic against bytes,
    // whichever queue they live in.
    bool bitstreamActive_ = false;
    FFF3FPAudioPassthroughFormat bitstreamFormat_{};
    std::function<bool(FFF3FPAudioPassthroughFormat&)> bitstreamDegrade_;
    struct BitstreamBurst final {
        std::vector<std::uint8_t> bytes;
        // Device-period boundaries need not align with burst boundaries, so a
        // burst can straddle two callbacks; the drain resumes at this offset.
        std::size_t offset = 0;
        std::int64_t pts100ns = 0;
        std::int64_t duration100ns = 0;
    };
    std::deque<BitstreamBurst> bitstreamQueue_;
    std::size_t bitstreamQueuedBytes_ = 0;
    // (Link rate / channel count of the open endpoint live in outputSampleRate_ /
    // outputChannels_: the bitstream device IS initialized at the link format, so
    // the two are the same numbers by construction.)
    // Clock anchor for the content timeline (see updateMediaClock): the media
    // PTS of the first burst queued after the last reset.
    bool bitstreamPtsAnchored_ = false;
    std::int64_t bitstreamPtsAnchor100ns_ = 0;
    std::atomic<std::uint64_t> ptBurstsRendered_{0};
    std::int64_t lastInitHresult_ = 0;
    bool bitstreamFormatRejected_ = false;
    // Scratch, kept as members so a steady-state stream allocates nothing per frame.
    std::vector<float> bedPlaneScratch_;    // channelCount planes, stride below
    std::vector<float> plugLeftScratch_;
    std::vector<float> plugRightScratch_;
    std::size_t bedPlaneStrideFrames_ = 0;
    std::uint32_t outputSampleRate_;
    std::int64_t deviceBufferDuration100ns_ = 0;
    std::uint16_t outputChannels_;
    std::uint16_t outputBlockAlign_;
    std::uint16_t outputBitsPerSample_;
    std::uint16_t outputValidBitsPerSample_;
    bool outputFloat_;
    std::atomic<float> volume_;
    std::atomic<bool> muted_;
    std::atomic<bool> running_;
    std::atomic<bool> clockRunning_;
    std::atomic<bool> paused_;
    std::atomic<bool> resetRequested_;
    std::atomic<bool> restartRequested_;
    // Binaural route policy (0 auto / 1 force / 2 off) and the device shape read
    // once per renderer instance. Both are plain atomics read on the bed path: no
    // lock, no COM, no property store on the audio thread after open.
    std::atomic<std::uint32_t> binauralPolicy_{0};
    std::atomic<std::uint32_t> endpointFormFactor_{0};

    std::atomic<std::int64_t> resetPosition100ns_;
    // IAudioClock is sampled on the event thread. Readers interpolate between
    // samples with the correlated QPC timestamp, but never beyond audio that
    // has actually been submitted to WASAPI.
    std::int64_t clockPosition100ns_;
    std::int64_t clockSampleQpc100ns_;
    std::int64_t clockLimitPosition100ns_;
    std::uint64_t clockEpoch_;
    std::atomic<std::uint64_t> pendingMediaFrames_;
    std::atomic<std::uint64_t> playedMediaFrames_;
    // Count once per starvation episode after media has actually arrived;
    // startup, pause and reset are deliberately excluded.
    std::atomic<std::uint64_t> underrunCount_;
    std::atomic<bool> hasSubmittedAudio_;
    std::atomic<bool> endOfStream_;
    // Decoded PCM is the authoritative continuous timeline. Container PTS is
    // used once to anchor it after open/seek and later only to identify a real
    // discontinuity. Chasing ordinary packet timestamp quantization here would
    // splice silence into valid AAC/VBR audio and audibly click.
    bool timelineAnchored_;
    std::uint64_t producedTimelineFrames_;
    // Number of output frames still covered by the startup/seek de-click ramp.
    // This is deliberately renderer-local so every device restart and seek has
    // the same zero-to-signal boundary regardless of codec/container PTS.
    std::uint32_t fadeInFramesRemaining_;
    std::atomic<std::uint64_t> timestampJitterCount_;
    std::atomic<std::uint64_t> discontinuityCount_;
    std::atomic<std::uint64_t> insertedSilenceFrames_;
    std::atomic<std::uint64_t> droppedOverlapFrames_;
    std::string lastError_;
};
