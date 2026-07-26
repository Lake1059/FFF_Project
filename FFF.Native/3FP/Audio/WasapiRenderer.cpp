#include "pch.h"
#include "3FP/Audio/WasapiRenderer.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <avrt.h>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
bool IsFloatWaveFormat(const WAVEFORMATEX* format) noexcept {
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE || format->cbSize < 22) return false;
    return IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->SubFormat,
        KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
}

std::uint32_t ChannelMask(const WAVEFORMATEX* format) noexcept {
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->dwChannelMask;
    return 0;
}
}

PlayerWasapiRenderer::PlayerWasapiRenderer(std::wstring endpointId)
    : endpointId_(std::move(endpointId)), stopEvent_(nullptr), sampleEvent_(nullptr),
      initializationFinished_(false), initializationResult_(FFFResult::InvalidState), resampler_(nullptr),
      outputSampleRate_(0), outputChannels_(0), outputBlockAlign_(0), outputBitsPerSample_(0),
      outputChannelMask_(0), outputFloat_(false), volume_(1.0f), muted_(false), running_(false),
      paused_(true), resetRequested_(false), resetPosition100ns_(0), clockPosition100ns_(0),
      pendingMediaFrames_(0) {}

PlayerWasapiRenderer::~PlayerWasapiRenderer() { Stop(); }

FFFResult PlayerWasapiRenderer::Start() noexcept {
    if (running_.exchange(true)) return FFFResult::Success;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    sampleEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stopEvent_ == nullptr || sampleEvent_ == nullptr) {
        running_ = false; SetError("Could not create WASAPI playback events."); return FFFResult::DeviceFailure;
    }
    try { thread_ = std::thread(&PlayerWasapiRenderer::RenderThread, this); }
    catch (...) { running_ = false; return FFFResult::NativeFailure; }
    std::unique_lock lock(mutex_);
    initializedCondition_.wait(lock, [this] { return initializationFinished_; });
    return initializationResult_;
}

void PlayerWasapiRenderer::Stop() noexcept {
    if (stopEvent_ != nullptr) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    running_ = false;
    if (sampleEvent_ != nullptr) { CloseHandle(sampleEvent_); sampleEvent_ = nullptr; }
    if (stopEvent_ != nullptr) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
    if (resampler_ != nullptr) { swr_free(&resampler_); }
    std::lock_guard lock(mutex_); queue_.clear(); pendingMediaFrames_ = 0;
}

FFFResult PlayerWasapiRenderer::EnsureResampler(const AVFrame* frame) noexcept {
    if (resampler_ != nullptr) return FFFResult::Success;
    if (outputSampleRate_ == 0 || outputChannels_ == 0) return FFFResult::InvalidState;
    AVChannelLayout outputLayout{};
    av_channel_layout_default(&outputLayout, outputChannels_);
    const auto outputFormat = outputFloat_ ? AV_SAMPLE_FMT_FLT :
        (outputBitsPerSample_ == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32);
    const auto result = swr_alloc_set_opts2(&resampler_, &outputLayout, outputFormat,
        outputSampleRate_, &frame->ch_layout, static_cast<AVSampleFormat>(frame->format),
        frame->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outputLayout);
    if (result < 0 || resampler_ == nullptr || swr_init(resampler_) < 0) {
        if (resampler_ != nullptr) swr_free(&resampler_);
        SetError("FFmpeg could not initialize the WASAPI resampler.");
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

FFFResult PlayerWasapiRenderer::Enqueue(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->nb_samples <= 0) return FFFResult::InvalidArgument;
    std::lock_guard lock(mutex_);
    const auto ensured = EnsureResampler(frame);
    if (ensured != FFFResult::Success) return ensured;
    const auto capacity = swr_get_out_samples(resampler_, frame->nb_samples);
    if (capacity <= 0) return FFFResult::FfmpegFailure;
    std::vector<std::uint8_t> converted(static_cast<std::size_t>(capacity) * outputBlockAlign_);
    std::uint8_t* output[] = { converted.data() };
    const auto frames = swr_convert(resampler_, output, capacity,
        const_cast<const std::uint8_t* const*>(frame->extended_data), frame->nb_samples);
    if (frames < 0) { SetError("FFmpeg failed to resample decoded audio."); return FFFResult::FfmpegFailure; }
    converted.resize(static_cast<std::size_t>(frames) * outputBlockAlign_);
    const auto gain = muted_ ? 0.0f : volume_;
    if (outputFloat_) {
        auto* samples = reinterpret_cast<float*>(converted.data());
        for (std::size_t index = 0; index < converted.size() / sizeof(float); ++index) samples[index] *= gain;
    } else if (outputBitsPerSample_ == 16) {
        auto* samples = reinterpret_cast<std::int16_t*>(converted.data());
        for (std::size_t index = 0; index < converted.size() / sizeof(std::int16_t); ++index)
            samples[index] = static_cast<std::int16_t>(std::lround(samples[index] * gain));
    } else {
        auto* samples = reinterpret_cast<std::int32_t*>(converted.data());
        for (std::size_t index = 0; index < converted.size() / sizeof(std::int32_t); ++index)
            samples[index] = static_cast<std::int32_t>(std::llround(samples[index] * gain));
    }
    const auto maximumBytes = static_cast<std::size_t>(outputSampleRate_) * outputBlockAlign_ * 4;
    if (queue_.size() + converted.size() > maximumBytes) return FFFResult::BufferTooSmall;
    queue_.insert(queue_.end(), converted.begin(), converted.end());
    return FFFResult::Success;
}

void PlayerWasapiRenderer::SetPaused(const bool paused) noexcept {
    paused_ = paused;
}
void PlayerWasapiRenderer::Reset(const std::int64_t position100ns) noexcept {
    { std::lock_guard lock(mutex_); queue_.clear(); if (resampler_ != nullptr) swr_close(resampler_), swr_init(resampler_); }
    pendingMediaFrames_ = 0; resetPosition100ns_ = position100ns;
    clockPosition100ns_ = position100ns; resetRequested_ = true;
}
void PlayerWasapiRenderer::SetVolume(const float volume, const bool muted) noexcept {
    std::lock_guard lock(mutex_); volume_ = std::clamp(volume, 0.0f, 1.0f); muted_ = muted;
}
std::int64_t PlayerWasapiRenderer::Position100ns() const noexcept { return clockPosition100ns_.load(); }
std::int64_t PlayerWasapiRenderer::Buffered100ns() const noexcept {
    std::lock_guard lock(mutex_);
    if (outputSampleRate_ == 0 || outputBlockAlign_ == 0) return 0;
    const auto frames = queue_.size() / outputBlockAlign_ + pendingMediaFrames_.load();
    return static_cast<std::int64_t>(frames) * 10'000'000 / outputSampleRate_;
}
std::string PlayerWasapiRenderer::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
void PlayerWasapiRenderer::SetError(std::string message) noexcept { try { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); } catch (...) {} }

void PlayerWasapiRenderer::RenderThread() noexcept {
    const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(comResult);
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> renderer;
    ComPtr<IAudioClock> clock;
    WAVEFORMATEX* format = nullptr;
    UINT32 bufferFrames = 0;
    auto fail = [&](const char* message) {
        SetError(message);
        { std::lock_guard lock(mutex_); initializationResult_ = FFFResult::DeviceFailure; initializationFinished_ = true; }
        initializedCondition_.notify_all();
    };
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) { fail("Could not initialize COM for WASAPI playback."); return; }
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        (endpointId_.empty() ? FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device)) :
            FAILED(enumerator->GetDevice(endpointId_.c_str(), &device))) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)) ||
        FAILED(client->GetMixFormat(&format))) {
        fail("Could not open the selected WASAPI playback endpoint.");
        if (uninitialize) CoUninitialize(); return;
    }
    const REFERENCE_TIME duration = 1'000'000;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        duration, 0, format, nullptr)) || FAILED(client->SetEventHandle(sampleEvent_)) ||
        FAILED(client->GetBufferSize(&bufferFrames)) || FAILED(client->GetService(IID_PPV_ARGS(&renderer))) ||
        FAILED(client->GetService(IID_PPV_ARGS(&clock)))) {
        fail("Could not initialize event-driven WASAPI playback."); CoTaskMemFree(format);
        if (uninitialize) CoUninitialize(); return;
    }
    {
        std::lock_guard lock(mutex_);
        outputSampleRate_ = format->nSamplesPerSec; outputChannels_ = format->nChannels;
        outputBlockAlign_ = format->nBlockAlign; outputBitsPerSample_ = format->wBitsPerSample;
        outputChannelMask_ = ChannelMask(format); outputFloat_ = IsFloatWaveFormat(format);
        initializationResult_ = FFFResult::Success; initializationFinished_ = true;
    }
    initializedCondition_.notify_all();
    CoTaskMemFree(format); format = nullptr;
    client->Start();
    bool clientPaused = false;
    UINT64 clockFrequency = 0; clock->GetFrequency(&clockFrequency);
    UINT32 previousPadding = 0;
    HANDLE events[] = { stopEvent_, sampleEvent_ };
    while (WaitForMultipleObjects(2, events, FALSE, 100) != WAIT_OBJECT_0) {
        if (resetRequested_.exchange(false)) {
            client->Stop(); client->Reset(); client->Start();
            previousPadding = 0; pendingMediaFrames_ = 0;
        }
        const auto shouldPause = paused_.load();
        if (shouldPause != clientPaused) {
            if (shouldPause) client->Stop(); else client->Start();
            clientPaused = shouldPause;
        }
        if (clientPaused) continue;
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) continue;
        const auto playedFrames = previousPadding > padding ? previousPadding - padding : 0;
        const auto pendingFrames = pendingMediaFrames_.load();
        pendingMediaFrames_ = pendingFrames > playedFrames ? pendingFrames - playedFrames : 0;
        if (padding >= bufferFrames) { previousPadding = padding; continue; }
        const auto wantedFrames = bufferFrames - padding;
        BYTE* destination = nullptr;
        if (FAILED(renderer->GetBuffer(wantedFrames, &destination))) continue;
        std::size_t copied = 0;
        {
            std::lock_guard lock(mutex_);
            const auto wantedBytes = static_cast<std::size_t>(wantedFrames) * outputBlockAlign_;
            copied = std::min(wantedBytes, queue_.size());
            for (std::size_t index = 0; index < copied; ++index) { destination[index] = queue_.front(); queue_.pop_front(); }
            if (copied < wantedBytes) std::memset(destination + copied, 0, wantedBytes - copied);
        }
        renderer->ReleaseBuffer(wantedFrames, 0);
        pendingMediaFrames_ += copied / outputBlockAlign_;
        previousPadding = padding + wantedFrames;
        UINT64 position = 0, qpc = 0;
        if (clockFrequency > 0 && SUCCEEDED(clock->GetPosition(&position, &qpc)))
            clockPosition100ns_ = resetPosition100ns_.load() + static_cast<std::int64_t>(position * 10'000'000 / clockFrequency);
    }
    client->Stop();
    running_ = false;
    if (uninitialize) CoUninitialize();
}
