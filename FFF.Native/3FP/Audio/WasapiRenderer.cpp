#include "pch.h"
#include "3FP/Audio/WasapiRenderer.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <avrt.h>
#include <array>
#include <cmath>
#include <cstring>
#include <type_traits>

using Microsoft::WRL::ComPtr;

namespace {
constexpr std::size_t MaximumReusableAudioBuffers = 8;
constexpr std::size_t MaximumReusableAudioBufferBytes = 1024 * 1024;
constexpr std::uint32_t StartupFadeInMilliseconds = 5;

// Downmix quality layer for the WASAPI output resampler. Every multichannel
// stream (IAMF renders 12ch) reaches the device through this rematrix, so the
// options belong to the common path, not to IAMF alone.
//
// Names, defaults and semantics were verified against the linked
// libswresample 7.2.100 (swresample-7.dll): av_opt_get_double on a freshly
// allocated SwrContext reports exactly the numbers below, and each key is a
// registered AVOption ("set rematrix maxval" & co. in the option table).
//
//   rematrix_maxval = 0.95 -- the fix. Default is 0.0, which leaves the
//     rematrix coefficients unbounded: measured 7.1.4 -> stereo with every
//     channel at -1 dBFS, the fold-down peaked at 2.11 (hard clipping).
//     0.95 normalizes the mixing matrix to a 0.95 ceiling, keeping 5% of
//     head-room for the intersample peaks that survive any later stage.
//     (1.0 normalizes to exactly full scale; 0.95 trades one loudness
//     percent for the only insurance this stage has against clipping.)
//   center_mix_level / surround_mix_level = 0.707 (-3 dB) -- pinned to the
//     verified defaults rather than left implicit, so a future libswresample
//     default change cannot silently retune the fold-down.
//   lfe_mix_level = 0.0 -- the verified default, written explicitly to pin
//     it: the LFE does not belong in the stereo main mix and would only
//     make the fold-down sound muddy/bassy (and on a stereo device it is
//     energy the limiter later has to throw away anyway).
//   rematrix_volume deliberately untouched (stays 1.0): the renderer's own
//     volume/gain stage owns absolute level, and scaling here would fight
//     the user's volume control. Head-room is maxval's job.
constexpr double RematrixMaxval = 0.95;
constexpr double CenterMixLevel = 0.707107;    // == M_SQRT1_2, the verified default
constexpr double SurroundMixLevel = 0.707107;  // == M_SQRT1_2, the verified default
constexpr double LfeMixLevel = 0.0;            // == the verified default, now pinned

// ---- the external bed render plug seam -------------------------------------
//
// What engages here and what does not (user decision, 2026-10-18):
//   * a plug is installed, AND
//   * the endpoint is a stereo (2-channel) endpoint, AND
//   * the frame carries a bed the ABI can describe: nb_channels == 1 or
//     3 <= nb_channels <= FFF3FP_AUDIO_PLUG_MAX_CHANNELS.
// A 2.0 frame is never handed over: the device target is exactly two channels, so
// the route would have to reproduce swr's passthrough byte-for-byte and prove it --
// all cost, no information. Mono IS in scope (the contract header's comment still
// says ">2ch"; that text is pending the main loop's sync). A single channel is not
// a stereo bed, it is a point source at one real azimuth -- the fold-down the body
// runs is av_get_default_channel_layout(1) -> {FC} -> front-left with unity, i.e.
// the same signal twice with no spatial cue. The plug places it once and rotates
// both ears around it, which is information the fold-down cannot produce.
//
// Decline accounting (2026-10-18, second round): `framesDeclined` counts exactly one
// thing -- the plug's answer of "no" to a bed, i.e. the bed-level decline from
// supports() (or from a bed wider than the ABI can describe, which the plug is never
// asked about). A per-frame render() bail counts nowhere, not even here: the
// contract only says "fall back for this frame", so 500 frames with a 3-frame bail
// and 503 clean frames are indistinguishable in the kernel's own numbers, and there
// is no field anywhere to hold the difference. The visible form of that distinction
// is the gap between framesRendered and the decoded frame count.
//
// audioBinaural reads 1 for a mono bed the plug rendered, exactly as for a 5.1 one.
//
// The seam never blocks and never takes a lock around plug->render(): it runs on
// the session worker next to the endpoint's event thread, and the queue mutex is
// what that thread needs every device period.

// Owns one channel layout so every early return still releases it.
struct ScopedChannelLayout final {
    AVChannelLayout layout{};
    ScopedChannelLayout() = default;
    ScopedChannelLayout(const ScopedChannelLayout&) = delete;
    ScopedChannelLayout& operator=(const ScopedChannelLayout&) = delete;
    ~ScopedChannelLayout() { av_channel_layout_uninit(&layout); }
    bool Copy(const AVChannelLayout* source) noexcept {
        return av_channel_layout_copy(&layout, source) >= 0;
    }
    void Default(const int channels) noexcept { av_channel_layout_default(&layout, channels); }
};

// Folds the bed's resolved position map into one comparable value. Built from
// av_channel_layout_channel_from_index, which answers for NATIVE, CUSTOM and
// AMBISONIC orders alike (AV_CHAN_NONE for UNSPEC, which is what a bare channel
// count decodes to) -- reading layout.u.mask directly would be wrong for CUSTOM,
// where the union holds the map instead.
std::uint64_t BedPositionsFingerprint(const AVChannelLayout& layout) noexcept {
    std::uint64_t value = 1469598103934665603ULL;  // FNV-1a offset basis
    const auto count = std::max(0, layout.nb_channels);
    value = (value ^ static_cast<std::uint64_t>(layout.order)) * 1099511628211ULL;
    for (int index = 0; index < count; ++index) {
        const auto channel = av_channel_layout_channel_from_index(&layout,
            static_cast<unsigned int>(index));
        value = (value ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(channel))) *
            1099511628211ULL;
    }
    return value;
}

class ComInitialization final {
public:
    ComInitialization() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInitialization() { if (SUCCEEDED(result_)) CoUninitialize(); }

    HRESULT Result() const noexcept { return result_; }

private:
    HRESULT result_;
};

struct CoTaskMemDeleter final {
    void operator()(void* value) const noexcept { CoTaskMemFree(value); }
};

bool IsAudioDeviceLost(const HRESULT result) noexcept {
    return result == AUDCLNT_E_DEVICE_INVALIDATED ||
        result == AUDCLNT_E_RESOURCES_INVALIDATED ||
        result == AUDCLNT_E_SERVICE_NOT_RUNNING ||
        result == AUDCLNT_E_ENDPOINT_CREATE_FAILED;
}

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
    PlayerAudioRuntimeState* const runtimeState, std::function<void()> restartCallback)
    : endpointId_(std::move(endpointId)), exclusive_(exclusive), runtimeState_(runtimeState),
      restartCallback_(std::move(restartCallback)),
      stopEvent_(nullptr), sampleEvent_(nullptr), controlEvent_(nullptr),
      initializationFinished_(false), initializationResult_(FFFResult::InvalidState), queuedBytes_(0),
      resampler_(nullptr),
      inputChannelLayout_{}, inputSampleRate_(0), inputSampleFormat_(-1),
      outputSampleRate_(0), outputChannels_(0), outputBlockAlign_(0), outputBitsPerSample_(0),
      outputValidBitsPerSample_(0),
      outputFloat_(false), volume_(1.0f), muted_(false), running_(false), clockRunning_(false),
      paused_(true), resetRequested_(false), restartRequested_(false), resetPosition100ns_(0), clockPosition100ns_(0),
      clockSampleQpc100ns_(0), clockLimitPosition100ns_(0), clockEpoch_(0),
      pendingMediaFrames_(0), playedMediaFrames_(0), underrunCount_(0),
      hasSubmittedAudio_(false), endOfStream_(false), timelineAnchored_(false), producedTimelineFrames_(0),
      fadeInFramesRemaining_(0),
      timestampJitterCount_(0), discontinuityCount_(0), insertedSilenceFrames_(0),
      droppedOverlapFrames_(0) {}

PlayerWasapiRenderer::~PlayerWasapiRenderer() { Stop(); }

FFFResult PlayerWasapiRenderer::Start() noexcept {
    if (running_.exchange(true)) return FFFResult::Success;
    restartRequested_ = false;
    {
        std::lock_guard lock(mutex_);
        initializationFinished_ = false;
        initializationResult_ = FFFResult::InvalidState;
    }
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    sampleEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    controlEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stopEvent_ == nullptr || sampleEvent_ == nullptr || controlEvent_ == nullptr) {
        running_ = false;
        CloseEvents();
        SetError("Could not create WASAPI playback events.");
        return FFFResult::DeviceFailure;
    }
    try { thread_ = std::thread(&PlayerWasapiRenderer::RenderThread, this); }
    catch (...) {
        running_ = false;
        CloseEvents();
        SetError("Could not start the WASAPI playback thread.");
        return FFFResult::NativeFailure;
    }
    std::unique_lock lock(mutex_);
    initializedCondition_.wait(lock, [this] { return initializationFinished_; });
    const auto result = initializationResult_;
    lock.unlock();
    if (result != FFFResult::Success) Stop();
    return result;
}

void PlayerWasapiRenderer::Stop() noexcept {
    const auto hadResources = thread_.joinable() || stopEvent_ != nullptr ||
        sampleEvent_ != nullptr || controlEvent_ != nullptr || resampler_ != nullptr;
    if (stopEvent_ != nullptr) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    running_ = false;
    clockRunning_ = false;
    CloseEvents();
    if (resampler_ != nullptr) { swr_free(&resampler_); }
    av_channel_layout_uninit(&inputChannelLayout_);
    inputSampleRate_ = 0; inputSampleFormat_ = -1;
    FreePlugResamplers();
    bedRenderedByPlug_ = false;
    outputSampleRate_ = 0; outputChannels_ = 0; outputBlockAlign_ = 0;
    outputBitsPerSample_ = outputValidBitsPerSample_ = 0;
    if (hadResources && runtimeState_ != nullptr) {
        runtimeState_->ClearAll();
        // A renderer instance's seam numbers end with it; the replacement starts at
        // zero on its first frame rather than inheriting a dead instance's totals.
        runtimeState_->plugBedActive.store(0, std::memory_order_relaxed);
        runtimeState_->plugFramesRendered.store(0, std::memory_order_relaxed);
        runtimeState_->plugFramesDeclined.store(0, std::memory_order_relaxed);
    }
    std::lock_guard lock(mutex_);
    queue_.clear(); reusableBuffers_.clear(); queuedBytes_ = 0; pendingMediaFrames_ = 0;
}

void PlayerWasapiRenderer::CloseEvents() noexcept {
    if (controlEvent_ != nullptr) { CloseHandle(controlEvent_); controlEvent_ = nullptr; }
    if (sampleEvent_ != nullptr) { CloseHandle(sampleEvent_); sampleEvent_ = nullptr; }
    if (stopEvent_ != nullptr) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
}

FFFResult PlayerWasapiRenderer::EnsureResampler(const AVFrame* frame, const BedRoute route) noexcept {
    // In the plug route swr's input is the plug's own stereo pair, so the cache key
    // below describes what is actually handed to swr_convert. Native and Declined
    // key it on the decoded frame exactly as before this seam existed, which is what
    // keeps those paths byte-for-byte unchanged.
    const bool plugFed = route == BedRoute::Plug;
    const auto inputFormat = plugFed ? static_cast<std::int32_t>(AV_SAMPLE_FMT_FLTP)
        : static_cast<std::int32_t>(frame->format);
    ScopedChannelLayout effective;
    if (plugFed) effective.Default(2);
    else if (!effective.Copy(&frame->ch_layout)) return FFFResult::FfmpegFailure;
    if (resampler_ != nullptr && inputSampleRate_ == frame->sample_rate &&
        inputSampleFormat_ == inputFormat &&
        av_channel_layout_compare(&inputChannelLayout_, &effective.layout) == 0)
        return FFFResult::Success;
    if (outputSampleRate_ == 0 || outputChannels_ == 0) return FFFResult::InvalidState;
    // Track the complete FFmpeg layout contract, not merely channel count. A
    // stream can switch between layouts with the same number of channels.
    if (resampler_ != nullptr) swr_free(&resampler_);
    av_channel_layout_uninit(&inputChannelLayout_);
    if (av_channel_layout_copy(&inputChannelLayout_, &effective.layout) < 0)
        return FFFResult::FfmpegFailure;
    inputSampleRate_ = frame->sample_rate;
    inputSampleFormat_ = inputFormat;
    AVChannelLayout outputLayout{};
    av_channel_layout_default(&outputLayout, outputChannels_);
    const auto outputFormat = outputFloat_ ? AV_SAMPLE_FMT_FLT :
        (outputBitsPerSample_ == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32);
    const auto result = swr_alloc_set_opts2(&resampler_, &outputLayout, outputFormat,
        outputSampleRate_, &effective.layout,
        static_cast<AVSampleFormat>(inputFormat), frame->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outputLayout);
    if (result < 0 || resampler_ == nullptr) {
        if (resampler_ != nullptr) swr_free(&resampler_);
        av_channel_layout_uninit(&inputChannelLayout_);
        inputSampleRate_ = 0; inputSampleFormat_ = -1;
        SetError("FFmpeg could not initialize the WASAPI resampler.");
        return FFFResult::FfmpegFailure;
    }
    // Rematrix options take effect only before swr_init. They are best-effort:
    // a rejected key (older libswresample without the option, a future rename)
    // must not take playback down with the resampler, so the failure is
    // recorded once and the default matrix -- the behaviour before this layer
    // existed -- keeps working. Fault tolerance beats strictly-correct here.
    unsigned int rejectedOptions = 0;
    const auto setRematrixOption = [&](const char* name, const double value) {
        if (av_opt_set_double(resampler_, name, value, 0) < 0) ++rejectedOptions;
    };
    setRematrixOption("rematrix_maxval", RematrixMaxval);
    setRematrixOption("center_mix_level", CenterMixLevel);
    setRematrixOption("surround_mix_level", SurroundMixLevel);
    setRematrixOption("lfe_mix_level", LfeMixLevel);
    if (rejectedOptions != 0)
        SetError("WASAPI downmix options were partially rejected; using FFmpeg defaults.");
    if (swr_init(resampler_) < 0) {
        if (resampler_ != nullptr) swr_free(&resampler_);
        av_channel_layout_uninit(&inputChannelLayout_);
        inputSampleRate_ = 0; inputSampleFormat_ = -1;
        SetError("FFmpeg could not initialize the WASAPI resampler.");
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

void PlayerWasapiRenderer::SetAudioRenderPlug(const FFF3FPAudioRenderPlug* const plug) noexcept {
    // Runs on the session worker -- the same thread that calls Enqueue and Reset --
    // so this swap cannot interrupt a render() call. Frames already inside the PCM
    // queue keep whatever produced them; the route changes at the next frame
    // boundary, which is the switch the contract asks for.
    plug_ = {};
    plugInstalled_.store(plug != nullptr, std::memory_order_release);
    if (plug != nullptr) {
        plug_ = *plug;
        // A fresh owner of the bed deserves a fresh pipeline: reset() is the
        // contract's "drop any internal phase/pipeline state" entry, so calling it
        // on install is allowed and is also what a replacement's successor needs.
        if (plug_.reset != nullptr && plug_.context != nullptr) plug_.reset(plug_.context);
    }
    plugBedIdentity_ = {};
    plugBedAccepted_ = false;
    plugFramesRendered_ = 0;
    plugFramesDeclined_ = 0;
    bedRenderedByPlug_ = false;
    // The plug-fed and fold slots exist only for a seam that can engage. resampler_
    // keeps its cached descriptor on purpose: EnsureResampler sees the mismatch on
    // the next frame and rebuilds it from the decoded bed, the same rebuild a media
    // switch already performs. No endpoint rebuild, no discontinuity count -- a bed
    // owner swap at a frame boundary is a route change, not a timeline break.
    FreePlugResamplers();
    ReleaseBedSeamBuffers();
    PublishRuntimeDiagnostics();
}

PlayerWasapiRenderer::BedRoute PlayerWasapiRenderer::SelectBedRoute(const AVFrame* const frame) noexcept {
    if (!plugInstalled_.load(std::memory_order_acquire)) return BedRoute::Native;
    // The seam renders a stereo pair, so it owns the bed only against a 2-channel
    // endpoint. A multichannel device keeps today's route, unchanged.
    if (outputChannels_ != 2) return BedRoute::Native;
    const auto channels = frame->ch_layout.nb_channels;
    if (channels <= 0) return BedRoute::Native;
    // 2.0 already is the device target: the route could only reproduce swr's
    // passthrough, so it is not a bed and it is never a decline.
    if (channels == 2) return BedRoute::Native;
    // Wider than the ABI can describe: the bed struct carries 16 positions, so the
    // plug is never asked. Reported as a decline -- the seam had this bed and did
    // not render it -- rather than hidden, which is what made the old field useless.
    if (channels > static_cast<int>(FFF3FP_AUDIO_PLUG_MAX_CHANNELS)) return BedRoute::Declined;
    const BedIdentity identity{frame->sample_rate, channels,
        static_cast<std::int32_t>(frame->format), BedPositionsFingerprint(frame->ch_layout)};
    if (!(identity == plugBedIdentity_)) {
        plugBedIdentity_ = identity;
        plugBedAccepted_ = false;
        if (plug_.supports != nullptr && plug_.context != nullptr) {
            FFF3FPAudioBedDescription bed{};
            bed.size = sizeof(bed);
            bed.version = FFF3FP_AUDIO_PLUG_VERSION;
            bed.sampleRate = static_cast<std::uint32_t>(std::max<std::int32_t>(0, frame->sample_rate));
            bed.channelCount = static_cast<std::uint32_t>(channels);
            bed.lfeIndex = 0xFFFFFFFFu;
            for (int index = 0; index < channels; ++index) {
                const auto channel = av_channel_layout_channel_from_index(&frame->ch_layout,
                    static_cast<unsigned int>(index));
                // AVChannel values as plain ints; AV_CHAN_NONE (-1) means "no such
                // slot" and the module maps them into its own layout tables.
                bed.positions[index] = static_cast<std::int32_t>(channel);
                if (channel == AV_CHAN_LOW_FREQUENCY) bed.lfeIndex = static_cast<std::uint32_t>(index);
            }
            // "no" is a normal answer: it costs nothing but the decline counter.
            plugBedAccepted_ = plug_.supports(plug_.context, &bed) != 0;
            if (plug_.reset != nullptr && plug_.context != nullptr) plug_.reset(plug_.context);
        }
    }
    return plugBedAccepted_ ? BedRoute::Plug : BedRoute::Declined;
}

FFFResult PlayerWasapiRenderer::EnsureBedConverter(const AVFrame* const frame) noexcept {
    const auto inputFormat = static_cast<std::int32_t>(frame->format);
    if (inputFormat == static_cast<std::int32_t>(AV_SAMPLE_FMT_FLTP)) {
        // Already the plug's input contract: release the slot so the caller reads
        // the decoded planes directly and nothing is copied twice.
        if (bedConverter_ != nullptr) {
            swr_free(&bedConverter_);
            av_channel_layout_uninit(&bedConverterLayout_);
            bedConverterRate_ = 0; bedConverterFormat_ = -1;
        }
        return FFFResult::Success;
    }
    if (bedConverter_ != nullptr && bedConverterRate_ == frame->sample_rate &&
        bedConverterFormat_ == inputFormat &&
        av_channel_layout_compare(&bedConverterLayout_, &frame->ch_layout) == 0)
        return FFFResult::Success;
    if (bedConverter_ != nullptr) swr_free(&bedConverter_);
    av_channel_layout_uninit(&bedConverterLayout_);
    // Layout and rate are passed through untouched -- this slot only changes the
    // sample format, which is the kernel's half of the "planar float32" contract.
    // A bare channel count (AV_CHANNEL_ORDER_UNSPEC) is normalized to FFmpeg's
    // speaker order for that count: swr would refuse the conversion otherwise, and
    // the plug needs positions it can place in azimuth. This normalization exists
    // only here; the fold-down and primary slots keep the frame's own layout, so
    // the paths outside the seam are untouched.
    ScopedChannelLayout normalized;
    if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        normalized.Default(frame->ch_layout.nb_channels);
    else if (!normalized.Copy(&frame->ch_layout)) return FFFResult::FfmpegFailure;
    if (av_channel_layout_copy(&bedConverterLayout_, &normalized.layout) < 0)
        return FFFResult::FfmpegFailure;
    bedConverterRate_ = frame->sample_rate;
    bedConverterFormat_ = inputFormat;
    if (swr_alloc_set_opts2(&bedConverter_, &bedConverterLayout_, AV_SAMPLE_FMT_FLTP,
        frame->sample_rate, &normalized.layout, static_cast<AVSampleFormat>(inputFormat),
        frame->sample_rate, 0, nullptr) < 0 || bedConverter_ == nullptr) {
        if (bedConverter_ != nullptr) swr_free(&bedConverter_);
        av_channel_layout_uninit(&bedConverterLayout_);
        bedConverterRate_ = 0; bedConverterFormat_ = -1;
        SetError("FFmpeg could not initialize the bed planar-float converter.");
        return FFFResult::FfmpegFailure;
    }
    if (swr_init(bedConverter_) < 0) {
        swr_free(&bedConverter_);
        av_channel_layout_uninit(&bedConverterLayout_);
        bedConverterRate_ = 0; bedConverterFormat_ = -1;
        SetError("FFmpeg could not initialize the bed planar-float converter.");
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

FFFResult PlayerWasapiRenderer::EnsureFoldResampler(const AVFrame* const frame) noexcept {
    // The frame the plug bailed on must fold exactly the way the body folded it
    // before the seam existed: same source layout, same device target, same
    // rematrix layer as EnsureResampler. Hence its own slot -- the plug-fed
    // resampler_ must not be torn down for a single frame.
    const auto inputFormat = static_cast<std::int32_t>(frame->format);
    if (foldResampler_ != nullptr && foldSampleRate_ == frame->sample_rate &&
        foldSampleFormat_ == inputFormat &&
        av_channel_layout_compare(&foldInputLayout_, &frame->ch_layout) == 0)
        return FFFResult::Success;
    if (outputSampleRate_ == 0 || outputChannels_ == 0) return FFFResult::InvalidState;
    if (foldResampler_ != nullptr) swr_free(&foldResampler_);
    av_channel_layout_uninit(&foldInputLayout_);
    if (av_channel_layout_copy(&foldInputLayout_, &frame->ch_layout) < 0)
        return FFFResult::FfmpegFailure;
    foldSampleRate_ = frame->sample_rate;
    foldSampleFormat_ = inputFormat;
    AVChannelLayout outputLayout{};
    av_channel_layout_default(&outputLayout, outputChannels_);
    const auto outputFormat = outputFloat_ ? AV_SAMPLE_FMT_FLT :
        (outputBitsPerSample_ == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32);
    const auto result = swr_alloc_set_opts2(&foldResampler_, &outputLayout, outputFormat,
        outputSampleRate_, &frame->ch_layout, static_cast<AVSampleFormat>(frame->format),
        frame->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outputLayout);
    if (result < 0 || foldResampler_ == nullptr) {
        if (foldResampler_ != nullptr) swr_free(&foldResampler_);
        av_channel_layout_uninit(&foldInputLayout_);
        foldSampleRate_ = 0; foldSampleFormat_ = -1;
        SetError("FFmpeg could not initialize the WASAPI downmix resampler.");
        return FFFResult::FfmpegFailure;
    }
    unsigned int rejectedOptions = 0;
    const auto setRematrixOption = [&](const char* name, const double value) {
        if (av_opt_set_double(foldResampler_, name, value, 0) < 0) ++rejectedOptions;
    };
    setRematrixOption("rematrix_maxval", RematrixMaxval);
    setRematrixOption("center_mix_level", CenterMixLevel);
    setRematrixOption("surround_mix_level", SurroundMixLevel);
    setRematrixOption("lfe_mix_level", LfeMixLevel);
    if (rejectedOptions != 0)
        SetError("WASAPI downmix options were partially rejected; using FFmpeg defaults.");
    if (swr_init(foldResampler_) < 0) {
        swr_free(&foldResampler_);
        av_channel_layout_uninit(&foldInputLayout_);
        foldSampleRate_ = 0; foldSampleFormat_ = -1;
        SetError("FFmpeg could not initialize the WASAPI downmix resampler.");
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

std::int32_t PlayerWasapiRenderer::RenderBedWithPlug(const AVFrame* const frame,
    const std::int64_t position100ns) noexcept {
    const auto channels = frame->ch_layout.nb_channels;
    const auto samples = frame->nb_samples;
    if (plug_.render == nullptr || plug_.context == nullptr || channels <= 0 || samples <= 0)
        return -1;
    if (EnsureBedConverter(frame) != FFFResult::Success) return -1;
    // Head-room for a converter that reports a few frames more than the decoded
    // frame; the plug's own capacity is the same stride, so it can never overrun.
    const std::size_t stride = static_cast<std::size_t>(samples) + 64;
    if (bedPlaneScratch_.size() < static_cast<std::size_t>(channels) * stride)
        bedPlaneScratch_.resize(static_cast<std::size_t>(channels) * stride);
    bedPlaneStrideFrames_ = stride;
    if (plugLeftScratch_.size() < stride) plugLeftScratch_.resize(stride);
    if (plugRightScratch_.size() < stride) plugRightScratch_.resize(stride);
    std::array<const float*, FFF3FP_AUDIO_PLUG_MAX_CHANNELS> planes{};
    std::int32_t bedFrames = samples;
    if (bedConverter_ != nullptr) {
        std::array<std::uint8_t*, FFF3FP_AUDIO_PLUG_MAX_CHANNELS> destination{};
        for (int index = 0; index < channels; ++index)
            destination[index] = reinterpret_cast<std::uint8_t*>(bedPlaneScratch_.data()) +
                static_cast<std::size_t>(index) * stride * sizeof(float);
        const auto inputFormat = static_cast<AVSampleFormat>(frame->format);
        const auto bytesPerSample = av_get_bytes_per_sample(inputFormat);
        if (bytesPerSample <= 0) return -1;
        const auto planar = av_sample_fmt_is_planar(inputFormat) != 0;
        const auto inputPlanes = planar ? channels : 1;
        if (inputPlanes <= 0 || inputPlanes > 64) return -1;
        std::array<const std::uint8_t*, 64> source{};
        for (int plane = 0; plane < inputPlanes; ++plane)
            source[plane] = frame->extended_data[plane];
        const auto converted = swr_convert(bedConverter_, destination.data(),
            static_cast<int>(stride), source.data(), samples);
        if (converted <= 0) return -1;
        bedFrames = std::min<std::int32_t>(converted, static_cast<std::int32_t>(stride));
        for (int index = 0; index < channels; ++index)
            planes[index] = bedPlaneScratch_.data() + static_cast<std::size_t>(index) * stride;
    } else {
        for (int index = 0; index < channels; ++index)
            planes[index] = reinterpret_cast<const float*>(frame->extended_data[index]);
    }
    FFF3FPAudioBedFrame bedFrame{};
    bedFrame.size = sizeof(bedFrame);
    bedFrame.version = FFF3FP_AUDIO_PLUG_VERSION;
    bedFrame.channels = planes.data();
    bedFrame.channelCount = static_cast<std::uint32_t>(channels);
    bedFrame.frameCount = static_cast<std::uint32_t>(bedFrames);
    bedFrame.position100ns = position100ns;
    const auto produced = plug_.render(plug_.context, &bedFrame, plugLeftScratch_.data(),
        plugRightScratch_.data(), static_cast<std::uint32_t>(stride));
    // <= 0 is "decline exactly this frame": the caller runs the fold-down for it.
    if (produced <= 0) return -1;
    return std::min<std::int32_t>(produced, static_cast<std::int32_t>(stride));
}

void PlayerWasapiRenderer::FreePlugResamplers() noexcept {
    if (bedConverter_ != nullptr) swr_free(&bedConverter_);
    av_channel_layout_uninit(&bedConverterLayout_);
    bedConverterRate_ = 0; bedConverterFormat_ = -1;
    if (foldResampler_ != nullptr) swr_free(&foldResampler_);
    av_channel_layout_uninit(&foldInputLayout_);
    foldSampleRate_ = 0; foldSampleFormat_ = -1;
}

void PlayerWasapiRenderer::ReleaseBedSeamBuffers() noexcept {
    std::vector<float>{}.swap(bedPlaneScratch_);
    std::vector<float>{}.swap(plugLeftScratch_);
    std::vector<float>{}.swap(plugRightScratch_);
    bedPlaneStrideFrames_ = 0;
}

FFFResult PlayerWasapiRenderer::Enqueue(const AVFrame* frame, const std::int64_t position100ns) noexcept {
    if (frame == nullptr || frame->nb_samples <= 0) return FFFResult::InvalidArgument;
    endOfStream_ = false;
    // Enqueue, reset and volume changes are serialized by PlayerSession's worker.
    // Do not take the PCM queue mutex while resampling or applying gain: the
    // WASAPI event thread must be able to take that mutex every device period.
    // The bed route is decided before the resampler because a plug-fed frame feeds
    // swr the plug's stereo pair instead of the decoded bed.
    const auto route = SelectBedRoute(frame);
    const auto ensured = EnsureResampler(frame, route);
    if (ensured != FFFResult::Success) return ensured;
    const auto resetPosition = resetPosition100ns_.load();
    const auto signedStartFrame = position100ns == AV_NOPTS_VALUE
        ? static_cast<std::int64_t>(producedTimelineFrames_)
        : av_rescale_rnd(position100ns - resetPosition, outputSampleRate_, 10'000'000, AV_ROUND_NEAR_INF);
    std::uint64_t preRollFrames = 0;
    std::uint64_t gapFrames = 0;
    std::uint64_t overlapFrames = 0;
    if (!timelineAnchored_) {
        if (fadeInFramesRemaining_ == 0 && outputSampleRate_ > 0)
            fadeInFramesRemaining_ = std::max<std::uint32_t>(1,
                outputSampleRate_ * StartupFadeInMilliseconds / 1000);
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
        // Rounding slack, in output frames. A frame timestamp is media-time in 100 ns and
        // the comparison is against a running sample count, so the two can never line up
        // exactly: at 48 kHz a 100 ns tick is 0.0048 samples, and measured on a plain Opus
        // stream every frame sits a constant 16-17 frames (0.33-0.35 ms) off. That is
        // quantisation, not jitter, but the counter used to increment on any nonzero delta
        // and so fired on essentially every frame -- 257 of 257 decoded, and 140 in two
        // seconds -- while the stream was perfectly healthy (no underruns, no
        // discontinuities, no inserted silence). A diagnostic that always reads "everything
        // is jittering" cannot report the real thing, so only deltas beyond rounding count.
        const auto jitterFloorFrames = std::max<std::int64_t>(1, outputSampleRate_ / 1000);
        if (std::abs(delta) < discontinuityThreshold) {
            if (std::abs(delta) > jitterFloorFrames) ++timestampJitterCount_;
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
    // --- seam: one bed frame, one owner ---
    // The plug is handed the whole frame, including the leading samples about to be
    // trimmed -- that trim is preroll, and preroll is what warms its pipeline. The
    // trim is then applied to its stereo pair, one output frame per input frame at
    // the bed's own rate, so the timeline math below is the same math as always.
    std::int32_t plugProduced = 0;
    if (route == BedRoute::Plug) {
        plugProduced = RenderBedWithPlug(frame, position100ns);
        if (plugProduced > 0) {
            ++plugFramesRendered_;
            bedRenderedByPlug_ = true;
        } else {
            // The plug bailed on exactly this frame; the fold-down below covers it.
            // Deliberately no counter moves: a per-frame bail is not a bed decline,
            // and its only visible trace is this frame missing from
            // framesRendered relative to the decoded frame count.
            bedRenderedByPlug_ = false;
        }
    } else {
        bedRenderedByPlug_ = false;
        // Reaching this route for a bed in scope is the plug's answer of "no" to
        // that bed -- supports() returning zero, or a bed wider than the ABI can
        // describe. Bed-level, so it counts, and it counts for a mono bed too: the
        // plug was shown one channel and said it would not place it.
        if (route == BedRoute::Declined) ++plugFramesDeclined_;
    }
    // Which slot and which input planes this frame converts. Native and Declined
    // hand over exactly the decoded bed at the same offset as before this seam
    // existed; a rendered frame hands over the plug's stereo pair; a frame the plug
    // bailed on mid-stream folds through its own slot so the plug-fed one survives.
    SwrContext* convert = resampler_;
    std::array<const std::uint8_t*, 64> input{};
    int convertSamples = inputSamples;
    if (route == BedRoute::Plug && plugProduced > 0) {
        const auto trim = static_cast<int>(std::min<std::int64_t>(skipSamples, plugProduced));
        convertSamples = plugProduced - trim;
        input[0] = reinterpret_cast<const std::uint8_t*>(plugLeftScratch_.data()) +
            static_cast<std::size_t>(trim) * sizeof(float);
        input[1] = reinterpret_cast<const std::uint8_t*>(plugRightScratch_.data()) +
            static_cast<std::size_t>(trim) * sizeof(float);
    } else {
        if (route == BedRoute::Plug) {
            const auto folded = EnsureFoldResampler(frame);
            if (folded != FFFResult::Success) return folded;
            convert = foldResampler_;
        }
        const auto inputFormat = static_cast<AVSampleFormat>(frame->format);
        const auto bytesPerSample = av_get_bytes_per_sample(inputFormat);
        if (bytesPerSample <= 0) return FFFResult::FfmpegFailure;
        const auto planar = av_sample_fmt_is_planar(inputFormat) != 0;
        const auto inputPlanes = planar ? frame->ch_layout.nb_channels : 1;
        // FFmpeg audio layouts have a bounded channel count.  Stack storage avoids a
        // heap allocation for every decoded audio frame.
        if (inputPlanes <= 0 || inputPlanes > 64) return FFFResult::NotSupported;
        const auto byteOffset = static_cast<std::size_t>(skipSamples) * bytesPerSample *
            (planar ? 1 : frame->ch_layout.nb_channels);
        for (int plane = 0; plane < inputPlanes; ++plane) {
            input[plane] = frame->extended_data[plane] + byteOffset;
        }
    }
    if (convertSamples <= 0) {
        // Only reachable when the plug produced no more frames than the trim asked
        // for: nothing to submit for this frame, exactly the early return above.
        PublishRuntimeDiagnostics();
        return FFFResult::Success;
    }
    const auto capacity = swr_get_out_samples(convert, convertSamples);
    if (capacity <= 0) return FFFResult::FfmpegFailure;
    std::vector<std::uint8_t> converted;
    {
        std::lock_guard lock(mutex_);
        if (!reusableBuffers_.empty()) {
            converted = std::move(reusableBuffers_.back());
            reusableBuffers_.pop_back();
        }
    }
    converted.resize(static_cast<std::size_t>(capacity) * outputBlockAlign_);
    std::uint8_t* output[] = { converted.data() };
    const auto frames = swr_convert(convert, output, capacity, input.data(), convertSamples);
    if (frames < 0) { SetError("FFmpeg failed to resample decoded audio."); return FFFResult::FfmpegFailure; }
    converted.resize(static_cast<std::size_t>(frames) * outputBlockAlign_);
    ApplyGain(converted);
    const auto gapBytes = static_cast<std::size_t>(gapFrames) * outputBlockAlign_;
    {
        std::lock_guard lock(mutex_);
        AudioChunk chunk;
        chunk.silenceBytes = gapBytes;
        chunk.bytes = std::move(converted);
        queuedBytes_ += gapBytes + chunk.bytes.size();
        queue_.push_back(std::move(chunk));
        producedTimelineFrames_ += gapFrames + static_cast<std::uint64_t>(frames);
        if (frames > 0) hasSubmittedAudio_ = true;
    }
    PublishRuntimeDiagnostics();
    if (controlEvent_ != nullptr) SetEvent(controlEvent_);
    return FFFResult::Success;
}

void PlayerWasapiRenderer::ApplyGain(std::vector<std::uint8_t>& converted) noexcept {
    const auto gain = muted_.load() ? 0.0f : volume_.load();
    const auto channels = static_cast<std::size_t>(std::max<std::uint16_t>(1, outputChannels_));
    const auto bytesPerFrame = static_cast<std::size_t>(outputBlockAlign_);
    const auto totalFrames = bytesPerFrame == 0 ? 0 : converted.size() / bytesPerFrame;
    const auto fadeFrames = std::min<std::size_t>(fadeInFramesRemaining_, totalFrames);
    const auto fadeTotalFrames = outputSampleRate_ == 0 ? 1u :
        std::max<std::uint32_t>(1, outputSampleRate_ * StartupFadeInMilliseconds / 1000);
    const auto fadeStartFrame = fadeTotalFrames - fadeInFramesRemaining_;
    const auto applySampleGain = [&](const float sampleGain) noexcept {
        if (outputFloat_) {
            auto* samples = reinterpret_cast<float*>(converted.data());
            for (std::size_t frame = 0; frame < totalFrames; ++frame) {
                const auto ramp = frame < fadeFrames
                    ? static_cast<float>(fadeStartFrame + frame + 1) / fadeTotalFrames
                    : 1.0f;
                const auto effective = sampleGain * std::min(ramp, 1.0f);
                for (std::size_t channel = 0; channel < channels; ++channel)
                    samples[frame * channels + channel] *= effective;
            }
        } else if (outputBitsPerSample_ == 16) {
            auto* samples = reinterpret_cast<std::int16_t*>(converted.data());
            for (std::size_t frame = 0; frame < totalFrames; ++frame) {
                const auto ramp = frame < fadeFrames
                    ? static_cast<float>(fadeStartFrame + frame + 1) / fadeTotalFrames
                    : 1.0f;
                const auto effective = sampleGain * std::min(ramp, 1.0f);
                for (std::size_t channel = 0; channel < channels; ++channel)
                    samples[frame * channels + channel] = static_cast<std::int16_t>(
                        std::lround(samples[frame * channels + channel] * effective));
            }
        } else {
            auto* samples = reinterpret_cast<std::int32_t*>(converted.data());
            for (std::size_t frame = 0; frame < totalFrames; ++frame) {
                const auto ramp = frame < fadeFrames
                    ? static_cast<float>(fadeStartFrame + frame + 1) / fadeTotalFrames
                    : 1.0f;
                const auto effective = sampleGain * std::min(ramp, 1.0f);
                for (std::size_t channel = 0; channel < channels; ++channel)
                    samples[frame * channels + channel] = static_cast<std::int32_t>(
                        std::llround(samples[frame * channels + channel] * effective));
            }
        }
    };
    if (totalFrames == 0) return;
    if (gain == 0.0f) {
        std::memset(converted.data(), 0, converted.size());
    } else if (gain != 1.0f || fadeFrames > 0) applySampleGain(gain);
    if (fadeFrames > 0) fadeInFramesRemaining_ -= static_cast<std::uint32_t>(fadeFrames);
}

FFFResult PlayerWasapiRenderer::Finish() noexcept {
    if (endOfStream_.load()) return FFFResult::Success;
    try {
        while (resampler_ != nullptr) {
            const auto capacity = swr_get_out_samples(resampler_, 0);
            if (capacity < 0) return FFFResult::FfmpegFailure;
            if (capacity == 0) break;
            AudioChunk chunk;
            chunk.bytes.resize(static_cast<std::size_t>(capacity) * outputBlockAlign_);
            std::uint8_t* output[] = {chunk.bytes.data()};
            const auto frames = swr_convert(resampler_, output, capacity, nullptr, 0);
            if (frames < 0) return FFFResult::FfmpegFailure;
            if (frames == 0) break;
            chunk.bytes.resize(static_cast<std::size_t>(frames) * outputBlockAlign_);
            ApplyGain(chunk.bytes);
            std::lock_guard lock(mutex_);
            queuedBytes_ += chunk.bytes.size();
            queue_.push_back(std::move(chunk));
            producedTimelineFrames_ += static_cast<std::uint64_t>(frames);
        }
    } catch (...) {
        SetError("Could not drain the audio resampler.");
        return FFFResult::NativeFailure;
    }
    endOfStream_ = true;
    PublishRuntimeDiagnostics();
    if (controlEvent_ != nullptr) SetEvent(controlEvent_);
    return FFFResult::Success;
}

void PlayerWasapiRenderer::SetPaused(const bool paused) noexcept {
    const auto now = QpcNow100ns();
    {
        std::lock_guard lock(clockMutex_);
        if (!paused_.load() && clockRunning_.load() &&
            clockSampleQpc100ns_ > 0 && now > clockSampleQpc100ns_) {
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
        fadeInFramesRemaining_ = outputSampleRate_ == 0 ? 0 :
            std::max<std::uint32_t>(1, outputSampleRate_ * StartupFadeInMilliseconds / 1000);
    }
    // A seek can also follow an audio-stream switch.  Recreating on the next
    // input frame keeps the resampler's source layout/rate contract correct.
    if (resampler_ != nullptr) swr_free(&resampler_);
    av_channel_layout_uninit(&inputChannelLayout_);
    inputSampleRate_ = 0; inputSampleFormat_ = -1;
    // A seek or a device restart also drops the plug's phase, and clears the verdict
    // for the bed it was asked about: a stream switch can bring a different bed
    // through the same sample rate, and the position map is what the plug placed.
    // The frame counters deliberately survive this -- ResetDiagnostics() below does
    // not touch them either, so a seek cannot erase which renderer owned the bed.
    FreePlugResamplers();
    ReleaseBedSeamBuffers();
    plugBedIdentity_ = {};
    plugBedAccepted_ = false;
    bedRenderedByPlug_ = false;
    if (plugInstalled_.load(std::memory_order_acquire) && plug_.reset != nullptr &&
        plug_.context != nullptr) plug_.reset(plug_.context);
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
    underrunCount_ = 0; hasSubmittedAudio_ = false; endOfStream_ = false;
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
    if (!clockRunning_.load() || paused_.load() || sampleQpc <= 0) return position;
    const auto now = QpcNow100ns();
    if (now <= sampleQpc) return position;
    return position + std::min(now - sampleQpc, std::max<std::int64_t>(0, limit - position));
}
std::int64_t PlayerWasapiRenderer::TimelineLimit100ns() const noexcept {
    std::lock_guard lock(clockMutex_);
    return clockRunning_.load() && !paused_.load() ? clockLimitPosition100ns_ : clockPosition100ns_;
}
std::int64_t PlayerWasapiRenderer::Buffered100ns() const noexcept {
    std::lock_guard lock(mutex_);
    if (outputSampleRate_ == 0 || outputBlockAlign_ == 0) return 0;
    // This is the amount of decoded PCM still ahead of the endpoint playback
    // cursor: application PCM waiting to be submitted plus samples already
    // owned by WASAPI. It is a queue lead/buffer duration, not hardware's
    // fixed output latency.
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
bool PlayerWasapiRenderer::RestartRequested() const noexcept { return restartRequested_.load(); }
std::string PlayerWasapiRenderer::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
void PlayerWasapiRenderer::RequestRestart(std::string message) noexcept {
    SetError(std::move(message));
    if (!restartRequested_.exchange(true) && restartCallback_) {
        try { restartCallback_(); } catch (...) {}
    }
}
void PlayerWasapiRenderer::SetError(std::string message) noexcept { try { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); } catch (...) {} }

void PlayerWasapiRenderer::UpdatePeakLevels(const std::uint8_t* const samples,
    const std::uint32_t frames) noexcept {
    if (runtimeState_ == nullptr || samples == nullptr || frames == 0 || outputChannels_ == 0) return;
    const auto channels = std::min<std::uint32_t>(outputChannels_, PlayerAudioRuntimeState::MaximumChannels);
    std::array<float, PlayerAudioRuntimeState::MaximumChannels> peaks{};
    const auto sampleCount = static_cast<std::size_t>(frames) * outputChannels_;
    const auto accumulate = [&](const auto* values, const float scale) {
        for (std::size_t index = 0; index < sampleCount; ++index) {
            float magnitude;
            if constexpr (std::is_floating_point_v<std::remove_cvref_t<decltype(values[index])>>)
                magnitude = std::isfinite(values[index]) ? std::abs(values[index]) : 0.0f;
            else
                magnitude = static_cast<float>(std::abs(static_cast<std::int64_t>(values[index]))) / scale;
            const auto channel = static_cast<std::uint32_t>(index % outputChannels_);
            if (channel < channels) peaks[channel] = std::max(peaks[channel], magnitude);
        }
    };
    if (outputFloat_) accumulate(reinterpret_cast<const float*>(samples), 1.0f);
    else if (outputBitsPerSample_ == 16) accumulate(reinterpret_cast<const std::int16_t*>(samples), 32768.0f);
    else accumulate(reinterpret_cast<const std::int32_t*>(samples), 2147483648.0f);
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
    // Seam readout. The API thread answers FFF3FP_GetAudioRenderPlugState from these
    // published values because it must not dereference the renderer: PlayerSession
    // may replace the renderer between one call and the next.
    runtimeState_->plugBedActive.store(bedRenderedByPlug_.load() ? 1u : 0u,
        std::memory_order_relaxed);
    runtimeState_->plugFramesRendered.store(plugFramesRendered_.load(), std::memory_order_relaxed);
    runtimeState_->plugFramesDeclined.store(plugFramesDeclined_.load(), std::memory_order_relaxed);
}

void PlayerWasapiRenderer::RenderThread() noexcept {
    const ComInitialization com;
    const auto comResult = com.Result();
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> renderer;
    ComPtr<IAudioClock> clock;
    WAVEFORMATEX* rawFormat = nullptr;
    std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> format;
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
        FAILED(client->GetMixFormat(&rawFormat))) {
        fail("Could not open the selected WASAPI playback endpoint.");
        return;
    }
    format.reset(rawFormat);
    // Shared mode lets the engine perform conversion and resampling.  Exclusive
    // mode deliberately bypasses that mixer: the device must accept the exact
    // endpoint format and the client owns the complete hardware period.
    WAVEFORMATEX* activeFormat = format.get();
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
                exclusiveCandidate.dwChannelMask = ChannelMask(format.get());
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
                return;
            }
        }
        REFERENCE_TIME defaultPeriod = 0;
        REFERENCE_TIME minimumPeriod = 0;
        if (FAILED(client->GetDevicePeriod(&defaultPeriod, &minimumPeriod))) {
            fail("Could not query the selected endpoint's exclusive-mode period.");
            return;
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
        if (exclusive_ && wasapiResult == AUDCLNT_E_DEVICE_IN_USE)
            message << "Another application already has exclusive control of the selected audio endpoint";
        else
            message << (exclusive_ ? "Could not initialize exclusive WASAPI playback for the selected endpoint"
                : "Could not initialize event-driven WASAPI playback");
        message
            << " (HRESULT 0x" << std::hex << static_cast<std::uint32_t>(wasapiResult) << ").";
        fail(message.str());
        return;
    }
    UINT64 clockFrequency = 0;
    if (FAILED(clock->GetFrequency(&clockFrequency)) || clockFrequency == 0) {
        fail("Could not query the WASAPI device clock frequency.");
        return;
    }
    {
        std::lock_guard lock(mutex_);
        outputSampleRate_ = activeFormat->nSamplesPerSec; outputChannels_ = activeFormat->nChannels;
        outputBlockAlign_ = activeFormat->nBlockAlign; outputBitsPerSample_ = activeFormat->wBitsPerSample;
        outputValidBitsPerSample_ = activeFormat->wBitsPerSample;
        if (activeFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && activeFormat->cbSize >= 22)
            outputValidBitsPerSample_ = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(activeFormat)->Samples.wValidBitsPerSample;
        outputFloat_ = IsFloatWaveFormat(activeFormat);
        initializationResult_ = FFFResult::Success; initializationFinished_ = true;
    }
    if (runtimeState_ != nullptr) runtimeState_->SetChannels(activeFormat->nChannels);
    initializedCondition_.notify_all();
    std::unique_ptr<wchar_t, CoTaskMemDeleter> openedEndpointId;
    if (endpointId_.empty()) {
        wchar_t* value = nullptr;
        if (SUCCEEDED(device->GetId(&value))) openedEndpointId.reset(value);
    }
    format.reset();
    bool clientStarted = false;
    UINT64 clientStartDevicePosition = 0;
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
        if (!clientStarted || !clockAnchorValid || outputSampleRate_ == 0) return;
        UINT64 devicePosition = 0;
        UINT64 qpcPosition100ns = 0;
        const auto positionResult = clock->GetPosition(&devicePosition, &qpcPosition100ns);
        if (FAILED(positionResult)) {
            if (IsAudioDeviceLost(positionResult))
                RequestRestart("The WASAPI output device was disconnected or reconfigured.");
            return;
        }
        const auto elapsedUnits = devicePosition > clockAnchorDevicePosition
            ? devicePosition - clockAnchorDevicePosition : 0;
        const auto elapsedFrames = static_cast<std::uint64_t>(
            static_cast<long double>(elapsedUnits) * outputSampleRate_ / clockFrequency);
        const auto playedFrames = std::min(deviceSubmittedFrames, elapsedFrames);
        std::lock_guard lock(clockMutex_);
        if (activeClockEpoch != clockEpoch_) return;
        const auto advancing = devicePosition > clientStartDevicePosition;
        clockRunning_ = advancing;
        playedMediaFrames_ = playedFrames;
        pendingMediaFrames_ = deviceSubmittedFrames - playedFrames;
        const auto resetPosition = resetPosition100ns_.load();
        clockPosition100ns_ = resetPosition +
            static_cast<std::int64_t>(playedFrames * 10'000'000 / outputSampleRate_);
        clockLimitPosition100ns_ = resetPosition +
            static_cast<std::int64_t>(deviceSubmittedFrames * 10'000'000 / outputSampleRate_);
        clockSampleQpc100ns_ = advancing && qpcPosition100ns != 0
            ? static_cast<std::int64_t>(qpcPosition100ns) : QpcNow100ns();
    };
    const auto startClient = [&]() noexcept {
        clock->GetPosition(&clientStartDevicePosition, nullptr);
        const auto result = client->Start();
        if (SUCCEEDED(result)) {
            std::lock_guard lock(clockMutex_);
            clockSampleQpc100ns_ = QpcNow100ns();
            clockRunning_ = false;
            clientStarted = true;
        }
        return result;
    };
    HANDLE events[] = { stopEvent_, sampleEvent_, controlEvent_ };
    auto nextDefaultEndpointCheck100ns = QpcNow100ns();
    for (;;) {
        const auto waitResult = WaitForMultipleObjects(3, events, FALSE, 100);
        if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_FAILED) break;
        if (endpointId_.empty() && openedEndpointId != nullptr &&
            QpcNow100ns() >= nextDefaultEndpointCheck100ns) {
            nextDefaultEndpointCheck100ns = QpcNow100ns() + 5'000'000;
            ComPtr<IMMDevice> currentDefault;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,
                    &currentDefault))) {
                wchar_t* rawCurrentId = nullptr;
                if (SUCCEEDED(currentDefault->GetId(&rawCurrentId))) {
                    std::unique_ptr<wchar_t, CoTaskMemDeleter> currentId(rawCurrentId);
                    if (CompareStringOrdinal(openedEndpointId.get(), -1,
                            currentId.get(), -1, TRUE) != CSTR_EQUAL) {
                        RequestRestart("The default Windows audio output device changed.");
                        break;
                    }
                }
            }
        }
        const auto deviceEvent = waitResult == WAIT_OBJECT_0 + 1;
        // In exclusive event mode the callback means that the complete endpoint
        // buffer has become writable. Producer/control wakeups must never be
        // mistaken for that notification.
        if (exclusive_ && deviceEvent) exclusiveBufferPrimed = false;
        if (resetRequested_.exchange(false)) {
            clockRunning_ = false;
            if (clientStarted) client->Stop();
            const auto resetResult = client->Reset();
            if (IsAudioDeviceLost(resetResult)) {
                RequestRestart("The WASAPI output device was invalidated while resetting playback.");
                break;
            }
            clientStarted = false;
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
            clockRunning_ = false;
            clientStarted = false;
        }
        updateMediaClock();
        if (restartRequested_.load()) break;
        PublishRuntimeDiagnostics();
        if (exclusive_) {
            if (shouldPause) continue;
            if (!clientStarted && exclusiveBufferPrimed) {
                const auto startResult = startClient();
                if (SUCCEEDED(startResult)) clientStarted = true;
                else if (IsAudioDeviceLost(startResult)) {
                    RequestRestart("The exclusive WASAPI output device could not resume.");
                    break;
                }
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
        if (!exclusive_) {
            const auto paddingResult = client->GetCurrentPadding(&padding);
            if (FAILED(paddingResult)) {
                if (IsAudioDeviceLost(paddingResult)) {
                    RequestRestart("The WASAPI output device stopped accepting audio.");
                    break;
                }
                continue;
            }
        }
        if (!exclusive_ && padding >= bufferFrames) {
            if (!shouldPause && !clientStarted) {
                const auto startResult = startClient();
                if (SUCCEEDED(startResult)) clientStarted = true;
                else if (IsAudioDeviceLost(startResult)) {
                    RequestRestart("The WASAPI output device could not start.");
                    break;
                }
            }
            continue;
        }
        if (!exclusive_ && clientStarted && !shouldPause && padding == 0 && endOfStream_.load()) {
            std::lock_guard queueLock(mutex_);
            if (queuedBytes_ == 0) {
                std::lock_guard clockLock(clockMutex_);
                if (activeClockEpoch == clockEpoch_) {
                    playedMediaFrames_ = deviceSubmittedFrames;
                    pendingMediaFrames_ = 0;
                    clockPosition100ns_ = resetPosition100ns_.load() +
                        static_cast<std::int64_t>(deviceSubmittedFrames * 10'000'000 / outputSampleRate_);
                    clockLimitPosition100ns_ = clockPosition100ns_;
                    clockSampleQpc100ns_ = QpcNow100ns();
                }
            }
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
                if (!shouldPause && clientStarted && hasSubmittedAudio_.load() && !endOfStream_.load() && padding == 0 && !starved) {
                    ++underrunCount_;
                    starved = true;
                }
                if (padding == 0 && runtimeState_ != nullptr) runtimeState_->ClearValues();
                if (runtimeState_ != nullptr)
                    runtimeState_->underruns.store(underrunCount_.load(), std::memory_order_relaxed);
                continue;
            }
            if (renderedFrames == 0) {
                if (hasSubmittedAudio_.load() && !endOfStream_.load() && !starved) ++underrunCount_;
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
            const auto bufferResult = renderer->GetBuffer(renderedFrames, &destination);
            if (FAILED(bufferResult)) {
                if (IsAudioDeviceLost(bufferResult))
                    RequestRestart("The WASAPI output buffer was invalidated.");
                continue;
            }
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
                if (chunk.silenceBytes == 0 && chunk.offset == chunk.bytes.size()) {
                    if (chunk.bytes.capacity() <= MaximumReusableAudioBufferBytes &&
                        reusableBuffers_.size() < MaximumReusableAudioBuffers) {
                        chunk.bytes.clear();
                        reusableBuffers_.push_back(std::move(chunk.bytes));
                    }
                    queue_.pop_front();
                }
            }
            const auto submittedBytes = static_cast<std::size_t>(renderedFrames) * outputBlockAlign_;
            if (submittedBytes > copied) std::memset(destination + copied, 0, submittedBytes - copied);
            queuedBytes_ -= copied;
            pendingMediaFrames_.fetch_add(mediaFrames);
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
        const auto releaseResult = renderer->ReleaseBuffer(renderedFrames, 0);
        if (SUCCEEDED(releaseResult)) {
            if (exclusive_) exclusiveBufferPrimed = true;
            {
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
            }
            if (!shouldPause && !clientStarted) {
                const auto startResult = startClient();
                if (SUCCEEDED(startResult)) clientStarted = true;
                else if (IsAudioDeviceLost(startResult))
                    RequestRestart("The WASAPI output device could not start after buffering audio.");
            }
        } else {
            {
                std::lock_guard lock(clockMutex_);
                if (activeClockEpoch == clockEpoch_)
                    pendingMediaFrames_ = deviceSubmittedFrames - playedMediaFrames_.load();
            }
            RequestRestart("The WASAPI output device rejected a rendered audio buffer.");
        }
        if (restartRequested_.load()) break;
        PublishRuntimeDiagnostics();
    }
    if (clientStarted) client->Stop();
    client->Reset();
    renderer.Reset(); clock.Reset(); client.Reset(); device.Reset(); enumerator.Reset();
    running_ = false;
}
