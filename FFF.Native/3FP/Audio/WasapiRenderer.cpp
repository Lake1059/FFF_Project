#include "pch.h"
#include "3FP/Audio/WasapiRenderer.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <avrt.h>
#include <array>
#include <cmath>
#include <cstring>

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

std::int64_t QpcNow100ns() noexcept {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    if (frequency <= 0) return 0;
    const auto seconds = value.QuadPart / frequency;
    const auto remainder = value.QuadPart % frequency;
    return seconds * 10'000'000 + remainder * 10'000'000 / frequency;
}
}

PlayerWasapiRenderer::PlayerWasapiRenderer(std::wstring endpointId, const bool exclusive,
    PlayerAudioRuntimeState* const runtimeState)
    : endpointId_(std::move(endpointId)), exclusive_(exclusive), runtimeState_(runtimeState),
      stopEvent_(nullptr), sampleEvent_(nullptr), controlEvent_(nullptr),
      initializationFinished_(false), initializationResult_(FFFResult::InvalidState), queuedBytes_(0),
      resampler_(nullptr),
      inputChannelLayout_{}, inputSampleRate_(0), inputSampleFormat_(-1),
      outputSampleRate_(0), outputChannels_(0), outputBlockAlign_(0), outputBitsPerSample_(0),
      outputValidBitsPerSample_(0),
      outputChannelMask_(0), outputFloat_(false), volume_(1.0f), muted_(false), running_(false),
      paused_(true), resetRequested_(false), resetPosition100ns_(0), clockPosition100ns_(0),
      clockSampleQpc100ns_(0), clockLimitPosition100ns_(0), clockEpoch_(0),
      pendingMediaFrames_(0), playedMediaFrames_(0), underrunCount_(0),
      hasSubmittedAudio_(false), timelineAnchored_(false), producedTimelineFrames_(0),
      timestampJitterCount_(0), discontinuityCount_(0), insertedSilenceFrames_(0),
      droppedOverlapFrames_(0) {}

PlayerWasapiRenderer::~PlayerWasapiRenderer() { Stop(); }

FFFResult PlayerWasapiRenderer::Start() noexcept {
    if (running_.exchange(true)) return FFFResult::Success;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    sampleEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    controlEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stopEvent_ == nullptr || sampleEvent_ == nullptr || controlEvent_ == nullptr) {
        running_ = false; SetError("Could not create WASAPI playback events."); return FFFResult::DeviceFailure;
    }
    try { thread_ = std::thread(&PlayerWasapiRenderer::RenderThread, this); }
    catch (...) { running_ = false; return FFFResult::NativeFailure; }
    std::unique_lock lock(mutex_);
    initializedCondition_.wait(lock, [this] { return initializationFinished_; });
    return initializationResult_;
}

void PlayerWasapiRenderer::Stop() noexcept {
    const auto hadResources = thread_.joinable() || stopEvent_ != nullptr ||
        sampleEvent_ != nullptr || controlEvent_ != nullptr || resampler_ != nullptr;
    if (stopEvent_ != nullptr) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    running_ = false;
    if (controlEvent_ != nullptr) { CloseHandle(controlEvent_); controlEvent_ = nullptr; }
    if (sampleEvent_ != nullptr) { CloseHandle(sampleEvent_); sampleEvent_ = nullptr; }
    if (stopEvent_ != nullptr) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
    if (resampler_ != nullptr) { swr_free(&resampler_); }
    av_channel_layout_uninit(&inputChannelLayout_);
    inputSampleRate_ = 0; inputSampleFormat_ = -1;
    outputSampleRate_ = 0; outputChannels_ = 0; outputBlockAlign_ = 0;
    outputBitsPerSample_ = outputValidBitsPerSample_ = 0;
    if (hadResources && runtimeState_ != nullptr) runtimeState_->ClearAll();
    std::lock_guard lock(mutex_);
    queue_.clear(); queuedBytes_ = 0; converted_.clear(); converted_.shrink_to_fit(); pendingMediaFrames_ = 0;
}

FFFResult PlayerWasapiRenderer::EnsureResampler(const AVFrame* frame) noexcept {
    const auto inputFormat = static_cast<std::int32_t>(frame->format);
    if (resampler_ != nullptr && inputSampleRate_ == frame->sample_rate &&
        inputSampleFormat_ == inputFormat &&
        av_channel_layout_compare(&inputChannelLayout_, &frame->ch_layout) == 0)
        return FFFResult::Success;
    if (outputSampleRate_ == 0 || outputChannels_ == 0) return FFFResult::InvalidState;
    // Track the complete FFmpeg layout contract, not merely channel count. A
    // stream can switch between layouts with the same number of channels.
    if (resampler_ != nullptr) swr_free(&resampler_);
    av_channel_layout_uninit(&inputChannelLayout_);
    if (av_channel_layout_copy(&inputChannelLayout_, &frame->ch_layout) < 0)
        return FFFResult::FfmpegFailure;
    inputSampleRate_ = frame->sample_rate;
    inputSampleFormat_ = inputFormat;
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
        av_channel_layout_uninit(&inputChannelLayout_);
        inputSampleRate_ = 0; inputSampleFormat_ = -1;
        SetError("FFmpeg could not initialize the WASAPI resampler.");
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

FFFResult PlayerWasapiRenderer::Enqueue(const AVFrame* frame, const std::int64_t position100ns) noexcept {
    if (frame == nullptr || frame->nb_samples <= 0) return FFFResult::InvalidArgument;
    // Enqueue, reset and volume changes are serialized by PlayerSession's worker.
    // Do not take the PCM queue mutex while resampling or applying gain: the
    // WASAPI event thread must be able to take that mutex every device period.
    const auto ensured = EnsureResampler(frame);
    if (ensured != FFFResult::Success) return ensured;
    const auto resetPosition = resetPosition100ns_.load();
    const auto signedStartFrame = position100ns == AV_NOPTS_VALUE
        ? static_cast<std::int64_t>(producedTimelineFrames_)
        : av_rescale_rnd(position100ns - resetPosition, outputSampleRate_, 10'000'000, AV_ROUND_NEAR_INF);
    std::uint64_t preRollFrames = 0;
    std::uint64_t gapFrames = 0;
    std::uint64_t overlapFrames = 0;
    if (!timelineAnchored_) {
        // The first decoded frame establishes the post-seek media anchor. Only
        // this path may trim preroll or synthesize leading silence.
        preRollFrames = static_cast<std::uint64_t>(std::max<std::int64_t>(0, -signedStartFrame));
        gapFrames = static_cast<std::uint64_t>(std::max<std::int64_t>(0, signedStartFrame));
        timelineAnchored_ = true;
    } else if (position100ns != AV_NOPTS_VALUE) {
        const auto delta = signedStartFrame - static_cast<std::int64_t>(producedTimelineFrames_);
        // 100 ms separates packet timestamp rounding and resampler phase from a
        // genuine edit/discontinuity. Small deltas are diagnostic only: decoded
        // samples stay bit-contiguous. A large jump is repaired once at the edit.
        const auto discontinuityThreshold = std::max<std::int64_t>(1, outputSampleRate_ / 10);
        if (std::abs(delta) < discontinuityThreshold) {
            if (delta != 0) ++timestampJitterCount_;
        } else {
            ++discontinuityCount_;
            if (delta > 0) {
                gapFrames = static_cast<std::uint64_t>(delta);
                insertedSilenceFrames_ += gapFrames;
            } else {
                overlapFrames = static_cast<std::uint64_t>(-delta);
                droppedOverlapFrames_ += overlapFrames;
            }
        }
    }
    const auto skipSamples = static_cast<int>(std::min<std::int64_t>(frame->nb_samples,
        av_rescale_rnd(preRollFrames + overlapFrames, frame->sample_rate,
            outputSampleRate_, AV_ROUND_UP)));
    if (skipSamples >= frame->nb_samples) {
        PublishRuntimeDiagnostics();
        return FFFResult::Success;
    }
    const auto inputSamples = frame->nb_samples - skipSamples;
    const auto capacity = swr_get_out_samples(resampler_, inputSamples);
    if (capacity <= 0) return FFFResult::FfmpegFailure;
    converted_.resize(static_cast<std::size_t>(capacity) * outputBlockAlign_);
    std::uint8_t* output[] = { converted_.data() };
    const auto inputFormat = static_cast<AVSampleFormat>(frame->format);
    const auto bytesPerSample = av_get_bytes_per_sample(inputFormat);
    if (bytesPerSample <= 0) return FFFResult::FfmpegFailure;
    const auto planar = av_sample_fmt_is_planar(inputFormat) != 0;
    const auto inputPlanes = planar ? frame->ch_layout.nb_channels : 1;
    // FFmpeg audio layouts have a bounded channel count.  Stack storage avoids a
    // heap allocation for every decoded audio frame.
    if (inputPlanes <= 0 || inputPlanes > 64) return FFFResult::NotSupported;
    std::array<const std::uint8_t*, 64> input{};
    for (int plane = 0; plane < inputPlanes; ++plane) {
        const auto byteOffset = static_cast<std::size_t>(skipSamples) * bytesPerSample *
            (planar ? 1 : frame->ch_layout.nb_channels);
        input[plane] = frame->extended_data[plane] + byteOffset;
    }
    const auto frames = swr_convert(resampler_, output, capacity, input.data(), inputSamples);
    if (frames < 0) { SetError("FFmpeg failed to resample decoded audio."); return FFFResult::FfmpegFailure; }
    converted_.resize(static_cast<std::size_t>(frames) * outputBlockAlign_);
    const auto gain = muted_.load() ? 0.0f : volume_.load();
    if (gain == 0.0f) {
        std::memset(converted_.data(), 0, converted_.size());
    } else if (gain != 1.0f && outputFloat_) {
        auto* samples = reinterpret_cast<float*>(converted_.data());
        for (std::size_t index = 0; index < converted_.size() / sizeof(float); ++index) samples[index] *= gain;
    } else if (gain != 1.0f && outputBitsPerSample_ == 16) {
        auto* samples = reinterpret_cast<std::int16_t*>(converted_.data());
        for (std::size_t index = 0; index < converted_.size() / sizeof(std::int16_t); ++index)
            samples[index] = static_cast<std::int16_t>(std::lround(samples[index] * gain));
    } else if (gain != 1.0f) {
        auto* samples = reinterpret_cast<std::int32_t*>(converted_.data());
        for (std::size_t index = 0; index < converted_.size() / sizeof(std::int32_t); ++index)
            samples[index] = static_cast<std::int32_t>(std::llround(samples[index] * gain));
    }
    const auto gapBytes = static_cast<std::size_t>(gapFrames) * outputBlockAlign_;
    {
        std::lock_guard lock(mutex_);
        // PlayerSession throttles production by media time. Keep the current decoded
        // frame intact even when a codec emits a burst; rejecting after resampling
        // would advance the resampler/timeline and silently create an audible hole.
        AudioChunk chunk;
        chunk.silenceBytes = gapBytes;
        chunk.bytes = converted_;
        queuedBytes_ += gapBytes + chunk.bytes.size();
        queue_.push_back(std::move(chunk));
        producedTimelineFrames_ += gapFrames + static_cast<std::uint64_t>(frames);
        if (frames > 0) hasSubmittedAudio_ = true;
    }
    PublishRuntimeDiagnostics();
    if (controlEvent_ != nullptr) SetEvent(controlEvent_);
    return FFFResult::Success;
}

void PlayerWasapiRenderer::SetPaused(const bool paused) noexcept {
    const auto now = QpcNow100ns();
    {
        std::lock_guard lock(clockMutex_);
        if (clockSampleQpc100ns_ > 0 && now > clockSampleQpc100ns_) {
            const auto elapsed = now - clockSampleQpc100ns_;
            clockPosition100ns_ += std::min(elapsed,
                std::max<std::int64_t>(0, clockLimitPosition100ns_ - clockPosition100ns_));
        }
        // On resume the old QPC sample includes the entire paused duration.
        // Re-anchor it without advancing media; on pause this freezes the most
        // recent interpolated device position until the event thread confirms it.
        clockSampleQpc100ns_ = now;
    }
    paused_ = paused;
    if (paused && runtimeState_ != nullptr) runtimeState_->ClearValues();
    if (controlEvent_ != nullptr) SetEvent(controlEvent_);
}
void PlayerWasapiRenderer::Reset(const std::int64_t position100ns) noexcept {
    {
        std::lock_guard lock(mutex_);
        queue_.clear(); queuedBytes_ = 0; timelineAnchored_ = false; producedTimelineFrames_ = 0;
    }
    // A seek can also follow an audio-stream switch.  Recreating on the next
    // input frame keeps the resampler's source layout/rate contract correct.
    if (resampler_ != nullptr) swr_free(&resampler_);
    av_channel_layout_uninit(&inputChannelLayout_);
    inputSampleRate_ = 0; inputSampleFormat_ = -1;
    pendingMediaFrames_ = 0; resetPosition100ns_ = position100ns;
    playedMediaFrames_ = 0;
    {
        std::lock_guard lock(clockMutex_);
        ++clockEpoch_;
        clockPosition100ns_ = position100ns;
        clockSampleQpc100ns_ = 0;
        clockLimitPosition100ns_ = position100ns;
    }
    resetRequested_ = true;
    underrunCount_ = 0; hasSubmittedAudio_ = false;
    timestampJitterCount_ = 0; discontinuityCount_ = 0;
    insertedSilenceFrames_ = 0; droppedOverlapFrames_ = 0;
    if (runtimeState_ != nullptr) runtimeState_->ResetDiagnostics();
    if (controlEvent_ != nullptr) SetEvent(controlEvent_);
}
void PlayerWasapiRenderer::SetVolume(const float volume, const bool muted) noexcept {
    volume_.store(std::clamp(volume, 0.0f, 1.0f)); muted_.store(muted);
}
std::int64_t PlayerWasapiRenderer::Position100ns() const noexcept {
    std::int64_t position = 0;
    std::int64_t sampleQpc = 0;
    std::int64_t limit = 0;
    {
        std::lock_guard lock(clockMutex_);
        position = clockPosition100ns_;
        sampleQpc = clockSampleQpc100ns_;
        limit = clockLimitPosition100ns_;
    }
    if (!running_.load() || paused_.load() || sampleQpc <= 0) return position;
    const auto now = QpcNow100ns();
    if (now <= sampleQpc) return position;
    return position + std::min(now - sampleQpc, std::max<std::int64_t>(0, limit - position));
}
std::int64_t PlayerWasapiRenderer::TimelineLimit100ns() const noexcept {
    if (outputSampleRate_ == 0) return resetPosition100ns_.load();
    // Called by the sole PCM producer. Unlike Buffered100ns, this endpoint does
    // not depend on the event thread's last padding sample and therefore cannot
    // overrun the decoded timeline during a starvation edge.
    return resetPosition100ns_.load() + static_cast<std::int64_t>(
        producedTimelineFrames_ * 10'000'000 / outputSampleRate_);
}
std::int64_t PlayerWasapiRenderer::Buffered100ns() const noexcept {
    std::lock_guard lock(mutex_);
    if (outputSampleRate_ == 0 || outputBlockAlign_ == 0) return 0;
    const auto frames = queuedBytes_ / outputBlockAlign_ + pendingMediaFrames_.load();
    return static_cast<std::int64_t>(frames) * 10'000'000 / outputSampleRate_;
}
std::uint64_t PlayerWasapiRenderer::UnderrunCount() const noexcept { return underrunCount_.load(); }
std::uint64_t PlayerWasapiRenderer::TimestampJitterCount() const noexcept { return timestampJitterCount_.load(); }
std::uint64_t PlayerWasapiRenderer::DiscontinuityCount() const noexcept { return discontinuityCount_.load(); }
std::uint64_t PlayerWasapiRenderer::InsertedSilenceFrames() const noexcept { return insertedSilenceFrames_.load(); }
std::uint64_t PlayerWasapiRenderer::DroppedOverlapFrames() const noexcept { return droppedOverlapFrames_.load(); }
std::uint32_t PlayerWasapiRenderer::OutputSampleRate() const noexcept { std::lock_guard lock(mutex_); return outputSampleRate_; }
std::uint16_t PlayerWasapiRenderer::OutputChannels() const noexcept { std::lock_guard lock(mutex_); return outputChannels_; }
std::uint16_t PlayerWasapiRenderer::OutputBitsPerSample() const noexcept { std::lock_guard lock(mutex_); return outputBitsPerSample_; }
std::uint16_t PlayerWasapiRenderer::OutputValidBitsPerSample() const noexcept { std::lock_guard lock(mutex_); return outputValidBitsPerSample_; }
bool PlayerWasapiRenderer::OutputIsFloat() const noexcept { std::lock_guard lock(mutex_); return outputFloat_; }
std::string PlayerWasapiRenderer::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
void PlayerWasapiRenderer::SetError(std::string message) noexcept { try { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); } catch (...) {} }

void PlayerWasapiRenderer::UpdatePeakLevels(const std::uint8_t* const samples,
    const std::uint32_t frames) noexcept {
    if (runtimeState_ == nullptr || samples == nullptr || frames == 0 || outputChannels_ == 0) return;
    const auto channels = std::min<std::uint32_t>(outputChannels_, PlayerAudioRuntimeState::MaximumChannels);
    std::array<float, PlayerAudioRuntimeState::MaximumChannels> peaks{};
    const auto sampleCount = static_cast<std::size_t>(frames) * outputChannels_;
    if (outputFloat_) {
        const auto* values = reinterpret_cast<const float*>(samples);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const auto value = std::isfinite(values[index]) ? std::abs(values[index]) : 0.0f;
            const auto channel = static_cast<std::uint32_t>(index % outputChannels_);
            if (channel < channels) peaks[channel] = std::max(peaks[channel], value);
        }
    } else if (outputBitsPerSample_ == 16) {
        const auto* values = reinterpret_cast<const std::int16_t*>(samples);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const auto magnitude = static_cast<float>(std::abs(static_cast<std::int32_t>(values[index]))) / 32768.0f;
            const auto channel = static_cast<std::uint32_t>(index % outputChannels_);
            if (channel < channels) peaks[channel] = std::max(peaks[channel], magnitude);
        }
    } else {
        const auto* values = reinterpret_cast<const std::int32_t*>(samples);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            const auto magnitude = static_cast<float>(std::abs(static_cast<std::int64_t>(values[index]))) / 2147483648.0f;
            const auto channel = static_cast<std::uint32_t>(index % outputChannels_);
            if (channel < channels) peaks[channel] = std::max(peaks[channel], magnitude);
        }
    }
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        runtimeState_->values[channel].store(std::clamp(peaks[channel], 0.0f, 1.0f), std::memory_order_relaxed);
}

void PlayerWasapiRenderer::PublishRuntimeDiagnostics() noexcept {
    if (runtimeState_ == nullptr) return;
    runtimeState_->buffered100ns.store(Buffered100ns(), std::memory_order_relaxed);
    runtimeState_->underruns.store(underrunCount_.load(), std::memory_order_relaxed);
    runtimeState_->timestampJitterFrames.store(timestampJitterCount_.load(), std::memory_order_relaxed);
    runtimeState_->discontinuities.store(discontinuityCount_.load(), std::memory_order_relaxed);
    runtimeState_->insertedSilenceFrames.store(insertedSilenceFrames_.load(), std::memory_order_relaxed);
    runtimeState_->droppedOverlapFrames.store(droppedOverlapFrames_.load(), std::memory_order_relaxed);
}

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
    auto fail = [&](std::string message) {
        SetError(std::move(message));
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
    // Shared mode lets the engine perform conversion and resampling.  Exclusive
    // mode deliberately bypasses that mixer: the device must accept the exact
    // endpoint format and the client owns the complete hardware period.
    WAVEFORMATEX* activeFormat = format;
    WAVEFORMATEXTENSIBLE exclusiveCandidate{};
    // Legacy shared-mode IAudioClient still lets us request a buffer duration.
    // Three engine periods absorb normal scheduling jitter without making the
    // device queue dominate the end-to-end playback latency.
    REFERENCE_TIME bufferDuration = 500'000;
    REFERENCE_TIME periodicity = 0;
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_SHARED;
    REFERENCE_TIME sharedDefaultPeriod = 0;
    REFERENCE_TIME sharedMinimumPeriod = 0;
    if (SUCCEEDED(client->GetDevicePeriod(&sharedDefaultPeriod, &sharedMinimumPeriod)) &&
        sharedDefaultPeriod > 0) {
        bufferDuration = std::clamp(sharedDefaultPeriod * 3,
            static_cast<REFERENCE_TIME>(300'000), static_cast<REFERENCE_TIME>(500'000));
    }
    if (exclusive_) {
        const bool nativeFormatSupported = SUCCEEDED(client->IsFormatSupported(
            AUDCLNT_SHAREMODE_EXCLUSIVE, activeFormat, nullptr)) &&
            (activeFormat->wBitsPerSample == 16 || activeFormat->wBitsPerSample == 32);
        if (!nativeFormatSupported) {
            const auto tryFormat = [&](const std::uint32_t sampleRate, const std::uint16_t containerBits,
                const std::uint16_t validBits, const bool floatingPoint) noexcept {
                exclusiveCandidate = {};
                exclusiveCandidate.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                exclusiveCandidate.Format.nChannels = format->nChannels;
                exclusiveCandidate.Format.nSamplesPerSec = sampleRate;
                exclusiveCandidate.Format.wBitsPerSample = containerBits;
                exclusiveCandidate.Format.nBlockAlign = static_cast<WORD>(exclusiveCandidate.Format.nChannels * containerBits / 8);
                exclusiveCandidate.Format.nAvgBytesPerSec = sampleRate * exclusiveCandidate.Format.nBlockAlign;
                exclusiveCandidate.Format.cbSize = 22;
                exclusiveCandidate.Samples.wValidBitsPerSample = validBits;
                exclusiveCandidate.dwChannelMask = ChannelMask(format);
                if (exclusiveCandidate.dwChannelMask == 0) {
                    if (format->nChannels == 1) exclusiveCandidate.dwChannelMask = KSAUDIO_SPEAKER_MONO;
                    else if (format->nChannels == 2) exclusiveCandidate.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
                    else if (format->nChannels == 6) exclusiveCandidate.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
                    else if (format->nChannels == 8) exclusiveCandidate.dwChannelMask = KSAUDIO_SPEAKER_7POINT1;
                }
                exclusiveCandidate.SubFormat = floatingPoint ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
                return SUCCEEDED(client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
                    &exclusiveCandidate.Format, nullptr));
            };
            const std::uint32_t sampleRates[] = {format->nSamplesPerSec, 192'000, 176'400,
                96'000, 88'200, 48'000, 44'100};
            bool supported = false;
            for (const auto sampleRate : sampleRates) {
                if (sampleRate == 0) continue;
                if (tryFormat(sampleRate, 32, 32, true) || tryFormat(sampleRate, 32, 32, false) ||
                    tryFormat(sampleRate, 32, 24, false) || tryFormat(sampleRate, 16, 16, false)) {
                    activeFormat = &exclusiveCandidate.Format;
                    supported = true;
                    break;
                }
            }
            if (!supported) {
                fail("The selected endpoint does not accept an exclusive-mode PCM format.");
                CoTaskMemFree(format); if (uninitialize) CoUninitialize(); return;
            }
        }
        REFERENCE_TIME defaultPeriod = 0;
        REFERENCE_TIME minimumPeriod = 0;
        if (FAILED(client->GetDevicePeriod(&defaultPeriod, &minimumPeriod))) {
            fail("Could not query the selected endpoint's exclusive-mode period.");
            CoTaskMemFree(format); if (uninitialize) CoUninitialize(); return;
        }
        bufferDuration = defaultPeriod > 0 ? defaultPeriod : minimumPeriod;
        periodicity = bufferDuration;
        streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        shareMode = AUDCLNT_SHAREMODE_EXCLUSIVE;
    }
    HRESULT initializeResult = client->Initialize(shareMode, streamFlags, bufferDuration, periodicity, activeFormat, nullptr);
    // Exclusive event-driven clients must use a whole number of device frames.
    // Many endpoints report a period that rounds differently at 44.1/96 kHz and
    // return AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED on the first Initialize call.
    if (exclusive_ && initializeResult == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        UINT32 alignedFrames = 0;
        const auto sampleRate = activeFormat->nSamplesPerSec;
        if (sampleRate > 0 && SUCCEEDED(client->GetBufferSize(&alignedFrames)) && alignedFrames > 0) {
            const auto alignedDuration = static_cast<REFERENCE_TIME>(
                (10'000'000.0L * alignedFrames / sampleRate) + 0.5L);
            client.Reset();
            if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)))
                initializeResult = client->Initialize(shareMode, streamFlags, alignedDuration,
                    alignedDuration, activeFormat, nullptr);
        }
    }
    auto wasapiResult = initializeResult;
    if (SUCCEEDED(wasapiResult)) wasapiResult = client->SetEventHandle(sampleEvent_);
    if (SUCCEEDED(wasapiResult)) wasapiResult = client->GetBufferSize(&bufferFrames);
    if (SUCCEEDED(wasapiResult)) wasapiResult = client->GetService(IID_PPV_ARGS(&renderer));
    if (SUCCEEDED(wasapiResult)) wasapiResult = client->GetService(IID_PPV_ARGS(&clock));
    if (FAILED(wasapiResult)) {
        std::ostringstream message;
        message << (exclusive_ ? "Could not initialize exclusive WASAPI playback for the selected endpoint"
            : "Could not initialize event-driven WASAPI playback")
            << " (HRESULT 0x" << std::hex << static_cast<std::uint32_t>(wasapiResult) << ").";
        fail(message.str()); CoTaskMemFree(format);
        if (uninitialize) CoUninitialize(); return;
    }
    {
        std::lock_guard lock(mutex_);
        outputSampleRate_ = activeFormat->nSamplesPerSec; outputChannels_ = activeFormat->nChannels;
        outputBlockAlign_ = activeFormat->nBlockAlign; outputBitsPerSample_ = activeFormat->wBitsPerSample;
        outputValidBitsPerSample_ = activeFormat->wBitsPerSample;
        if (activeFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && activeFormat->cbSize >= 22)
            outputValidBitsPerSample_ = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(activeFormat)->Samples.wValidBitsPerSample;
        outputChannelMask_ = ChannelMask(activeFormat); outputFloat_ = IsFloatWaveFormat(activeFormat);
        initializationResult_ = FFFResult::Success; initializationFinished_ = true;
    }
    if (runtimeState_ != nullptr) runtimeState_->SetChannels(activeFormat->nChannels);
    initializedCondition_.notify_all();
    UINT64 clockFrequency = 0;
    if (FAILED(clock->GetFrequency(&clockFrequency)) || clockFrequency == 0) {
        fail("Could not query the WASAPI device clock frequency."); CoTaskMemFree(format);
        if (uninitialize) CoUninitialize(); return;
    }
    CoTaskMemFree(format); format = nullptr;
    bool clientStarted = false;
    bool exclusiveBufferPrimed = false;
    bool starved = false;
    bool clockAnchorValid = false;
    UINT64 clockAnchorDevicePosition = 0;
    std::uint64_t deviceSubmittedFrames = 0;
    std::uint64_t activeClockEpoch = 0;
    {
        std::lock_guard lock(clockMutex_);
        activeClockEpoch = clockEpoch_;
    }
    auto updateMediaClock = [&]() noexcept {
        if (!clockAnchorValid || outputSampleRate_ == 0) return;
        UINT64 devicePosition = 0;
        UINT64 qpcPosition100ns = 0;
        if (FAILED(clock->GetPosition(&devicePosition, &qpcPosition100ns))) return;
        const auto elapsedUnits = devicePosition > clockAnchorDevicePosition
            ? devicePosition - clockAnchorDevicePosition : 0;
        const auto elapsedFrames = static_cast<std::uint64_t>(
            static_cast<long double>(elapsedUnits) * outputSampleRate_ / clockFrequency);
        const auto playedFrames = std::min(deviceSubmittedFrames, elapsedFrames);
        std::lock_guard lock(clockMutex_);
        if (activeClockEpoch != clockEpoch_) return;
        playedMediaFrames_ = playedFrames;
        pendingMediaFrames_ = deviceSubmittedFrames - playedFrames;
        const auto resetPosition = resetPosition100ns_.load();
        clockPosition100ns_ = resetPosition +
            static_cast<std::int64_t>(playedFrames * 10'000'000 / outputSampleRate_);
        clockLimitPosition100ns_ = resetPosition +
            static_cast<std::int64_t>(deviceSubmittedFrames * 10'000'000 / outputSampleRate_);
        clockSampleQpc100ns_ = qpcPosition100ns != 0
            ? static_cast<std::int64_t>(qpcPosition100ns) : QpcNow100ns();
    };
    HANDLE events[] = { stopEvent_, sampleEvent_, controlEvent_ };
    for (;;) {
        const auto waitResult = WaitForMultipleObjects(3, events, FALSE, 100);
        if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_FAILED) break;
        const auto deviceEvent = waitResult == WAIT_OBJECT_0 + 1;
        // In exclusive event mode the callback means that the complete endpoint
        // buffer has become writable. Producer/control wakeups must never be
        // mistaken for that notification.
        if (exclusive_ && deviceEvent) exclusiveBufferPrimed = false;
        if (resetRequested_.exchange(false)) {
            if (clientStarted) client->Stop();
            client->Reset(); clientStarted = false;
            exclusiveBufferPrimed = false;
            clockAnchorValid = false; clockAnchorDevicePosition = 0; deviceSubmittedFrames = 0;
            pendingMediaFrames_ = 0; playedMediaFrames_ = 0;
            {
                std::lock_guard lock(clockMutex_);
                activeClockEpoch = clockEpoch_;
            }
            starved = false;
        }
        const auto shouldPause = paused_.load();
        if (shouldPause && clientStarted) {
            client->Stop();
            clientStarted = false;
        }
        updateMediaClock();
        PublishRuntimeDiagnostics();
        if (exclusive_) {
            if (shouldPause) continue;
            if (!clientStarted && exclusiveBufferPrimed) {
                if (SUCCEEDED(client->Start())) clientStarted = true;
                continue;
            }
            // Before Start, a control wakeup may prefill the empty endpoint.
            // Once running, only a real WASAPI callback grants another buffer.
            if (clientStarted && !deviceEvent) continue;
        }
        UINT32 padding = 0;
        // An exclusive event means one complete endpoint buffer is available.
        // GetCurrentPadding describes the buffer still owned by the endpoint and
        // must not be used as the shared-mode writable-frame gate here.
        if (!exclusive_ && FAILED(client->GetCurrentPadding(&padding))) continue;
        if (!exclusive_ && padding >= bufferFrames) {
            if (!shouldPause && !clientStarted && SUCCEEDED(client->Start())) clientStarted = true;
            continue;
        }
        const auto wantedFrames = exclusive_ ? bufferFrames : bufferFrames - padding;
        std::size_t copied = 0;
        UINT32 renderedFrames = 0;
        UINT32 mediaFrames = 0;
        BYTE* destination = nullptr;
        {
            // Keep reset and consumption atomic with respect to the PCM bytes;
            // otherwise Reset could clear the queue between GetBuffer and copy.
            std::lock_guard lock(mutex_);
            copied = std::min<std::size_t>(static_cast<std::size_t>(wantedFrames) * outputBlockAlign_,
                queuedBytes_);
            renderedFrames = static_cast<UINT32>(copied / outputBlockAlign_);
            if (renderedFrames == 0 && (!exclusive_ || !clientStarted || !deviceEvent)) {
                if (!shouldPause && clientStarted && hasSubmittedAudio_.load() && padding == 0 && !starved) {
                    ++underrunCount_;
                    starved = true;
                }
                if (padding == 0 && runtimeState_ != nullptr) runtimeState_->ClearValues();
                if (runtimeState_ != nullptr)
                    runtimeState_->underruns.store(underrunCount_.load(), std::memory_order_relaxed);
                continue;
            }
            if (renderedFrames == 0) {
                if (hasSubmittedAudio_.load() && !starved) ++underrunCount_;
                starved = true;
            } else {
                starved = false;
            }
            copied = static_cast<std::size_t>(renderedFrames) * outputBlockAlign_;
            mediaFrames = renderedFrames;
            // Exclusive event-driven render clients accept exactly one complete
            // endpoint buffer per callback. Preserve the real queued-byte count,
            // then silence-fill the unused tail when the final packet is short.
            if (exclusive_) renderedFrames = wantedFrames;
            if (FAILED(renderer->GetBuffer(renderedFrames, &destination))) continue;
            auto remaining = copied;
            auto* write = destination;
            while (remaining > 0 && !queue_.empty()) {
                auto& chunk = queue_.front();
                const auto silence = std::min(remaining, chunk.silenceBytes);
                if (silence > 0) {
                    std::memset(write, 0, silence);
                    chunk.silenceBytes -= silence; write += silence; remaining -= silence;
                }
                if (remaining > 0 && chunk.offset < chunk.bytes.size()) {
                    const auto bytes = std::min(remaining, chunk.bytes.size() - chunk.offset);
                    std::memcpy(write, chunk.bytes.data() + chunk.offset, bytes);
                    chunk.offset += bytes; write += bytes; remaining -= bytes;
                }
                if (chunk.silenceBytes == 0 && chunk.offset == chunk.bytes.size()) queue_.pop_front();
            }
            const auto submittedBytes = static_cast<std::size_t>(renderedFrames) * outputBlockAlign_;
            if (submittedBytes > copied) std::memset(destination + copied, 0, submittedBytes - copied);
            queuedBytes_ -= copied;
        }
        UpdatePeakLevels(destination, renderedFrames);
        UINT64 anchorCandidate = 0;
        bool hasAnchorCandidate = false;
        if (!clockAnchorValid) {
            UINT64 devicePosition = 0;
            if (SUCCEEDED(clock->GetPosition(&devicePosition, nullptr))) {
                anchorCandidate = devicePosition + static_cast<UINT64>(
                    static_cast<long double>(padding) * clockFrequency / outputSampleRate_);
                hasAnchorCandidate = true;
            }
        }
        if (SUCCEEDED(renderer->ReleaseBuffer(renderedFrames, 0))) {
            if (exclusive_) exclusiveBufferPrimed = true;
            std::lock_guard lock(clockMutex_);
            if (activeClockEpoch == clockEpoch_) {
                if (!clockAnchorValid && hasAnchorCandidate) {
                    clockAnchorDevicePosition = anchorCandidate;
                    clockAnchorValid = true;
                }
                deviceSubmittedFrames += mediaFrames;
                pendingMediaFrames_ = deviceSubmittedFrames - playedMediaFrames_.load();
                clockLimitPosition100ns_ = resetPosition100ns_.load() +
                    static_cast<std::int64_t>(deviceSubmittedFrames * 10'000'000 / outputSampleRate_);
            }
            if (!shouldPause && !clientStarted && SUCCEEDED(client->Start())) clientStarted = true;
        }
        PublishRuntimeDiagnostics();
    }
    if (clientStarted) client->Stop();
    client->Reset();
    renderer.Reset(); clock.Reset(); client.Reset(); device.Reset(); enumerator.Reset();
    running_ = false;
    if (uninitialize) CoUninitialize();
}
