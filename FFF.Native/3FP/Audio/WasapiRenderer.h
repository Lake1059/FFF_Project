#pragma once

#include "3FR/Api/FFF.Native.Api.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

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
    std::int64_t Buffered100ns() const noexcept;
    std::string LastError() const;

private:
    void RenderThread() noexcept;
    FFFResult EnsureResampler(const AVFrame* frame) noexcept;
    void SetError(std::string message) noexcept;

    std::wstring endpointId_;
    HANDLE stopEvent_;
    HANDLE sampleEvent_;
    std::thread thread_;
    mutable std::mutex mutex_;
    mutable std::mutex errorMutex_;
    std::condition_variable initializedCondition_;
    bool initializationFinished_;
    FFFResult initializationResult_;
    std::deque<std::uint8_t> queue_;
    SwrContext* resampler_;
    std::uint32_t outputSampleRate_;
    std::uint16_t outputChannels_;
    std::uint16_t outputBlockAlign_;
    std::uint16_t outputBitsPerSample_;
    std::uint32_t outputChannelMask_;
    bool outputFloat_;
    float volume_;
    bool muted_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::atomic<bool> resetRequested_;
    std::atomic<std::int64_t> resetPosition100ns_;
    std::atomic<std::int64_t> clockPosition100ns_;
    std::atomic<std::uint64_t> pendingMediaFrames_;
    std::atomic<std::uint64_t> playedMediaFrames_;
    std::uint64_t submittedTimelineFrames_;
    std::string lastError_;
};
