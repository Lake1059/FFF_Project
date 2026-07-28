#pragma once

#include "3FR/Api/FFF.Native.Api.h"

extern "C" {
#include <libavutil/channel_layout.h>
}

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFrame;
struct SwrContext;

class PlayerWasapiRenderer final {
public:
    explicit PlayerWasapiRenderer(std::wstring endpointId);
    ~PlayerWasapiRenderer();

    FFFResult Start() noexcept;
    void Stop() noexcept;
    FFFResult Enqueue(const AVFrame* frame, std::int64_t position100ns) noexcept;
    void SetPaused(bool paused) noexcept;
    void Reset(std::int64_t position100ns) noexcept;
    void SetVolume(float volume, bool muted) noexcept;
    std::int64_t Position100ns() const noexcept;
    std::int64_t TimelineLimit100ns() const noexcept;
    std::int64_t Buffered100ns() const noexcept;
    std::uint64_t UnderrunCount() const noexcept;
    std::uint64_t TimestampJitterCount() const noexcept;
    std::uint64_t DiscontinuityCount() const noexcept;
    std::uint64_t InsertedSilenceFrames() const noexcept;
    std::uint64_t DroppedOverlapFrames() const noexcept;
    std::string LastError() const;

private:
    struct AudioChunk {
        std::vector<std::uint8_t> bytes;
        std::size_t offset = 0;
        std::size_t silenceBytes = 0;
    };
    void RenderThread() noexcept;
    FFFResult EnsureResampler(const AVFrame* frame) noexcept;
    void SetError(std::string message) noexcept;

    std::wstring endpointId_;
    HANDLE stopEvent_;
    HANDLE sampleEvent_;
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
    std::size_t queuedBytes_;
    std::vector<std::uint8_t> converted_;
    SwrContext* resampler_;
    AVChannelLayout inputChannelLayout_;
    std::int32_t inputSampleRate_;
    std::int32_t inputSampleFormat_;
    std::uint32_t outputSampleRate_;
    std::uint16_t outputChannels_;
    std::uint16_t outputBlockAlign_;
    std::uint16_t outputBitsPerSample_;
    std::uint32_t outputChannelMask_;
    bool outputFloat_;
    std::atomic<float> volume_;
    std::atomic<bool> muted_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<bool> resetRequested_;
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
    // Decoded PCM is the authoritative continuous timeline. Container PTS is
    // used once to anchor it after open/seek and later only to identify a real
    // discontinuity. Chasing ordinary packet timestamp quantization here would
    // splice silence into valid AAC/VBR audio and audibly click.
    bool timelineAnchored_;
    std::uint64_t producedTimelineFrames_;
    std::atomic<std::uint64_t> timestampJitterCount_;
    std::atomic<std::uint64_t> discontinuityCount_;
    std::atomic<std::uint64_t> insertedSilenceFrames_;
    std::atomic<std::uint64_t> droppedOverlapFrames_;
    std::string lastError_;
};
