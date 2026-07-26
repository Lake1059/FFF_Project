#include "pch.h"
#include "3FP/Core/PlayerSession.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#include <filesystem>
#include <iomanip>
#include <cmath>
#include <chrono>

namespace {
constexpr std::int64_t TicksPerSecond = 10'000'000;

std::string EscapeJson(const std::string& value) {
    std::ostringstream output;
    static constexpr char Hex[] = "0123456789abcdef";
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) output << "\\u00" << Hex[character >> 4] << Hex[character & 15];
            else output << raw;
        }
    }
    return output.str();
}

std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    return av_strerror(error, buffer, sizeof(buffer)) == 0 ? buffer : "FFmpeg error " + std::to_string(error);
}

std::string ToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), length, nullptr, nullptr);
    result.resize(length - 1); return result;
}

std::wstring FromUtf8(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), length);
    result.resize(length - 1); return result;
}

bool FromUtf8Strict(const char* value, std::wstring& output) noexcept {
    try {
        if (value == nullptr) { output.clear(); return true; }
        const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
        if (length <= 0) return false;
        std::wstring converted(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
            converted.data(), length) <= 0) return false;
        converted.resize(static_cast<std::size_t>(length - 1));
        output = std::move(converted);
        return true;
    } catch (...) { return false; }
}

AVPixelFormat SelectHardwareFormat(AVCodecContext* context, const AVPixelFormat* formats) {
    const auto expected = static_cast<AVPixelFormat>(reinterpret_cast<std::intptr_t>(context->opaque));
    for (auto format = formats; *format != AV_PIX_FMT_NONE; ++format)
        if (*format == expected) return *format;
    return AV_PIX_FMT_NONE;
}

const AVCodec* FindHardwareDecoder(const AVCodecID codecId, const AVHWDeviceType deviceType,
    AVPixelFormat& pixelFormat) noexcept {
    const auto expectedFormat = deviceType == AV_HWDEVICE_TYPE_D3D11VA
        ? AV_PIX_FMT_D3D11 : deviceType == AV_HWDEVICE_TYPE_CUDA ? AV_PIX_FMT_CUDA : AV_PIX_FMT_NONE;
    if (expectedFormat == AV_PIX_FMT_NONE) return nullptr;
    const AVCodec* fallback = nullptr;
    void* iterator = nullptr;
    while (const auto* codec = av_codec_iterate(&iterator)) {
        if (codec->id != codecId || !av_codec_is_decoder(codec)) continue;
        for (int configIndex = 0;; ++configIndex) {
            const auto* hardware = avcodec_get_hw_config(codec, configIndex);
            if (hardware == nullptr) break;
            if (hardware->device_type != deviceType || hardware->pix_fmt != expectedFormat ||
                (hardware->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) == 0) continue;

            // The native AV1 decoder parses the bitstream before NVDEC sees it.
            // Some otherwise valid NVENC AV1 streams are accepted by NVIDIA's
            // CUVID parser but rejected there, so prefer the dedicated NVDEC
            // wrapper whenever CUDA exposes one.
            if (deviceType == AV_HWDEVICE_TYPE_CUDA && codec->name != nullptr &&
                std::string_view(codec->name).ends_with("_cuvid")) {
                pixelFormat = expectedFormat;
                return codec;
            }
            if (fallback == nullptr) fallback = codec;
        }
    }
    if (fallback != nullptr) pixelFormat = expectedFormat;
    return fallback;
}

bool IsHardwareFrame(const AVFrame* frame) noexcept {
    if (frame == nullptr) return false;
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0;
}

bool IsLoopAwareImageDemuxer(const AVInputFormat* inputFormat) noexcept {
    if (inputFormat == nullptr || inputFormat->name == nullptr) return false;
    const std::string_view name(inputFormat->name);
    return name == "gif" || name == "apng" || name == "webp_anim" || name == "jpegxl_anim";
}

std::int32_t FindTimedVideoStream(AVFormatContext* format) noexcept {
    if (format == nullptr) return -1;
    const auto best = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (best >= 0 && (format->streams[best]->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0)
        return best;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        const auto* stream = format->streams[index];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0) return static_cast<std::int32_t>(index);
    }
    return -1;
}

std::int32_t FindCoverArtStream(AVFormatContext* format) noexcept {
    if (format == nullptr) return -1;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        const auto* stream = format->streams[index];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) return static_cast<std::int32_t>(index);
    }
    return -1;
}

const char* MediaTypeName(const AVMediaType type) noexcept {
    switch (type) { case AVMEDIA_TYPE_VIDEO: return "video"; case AVMEDIA_TYPE_AUDIO: return "audio";
    case AVMEDIA_TYPE_SUBTITLE: return "subtitle"; default: return "other"; }
}
}

PlayerSession::PlayerSession(const FFF3FPConfiguration& configuration)
    : decodeMode_(configuration.decodeMode), callback_(configuration.eventCallback),
      callbackContext_(configuration.eventCallbackContext), terminate_(false), format_(nullptr),
      videoDecoder_(nullptr), audioDecoder_(nullptr), videoStream_(-1),
      audioStream_(-1), coverArtStream_(-1), coverArtFrame_(nullptr), externalFormat_(nullptr), externalAudioDecoder_(nullptr),
      externalAudioStream_(-1), externalAudioOffset100ns_(0), volume_(1.0f), muted_(false),
      clockOriginPosition100ns_(0), clockOriginQpc_(0), playbackPosition100ns_(0),
      state_(FFF3FPState::Idle), qpcFrequency_(0), seekTarget100ns_(-1), seekTargetFrame_(-1),
      keyframeSeekPending_(false), lastVideoFrameDuration100ns_(0),
      displayedFrame_(nullptr), draining_(false), timedTextCommandQueued_(false) {
    snapshot_ = {};
    snapshot_.size = sizeof(snapshot_); snapshot_.version = 2; snapshot_.state = FFF3FPState::Idle;
    snapshot_.decodeMode = configuration.decodeMode; snapshot_.requestedColorMode = configuration.colorMode;
    snapshot_.actualColorMode = FFF3FPColorMode::MapToSdr; snapshot_.frameIndex = -1;
    snapshot_.framePts = AV_NOPTS_VALUE; snapshot_.selectedVideoStream = -1; snapshot_.selectedAudioStream = -1;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); qpcFrequency_ = frequency.QuadPart;
    if (configuration.audioEndpointIdUtf8 != nullptr) audioEndpointId_ = FromUtf8(configuration.audioEndpointIdUtf8);
    videoRenderer_.SetWindow(static_cast<HWND>(configuration.outputWindow));
    videoRenderer_.SetColorMode(configuration.colorMode, configuration.sdrPeakNits,
        configuration.hdrPeakNits, configuration.sdrPaperWhiteNits);
    snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    publishedSnapshot_ = snapshot_;
    worker_ = std::thread(&PlayerSession::Worker, this);
}

PlayerSession::~PlayerSession() {
    { std::lock_guard lock(mutex_); terminate_ = true; commands_.clear(); }
    commandCondition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void PlayerSession::Enqueue(Command command) noexcept {
    try { { std::lock_guard lock(mutex_); if (terminate_) return; commands_.push_back(std::move(command)); } commandCondition_.notify_one(); }
    catch (...) { ReportError(FFFResult::NativeFailure, "Could not queue the playback command."); }
}

FFFResult PlayerSession::Open(const char* path) noexcept {
    std::string normalized, error;
    if (!NormalizeLocalPath(path, normalized, error)) { ReportError(FFFResult::InvalidArgument, std::move(error), "open"); return FFFResult::InvalidArgument; }
    if (state_.exchange(FFF3FPState::Opening) == FFF3FPState::Opening) return FFFResult::InvalidState;
    { std::lock_guard lock(snapshotMutex_); publishedSnapshot_.state = FFF3FPState::Opening; }
    Emit(FFF3FPEvent::StateChanged, "{\"state\":1}");
    Enqueue([this, value = std::move(normalized)] { DoOpen(value); }); return FFFResult::Success;
}
FFFResult PlayerSession::Play() noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this] { const auto current = state_.load(); if (current == FFF3FPState::Ended) DoSeek(0); ResetClock(snapshot_.position100ns); if (audioRenderer_) audioRenderer_->SetPaused(false); SetState(FFF3FPState::Playing, "play"); }); return FFFResult::Success; }
FFFResult PlayerSession::Pause() noexcept { if (state_.load() != FFF3FPState::Playing) return FFFResult::InvalidState; Enqueue([this] { snapshot_.position100ns = ClockPosition(); if (audioRenderer_) audioRenderer_->SetPaused(true); SetState(FFF3FPState::Paused, "pause"); }); return FFFResult::Success; }
FFFResult PlayerSession::Stop() noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this] { DoClose(FFF3FPState::Idle); }); return FFFResult::Success; }
FFFResult PlayerSession::Close() noexcept { Enqueue([this] { DoClose(); }); return FFFResult::Success; }
FFFResult PlayerSession::Seek(const std::int64_t value) noexcept { if (value < 0) return FFFResult::InvalidArgument; const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, value] { DoSeek(value); if (state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget(); Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek\",\"position100ns\":" + std::to_string(snapshot_.position100ns) + "}"); }); return FFFResult::Success; }
FFFResult PlayerSession::SeekKeyframe(const std::int64_t value) noexcept {
    if (value < 0) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this, value] {
        const auto hasVideo = videoStream_ >= 0 && videoDecoder_ != nullptr;
        DoSeek(value, -1, !hasVideo);
        if (hasVideo && state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget();
        Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek-keyframe\",\"position100ns\":" +
            std::to_string(snapshot_.position100ns) + "}");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SeekFrame(const std::int64_t value) noexcept {
    if (value < 0) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    { std::lock_guard lock(snapshotMutex_); if ((state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) || publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState; }
    Enqueue([this, value] { if (value < static_cast<std::int64_t>(framePtsIndex_.size())) { const auto* stream = format_->streams[videoStream_]; const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time; DoSeek(av_rescale_q(framePtsIndex_[value] - start, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}), value); } else DoSeek(0, value); if (state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget(); Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek-frame\",\"frame\":" + std::to_string(snapshot_.frameIndex) + "}"); });
    return FFFResult::Success;
}
FFFResult PlayerSession::StepFrame(const std::int32_t direction) noexcept {
    if (direction != -1 && direction != 1) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    { std::lock_guard lock(snapshotMutex_); if ((state != FFF3FPState::Ready && state != FFF3FPState::Paused && state != FFF3FPState::Ended) || publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState; }
    Enqueue([this, direction] { const auto target = std::max<std::int64_t>(0, snapshot_.frameIndex + direction); if (target < static_cast<std::int64_t>(framePtsIndex_.size())) { const auto* stream = format_->streams[videoStream_]; const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time; DoSeek(av_rescale_q(framePtsIndex_[target] - start, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}), target); } else DoSeek(0, target); DecodeUntilSeekTarget(); SetState(FFF3FPState::Paused, "step-frame"); });
    return FFFResult::Success;
}
FFFResult PlayerSession::SelectVideoStream(const std::int32_t index) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, index] { DoSelectStream(index, true); }); return FFFResult::Success; }
FFFResult PlayerSession::SelectAudioStream(const std::int32_t index) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, index] { DoSelectStream(index, false); }); return FFFResult::Success; }

FFFResult PlayerSession::LoadExternalAudio(const char* path, const std::int32_t index, const std::int64_t offset) noexcept {
    const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    std::string normalized, error; if (!NormalizeLocalPath(path, normalized, error)) return FFFResult::InvalidArgument;
    Enqueue([this, value = std::move(normalized), index, offset] { DoLoadExternalAudio(value, index, offset); }); return FFFResult::Success;
}
FFFResult PlayerSession::ClearExternalAudio() noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this] { if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); if (externalFormat_) avformat_close_input(&externalFormat_); externalAudioStream_ = -1; externalAudioPath_.clear(); snapshot_.isExternalAudio = 0; if (audioRenderer_) audioRenderer_->Reset(snapshot_.position100ns); PublishSnapshot(); Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"clear-external-audio\"}"); }); return FFFResult::Success; }
FFFResult PlayerSession::SetExternalAudioOffset(const std::int64_t offset) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, offset] { externalAudioOffset100ns_ = offset; snapshot_.externalAudioOffset100ns = offset; if (externalFormat_) DoSeek(snapshot_.position100ns); else PublishSnapshot(); }); return FFFResult::Success; }
FFFResult PlayerSession::SetColorMode(const FFF3FPColorMode mode, const float sdr, const float hdr, const float paper) noexcept { if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdr) || sdr <= 0 || !std::isfinite(hdr) || hdr <= 0 || hdr > 10000 || !std::isfinite(paper) || paper <= 0) return FFFResult::InvalidArgument; Enqueue([this, mode, sdr, hdr, paper] { snapshot_.requestedColorMode = mode; const auto previous = snapshot_.actualColorMode; const auto result = videoRenderer_.SetColorMode(mode, sdr, hdr, paper); if (result != FFFResult::Success) { Fail(result, "The color output configuration is invalid.", "color-mode"); return; } const auto* frame = displayedFrame_ != nullptr ? displayedFrame_ : coverArtFrame_; if (frame != nullptr && videoRenderer_.Render(frame) != FFFResult::Success) { Fail(FFFResult::DeviceFailure, videoRenderer_.LastError(), "redraw"); return; } snapshot_.actualColorMode = videoRenderer_.ActualColorMode(); PublishSnapshot(); std::ostringstream json; json << "{\"requested\":" << static_cast<unsigned>(mode) << ",\"actual\":" << static_cast<unsigned>(snapshot_.actualColorMode) << ",\"reason\":\"" << EscapeJson(videoRenderer_.FallbackReason()) << "\"}"; if (previous != snapshot_.actualColorMode || mode != snapshot_.actualColorMode) Emit(FFF3FPEvent::ColorModeChanged, json.str()); }); return FFFResult::Success; }
FFFResult PlayerSession::SetOutputWindow(void* window) noexcept { if (window != nullptr && !IsWindow(static_cast<HWND>(window))) return FFFResult::InvalidArgument; Enqueue([this, window] { const auto result = videoRenderer_.SetWindow(static_cast<HWND>(window)); if (result != FFFResult::Success) { Fail(result, "The playback window handle is invalid.", "output-window"); return; } const auto* frame = displayedFrame_ != nullptr ? displayedFrame_ : coverArtFrame_; if (frame != nullptr && videoRenderer_.Render(frame) != FFFResult::Success) Fail(FFFResult::DeviceFailure, videoRenderer_.LastError(), "redraw"); }); return FFFResult::Success; }
FFFResult PlayerSession::SetAudioEndpoint(const char* endpoint) noexcept { const auto value = endpoint == nullptr ? std::wstring{} : FromUtf8(endpoint); Enqueue([this, value] { audioEndpointId_ = value; if (audioRenderer_) { const auto paused = snapshot_.state != FFF3FPState::Playing; audioRenderer_->Stop(); audioRenderer_ = std::make_unique<PlayerWasapiRenderer>(audioEndpointId_); const auto result = audioRenderer_->Start(); if (result != FFFResult::Success) { Fail(result, audioRenderer_->LastError(), "audio-endpoint"); return; } audioRenderer_->SetVolume(volume_, muted_); audioRenderer_->Reset(snapshot_.position100ns); audioRenderer_->SetPaused(paused); Emit(FFF3FPEvent::DeviceChanged, "{\"type\":\"audio\"}"); } }); return FFFResult::Success; }
FFFResult PlayerSession::SetVolume(const float volume, const bool muted) noexcept { if (!std::isfinite(volume) || volume < 0 || volume > 1) return FFFResult::InvalidArgument; Enqueue([this, volume, muted] { volume_ = volume; muted_ = muted; if (audioRenderer_) audioRenderer_->SetVolume(volume_, muted_); }); return FFFResult::Success; }

FFFResult PlayerSession::SetTimedTextLayer(const FFF3FPTimedTextLayer& input) noexcept {
    if (input.size < sizeof(FFF3FPTimedTextLayer) || input.version != 1 ||
        input.canvasWidth == 0 || input.canvasHeight == 0 || input.commandCount > 4096 ||
        (input.commandCount != 0 && input.commands == nullptr)) return FFFResult::InvalidArgument;
    try {
        TimedTextRenderLayer layer;
        layer.canvasWidth = input.canvasWidth;
        layer.canvasHeight = input.canvasHeight;
        layer.sequence = input.sequence;
        layer.commands.reserve(input.commandCount);
        for (std::uint32_t index = 0; index < input.commandCount; ++index) {
            const auto& source = input.commands[index];
            if (source.size < sizeof(FFF3FPTimedTextCommand) || source.version != 1 ||
                source.type < FFF3FPTimedTextCommandType::Text || source.type > FFF3FPTimedTextCommandType::Bitmap ||
                !std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.width) ||
                !std::isfinite(source.height) || source.width <= 0 || source.height <= 0)
                return FFFResult::InvalidArgument;
            TimedTextRenderCommand command;
            command.type = source.type; command.flags = source.flags;
            command.x = source.x; command.y = source.y; command.width = source.width; command.height = source.height;
            command.foregroundArgb = source.foregroundArgb; command.outlineArgb = source.outlineArgb;
            command.fontSize = source.fontSize; command.outlineWidth = source.outlineWidth;
            command.horizontalAlignment = source.horizontalAlignment;
            command.verticalAlignment = source.verticalAlignment;
            command.contentId = source.contentId;
            if (source.type == FFF3FPTimedTextCommandType::Text) {
                if (source.textUtf8 == nullptr || !std::isfinite(source.fontSize) || source.fontSize <= 0 ||
                    !std::isfinite(source.outlineWidth) || source.outlineWidth < 0 ||
                    source.horizontalAlignment > FFF3FPTimedTextAlignment::Far ||
                    source.verticalAlignment > FFF3FPTimedTextAlignment::Far ||
                    !FromUtf8Strict(source.textUtf8, command.text) || command.text.empty() ||
                    !FromUtf8Strict(source.fontFamilyUtf8, command.fontFamily)) return FFFResult::InvalidArgument;
                if (command.fontFamily.empty()) command.fontFamily = L"Segoe UI";
            } else {
                const auto required = static_cast<std::uint64_t>(source.bitmapStride) * source.bitmapHeight;
                if (source.bitmapBgra == nullptr || source.bitmapWidth == 0 || source.bitmapHeight == 0 ||
                    static_cast<std::uint64_t>(source.bitmapStride) <
                        static_cast<std::uint64_t>(source.bitmapWidth) * 4u || required > source.bitmapBytes ||
                    required > 256ull * 1024 * 1024) return FFFResult::InvalidArgument;
                command.bitmapWidth = source.bitmapWidth; command.bitmapHeight = source.bitmapHeight;
                command.bitmapStride = source.bitmapStride;
                const auto* bytes = static_cast<const std::uint8_t*>(source.bitmapBgra);
                command.bitmap.assign(bytes, bytes + static_cast<std::size_t>(required));
            }
            layer.commands.push_back(std::move(command));
        }
        {
            std::lock_guard lock(timedTextSubmitMutex_);
            pendingTimedTextLayer_ = std::move(layer);
            if (timedTextCommandQueued_) return FFFResult::Success;
            timedTextCommandQueued_ = true;
        }
        Enqueue([this] {
            TimedTextRenderLayer latest;
            {
                std::lock_guard lock(timedTextSubmitMutex_);
                latest = std::move(pendingTimedTextLayer_);
                timedTextCommandQueued_ = false;
            }
            const auto result = videoRenderer_.SetTimedTextLayer(std::move(latest));
            if (result != FFFResult::Success) ReportError(result, videoRenderer_.LastError(), "timed-text");
            const auto* frame = displayedFrame_ != nullptr ? displayedFrame_ : coverArtFrame_;
            if (frame != nullptr && state_.load() != FFF3FPState::Playing &&
                videoRenderer_.Render(frame) != FFFResult::Success)
                ReportError(FFFResult::DeviceFailure, videoRenderer_.LastError(), "timed-text-redraw");
        });
        return FFFResult::Success;
    } catch (...) { return FFFResult::NativeFailure; }
}

FFFResult PlayerSession::GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept {
    return videoRenderer_.GetTimedTextStatus(status);
}

FFFResult PlayerSession::GetSnapshot(FFF3FPSnapshot& output) const noexcept {
    if (output.size < sizeof(FFF3FPSnapshot) || output.version != 2) return FFFResult::InvalidArgument;
    { std::lock_guard lock(snapshotMutex_); output = publishedSnapshot_; }
    if (output.state == FFF3FPState::Playing) {
        output.position100ns = playbackPosition100ns_.load();
        if (output.duration100ns > 0) output.position100ns = std::min(output.position100ns, output.duration100ns);
    }
    return FFFResult::Success;
}
std::string PlayerSession::MediaInfo() const { std::lock_guard lock(mutex_); return mediaInfoJson_; }
std::string PlayerSession::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }

void PlayerSession::Worker() noexcept {
    for (;;) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            if (commands_.empty() && state_.load() != FFF3FPState::Playing)
                commandCondition_.wait(lock, [this] { return terminate_ || !commands_.empty() || state_.load() == FFF3FPState::Playing; });
            if (terminate_) break;
            if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
        }
        if (command) { try { command(); } catch (...) { Fail(FFFResult::NativeFailure, "An unhandled player command exception occurred."); } }
        else PumpPlayback();
    }
    DoClose(FFF3FPState::Closed);
}

FFFResult PlayerSession::OpenFormat(const std::string& path, AVFormatContext** output,
    std::string& error) noexcept {
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file,crypto,data", 0);
    av_dict_set(&options, "protocol_blacklist", "http,https,tcp,tls,udp,rtp,rtsp,srt,rist,ftp,ssh", 0);
    av_dict_set(&options, "ignore_loop", "0", 0);
    auto result = avformat_open_input(output, path.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (result < 0) { error = "Could not open local media: " + FfmpegError(result); return FFFResult::FfmpegFailure; }
    if ((*output)->iformat == nullptr || ((*output)->iformat->flags & AVFMT_NOFILE) != 0 ||
        std::string((*output)->iformat->name).find("hls") != std::string::npos ||
        std::string((*output)->iformat->name).find("dash") != std::string::npos) {
        avformat_close_input(output); error = "Network and virtual-device demuxers are disabled."; return FFFResult::NotSupported;
    }
    result = avformat_find_stream_info(*output, nullptr);
    if (result < 0) { avformat_close_input(output); error = "Could not read media streams: " + FfmpegError(result); return FFFResult::FfmpegFailure; }
    return FFFResult::Success;
}

FFFResult PlayerSession::OpenDecoder(AVFormatContext* owner, const std::int32_t index, const bool video,
    AVCodecContext** output, const std::int32_t hardwareDeviceType,
    std::int32_t* hardwarePixelFormat, const bool useConfiguredHardware) noexcept {
    if (owner == nullptr || index < 0 || index >= static_cast<std::int32_t>(owner->nb_streams)) return FFFResult::InvalidArgument;
    auto* stream = owner->streams[index];
    if (stream->codecpar->codec_type != (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO)) return FFFResult::InvalidArgument;
    AVPixelFormat selectedHardwareFormat = AV_PIX_FMT_NONE;
    const auto deviceType = static_cast<AVHWDeviceType>(hardwareDeviceType);
    const auto* codec = video && useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11
        ? FindHardwareDecoder(stream->codecpar->codec_id, deviceType, selectedHardwareFormat)
        : video && stream->codecpar->codec_id == AV_CODEC_ID_AV1
            ? avcodec_find_decoder_by_name("libdav1d") : nullptr;
    if (codec == nullptr && !(video && useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11))
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) return FFFResult::NotSupported;
    auto* context = avcodec_alloc_context3(codec);
    if (context == nullptr) return FFFResult::NativeFailure;
    auto result = avcodec_parameters_to_context(context, stream->codecpar);
    context->pkt_timebase = stream->time_base;
    if (result >= 0 && video && !(useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11)) {
        context->thread_count = 0;
        context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    if (result >= 0 && video && useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11) {
        AVBufferRef* hardwareDevice = nullptr;
        result = av_hwdevice_ctx_create(&hardwareDevice, deviceType, nullptr, nullptr, 0);
        if (result >= 0) {
            context->hw_device_ctx = av_buffer_ref(hardwareDevice);
            context->opaque = reinterpret_cast<void*>(static_cast<std::intptr_t>(selectedHardwareFormat));
            context->get_format = SelectHardwareFormat;
        }
        av_buffer_unref(&hardwareDevice);
    }
    if (result >= 0) result = avcodec_open2(context, codec, nullptr);
    if (result < 0) { avcodec_free_context(&context); return video && useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11 ? FFFResult::NotSupported : FFFResult::FfmpegFailure; }
    *output = context;
    if (hardwarePixelFormat != nullptr) *hardwarePixelFormat = selectedHardwareFormat;
    return FFFResult::Success;
}

FFFResult PlayerSession::LoadCoverArt() noexcept {
    if (format_ == nullptr || coverArtStream_ < 0 ||
        coverArtStream_ >= static_cast<std::int32_t>(format_->nb_streams)) return FFFResult::InvalidState;
    auto* stream = format_->streams[coverArtStream_];
    if (stream->attached_pic.size <= 0) return FFFResult::NotSupported;
    AVCodecContext* decoder = nullptr;
    auto result = OpenDecoder(format_, coverArtStream_, true, &decoder, -1, nullptr, false);
    if (result != FFFResult::Success) return result;
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) { avcodec_free_context(&decoder); return FFFResult::NativeFailure; }
    auto decodeResult = avcodec_send_packet(decoder, &stream->attached_pic);
    if (decodeResult >= 0) decodeResult = avcodec_receive_frame(decoder, frame);
    if (decodeResult == AVERROR(EAGAIN)) {
        avcodec_send_packet(decoder, nullptr);
        decodeResult = avcodec_receive_frame(decoder, frame);
    }
    if (decodeResult < 0) result = FFFResult::FfmpegFailure;
    else {
        if (coverArtFrame_ != nullptr) av_frame_free(&coverArtFrame_);
        coverArtFrame_ = av_frame_clone(frame);
        if (coverArtFrame_ == nullptr) result = FFFResult::NativeFailure;
        else result = videoRenderer_.Render(coverArtFrame_);
    }
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    return result;
}

FFFResult PlayerSession::OpenHardwareVideoDecoder(AVFormatContext* owner, const std::int32_t index,
    AVCodecContext** output) noexcept {
    if (owner == nullptr || index < 0 || index >= static_cast<std::int32_t>(owner->nb_streams))
        return FFFResult::InvalidArgument;
    const AVHWDeviceType Backends[] = {
        // CUDA selects NVIDIA's CUVID/NVDEC wrapper when available.  D3D11VA
        // remains the hardware fallback for systems without an NVIDIA backend.
        AV_HWDEVICE_TYPE_CUDA,
        AV_HWDEVICE_TYPE_D3D11VA,
    };
    for (const auto backend : Backends) {
        AVCodecContext* candidate = nullptr;
        std::int32_t hardwareFormat = AV_PIX_FMT_NONE;
        if (OpenDecoder(owner, index, true, &candidate, backend, &hardwareFormat) != FFFResult::Success) continue;
        const auto probeResult = ProbeHardwareVideo(owner, candidate, index, hardwareFormat);
        if (probeResult == FFFResult::Success) {
            *output = candidate;
            return FFFResult::Success;
        }
        avcodec_free_context(&candidate);
    }
    return FFFResult::NotSupported;
}

void PlayerSession::DoOpen(std::string path) noexcept {
    DoClose(FFF3FPState::Opening);
    std::string openError;
    const auto openResult = OpenFormat(path, &format_, openError);
    if (openResult != FFFResult::Success) { Fail(openResult, std::move(openError), "open"); return; }
    videoStream_ = FindTimedVideoStream(format_);
    coverArtStream_ = FindCoverArtStream(format_);
    audioStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, videoStream_, nullptr, 0);
    if (videoStream_ < 0 && audioStream_ < 0) { Fail(FFFResult::NotSupported, "The file contains no playable video or audio stream.", "open"); return; }
    if (videoStream_ >= 0) {
        const auto result = decodeMode_ == FFF3FPDecodeMode::D3D11
            ? OpenHardwareVideoDecoder(format_, videoStream_, &videoDecoder_)
            : OpenDecoder(format_, videoStream_, true, &videoDecoder_);
        if (result != FFFResult::Success) { Fail(result, decodeMode_ == FFF3FPDecodeMode::D3D11 ? "D3D11VA and CUDA/NVDEC rejected the selected codec profile or pixel format; CPU fallback is disabled." : "Could not open the video decoder.", "open"); return; }
    }
    if (audioStream_ >= 0 && OpenDecoder(format_, audioStream_, false, &audioDecoder_) != FFFResult::Success) audioStream_ = -1;
    if (audioStream_ >= 0) {
        audioRenderer_ = std::make_unique<PlayerWasapiRenderer>(audioEndpointId_);
        const auto result = audioRenderer_->Start();
        if (result != FFFResult::Success) { Fail(result, audioRenderer_->LastError(), "open-audio"); return; }
        audioRenderer_->SetVolume(volume_, muted_);
        audioRenderer_->SetPaused(true);
    }
    if (videoStream_ < 0 && coverArtStream_ >= 0) {
        const auto result = LoadCoverArt();
        if (result != FFFResult::Success) { Fail(result, "Could not decode or render the attached cover art.", "cover-art"); return; }
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.duration100ns = format_->duration > 0 && !IsLoopAwareImageDemuxer(format_->iformat)
            ? av_rescale(format_->duration, 10, 1) : 0;
        snapshot_.position100ns = 0; snapshot_.frameIndex = -1; snapshot_.framePts = AV_NOPTS_VALUE;
        snapshot_.selectedVideoStream = videoStream_; snapshot_.selectedAudioStream = audioStream_;
        snapshot_.videoWidth = snapshot_.videoHeight = 0; snapshot_.isHdrSource = 0;
        if (videoStream_ >= 0) { snapshot_.videoWidth = videoDecoder_->width; snapshot_.videoHeight = videoDecoder_->height; const auto* parameters = format_->streams[videoStream_]->codecpar; snapshot_.isHdrSource = parameters->color_trc == AVCOL_TRC_SMPTE2084 || parameters->color_trc == AVCOL_TRC_ARIB_STD_B67; }
        else if (coverArtFrame_ != nullptr) { snapshot_.videoWidth = coverArtFrame_->width; snapshot_.videoHeight = coverArtFrame_->height; snapshot_.isHdrSource = 0; }
    }
    framePtsIndex_.clear(); seekTarget100ns_ = -1; seekTargetFrame_ = -1; keyframeSeekPending_ = false;
    lastVideoFrameDuration100ns_ = 0; draining_ = false;
    RebuildMediaInfo(); SetState(FFF3FPState::Ready, "open");
    Emit(FFF3FPEvent::OpenCompleted, "{\"success\":true}");
    if (snapshot_.requestedColorMode == FFF3FPColorMode::MapToHdr && snapshot_.actualColorMode != snapshot_.requestedColorMode)
        Emit(FFF3FPEvent::ColorModeChanged, "{\"requested\":2,\"actual\":0,\"reason\":\"" + EscapeJson(videoRenderer_.FallbackReason()) + "\"}");
}

FFFResult PlayerSession::ProbeHardwareVideo(AVFormatContext* owner, AVCodecContext* decoder,
    const std::int32_t streamIndex, const std::int32_t hardwarePixelFormat) noexcept {
    if (owner == nullptr || decoder == nullptr || streamIndex < 0) return FFFResult::InvalidState;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) { av_packet_free(&packet); av_frame_free(&frame); return FFFResult::NativeFailure; }
    FFFResult result = FFFResult::NotSupported;
    for (int packetCount = 0; packetCount < 512 && av_read_frame(owner, packet) >= 0; ++packetCount) {
        if (packet->stream_index != streamIndex) { av_packet_unref(packet); continue; }
        auto decodeResult = avcodec_send_packet(decoder, packet);
        av_packet_unref(packet);
        if (decodeResult < 0 && decodeResult != AVERROR(EAGAIN)) break;
        while ((decodeResult = avcodec_receive_frame(decoder, frame)) >= 0) {
            result = frame->format == hardwarePixelFormat ? FFFResult::Success : FFFResult::NotSupported;
            av_frame_unref(frame); break;
        }
        if (result == FFFResult::Success) break;
        if (decodeResult < 0 && decodeResult != AVERROR(EAGAIN)) break;
    }
    av_packet_free(&packet); av_frame_free(&frame);
    const auto* stream = owner->streams[streamIndex];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    av_seek_frame(owner, streamIndex, start, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(decoder);
    return result;
}

void PlayerSession::PumpPlayback() noexcept {
    if (format_ == nullptr) { SetState(FFF3FPState::Failed); return; }
    playbackPosition100ns_.store(ClockPosition());
    if (PumpVideoPresentation()) return;
    if (videoStream_ < 0 && audioRenderer_ && audioRenderer_->Buffered100ns() > TicksPerSecond) { Sleep(2); return; }
    if (!draining_ && externalFormat_ != nullptr && audioRenderer_ && audioRenderer_->Buffered100ns() < 5'000'000) PumpExternalAudio();
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) { Fail(FFFResult::NativeFailure, "Could not allocate a playback packet."); return; }
    const auto result = av_read_frame(format_, packet);
    if (result < 0) {
        av_packet_free(&packet); FlushAtEnd(); return;
    }
    if (packet->stream_index == videoStream_) DecodePacket(videoDecoder_, packet, true, format_);
    else if (packet->stream_index == audioStream_ && externalFormat_ == nullptr) DecodePacket(audioDecoder_, packet, false, format_);
    av_packet_free(&packet);
}

FFFResult PlayerSession::DecodePacket(AVCodecContext* decoder, AVPacket* packet, const bool video,
    AVFormatContext* owner) noexcept {
    if (decoder == nullptr) return FFFResult::Success;
    if (packet != nullptr && packet->size == 0 && packet->side_data_elems == 0)
        return FFFResult::Success;
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) return FFFResult::NativeFailure;
    const auto handleFrame = [this, video, owner, decoder](AVFrame* decoded) {
        if (video) {
            const auto seeking = seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_;
            if (state_.load() == FFF3FPState::Playing && !seeking) QueueVideoFrame(decoded);
            else PresentVideoFrame(decoded, owner);
        } else QueueAudioFrame(decoded, owner, owner == format_ ? audioStream_ : externalAudioStream_);
    };
    auto receiveFrames = [&]() {
        int receiveResult = 0;
        while ((receiveResult = avcodec_receive_frame(decoder, frame)) >= 0) {
            handleFrame(frame);
            av_frame_unref(frame);
        }
        return receiveResult;
    };
    auto result = avcodec_send_packet(decoder, packet);
    if (result == AVERROR(EAGAIN)) {
        const auto receiveResult = receiveFrames();
        if (receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF) {
            av_frame_free(&frame);
            Fail(FFFResult::FfmpegFailure, "Decoder failed while making room for a packet: " +
                FfmpegError(receiveResult));
            return FFFResult::FfmpegFailure;
        }
        result = avcodec_send_packet(decoder, packet);
    }
    if (result < 0 && result != AVERROR_EOF) {
        av_frame_free(&frame);
        Fail(FFFResult::FfmpegFailure, "Decoder rejected packet: " + FfmpegError(result));
        return FFFResult::FfmpegFailure;
    }
    result = receiveFrames();
    av_frame_free(&frame);
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF ? FFFResult::Success : FFFResult::FfmpegFailure;
}

bool PlayerSession::PumpVideoPresentation() noexcept {
    if (videoFrameQueue_.empty()) return false;
    auto* frame = videoFrameQueue_.front();
    const auto position = VideoFramePosition(frame);
    const auto now = ClockPosition();
    if (position <= now + 20'000) {
        videoFrameQueue_.pop_front();
        PresentVideoFrame(frame, format_);
        av_frame_free(&frame);
        return true;
    }
    if (videoFrameQueue_.size() >= 12) {
        std::unique_lock lock(mutex_);
        if (commands_.empty() && !terminate_) commandCondition_.wait_for(lock, std::chrono::milliseconds(1));
        return true;
    }
    return false;
}

void PlayerSession::QueueVideoFrame(AVFrame* frame) noexcept {
    auto* queued = av_frame_clone(frame);
    if (queued == nullptr) {
        Fail(FFFResult::NativeFailure, "Could not queue the decoded video frame.", "decode");
        return;
    }
    videoFrameQueue_.push_back(queued);
    ++snapshot_.decodedVideoFrames;
    snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
}

void PlayerSession::ClearVideoQueue() noexcept {
    for (auto*& frame : videoFrameQueue_) av_frame_free(&frame);
    videoFrameQueue_.clear();
    snapshot_.queuedVideoFrames = 0;
}

std::int64_t PlayerSession::VideoFramePosition(const AVFrame* frame) const noexcept {
    if (frame == nullptr || format_ == nullptr || videoStream_ < 0) return snapshot_.position100ns;
    const auto* stream = format_->streams[videoStream_];
    const auto pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) return snapshot_.position100ns;
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    return av_rescale_q(pts - start, stream->time_base,
        AVRational{1, static_cast<int>(TicksPerSecond)});
}

void PlayerSession::PresentVideoFrame(AVFrame* frame, AVFormatContext* owner) noexcept {
    auto* stream = owner->streams[videoStream_];
    const auto pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const auto position = pts == AV_NOPTS_VALUE ? snapshot_.position100ns :
        av_rescale_q(pts - start, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)});
    if (frame->duration > 0) {
        lastVideoFrameDuration100ns_ = av_rescale_q(frame->duration, stream->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)});
    } else if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        lastVideoFrameDuration100ns_ = av_rescale_q(1, av_inv_q(stream->avg_frame_rate),
            AVRational{1, static_cast<int>(TicksPerSecond)});
    }
    auto nextIndex = snapshot_.frameIndex + 1;
    if (nextIndex < 0) nextIndex = 0;
    if (static_cast<std::size_t>(nextIndex) >= framePtsIndex_.size() && pts != AV_NOPTS_VALUE) framePtsIndex_.push_back(pts);
    const auto fulfillingSeek = seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_;
    if ((!keyframeSeekPending_ && seekTarget100ns_ >= 0 && position < seekTarget100ns_) ||
        (seekTargetFrame_ >= 0 && nextIndex < seekTargetFrame_)) { snapshot_.frameIndex = nextIndex; return; }

    if (keyframeSeekPending_) {
        keyframeSeekPending_ = false;
        seekTarget100ns_ = -1;
        seekTargetFrame_ = -1;
        ResetClock(position);
        if (audioRenderer_) audioRenderer_->Reset(position);
    }

    const auto frameDuration = std::max<std::int64_t>(0, lastVideoFrameDuration100ns_);
    const auto lateTolerance = std::max<std::int64_t>(frameDuration * 2, 500'000);
    if (state_.load() == FFF3FPState::Playing && !fulfillingSeek &&
        position + lateTolerance < ClockPosition()) {
        snapshot_.frameIndex = nextIndex;
        ++snapshot_.droppedVideoFrames;
        snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
        PublishSnapshot();
        return;
    }

    AVFrame* renderFrame = frame;
    AVFrame* transferred = nullptr;
    if (IsHardwareFrame(frame)) {
        transferred = av_frame_alloc();
        if (transferred == nullptr || av_hwframe_transfer_data(transferred, frame, 0) < 0) {
            if (transferred) av_frame_free(&transferred); Fail(FFFResult::FfmpegFailure, "Could not transfer the hardware-decoded frame for presentation."); return;
        }
        av_frame_copy_props(transferred, frame);
        renderFrame = transferred;
    }
    const auto previousColorMode = snapshot_.actualColorMode;
    const auto renderResult = videoRenderer_.Render(renderFrame);
    AVFrame* displayed = renderResult == FFFResult::Success ? av_frame_clone(renderFrame) : nullptr;
    if (transferred) av_frame_free(&transferred);
    if (renderResult != FFFResult::Success) { Fail(renderResult, videoRenderer_.LastError(), "render"); return; }
    if (displayed == nullptr) { Fail(FFFResult::NativeFailure, "Could not retain the displayed video frame.", "render"); return; }
    if (displayedFrame_ != nullptr) av_frame_free(&displayedFrame_);
    displayedFrame_ = displayed;
    ++snapshot_.presentedVideoFrames;
    snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
    {
        std::lock_guard lock(mutex_);
        snapshot_.position100ns = position; snapshot_.frameIndex = nextIndex; snapshot_.framePts = pts;
        snapshot_.frameTimeBaseNumerator = stream->time_base.num; snapshot_.frameTimeBaseDenominator = stream->time_base.den;
        snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    }
    PublishSnapshot();
    if (snapshot_.actualColorMode != previousColorMode) {
        std::ostringstream json; json << "{\"requested\":" << static_cast<unsigned>(snapshot_.requestedColorMode)
            << ",\"actual\":" << static_cast<unsigned>(snapshot_.actualColorMode) << ",\"reason\":\""
            << EscapeJson(videoRenderer_.FallbackReason()) << "\"}";
        Emit(FFF3FPEvent::ColorModeChanged, json.str());
    }
    seekTarget100ns_ = -1; seekTargetFrame_ = -1; keyframeSeekPending_ = false;
}

void PlayerSession::QueueAudioFrame(AVFrame* frame, AVFormatContext* owner, const std::int32_t streamIndex) noexcept {
    if (!audioRenderer_ || streamIndex < 0) return;
    auto* stream = owner->streams[streamIndex];
    const auto pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    auto position = AV_NOPTS_VALUE;
    if (pts != AV_NOPTS_VALUE) {
        const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        position = av_rescale_q(pts - start, stream->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)});
        if (owner == externalFormat_) position += externalAudioOffset100ns_;
    }
    if (pts != AV_NOPTS_VALUE && seekTarget100ns_ >= 0) {
        if (position + av_rescale(frame->nb_samples, TicksPerSecond, frame->sample_rate) < seekTarget100ns_) return;
    }
    const auto result = audioRenderer_->Enqueue(frame, position);
    if (result != FFFResult::Success && result != FFFResult::BufferTooSmall) Fail(result, audioRenderer_->LastError(), "audio-render");
}

void PlayerSession::PumpExternalAudio() noexcept {
    if (externalFormat_ == nullptr || externalAudioDecoder_ == nullptr) return;
    if (snapshot_.position100ns < externalAudioOffset100ns_) return;
    AVPacket* packet = av_packet_alloc(); if (!packet) return;
    while (av_read_frame(externalFormat_, packet) >= 0) {
        if (packet->stream_index == externalAudioStream_) { DecodePacket(externalAudioDecoder_, packet, false, externalFormat_); av_packet_unref(packet); break; }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
}

void PlayerSession::FlushAtEnd() noexcept {
    if (!draining_) {
        draining_ = true;
        if (videoDecoder_) DecodePacket(videoDecoder_, nullptr, true, format_);
        if (audioDecoder_ && externalFormat_ == nullptr) DecodePacket(audioDecoder_, nullptr, false, format_);
    }
    if (audioRenderer_ && audioRenderer_->Buffered100ns() > 0) { Sleep(2); return; }
    auto endPosition = snapshot_.position100ns;
    if (videoStream_ >= 0 && lastVideoFrameDuration100ns_ > 0)
        endPosition += lastVideoFrameDuration100ns_;
    if (audioRenderer_) endPosition = std::max(endPosition, audioRenderer_->Position100ns());
    snapshot_.duration100ns = std::max(snapshot_.duration100ns, endPosition);
    snapshot_.position100ns = snapshot_.duration100ns;
    RebuildMediaInfo();
    if (audioRenderer_) audioRenderer_->SetPaused(true);
    SetState(FFF3FPState::Ended, "end"); Emit(FFF3FPEvent::PlaybackEnded, "{}");
}

void PlayerSession::DoSeek(std::int64_t position, const std::int64_t targetFrame,
    const bool exact) noexcept {
    if (!format_) return; position = std::clamp<std::int64_t>(position, 0, snapshot_.duration100ns > 0 ? snapshot_.duration100ns : position);
    const auto referenceStream = videoStream_ >= 0 ? videoStream_ : audioStream_;
    auto timestamp = av_rescale_q(position, AVRational{1, static_cast<int>(TicksPerSecond)}, format_->streams[referenceStream]->time_base);
    if (format_->streams[referenceStream]->start_time != AV_NOPTS_VALUE) timestamp += format_->streams[referenceStream]->start_time;
    if (av_seek_frame(format_, referenceStream, timestamp, AVSEEK_FLAG_BACKWARD) < 0) { Fail(FFFResult::FfmpegFailure, "FFmpeg could not seek to the requested position.", "seek"); return; }
    ClearVideoQueue();
    if (videoDecoder_) avcodec_flush_buffers(videoDecoder_); if (audioDecoder_) avcodec_flush_buffers(audioDecoder_);
    seekTarget100ns_ = position; seekTargetFrame_ = targetFrame;
    keyframeSeekPending_ = !exact && videoStream_ >= 0; draining_ = false;
    lastVideoFrameDuration100ns_ = 0;
    snapshot_.position100ns = position;
    snapshot_.frameIndex = targetFrame >= 0 && position > 0 ? targetFrame - 1 : -1;
    PublishSnapshot();
    ResetClock(position); if (audioRenderer_) audioRenderer_->Reset(position);
    if (externalFormat_ && externalAudioStream_ >= 0) {
        auto externalPosition = std::max<std::int64_t>(0, position - externalAudioOffset100ns_);
        auto* stream = externalFormat_->streams[externalAudioStream_];
        auto externalTimestamp = av_rescale_q(externalPosition, AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base);
        if (stream->start_time != AV_NOPTS_VALUE) externalTimestamp += stream->start_time;
        av_seek_frame(externalFormat_, externalAudioStream_, externalTimestamp, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(externalAudioDecoder_);
    }
}

void PlayerSession::DecodeUntilSeekTarget() noexcept {
    if (format_ == nullptr || videoDecoder_ == nullptr) return;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) return;
    while ((seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_) && !terminate_) {
        if (av_read_frame(format_, packet) < 0) break;
        if (packet->stream_index == videoStream_) DecodePacket(videoDecoder_, packet, true, format_);
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
}

void PlayerSession::DoSelectStream(const std::int32_t index, const bool video) noexcept {
    if (!format_ || index < 0 || index >= static_cast<std::int32_t>(format_->nb_streams) ||
        format_->streams[index]->codecpar->codec_type != (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO) ||
        (video && (format_->streams[index]->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0)) { ReportError(FFFResult::InvalidArgument, "The requested media stream is invalid.", "select-stream"); return; }
    AVCodecContext* replacement = nullptr;
    const auto result = video && decodeMode_ == FFF3FPDecodeMode::D3D11
        ? OpenHardwareVideoDecoder(format_, index, &replacement)
        : OpenDecoder(format_, index, video, &replacement);
    if (result != FFFResult::Success) { ReportError(result, "Could not open the selected media stream.", "select-stream"); return; }
    if (video) { if (videoDecoder_) avcodec_free_context(&videoDecoder_); videoDecoder_ = replacement; videoStream_ = index; snapshot_.selectedVideoStream = index; framePtsIndex_.clear(); }
    else { if (audioDecoder_) avcodec_free_context(&audioDecoder_); audioDecoder_ = replacement; audioStream_ = index; snapshot_.selectedAudioStream = index; }
    DoSeek(snapshot_.position100ns); RebuildMediaInfo();
    Emit(FFF3FPEvent::OperationCompleted, std::string("{\"operation\":\"select-") + (video ? "video" : "audio") + "\",\"stream\":" + std::to_string(index) + "}");
}

void PlayerSession::DoLoadExternalAudio(std::string path, const std::int32_t requestedIndex, const std::int64_t offset) noexcept {
    AVFormatContext* replacementFormat = nullptr;
    std::string openError;
    const auto openResult = OpenFormat(path, &replacementFormat, openError);
    if (openResult != FFFResult::Success) { ReportError(openResult, std::move(openError), "external-audio"); return; }
    auto index = requestedIndex;
    if (index < 0) index = av_find_best_stream(replacementFormat, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVCodecContext* replacementDecoder = nullptr;
    const auto result = OpenDecoder(replacementFormat, index, false, &replacementDecoder);
    if (result != FFFResult::Success) { avformat_close_input(&replacementFormat); ReportError(result, "Could not open the external audio stream.", "external-audio"); return; }
    if (!audioRenderer_) { audioRenderer_ = std::make_unique<PlayerWasapiRenderer>(audioEndpointId_); if (audioRenderer_->Start() != FFFResult::Success) { avcodec_free_context(&replacementDecoder); avformat_close_input(&replacementFormat); ReportError(FFFResult::DeviceFailure, audioRenderer_->LastError(), "external-audio"); audioRenderer_.reset(); return; } audioRenderer_->SetVolume(volume_, muted_); }
    if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); if (externalFormat_) avformat_close_input(&externalFormat_);
    externalFormat_ = replacementFormat; externalAudioDecoder_ = replacementDecoder; externalAudioStream_ = index;
    externalAudioOffset100ns_ = offset; externalAudioPath_ = std::move(path);
    snapshot_.isExternalAudio = 1; snapshot_.externalAudioOffset100ns = offset; DoSeek(snapshot_.position100ns);
    if (audioRenderer_) audioRenderer_->SetPaused(snapshot_.state != FFF3FPState::Playing);
    Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"load-external-audio\",\"stream\":" + std::to_string(index) + "}");
}

void PlayerSession::DoClose(const FFF3FPState finalState) noexcept {
    if (audioRenderer_) { audioRenderer_->Stop(); audioRenderer_.reset(); }
    videoRenderer_.Close();
    if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); if (externalFormat_) avformat_close_input(&externalFormat_);
    if (videoDecoder_) avcodec_free_context(&videoDecoder_); if (audioDecoder_) avcodec_free_context(&audioDecoder_);
    if (coverArtFrame_) av_frame_free(&coverArtFrame_);
    if (displayedFrame_) av_frame_free(&displayedFrame_);
    ClearVideoQueue();
    if (format_) avformat_close_input(&format_);
    videoStream_ = audioStream_ = coverArtStream_ = externalAudioStream_ = -1; externalAudioPath_.clear(); framePtsIndex_.clear();
    externalAudioOffset100ns_ = 0; seekTarget100ns_ = seekTargetFrame_ = -1;
    keyframeSeekPending_ = false; lastVideoFrameDuration100ns_ = 0; draining_ = false;
    {
        std::lock_guard lock(mutex_); snapshot_.state = finalState; snapshot_.position100ns = 0; snapshot_.duration100ns = 0;
        snapshot_.frameIndex = -1; snapshot_.selectedVideoStream = -1; snapshot_.selectedAudioStream = -1;
        snapshot_.videoWidth = snapshot_.videoHeight = 0; snapshot_.isHdrSource = 0;
        snapshot_.isExternalAudio = 0; snapshot_.externalAudioOffset100ns = 0;
        snapshot_.decodedVideoFrames = snapshot_.presentedVideoFrames = snapshot_.droppedVideoFrames = 0;
        snapshot_.queuedVideoFrames = 0;
        snapshot_.actualColorMode = FFF3FPColorMode::MapToSdr; mediaInfoJson_.clear();
    }
    state_.store(finalState);
    PublishSnapshot();
    if (finalState == FFF3FPState::Closed || finalState == FFF3FPState::Idle) {
        Emit(FFF3FPEvent::StateChanged, "{\"state\":" +
            std::to_string(static_cast<unsigned>(finalState)) +
            (finalState == FFF3FPState::Idle ? ",\"operation\":\"stop\"}" : "}"));
    }
}

void PlayerSession::RebuildMediaInfo() noexcept {
    if (!format_) return;
    std::ostringstream json; json << "{\"format\":\"" << EscapeJson(format_->iformat ? format_->iformat->name : "")
        << "\",\"duration100ns\":" << snapshot_.duration100ns << ",\"streams\":[";
    for (unsigned index = 0; index < format_->nb_streams; ++index) {
        if (index) json << ','; const auto* stream = format_->streams[index]; const auto* parameters = stream->codecpar;
        const auto* descriptor = avcodec_descriptor_get(parameters->codec_id);
        json << "{\"index\":" << index << ",\"type\":\"" << MediaTypeName(parameters->codec_type)
             << "\",\"codec\":\"" << EscapeJson(descriptor ? descriptor->name : "unknown") << "\",\"timeBaseNumerator\":"
             << stream->time_base.num << ",\"timeBaseDenominator\":" << stream->time_base.den;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) json << ",\"width\":" << parameters->width << ",\"height\":" << parameters->height << ",\"hdr\":" << ((parameters->color_trc == AVCOL_TRC_SMPTE2084 || parameters->color_trc == AVCOL_TRC_ARIB_STD_B67) ? "true" : "false") << ",\"attachedPicture\":" << ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0 ? "true" : "false");
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) json << ",\"sampleRate\":" << parameters->sample_rate << ",\"channels\":" << parameters->ch_layout.nb_channels;
        AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0);
        AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
        json << ",\"language\":\"" << EscapeJson(language ? language->value : "") << "\",\"title\":\"" << EscapeJson(title ? title->value : "") << "\"}";
    }
    json << "]}"; std::lock_guard lock(mutex_); mediaInfoJson_ = json.str();
}

void PlayerSession::SetState(const FFF3FPState state, const char* operation) noexcept {
    snapshot_.state = state;
    state_.store(state);
    PublishSnapshot();
    std::string json = "{\"state\":" + std::to_string(static_cast<unsigned>(state));
    if (operation) json += ",\"operation\":\"" + EscapeJson(operation) + "\""; json += '}';
    Emit(FFF3FPEvent::StateChanged, json);
}

void PlayerSession::Fail(const FFFResult result, std::string message, const char* operation) noexcept {
    try { if (audioRenderer_) audioRenderer_->SetPaused(true); snapshot_.state = FFF3FPState::Failed; state_.store(FFF3FPState::Failed); PublishSnapshot();
        ReportError(result, std::move(message), operation); }
    catch (...) {}
}

void PlayerSession::ReportError(const FFFResult result, std::string message, const char* operation) noexcept {
    try {
        { std::lock_guard lock(errorMutex_); lastError_ = message; }
        std::string json = "{\"result\":" + std::to_string(static_cast<int>(result)) +
            ",\"message\":\"" + EscapeJson(message) + "\"";
        if (operation) json += ",\"operation\":\"" + EscapeJson(operation) + "\"";
        json += '}';
        Emit(FFF3FPEvent::Error, json);
    } catch (...) {}
}

void PlayerSession::Emit(const FFF3FPEvent event, const std::string& json) const noexcept {
    if (callback_ == nullptr) return; try { callback_(callbackContext_, event, json.c_str()); } catch (...) {}
}

std::int64_t PlayerSession::ClockPosition() const noexcept {
    if (audioRenderer_ && (audioStream_ >= 0 || externalAudioStream_ >= 0)) {
        const auto audioPosition = audioRenderer_->Position100ns();
        if (externalFormat_ == nullptr || audioPosition > clockOriginPosition100ns_.load() ||
            audioRenderer_->Buffered100ns() > 0) {
            playbackPosition100ns_.store(audioPosition);
            return audioPosition;
        }
    }
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const auto origin = clockOriginQpc_.load(); if (origin == 0 || qpcFrequency_ <= 0) return clockOriginPosition100ns_.load();
    const auto position = clockOriginPosition100ns_.load() + (now.QuadPart - origin) * TicksPerSecond / qpcFrequency_;
    playbackPosition100ns_.store(position);
    return position;
}
void PlayerSession::ResetClock(const std::int64_t position) noexcept { LARGE_INTEGER now{}; QueryPerformanceCounter(&now); clockOriginPosition100ns_ = position; clockOriginQpc_ = now.QuadPart; playbackPosition100ns_ = position; }

void PlayerSession::PublishSnapshot() noexcept {
    std::lock_guard lock(snapshotMutex_);
    publishedSnapshot_ = snapshot_;
}

bool PlayerSession::NormalizeLocalPath(const char* path, std::string& normalized, std::string& error) noexcept {
    try {
        auto wide = FromUtf8(path); if (wide.empty()) { error = "A non-empty UTF-8 local path is required."; return false; }
        if (wide.rfind(L"\\\\", 0) == 0 || wide.rfind(L"//", 0) == 0 || wide.find(L"://") != std::wstring::npos ||
            wide.rfind(L"\\\\.\\", 0) == 0 || wide.rfind(L"\\\\?\\GLOBALROOT", 0) == 0) { error = "Network, device and pipe paths are disabled."; return false; }
        std::vector<wchar_t> full(32768); const auto length = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length == 0 || length >= full.size()) { error = "The local path is invalid."; return false; }
        const auto attributes = GetFileAttributesW(full.data());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { error = "The path must identify an existing regular local file."; return false; }
        normalized = ToUtf8(full.data()); return !normalized.empty();
    } catch (...) { error = "The local path could not be normalized."; return false; }
}
