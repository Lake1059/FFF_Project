#include "pch.h"
#include "3FP/Core/PlayerSession.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
#include <libavutil/dict.h>
#include <libavutil/samplefmt.h>
}

#include <filesystem>
#include <iomanip>
#include <limits>
#include <cmath>
#include <chrono>

namespace {
constexpr std::int64_t TicksPerSecond = 10'000'000;
constexpr std::size_t MaxQueuedVideoFrames = 8;
constexpr std::size_t MinimumQueuedVideoFrames = 3;
constexpr std::size_t MinimumMemoryBoundVideoFrames = 2;
constexpr std::size_t DecodedVideoQueueBudgetBytes = 128 * 1024 * 1024;
constexpr std::uint32_t MaximumSoftwareDecoderThreads = 8;
constexpr std::int64_t TargetVideoLookAhead100ns = 1'500'000;
// Buffered100ns includes both application PCM and samples already submitted to
// WASAPI.  Keep the complete audible queue short so seek, stream/volume changes
// and the information overlay reflect a low-latency local playback pipeline.
constexpr std::int64_t TargetAudioBuffer100ns = 1'200'000;
constexpr std::size_t MaximumIndexedVideoFrames = 32'768;
constexpr std::size_t MaximumPendingVideoPackets = 64;
constexpr std::size_t MaximumPendingVideoPacketBytes = 16 * 1024 * 1024;
constexpr std::size_t MaximumPendingAudioPackets = 512;
constexpr std::size_t MaximumPendingAudioPacketBytes = 8 * 1024 * 1024;

std::size_t EstimateDecodedFrameBytes(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return 0;
    auto format = static_cast<AVPixelFormat>(frame->format);
    if (frame->hw_frames_ctx != nullptr) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        if (frames != nullptr) format = frames->sw_format;
    }
    const auto size = av_image_get_buffer_size(format, frame->width, frame->height, 1);
    if (size > 0) return static_cast<std::size_t>(size);
    const auto pixels = static_cast<std::uint64_t>(frame->width) * frame->height;
    return static_cast<std::size_t>(std::min<std::uint64_t>(
        pixels * 4u, std::numeric_limits<std::size_t>::max()));
}

std::size_t VideoFrameQueueLimit(const std::deque<AVFrame*>& queue) noexcept {
    if (queue.empty()) return MaxQueuedVideoFrames;
    const auto bytesPerFrame = EstimateDecodedFrameBytes(queue.back());
    if (bytesPerFrame == 0) return MaxQueuedVideoFrames;
    const auto memoryBound = DecodedVideoQueueBudgetBytes / bytesPerFrame;
    return std::clamp(memoryBound, MinimumMemoryBoundVideoFrames, MaxQueuedVideoFrames);
}

void ApplyHdrState(FFF3FPSnapshot& snapshot, const HdrFrameState& hdr) noexcept {
    const auto toUnsigned = [](const float value, const float scale = 1.0f) {
        if (!std::isfinite(value) || value <= 0.0f) return 0u;
        return static_cast<std::uint32_t>(std::lround(std::min(
            static_cast<double>(value) * scale,
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()))));
    };
    snapshot.hdrFormat = hdr.format;
    snapshot.compatibleHdrFormats = hdr.compatibility;
    snapshot.hdrProcessingPath = hdr.processingPath;
    snapshot.dolbyVisionProfile = hdr.dolbyVisionProfile;
    snapshot.dolbyVisionLevel = hdr.dolbyVisionLevel;
    snapshot.hasDolbyVisionRpu = hdr.hasRpu ? 1u : 0u;
    snapshot.hasDolbyVisionEnhancementLayer = hdr.hasEnhancementLayer ? 1u : 0u;
    snapshot.dolbyVisionEnhancementLayer = hdr.enhancementLayer;
    snapshot.dynamicHdrMetadataActive = hdr.dynamicMetadata ? 1u : 0u;
    snapshot.hdrFallbackActive = hdr.fallback ? 1u : 0u;
    snapshot.displayMinLuminanceMilliNits = toUnsigned(hdr.display.minimumNits, 1000.0f);
    snapshot.displayPeakNits = toUnsigned(hdr.display.maximumNits);
    snapshot.displayFullFramePeakNits = toUnsigned(hdr.display.maximumFullFrameNits);
    snapshot.effectiveTargetPeakNits = toUnsigned(hdr.targetPeakNits);
    snapshot.isHdrSource = hdr.format == FFF3FPHdrFormat::Sdr ? 0u : 1u;
}

bool ReadLeb128(const std::uint8_t* data, const std::size_t size,
    std::size_t& offset, std::uint64_t& value) noexcept {
    value = 0;
    for (unsigned shift = 0; shift < 56 && offset < size; shift += 7) {
        const auto current = data[offset++];
        value |= static_cast<std::uint64_t>(current & 0x7f) << shift;
        if ((current & 0x80) == 0) return true;
    }
    return false;
}

void FilterAv1HardwareTimecodeMetadata(const AVCodecContext* decoder,
    AVPacket* packet) noexcept {
    if (decoder == nullptr || packet == nullptr || packet->data == nullptr || packet->size <= 0 ||
        decoder->codec_id != AV_CODEC_ID_AV1 || decoder->hw_device_ctx == nullptr) return;

    auto* source = packet->data;
    const auto sourceSize = static_cast<std::size_t>(packet->size);
    const auto readObu = [&source, sourceSize](std::size_t& offset, std::size_t& obuStart,
        std::size_t& obuEnd, bool& remove) noexcept {
        obuStart = offset;
        const auto header = source[offset++];
        if ((header & 0x81) != 0) return false;
        const auto type = static_cast<unsigned>((header >> 3) & 0x0f);
        const auto hasExtension = (header & 0x04) != 0;
        const auto hasSize = (header & 0x02) != 0;
        if (hasExtension) {
            if (offset >= sourceSize) return false;
            ++offset;
        }
        if (!hasSize) return false;
        std::uint64_t payloadSize = 0;
        if (!ReadLeb128(source, sourceSize, offset, payloadSize) ||
            payloadSize > sourceSize - offset) return false;
        const auto payloadStart = offset;
        obuEnd = offset + static_cast<std::size_t>(payloadSize);
        remove = false;
        if (type == 5 && payloadSize > 0) {
            std::size_t metadataOffset = payloadStart;
            std::uint64_t metadataType = 0;
            // NVIDIA's AV1 encoder can emit metadata_type=5 timecode OBUs that
            // libdav1d tolerates but FFmpeg's hardware parsers reject. Timecode
            // is not needed for presentation; retain every other metadata type,
            // including HDR CLL/MDCV and ITU-T T.35 dynamic metadata.
            remove = ReadLeb128(source, obuEnd, metadataOffset, metadataType) && metadataType == 5;
        }
        offset = obuEnd;
        return true;
    };

    std::size_t read = 0;
    std::size_t filteredSize = 0;
    bool removedTimecode = false;
    while (read < sourceSize) {
        std::size_t obuStart = 0;
        std::size_t obuEnd = 0;
        bool remove = false;
        if (!readObu(read, obuStart, obuEnd, remove)) return;
        if (remove) removedTimecode = true;
        else filteredSize += obuEnd - obuStart;
    }
    if (!removedTimecode || filteredSize == 0 || filteredSize > static_cast<std::size_t>(INT_MAX)) return;
    if (av_packet_make_writable(packet) < 0) return;
    source = packet->data;
    read = 0;
    std::size_t write = 0;
    while (read < sourceSize) {
        std::size_t obuStart = 0;
        std::size_t obuEnd = 0;
        bool remove = false;
        if (!readObu(read, obuStart, obuEnd, remove)) return;
        if (remove) continue;
        const auto obuSize = obuEnd - obuStart;
        std::memmove(packet->data + write, source + obuStart, obuSize);
        write += obuSize;
    }
    av_shrink_packet(packet, static_cast<int>(filteredSize));
}

std::uint64_t TimedTextContentKey(const std::uint64_t contentId, const char* text,
    const char* fontFamily) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](const char* value) noexcept {
        for (auto* current = reinterpret_cast<const unsigned char*>(value); *current != 0; ++current) {
            hash ^= *current;
            hash *= 1099511628211ull;
        }
        hash ^= 0xffu;
        hash *= 1099511628211ull;
    };
    for (std::size_t index = 0; index < sizeof(contentId); ++index) {
        hash ^= reinterpret_cast<const std::uint8_t*>(&contentId)[index];
        hash *= 1099511628211ull;
    }
    append(text); append(fontFamily);
    return hash == 0 ? 1 : hash;
}

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

void AppendDictionaryJson(std::ostringstream& json, const AVDictionary* dictionary) {
    json << '{';
    bool first = true;
    const AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(dictionary, "", entry, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        if (!first) json << ',';
        first = false;
        json << '"' << EscapeJson(entry->key ? entry->key : "") << "\":\""
             << EscapeJson(entry->value ? entry->value : "") << '"';
    }
    json << '}';
}

std::string ChannelLayoutName(const AVChannelLayout& layout) {
    char text[256]{};
    return av_channel_layout_describe(&layout, text, sizeof(text)) >= 0 ? text : std::string{};
}

std::string CodecTagName(const std::uint32_t tag) {
    if (tag == 0) return {};
    std::string fourcc;
    fourcc.reserve(4);
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const auto character = static_cast<unsigned char>((tag >> shift) & 0xffu);
        if (character < 0x20 || character > 0x7e) {
            fourcc.clear();
            break;
        }
        fourcc.push_back(static_cast<char>(character));
    }
    if (!fourcc.empty()) return fourcc;
    std::ostringstream value;
    value << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << tag;
    return value.str();
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

AVPixelFormat FindHardwareFormat(const AVCodec* codec, const AVHWDeviceType deviceType) noexcept {
    if (codec == nullptr) return AV_PIX_FMT_NONE;
    const auto expectedFormat = deviceType == AV_HWDEVICE_TYPE_D3D11VA
        ? AV_PIX_FMT_D3D11 : deviceType == AV_HWDEVICE_TYPE_CUDA ? AV_PIX_FMT_CUDA : AV_PIX_FMT_NONE;
    if (expectedFormat == AV_PIX_FMT_NONE) return AV_PIX_FMT_NONE;
    for (int configIndex = 0;; ++configIndex) {
        const auto* hardware = avcodec_get_hw_config(codec, configIndex);
        if (hardware == nullptr) break;
        if (hardware->device_type == deviceType && hardware->pix_fmt == expectedFormat &&
            (hardware->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0)
            return expectedFormat;
    }
    return AV_PIX_FMT_NONE;
}

bool IsDedicatedCudaDecoder(const AVCodec* codec) noexcept {
    return codec != nullptr && codec->name != nullptr &&
        std::string_view(codec->name).ends_with("_cuvid");
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

bool IsStaticImageDemuxer(const AVInputFormat* inputFormat) noexcept {
    if (inputFormat == nullptr || inputFormat->name == nullptr) return false;
    const std::string_view name(inputFormat->name);
    // FFmpeg's single-image demuxers are image2 or codec-specific *_pipe
    // demuxers. Animated image demuxers deliberately remain timed video.
    return name == "image2" || name == "image2pipe" || name == "ico" ||
        (name.size() > 5 && name.ends_with("_pipe"));
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

int PixelFormatBitDepth(const AVPixelFormat format) noexcept {
    const auto* descriptor = av_pix_fmt_desc_get(format);
    return descriptor != nullptr && descriptor->nb_components > 0 ? descriptor->comp[0].depth : 0;
}

std::string HardwareAccelerationName(const AVCodecContext* decoder) {
    if (decoder == nullptr || decoder->hw_device_ctx == nullptr || decoder->hw_device_ctx->data == nullptr)
        return {};
    const auto* context = reinterpret_cast<const AVHWDeviceContext*>(decoder->hw_device_ctx->data);
    const auto* type = av_hwdevice_get_type_name(context->type);
    if (type == nullptr) return {};
    if (context->type == AV_HWDEVICE_TYPE_D3D11VA) return "DXVA (D3D11VA)";
    return type;
}
}

PlayerSession::PlayerSession(const FFF3FPConfiguration& configuration)
    : decodeMode_(configuration.decodeMode), callback_(configuration.eventCallback),
      callbackContext_(configuration.eventCallbackContext), terminate_(false), format_(nullptr),
      playbackPacket_(nullptr), externalAudioPacket_(nullptr), videoDecodeFrame_(nullptr),
      videoTransferFrame_(nullptr), audioDecodeFrame_(nullptr), externalAudioDecodeFrame_(nullptr),
      videoDecoder_(nullptr), audioDecoder_(nullptr), videoStream_(-1),
      audioStream_(-1), coverArtStream_(-1), coverArtFrame_(nullptr), stillImageFrame_(nullptr),
      externalFormat_(nullptr), externalAudioDecoder_(nullptr),
      externalAudioStream_(-1), externalAudioOffset100ns_(0),
      videoRenderer_([this] { NotifyVideoRecovery(); }), audioExclusive_(false), volume_(1.0f), muted_(false),
      clockOriginPosition100ns_(0), clockOriginQpc_(0), playbackPosition100ns_(0),
      playbackClockSampleQpc_(0), playbackClockLimit100ns_(0), playbackClockSequence_(0),
      state_(FFF3FPState::Idle), qpcFrequency_(0), seekTarget100ns_(-1), seekTargetFrame_(-1),
      keyframeSeekPending_(false), lastVideoFrameDuration100ns_(0),
      framePtsIndexBase_(0),
      rebuildingFrameIndex_(false),
      stepScheduled_(false), stepRepeatRequested_(false), pendingStepOperation_(StepOperation::Frame),
      pendingStepDirection_(0), pendingVideoPacketBytes_(0), pendingAudioPacketBytes_(0),
      publishedBitRateSecond_(-1),
      draining_(false), staticImage_(false), hardwareFallbackPending_(false), internalAudioFailurePending_(false),
      internalAudioFailureResult_(FFFResult::Success), internalAudioDecodeErrorCount_(0) {
    snapshot_ = {};
    snapshot_.size = sizeof(snapshot_); snapshot_.version = 7; snapshot_.state = FFF3FPState::Idle;
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

void PlayerSession::NotifyAudioRestart() noexcept {
    std::lock_guard lock(mutex_);
    commandCondition_.notify_one();
}

void PlayerSession::NotifyVideoRecovery() noexcept {
    commandCondition_.notify_one();
}

FFFResult PlayerSession::Open(const char* path) noexcept {
    std::string normalized, error;
    if (!NormalizeLocalPath(path, normalized, error)) { ReportError(FFFResult::InvalidArgument, std::move(error), "open"); return FFFResult::InvalidArgument; }
    if (state_.exchange(FFF3FPState::Opening) == FFF3FPState::Opening) return FFFResult::InvalidState;
    { std::lock_guard lock(snapshotMutex_); publishedSnapshot_.state = FFF3FPState::Opening; }
    Emit(FFF3FPEvent::StateChanged, "{\"state\":1}");
    Enqueue([this, value = std::move(normalized)] { DoOpen(value); }); return FFFResult::Success;
}
FFFResult PlayerSession::Play() noexcept {
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Paused &&
        state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this] {
        const auto current = state_.load();
        if (current == FFF3FPState::Ended) DoSeek(0);
        if (ResumeAudioRenderer() != FFFResult::Success) return;
        ResetClock(snapshot_.position100ns);
        if (audioRenderer_) audioRenderer_->SetPaused(false);
        SetState(FFF3FPState::Playing, "play");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::Pause() noexcept {
    if (state_.load() != FFF3FPState::Playing) return FFFResult::InvalidState;
    Enqueue([this] {
        snapshot_.position100ns = ClockPosition();
        SuspendAudioRenderer(true);
        SetState(FFF3FPState::Paused, "pause");
    });
    return FFFResult::Success;
}
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
    Enqueue([this, value] {
        const auto end = framePtsIndexBase_ + static_cast<std::int64_t>(framePtsIndex_.size());
        if (value >= framePtsIndexBase_ && value < end) {
            const auto* stream = format_->streams[videoStream_];
            const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
            DoSeek(av_rescale_q(framePtsIndex_[static_cast<std::size_t>(value - framePtsIndexBase_)] - start,
                stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}), value);
        } else {
            // Preserve exact frame navigation outside the bounded index window.
            // Rebuild the sparse rolling index from the stream start while decoding.
            framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = true;
            DoSeek(0, value);
            // DoSeek stores targetFrame-1 so a normal exact frame seek can
            // resume at the requested ordinal. A rebuild must count from zero
            // while decoding the stream prefix toward that ordinal.
            snapshot_.frameIndex = -1;
            PublishSnapshot();
        }
        if (state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget();
        Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek-frame\",\"frame\":" +
            std::to_string(snapshot_.frameIndex) + "}");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::StepFrame(const std::int32_t direction) noexcept {
    if (direction != -1 && direction != 1) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    {
        std::lock_guard lock(snapshotMutex_);
        if ((state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
            state != FFF3FPState::Paused && state != FFF3FPState::Ended) ||
            publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState;
    }
    return ScheduleStep(StepOperation::Frame, direction);
}

FFFResult PlayerSession::ScheduleStep(const StepOperation operation,
    const std::int32_t direction) noexcept {
    try {
        {
            std::lock_guard lock(mutex_);
            if (terminate_) return FFFResult::InvalidState;
            pendingStepOperation_ = operation;
            pendingStepDirection_ = direction;
            if (stepScheduled_) {
                stepRepeatRequested_ = true;
                return FFFResult::Success;
            }
            stepScheduled_ = true;
            stepRepeatRequested_ = false;
            commands_.push_back([this] { ProcessStep(); });
        }
        commandCondition_.notify_one();
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stepScheduled_ = false;
            stepRepeatRequested_ = false;
        }
        ReportError(FFFResult::NativeFailure, "Could not queue media stepping.", "step");
        return FFFResult::NativeFailure;
    }
    return FFFResult::Success;
}

void PlayerSession::ProcessStep() noexcept {
    StepOperation operation = StepOperation::Frame;
    std::int32_t direction = 0;
    {
        std::lock_guard lock(mutex_);
        operation = pendingStepOperation_;
        direction = pendingStepDirection_;
        stepRepeatRequested_ = false;
    }
    try {
        if (operation == StepOperation::Frame) DoStepFrame(direction);
        else DoStepKeyframe(direction);
    } catch (...) {
        ReportError(FFFResult::NativeFailure, "An unhandled media-step exception occurred.", "step");
    }

    try {
        std::lock_guard lock(mutex_);
        if (!terminate_ && stepRepeatRequested_) {
            stepRepeatRequested_ = false;
            commands_.push_back([this] { ProcessStep(); });
        } else {
            stepScheduled_ = false;
        }
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stepScheduled_ = false;
            stepRepeatRequested_ = false;
        }
        ReportError(FFFResult::NativeFailure, "Could not queue the trailing media step.", "step");
    }
    commandCondition_.notify_one();
}

void PlayerSession::DoStepFrame(const std::int32_t direction) {
    if (format_ == nullptr || videoStream_ < 0 || videoDecoder_ == nullptr) return;
    SuspendAudioRenderer(true);
    auto* stream = format_->streams[videoStream_];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const auto originalPosition = snapshot_.position100ns;
    auto currentPts = snapshot_.framePts;
    if (currentPts == AV_NOPTS_VALUE) {
        currentPts = av_rescale_q(originalPosition,
            AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base) + start;
    }

    // Repeated key messages at the media boundary must not trigger an expensive
    // seek/decode cycle for the same first frame.
    if ((direction < 0 && originalPosition <= 0) ||
        (direction > 0 && snapshot_.duration100ns > 0 &&
            originalPosition >= snapshot_.duration100ns)) {
        SetState(FFF3FPState::Paused, "step-frame");
        Emit(FFF3FPEvent::OperationCompleted,
            "{\"operation\":\"step-frame\",\"direction\":" +
            std::to_string(direction) + ",\"position100ns\":" +
            std::to_string(originalPosition) + ",\"boundary\":true}");
        return;
    }

    std::int64_t targetPosition = 0;
    auto targetPts = AV_NOPTS_VALUE;
    if (direction < 0) {
        auto previous = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), currentPts);
        if (previous != framePtsIndex_.begin()) targetPts = *--previous;
    } else {
        const auto next = std::upper_bound(framePtsIndex_.begin(), framePtsIndex_.end(), currentPts);
        if (next != framePtsIndex_.end()) targetPts = *next;
    }

    if (targetPts != AV_NOPTS_VALUE) {
        targetPosition = av_rescale_q(targetPts - start, stream->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)});
    } else {
        auto frameQuantum = lastVideoFrameDuration100ns_;
        if (frameQuantum <= 0) {
            const auto frameRate = av_guess_frame_rate(format_, stream, nullptr);
            if (frameRate.num > 0 && frameRate.den > 0) {
                frameQuantum = av_rescale_q(1, av_inv_q(frameRate),
                    AVRational{1, static_cast<int>(TicksPerSecond)});
            }
        }
        if (frameQuantum <= 0) {
            frameQuantum = av_rescale_q(1, stream->time_base,
                AVRational{1, static_cast<int>(TicksPerSecond)});
        }
        frameQuantum = std::max<std::int64_t>(1, frameQuantum);
        targetPosition = originalPosition + direction * frameQuantum;
    }
    targetPosition = std::clamp<std::int64_t>(targetPosition, 0, snapshot_.duration100ns);
    DoSeek(targetPosition);
    DecodeUntilSeekTarget();

    // Rounding a VFR timestamp to 100 ns can select the original frame again.
    // The first decode has populated the local PTS index, so retry its strict
    // predecessor without making the user press the key a second time.
    if (direction < 0 && snapshot_.framePts != AV_NOPTS_VALUE && snapshot_.framePts >= currentPts) {
        auto previous = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), currentPts);
        if (previous != framePtsIndex_.begin()) {
            const auto strictPreviousPts = *--previous;
            const auto strictPreviousPosition = std::max<std::int64_t>(0,
                av_rescale_q(strictPreviousPts - start, stream->time_base,
                    AVRational{1, static_cast<int>(TicksPerSecond)}));
            DoSeek(strictPreviousPosition);
            DecodeUntilSeekTarget();
        }
    }

    SetState(FFF3FPState::Paused, "step-frame");
    Emit(FFF3FPEvent::OperationCompleted,
        "{\"operation\":\"step-frame\",\"direction\":" +
        std::to_string(direction) + ",\"position100ns\":" +
        std::to_string(snapshot_.position100ns) + "}");
}

FFFResult PlayerSession::StepKeyframe(const std::int32_t direction) noexcept {
    if (direction != -1 && direction != 1) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    {
        std::lock_guard lock(snapshotMutex_);
        if ((state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
            state != FFF3FPState::Paused && state != FFF3FPState::Ended) ||
            publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState;
    }
    return ScheduleStep(StepOperation::Keyframe, direction);
}

void PlayerSession::DoStepKeyframe(const std::int32_t direction) {
    if (format_ == nullptr || videoStream_ < 0 || videoDecoder_ == nullptr) return;
    SuspendAudioRenderer(true);
    if ((direction < 0 && snapshot_.position100ns <= 0) ||
        (direction > 0 && snapshot_.duration100ns > 0 &&
            snapshot_.position100ns >= snapshot_.duration100ns)) {
        SetState(FFF3FPState::Paused, "step-keyframe");
        Emit(FFF3FPEvent::OperationCompleted,
            "{\"operation\":\"step-keyframe\",\"direction\":" +
            std::to_string(direction) + ",\"position100ns\":" +
            std::to_string(snapshot_.position100ns) + ",\"boundary\":true}");
        return;
    }

    auto* stream = format_->streams[videoStream_];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    auto currentTimestamp = snapshot_.framePts;
    if (currentTimestamp == AV_NOPTS_VALUE) {
        currentTimestamp = av_rescale_q(snapshot_.position100ns,
            AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base) + start;
    }
    const auto searchTimestamp = currentTimestamp + direction;
    const auto flags = direction < 0 ? AVSEEK_FLAG_BACKWARD : 0;
    const auto entryIndex = av_index_search_timestamp(stream, searchTimestamp, flags);
    const auto* entry = entryIndex >= 0 ? avformat_index_get_entry(stream, entryIndex) : nullptr;

    if (entry != nullptr) {
        const auto targetPosition = std::max<std::int64_t>(0,
            av_rescale_q(entry->timestamp - start, stream->time_base,
                AVRational{1, static_cast<int>(TicksPerSecond)}));
        DoSeek(targetPosition, -1, false);
    } else {
        if (av_seek_frame(format_, videoStream_, searchTimestamp, flags) < 0) {
            SetState(FFF3FPState::Paused, "step-keyframe");
            return;
        }
        ClearVideoQueue();
        avcodec_flush_buffers(videoDecoder_);
        if (audioDecoder_) avcodec_flush_buffers(audioDecoder_);
        seekTarget100ns_ = -1;
        seekTargetFrame_ = -1;
        keyframeSeekPending_ = true;
        draining_ = false;
        lastVideoFrameDuration100ns_ = 0;
        snapshot_.frameIndex = -1;
        PublishSnapshot();
    }
    DecodeUntilSeekTarget();
    SetState(FFF3FPState::Paused, "step-keyframe");
    Emit(FFF3FPEvent::OperationCompleted,
        "{\"operation\":\"step-keyframe\",\"direction\":" +
        std::to_string(direction) + ",\"position100ns\":" +
        std::to_string(snapshot_.position100ns) + "}");
}
FFFResult PlayerSession::SelectVideoStream(const std::int32_t index) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, index] { DoSelectStream(index, true); }); return FFFResult::Success; }
FFFResult PlayerSession::SelectAudioStream(const std::int32_t index) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, index] { DoSelectStream(index, false); }); return FFFResult::Success; }

FFFResult PlayerSession::LoadExternalAudio(const char* path, const std::int32_t index, const std::int64_t offset) noexcept {
    const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    std::string normalized, error; if (!NormalizeLocalPath(path, normalized, error)) return FFFResult::InvalidArgument;
    Enqueue([this, value = std::move(normalized), index, offset] { DoLoadExternalAudio(value, index, offset); }); return FFFResult::Success;
}
FFFResult PlayerSession::ClearExternalAudio() noexcept {
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended)
        return FFFResult::InvalidState;
    Enqueue([this] {
        const auto position = state_.load() == FFF3FPState::Playing
            ? ClockPosition() : snapshot_.position100ns;
        if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_);
        if (externalFormat_) avformat_close_input(&externalFormat_);
        externalAudioStream_ = -1;
        externalAudioPath_.clear();
        snapshot_.isExternalAudio = 0;
        // Main-container audio packets were intentionally skipped while the
        // external track was active. A full seek is required to put demux,
        // video, audio and the resampler back on one timeline.
        DoSeek(position);
        Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"clear-external-audio\"}");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetExternalAudioOffset(const std::int64_t offset) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, offset] { externalAudioOffset100ns_ = offset; snapshot_.externalAudioOffset100ns = offset; if (externalFormat_) DoSeek(snapshot_.position100ns); else PublishSnapshot(); }); return FFFResult::Success; }
FFFResult PlayerSession::SetColorMode(const FFF3FPColorMode mode, const float sdr, const float hdr, const float paper) noexcept {
    if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdr) || sdr <= 0 ||
        !std::isfinite(hdr) || hdr < 0 || hdr > 10000 || !std::isfinite(paper) || paper <= 0)
        return FFFResult::InvalidArgument;
    Enqueue([this, mode, sdr, hdr, paper] {
        const auto forceSdr = mode == FFF3FPColorMode::MapToHdr && snapshot_.isHdrSource == 0;
        const auto effectiveMode = forceSdr ? FFF3FPColorMode::MapToSdr : mode;
        snapshot_.requestedColorMode = effectiveMode;
        const auto previous = snapshot_.actualColorMode;
        const auto result = videoRenderer_.SetColorMode(effectiveMode, sdr, hdr, paper);
        if (result != FFFResult::Success) {
            if (result == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return;
            Fail(result, "The color output configuration is invalid.", "color-mode");
            return;
        }
        const auto redrawResult = videoRenderer_.Redraw();
        if (redrawResult != FFFResult::Success) {
            if (redrawResult == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return;
            Fail(FFFResult::DeviceFailure, videoRenderer_.LastError(), "redraw"); return;
        }
        snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
        PublishSnapshot();
        const auto reason = forceSdr ? "True HDR output is only available for HDR source video."
            : videoRenderer_.FallbackReason();
        std::ostringstream json;
        json << "{\"requested\":" << static_cast<unsigned>(effectiveMode)
            << ",\"actual\":" << static_cast<unsigned>(snapshot_.actualColorMode)
            << ",\"reason\":\"" << EscapeJson(reason) << "\"}";
        if (previous != snapshot_.actualColorMode || effectiveMode != snapshot_.actualColorMode || forceSdr)
            Emit(FFF3FPEvent::ColorModeChanged, json.str());
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetOutputWindow(void* window) noexcept {
    if (window != nullptr && !IsWindow(static_cast<HWND>(window))) return FFFResult::InvalidArgument;
    Enqueue([this, window] {
        const auto result = videoRenderer_.SetWindow(static_cast<HWND>(window));
        if (result != FFFResult::Success) {
            Fail(result, "The playback window handle is invalid.", "output-window");
            return;
        }
        // Audio media is opened before the controller binds its final HWND.
        // Headless Render intentionally owns no GPU cache, so submit the retained
        // attached picture once when a real target first becomes available.
        const auto redrawResult = coverArtFrame_ != nullptr && videoStream_ < 0 && window != nullptr
            ? videoRenderer_.Render(coverArtFrame_) : videoRenderer_.Redraw();
        if (redrawResult == FFFResult::DeviceFailure &&
            videoRenderer_.RequestRecoveryIfDeviceLost()) return;
        if (redrawResult != FFFResult::Success)
            Fail(FFFResult::DeviceFailure, videoRenderer_.LastError(), "redraw");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::RecreateAudioRenderer(const std::wstring& endpointId,
    const bool exclusive, const bool paused, std::string& error) noexcept {
    try {
        audioRenderer_.reset();
        auto replacement = std::make_unique<PlayerWasapiRenderer>(endpointId, exclusive,
            &audioRuntimeState_, [this] { NotifyAudioRestart(); });
        const auto result = replacement->Start();
        if (result != FFFResult::Success) {
            error = replacement->LastError();
            return result;
        }
        replacement->SetVolume(volume_, muted_);
        replacement->SetPaused(paused);
        audioRenderer_ = std::move(replacement);
        return FFFResult::Success;
    } catch (...) {
        error = "Could not allocate a replacement WASAPI renderer.";
        return FFFResult::NativeFailure;
    }
}

void PlayerSession::SuspendAudioRenderer(const bool releaseExclusive) noexcept {
    if (!audioRenderer_) return;
    audioRenderer_->SetPaused(true);
    if (releaseExclusive && audioExclusive_) {
        audioRenderer_->Stop();
        audioRenderer_.reset();
        audioRuntimeState_.ClearValues();
    }
}

FFFResult PlayerSession::ResumeAudioRenderer() noexcept {
    if (audioRenderer_ || (audioDecoder_ == nullptr && externalAudioDecoder_ == nullptr))
        return FFFResult::Success;
    const auto position = std::max<std::int64_t>(0, snapshot_.position100ns);
    std::string error;
    auto result = RecreateAudioRenderer(audioEndpointId_, audioExclusive_, true, error);
    auto exclusiveError = std::string{};
    bool fellBackToShared = false;
    if (result != FFFResult::Success && audioExclusive_) {
        exclusiveError = std::move(error);
        result = RecreateAudioRenderer(audioEndpointId_, false, true, error);
        if (result == FFFResult::Success) {
            audioExclusive_ = false;
            fellBackToShared = true;
        }
    }
    if (result != FFFResult::Success) {
        // A system-level exclusive owner also blocks shared Initialize. Keep
        // the media session playable on its video/QPC clock and remember the
        // user's effective mode as shared instead of failing the whole file.
        audioExclusive_ = false;
        const auto reason = error.empty()
            ? std::string("The Windows audio output device is currently unavailable.")
            : std::move(error);
        RebuildMediaInfo();
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"audio\",\"exclusive\":false,\"audioUnavailable\":true,\"reason\":\"" +
            EscapeJson(reason + " The media continued in shared mode without audio output.") + "\"}");
        return FFFResult::Success;
    }
    DoSeek(position);
    RebuildMediaInfo();
    if (fellBackToShared) {
        const auto reason = exclusiveError.empty()
            ? std::string("The audio endpoint is already in use or rejected exclusive mode.")
            : std::move(exclusiveError);
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"audio\",\"exclusive\":false,\"exclusiveFallback\":true,\"reason\":\"" +
            EscapeJson(reason + " Playback continued in shared mode.") + "\"}");
    }
    return FFFResult::Success;
}

bool PlayerSession::RecoverAudioDevice() noexcept {
    if (!audioRenderer_ || !audioRenderer_->RestartRequested()) return false;
    const auto paused = snapshot_.state != FFF3FPState::Playing;
    const auto position = std::max<std::int64_t>(0, ClockPosition());
    auto reason = audioRenderer_->LastError();
    std::string error;
    auto result = RecreateAudioRenderer(audioEndpointId_, audioExclusive_, paused, error);
    auto exclusiveError = std::string{};
    bool fellBackToShared = false;
    if (result != FFFResult::Success && audioExclusive_) {
        exclusiveError = std::move(error);
        result = RecreateAudioRenderer(audioEndpointId_, false, paused, error);
        if (result == FFFResult::Success) {
            audioExclusive_ = false;
            fellBackToShared = true;
        }
    }
    if (result != FFFResult::Success) {
        Fail(result, error.empty() ? "Could not reopen the Windows audio output device." :
            std::move(error), "audio-device-recovery");
        return true;
    }
    snapshot_.position100ns = position;
    DoSeek(position);
    RebuildMediaInfo();
    Emit(FFF3FPEvent::DeviceChanged,
        "{\"type\":\"audio\",\"recovered\":true,\"exclusive\":" +
        std::string(audioExclusive_ ? "true" : "false") + ",\"reason\":\"" +
        EscapeJson(reason.empty() ? "The Windows audio output device changed." : reason) + "\"}");
    if (fellBackToShared) {
        ReportError(FFFResult::DeviceFailure,
            exclusiveError.empty() ?
                "The new audio device does not support the active exclusive format; playback continued in shared mode." :
                exclusiveError + " Playback continued in shared mode.",
            "audio-device-recovery");
    }
    return true;
}

bool PlayerSession::RecoverVideoDevice() noexcept {
    if (!videoRenderer_.DeviceRecoveryRequested()) return false;

    const auto previousState = state_.load();
    const auto wasPlaying = previousState == FFF3FPState::Playing;
    const auto resumePosition = std::max<std::int64_t>(0,
        wasPlaying ? ClockPosition() : snapshot_.position100ns);
    auto redrawPosition = resumePosition;
    if (previousState == FFF3FPState::Ended && format_ != nullptr && videoStream_ >= 0 &&
        snapshot_.framePts != AV_NOPTS_VALUE) {
        const auto* stream = format_->streams[videoStream_];
        const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        redrawPosition = std::max<std::int64_t>(0, av_rescale_q(snapshot_.framePts - start,
            stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}));
    }
    const auto reason = videoRenderer_.LastError();
    SuspendAudioRenderer(false);

    ClearVideoQueue();
    if (videoDecodeFrame_ != nullptr) av_frame_unref(videoDecodeFrame_);
    if (videoTransferFrame_ != nullptr) av_frame_unref(videoTransferFrame_);
    if (videoDecoder_ != nullptr) avcodec_free_context(&videoDecoder_);

    FFFResult result = FFFResult::DeviceFailure;
    {
        // SetTimedTextLayer can run outside the session queue. Keep it from
        // restarting the presenter while the old device is being dismantled.
        std::unique_lock contentLock(timedTextContentMutex_);
        constexpr int MaximumRecreateAttempts = 20;
        for (int attempt = 0; attempt < MaximumRecreateAttempts && !terminate_; ++attempt) {
            result = videoRenderer_.RecreateDeviceResources();
            if (result == FFFResult::Success) break;
            Sleep(attempt == 0 ? 50 : 200);
        }
    }
    if (terminate_) return true;
    if (result != FFFResult::Success) {
        Fail(result, videoRenderer_.LastError().empty()
            ? "Could not recreate the D3D11 playback device." : videoRenderer_.LastError(),
            "video-device-recovery");
        return true;
    }

    bool hardwareFallback = false;
    std::string hardwareFailureReason;
    if (format_ != nullptr && videoStream_ >= 0) {
        if (decodeMode_ == FFF3FPDecodeMode::D3D11 && !staticImage_) {
            result = OpenHardwareVideoDecoder(format_, videoStream_, &videoDecoder_,
                &hardwareFailureReason);
            if (result != FFFResult::Success) {
                result = OpenDecoder(format_, videoStream_, true, &videoDecoder_,
                    -1, nullptr, false);
                hardwareFallback = result == FFFResult::Success;
            }
        } else {
            result = OpenDecoder(format_, videoStream_, true, &videoDecoder_,
                -1, nullptr, false);
        }
        if (result != FFFResult::Success) {
            Fail(result, "Could not reopen the video decoder after rebuilding the graphics device.",
                "video-device-recovery");
            return true;
        }
        snapshot_.decodeMode = decodeMode_ == FFF3FPDecodeMode::D3D11 &&
            !staticImage_ && !hardwareFallback
            ? FFF3FPDecodeMode::Gpu : FFF3FPDecodeMode::Cpu;
        if (staticImage_ && stillImageFrame_ != nullptr) {
            const auto renderResult = videoRenderer_.Render(stillImageFrame_);
            if (renderResult != FFFResult::Success) {
                if (renderResult == FFFResult::DeviceFailure &&
                    videoRenderer_.RequestRecoveryIfDeviceLost()) return true;
                Fail(renderResult, videoRenderer_.LastError(), "video-device-recovery");
                return true;
            }
        } else {
            DoSeek(redrawPosition);
            DecodeUntilSeekTarget();
        }
        if (videoRenderer_.DeviceRecoveryRequested() || state_.load() == FFF3FPState::Failed)
            return true;
    } else if (coverArtFrame_ != nullptr) {
        const auto renderResult = videoRenderer_.Render(coverArtFrame_);
        if (renderResult != FFFResult::Success) {
            if (renderResult == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return true;
            Fail(renderResult, videoRenderer_.LastError(), "video-device-recovery");
            return true;
        }
    }

    if (previousState == FFF3FPState::Ended) {
        snapshot_.position100ns = resumePosition;
        ResetClock(resumePosition);
    }
    if (audioRenderer_) audioRenderer_->SetPaused(!wasPlaying);
    RebuildMediaInfo();
    PublishSnapshot();
    std::ostringstream json;
    json << "{\"type\":\"video\",\"recovered\":true,\"decodeMode\":"
         << static_cast<unsigned>(snapshot_.decodeMode)
         << ",\"hardwareFallback\":" << (hardwareFallback ? "true" : "false")
         << ",\"reason\":\"" << EscapeJson(hardwareFallback && !hardwareFailureReason.empty()
            ? hardwareFailureReason
            : (reason.empty() ? "The Windows graphics device changed." : reason)) << "\"}";
    Emit(FFF3FPEvent::DeviceChanged, json.str());
    return true;
}

FFFResult PlayerSession::SetAudioEndpoint(const char* endpoint) noexcept {
    const auto value = endpoint == nullptr ? std::wstring{} : FromUtf8(endpoint);
    Enqueue([this, value] {
        if (!audioRenderer_) {
            audioEndpointId_ = value;
            return;
        }
        const auto previousEndpoint = audioEndpointId_;
        const auto paused = snapshot_.state != FFF3FPState::Playing;
        const auto position = std::max<std::int64_t>(0, ClockPosition());
        std::string error;
        const auto result = RecreateAudioRenderer(value, audioExclusive_, paused, error);
        if (result != FFFResult::Success) {
            std::string restoreError;
            if (RecreateAudioRenderer(previousEndpoint, audioExclusive_, paused, restoreError) ==
                FFFResult::Success) {
                DoSeek(position);
                RebuildMediaInfo();
                ReportError(result, error.empty() ? "Could not open the selected audio endpoint." :
                    std::move(error), "audio-endpoint");
            } else {
                Fail(result, restoreError.empty() ? "Could not restore the previous audio endpoint." :
                    std::move(restoreError), "audio-endpoint");
            }
            return;
        }
        audioEndpointId_ = value;
        snapshot_.position100ns = position;
        DoSeek(position);
        RebuildMediaInfo();
        Emit(FFF3FPEvent::DeviceChanged,
            std::string("{\"type\":\"audio\",\"default\":") +
            (audioEndpointId_.empty() ? "true}" : "false}"));
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetAudioExclusiveMode(const bool exclusive) noexcept {
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this, exclusive] {
        if (audioExclusive_ == exclusive) {
            if (exclusive && snapshot_.state != FFF3FPState::Playing)
                SuspendAudioRenderer(true);
            Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
                (exclusive ? "true}" : "false}"));
            return;
        }
        if (exclusive && snapshot_.state != FFF3FPState::Playing) {
            // Exclusive mode is a preference while paused. Do not reserve the
            // endpoint until playback actually starts.
            audioExclusive_ = true;
            SuspendAudioRenderer(true);
            Emit(FFF3FPEvent::DeviceChanged,
                "{\"type\":\"audio\",\"exclusive\":true,\"releasedWhilePaused\":true}");
            return;
        }
        if (!audioRenderer_) {
            audioExclusive_ = exclusive;
            Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
                (exclusive ? "true}" : "false}"));
            return;
        }
        const auto paused = snapshot_.state != FFF3FPState::Playing;
        const auto position = std::max<std::int64_t>(0, ClockPosition());
        snapshot_.position100ns = position;
        std::string message;
        const auto result = RecreateAudioRenderer(audioEndpointId_, exclusive, paused, message);
        if (result != FFFResult::Success) {
            // Restore the previous renderer so an unsupported exclusive format
            // cannot turn a working session into a silent session.
            std::string restoreError;
            if (RecreateAudioRenderer(audioEndpointId_, audioExclusive_, paused, restoreError) ==
                FFFResult::Success) {
                DoSeek(position);
                RebuildMediaInfo();
            } else {
                Fail(result, restoreError.empty() ? "Could not restore shared-mode audio playback." :
                    std::move(restoreError), "audio-exclusive-mode");
                return;
            }
            if (exclusive) {
                const auto reason = message.empty()
                    ? std::string("The selected endpoint rejected exclusive mode.")
                    : std::move(message);
                Emit(FFF3FPEvent::DeviceChanged,
                    "{\"type\":\"audio\",\"exclusive\":false,\"exclusiveFallback\":true,\"reason\":\"" +
                    EscapeJson(reason + " Playback continued in shared mode.") + "\"}");
            } else {
                Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
                    (audioExclusive_ ? "true}" : "false}"));
                ReportError(result, message.empty() ? "The selected endpoint rejected shared mode." : message,
                    "audio-exclusive-mode");
            }
            return;
        }
        audioExclusive_ = exclusive;
        DoSeek(position);
        RebuildMediaInfo();
        Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
            (exclusive ? "true}" : "false}"));
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetVolume(const float volume, const bool muted) noexcept { if (!std::isfinite(volume) || volume < 0 || volume > 1) return FFFResult::InvalidArgument; Enqueue([this, volume, muted] { volume_ = volume; muted_ = muted; if (audioRenderer_) audioRenderer_->SetVolume(volume_, muted_); }); return FFFResult::Success; }

FFFResult PlayerSession::SetTimedTextLayer(const FFF3FPTimedTextLayer& input) noexcept {
    constexpr auto legacyLayerSize = offsetof(FFF3FPTimedTextLayer, targetFrameRate);
    const auto targetFrameRate = input.size >= sizeof(FFF3FPTimedTextLayer)
        ? input.targetFrameRate : 60.0f;
    if (input.size < legacyLayerSize || input.version != 1 ||
        input.canvasWidth == 0 || input.canvasHeight == 0 || input.commandCount > 4096 ||
        input.layerSlot > static_cast<std::uint32_t>(TimedTextLayerSlot::PlayerInformation) ||
        !std::isfinite(targetFrameRate) || targetFrameRate < 1.0f || targetFrameRate > 240.0f ||
        (input.commandCount != 0 && input.commands == nullptr)) return FFFResult::InvalidArgument;
    try {
        std::lock_guard contentLock(timedTextContentMutex_);
        const auto state = state_.load();
        if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
            state != FFF3FPState::Paused && state != FFF3FPState::Ended)
            return FFFResult::InvalidState;
        TimedTextRenderLayer layer;
        layer.canvasWidth = input.canvasWidth;
        layer.canvasHeight = input.canvasHeight;
        layer.sequence = input.sequence;
        layer.targetFrameRate = targetFrameRate;
        layer.commands.reserve(input.commandCount);
        for (std::uint32_t index = 0; index < input.commandCount; ++index) {
            const auto& source = input.commands[index];
            constexpr auto legacyCommandSize = offsetof(FFF3FPTimedTextCommand, shadowArgb);
            if (source.size < legacyCommandSize || source.version != 1 ||
                source.type < FFF3FPTimedTextCommandType::Text || source.type > FFF3FPTimedTextCommandType::Bitmap ||
                !std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.width) ||
                !std::isfinite(source.height) || source.width <= 0 || source.height <= 0)
                return FFFResult::InvalidArgument;
            TimedTextRenderCommand command;
            command.type = source.type; command.flags = source.flags;
            command.x = source.x; command.y = source.y; command.width = source.width; command.height = source.height;
            command.foregroundArgb = source.foregroundArgb; command.outlineArgb = source.outlineArgb;
            command.fontSize = source.fontSize; command.outlineWidth = source.outlineWidth;
            if (source.size >= sizeof(FFF3FPTimedTextCommand)) {
                command.shadowArgb = source.shadowArgb;
                command.shadowOffsetX = source.shadowOffsetX;
                command.shadowOffsetY = source.shadowOffsetY;
            }
            command.horizontalAlignment = source.horizontalAlignment;
            command.verticalAlignment = source.verticalAlignment;
            command.contentId = source.contentId;
            if (source.type == FFF3FPTimedTextCommandType::Text) {
                if (source.textUtf8 == nullptr || !std::isfinite(source.fontSize) || source.fontSize <= 0 ||
                    !std::isfinite(source.outlineWidth) || source.outlineWidth < 0 ||
                    !std::isfinite(command.shadowOffsetX) || !std::isfinite(command.shadowOffsetY) ||
                    source.horizontalAlignment > FFF3FPTimedTextAlignment::Far ||
                    source.verticalAlignment > FFF3FPTimedTextAlignment::Far ||
                    source.fontFamilyUtf8 == nullptr) return FFFResult::InvalidArgument;
                const auto key = source.contentId == 0 ? 0 :
                    TimedTextContentKey(source.contentId, source.textUtf8, source.fontFamilyUtf8);
                if (key != 0) {
                    const auto cached = timedTextContentCache_.find(key);
                    if (cached != timedTextContentCache_.end()) command.content = cached->second;
                }
                if (!command.content) {
                    auto content = std::make_shared<TimedTextRenderCommand::TextContent>();
                    content->identity = TimedTextContentKey(source.contentId,
                        source.textUtf8, source.fontFamilyUtf8);
                    if (!FromUtf8Strict(source.textUtf8, content->text) || content->text.empty() ||
                        !FromUtf8Strict(source.fontFamilyUtf8, content->fontFamily)) return FFFResult::InvalidArgument;
                    if (content->fontFamily.empty()) content->fontFamily = L"Segoe UI";
                    command.content = content;
                    if (key != 0) {
                        constexpr std::size_t MaximumTimedTextContents = 512;
                        if (timedTextContentCache_.size() >= MaximumTimedTextContents)
                            timedTextContentCache_.clear();
                        timedTextContentCache_[key] = std::move(content);
                    }
                }
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
        // Timed text has its own high-precision 60 Hz producer. Routing it through
        // the decode command queue coalesced frames whenever video work was busy.
        const auto result = videoRenderer_.SetTimedTextLayer(std::move(layer),
            static_cast<TimedTextLayerSlot>(input.layerSlot));
        if (result != FFFResult::Success) {
            ReportError(result, videoRenderer_.LastError(), "timed-text");
            return result;
        }
        return FFFResult::Success;
    } catch (...) { return FFFResult::NativeFailure; }
}

FFFResult PlayerSession::GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept {
    return videoRenderer_.GetTimedTextStatus(status, TimedTextLayerSlot::Subtitle);
}

FFFResult PlayerSession::GetDanmakuStatus(FFF3FPTimedTextStatus& status) noexcept {
    return videoRenderer_.GetTimedTextStatus(status, TimedTextLayerSlot::Danmaku);
}

FFFResult PlayerSession::GetSnapshot(FFF3FPSnapshot& output) const noexcept {
    if (output.size < sizeof(FFF3FPSnapshot) || output.version != 7) return FFFResult::InvalidArgument;
    { std::lock_guard lock(snapshotMutex_); output = publishedSnapshot_; }
    // Presentation completes asynchronously on the dedicated swap-chain owner;
    // expose its live counters even if no later decode-frame snapshot was needed.
    output.presentedVideoFrames = videoRenderer_.PresentedVideoFrames();
    output.coalescedVideoFrames = videoRenderer_.CoalescedVideoFrames();
    output.swapChainPresents = videoRenderer_.SwapChainPresents();
    output.presentWait100ns = videoRenderer_.PresentWait100ns();
    output.deviceLockWait100ns = videoRenderer_.DeviceLockWait100ns();
    output.softwareConvert100ns = videoRenderer_.SoftwareConvert100ns();
    output.videoOutputBitDepth = videoRenderer_.OutputBitDepth();
    output.videoScalingMode = videoRenderer_.ActualVideoScalingMode();
    if (output.state == FFF3FPState::Playing) {
        std::uint64_t firstSequence = 0;
        std::uint64_t secondSequence = 0;
        std::int64_t sampledPosition = 0;
        std::int64_t sampledQpc = 0;
        std::int64_t limit = 0;
        do {
            firstSequence = playbackClockSequence_.load(std::memory_order_acquire);
            if ((firstSequence & 1u) != 0) continue;
            sampledPosition = playbackPosition100ns_.load(std::memory_order_relaxed);
            sampledQpc = playbackClockSampleQpc_.load(std::memory_order_relaxed);
            limit = playbackClockLimit100ns_.load(std::memory_order_relaxed);
            secondSequence = playbackClockSequence_.load(std::memory_order_acquire);
        } while (firstSequence != secondSequence || (secondSequence & 1u) != 0);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto elapsed = sampledQpc > 0 && now.QuadPart > sampledQpc && qpcFrequency_ > 0
            ? (now.QuadPart - sampledQpc) * TicksPerSecond / qpcFrequency_ : 0;
        output.position100ns = sampledPosition + std::min(elapsed,
            std::max<std::int64_t>(0, limit - sampledPosition));
        if (output.duration100ns > 0) output.position100ns = std::min(output.position100ns, output.duration100ns);
    }
    output.bufferedAudio100ns = audioRuntimeState_.buffered100ns.load(std::memory_order_relaxed);
    output.audioUnderruns = audioRuntimeState_.underruns.load(std::memory_order_relaxed);
    output.audioTimestampJitterFrames = audioRuntimeState_.timestampJitterFrames.load(std::memory_order_relaxed);
    output.audioDiscontinuities = audioRuntimeState_.discontinuities.load(std::memory_order_relaxed);
    output.audioInsertedSilenceFrames = audioRuntimeState_.insertedSilenceFrames.load(std::memory_order_relaxed);
    output.audioDroppedOverlapFrames = audioRuntimeState_.droppedOverlapFrames.load(std::memory_order_relaxed);
    if (output.selectedAudioStream >= 0 || output.isExternalAudio != 0)
        output.audioPosition100ns = output.position100ns;
    return FFFResult::Success;
}

FFFResult PlayerSession::ReadVideoPixel(FFF3FPVideoPixelProbe& probe) noexcept {
    const auto result = videoRenderer_.ReadPixel(probe);
    if (result == FFFResult::DeviceFailure)
        videoRenderer_.RequestRecoveryIfDeviceLost();
    return result;
}

FFFResult PlayerSession::GetAudioPeakLevels(FFF3FPAudioPeakLevels& output) const noexcept {
    if (output.size < sizeof(FFF3FPAudioPeakLevels) || output.version != 1)
        return FFFResult::InvalidArgument;
    output.channelCount = 0;
    output.reserved = 0;
    std::fill(std::begin(output.values), std::end(output.values), 0.0f);
    output.channelCount = audioRuntimeState_.Copy(output.values,
        static_cast<std::uint32_t>(std::size(output.values)));
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
                commandCondition_.wait(lock, [this] {
                    return terminate_ || !commands_.empty() ||
                        state_.load() == FFF3FPState::Playing ||
                        videoRenderer_.DeviceRecoveryRequested() ||
                        (audioRenderer_ && audioRenderer_->RestartRequested());
                });
            if (terminate_) break;
            if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
        }
        if (command) { try { command(); } catch (...) { Fail(FFFResult::NativeFailure, "An unhandled player command exception occurred."); } }
        else if (!RecoverVideoDevice() && !RecoverAudioDevice() &&
            state_.load() == FFF3FPState::Playing) PumpPlayback();
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
    if ((*output)->iformat == nullptr ||
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
    std::int32_t* hardwarePixelFormat, const bool useConfiguredHardware,
    const AVCodec* codecOverride, std::string* failureReason) noexcept {
    const auto setFailure = [failureReason](std::string message) {
        if (failureReason != nullptr) *failureReason = std::move(message);
    };
    if (owner == nullptr || index < 0 || index >= static_cast<std::int32_t>(owner->nb_streams)) {
        setFailure("The requested stream index is invalid.");
        return FFFResult::InvalidArgument;
    }
    auto* stream = owner->streams[index];
    if (stream->codecpar->codec_type != (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO)) {
        setFailure("The requested stream has the wrong media type.");
        return FFFResult::InvalidArgument;
    }
    AVPixelFormat selectedHardwareFormat = AV_PIX_FMT_NONE;
    const auto deviceType = static_cast<AVHWDeviceType>(hardwareDeviceType);
    const auto hardwareRequested = video && useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11;
    const AVCodec* codec = hardwareRequested
        ? codecOverride
        : video && stream->codecpar->codec_id == AV_CODEC_ID_AV1
            ? avcodec_find_decoder_by_name("libdav1d") : nullptr;
    if (hardwareRequested) selectedHardwareFormat = FindHardwareFormat(codec, deviceType);
    if (codec == nullptr && !hardwareRequested)
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr || (hardwareRequested && selectedHardwareFormat == AV_PIX_FMT_NONE)) {
        setFailure("No compatible FFmpeg decoder exposes the requested hardware pixel format.");
        return FFFResult::NotSupported;
    }
    auto* context = avcodec_alloc_context3(codec);
    if (context == nullptr) {
        setFailure("FFmpeg could not allocate the decoder context.");
        return FFFResult::NativeFailure;
    }
    auto result = avcodec_parameters_to_context(context, stream->codecpar);
    context->pkt_timebase = stream->time_base;
    if (result >= 0 && video && !hardwareRequested) {
        const auto hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        context->thread_count = static_cast<int>(std::min(
            hardwareThreads, MaximumSoftwareDecoderThreads));
        context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    if (result >= 0 && hardwareRequested) {
        // The decoder already allocates its codec-specific DPB. Extra surfaces
        // only cover the bounded presentation queue plus in-flight copies.
        context->extra_hw_frames = std::max(context->extra_hw_frames,
            static_cast<int>(MaxQueuedVideoFrames + 2));
        AVBufferRef* hardwareDevice = nullptr;
        if (deviceType == AV_HWDEVICE_TYPE_D3D11VA) {
            const auto shared = videoRenderer_.CreateD3D11HardwareDeviceContext(&hardwareDevice);
            result = shared == FFFResult::Success ? 0 : AVERROR(ENODEV);
            if (result < 0) setFailure(videoRenderer_.LastError());
        } else {
            result = av_hwdevice_ctx_create(&hardwareDevice, deviceType, nullptr, nullptr, 0);
            if (result < 0)
                setFailure("FFmpeg could not create the hardware device: " + FfmpegError(result));
        }
        if (result >= 0) {
            context->hw_device_ctx = av_buffer_ref(hardwareDevice);
            context->opaque = reinterpret_cast<void*>(static_cast<std::intptr_t>(selectedHardwareFormat));
            context->get_format = SelectHardwareFormat;
        }
        av_buffer_unref(&hardwareDevice);
    }
    if (result >= 0) result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        if (failureReason != nullptr && failureReason->empty())
            setFailure("FFmpeg could not open decoder " + std::string(codec->name == nullptr ? "unknown" : codec->name) +
                ": " + FfmpegError(result));
        avcodec_free_context(&context);
        return hardwareRequested ? FFFResult::NotSupported : FFFResult::FfmpegFailure;
    }
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
    AVCodecContext** output, std::string* failureReason) noexcept {
    if (owner == nullptr || index < 0 || index >= static_cast<std::int32_t>(owner->nb_streams))
        return FFFResult::InvalidArgument;
    const AVHWDeviceType Backends[] = {
        // Prefer the renderer's own D3D11 device: decoded surfaces can then be
        // sampled directly without a GPU-to-CPU-to-GPU round trip. CUDA remains
        // available for profiles rejected by D3D11VA.
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_CUDA,
    };
    std::string lastFailure = "No compatible hardware decoder was found.";
    for (const auto backend : Backends) {
        // A backend can expose both a dedicated wrapper and the native FFmpeg
        // decoder. Try both: profiles accepted by one parser are not always
        // accepted by the other, notably 4:2:2/4:4:4 and some AV1 streams.
        for (int dedicatedPass = 0; dedicatedPass < 2; ++dedicatedPass) {
            void* iterator = nullptr;
            while (const auto* codec = av_codec_iterate(&iterator)) {
                const auto* parameters = owner->streams[index]->codecpar;
                if (codec->id != parameters->codec_id || !av_codec_is_decoder(codec) ||
                    (IsDedicatedCudaDecoder(codec) ? 0 : 1) != dedicatedPass ||
                    FindHardwareFormat(codec, backend) == AV_PIX_FMT_NONE) continue;
                AVCodecContext* candidate = nullptr;
                std::int32_t hardwareFormat = AV_PIX_FMT_NONE;
                std::string candidateFailure;
                if (OpenDecoder(owner, index, true, &candidate, backend, &hardwareFormat,
                    true, codec, &candidateFailure) != FFFResult::Success) {
                    if (!candidateFailure.empty()) lastFailure = std::move(candidateFailure);
                    continue;
                }
                const auto probeResult = ProbeHardwareVideo(owner, candidate, index, hardwareFormat);
                if (probeResult == FFFResult::Success) {
                    *output = candidate;
                    return FFFResult::Success;
                }
                const auto* backendName = av_hwdevice_get_type_name(backend);
                lastFailure = "Hardware decoder " + std::string(codec->name == nullptr ? "unknown" : codec->name) +
                    " opened with " + (backendName == nullptr ? std::string("unknown backend") : backendName) +
                    " but did not produce the required hardware frame format.";
                avcodec_free_context(&candidate);
            }
        }
    }
    if (failureReason != nullptr) *failureReason = std::move(lastFailure);
    return FFFResult::NotSupported;
}

void PlayerSession::DoOpen(std::string path) noexcept {
    // Keep the flip-model chain for a same-HWND media switch. DXGI can reject
    // an immediate replacement while the previous chain is still retiring.
    DoClose(FFF3FPState::Opening, true);
    std::string openError;
    const auto openResult = OpenFormat(path, &format_, openError);
    if (openResult != FFFResult::Success) { Fail(openResult, std::move(openError), "open"); return; }
    videoStream_ = FindTimedVideoStream(format_);
    staticImage_ = videoStream_ >= 0 && IsStaticImageDemuxer(format_->iformat);
    coverArtStream_ = FindCoverArtStream(format_);
    audioStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, videoStream_, nullptr, 0);
    if (videoStream_ < 0 && audioStream_ < 0) { Fail(FFFResult::NotSupported, "The file contains no playable video or audio stream.", "open"); return; }
    snapshot_.decodeMode = decodeMode_;
    bool hardwareFallback = false;
    std::string hardwareFailureReason;
    if (videoStream_ >= 0) {
        auto result = decodeMode_ == FFF3FPDecodeMode::D3D11 && !staticImage_
            ? OpenHardwareVideoDecoder(format_, videoStream_, &videoDecoder_, &hardwareFailureReason)
            : OpenDecoder(format_, videoStream_, true, &videoDecoder_);
        if (result != FFFResult::Success && decodeMode_ == FFF3FPDecodeMode::D3D11) {
            result = OpenDecoder(format_, videoStream_, true, &videoDecoder_, -1, nullptr, false);
            if (result == FFFResult::Success) {
                snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
                hardwareFallback = true;
            }
        }
        if (result != FFFResult::Success) { Fail(result, "Could not open a hardware or software video decoder.", "open"); return; }
        if (staticImage_ && decodeMode_ == FFF3FPDecodeMode::D3D11)
            snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
    }
    if (audioStream_ >= 0 && OpenDecoder(format_, audioStream_, false, &audioDecoder_) != FFFResult::Success) audioStream_ = -1;
    videoRenderer_.ConfigureHdrStream(videoStream_ >= 0 ?
        format_->streams[videoStream_]->codecpar : nullptr);
    if (audioStream_ >= 0 && !audioExclusive_) {
        std::string audioError;
        const auto result = RecreateAudioRenderer(audioEndpointId_, audioExclusive_, true, audioError);
        if (result != FFFResult::Success) {
            // The endpoint may currently be held in system-level exclusive
            // mode. Opening the media must still succeed; Play retries the
            // shared renderer and publishes a user-facing availability notice.
            audioRenderer_.reset();
        }
    }
    if (videoStream_ < 0 && coverArtStream_ >= 0) {
        const auto result = LoadCoverArt();
        if (result != FFFResult::Success) { Fail(result, "Could not decode or render the attached cover art.", "cover-art"); return; }
    }
    bool forcedSdrOutput = false;
    {
        std::lock_guard lock(mutex_);
        snapshot_.duration100ns = format_->duration > 0 && !IsLoopAwareImageDemuxer(format_->iformat)
            ? av_rescale(format_->duration, 10, 1) : 0;
        snapshot_.position100ns = 0; snapshot_.frameIndex = -1; snapshot_.framePts = AV_NOPTS_VALUE;
        snapshot_.selectedVideoStream = videoStream_; snapshot_.selectedAudioStream = audioStream_;
        snapshot_.videoWidth = snapshot_.videoHeight = 0; snapshot_.isHdrSource = 0;
        if (videoStream_ >= 0) { snapshot_.videoWidth = videoDecoder_->width; snapshot_.videoHeight = videoDecoder_->height; ApplyHdrState(snapshot_, videoRenderer_.HdrState()); }
        else if (coverArtFrame_ != nullptr) { snapshot_.videoWidth = coverArtFrame_->width; snapshot_.videoHeight = coverArtFrame_->height; snapshot_.isHdrSource = 0; }
        if (snapshot_.isHdrSource == 0 && snapshot_.requestedColorMode == FFF3FPColorMode::MapToHdr) {
            snapshot_.requestedColorMode = FFF3FPColorMode::MapToSdr;
            forcedSdrOutput = true;
        }
    }
    if (forcedSdrOutput) {
        const auto resetResult = videoRenderer_.ForceSdrOutputForSdrSource();
        if (resetResult != FFFResult::Success) {
            Fail(resetResult, videoRenderer_.LastError(), "sdr-output-reset");
            return;
        }
        snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    }
    framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = false;
    seekTarget100ns_ = -1; seekTargetFrame_ = -1; keyframeSeekPending_ = false;
    lastVideoFrameDuration100ns_ = 0; draining_ = false;
    if (staticImage_) {
        const auto imageResult = DecodeInitialStillImage();
        if (imageResult != FFFResult::Success) {
            Fail(imageResult, "Could not decode or render the still image during open.", "open-image");
            return;
        }
    }
    RebuildMediaInfo(); SetState(FFF3FPState::Ready, "open");
    Emit(FFF3FPEvent::OpenCompleted, "{\"success\":true}");
    if (staticImage_ && decodeMode_ == FFF3FPDecodeMode::D3D11)
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"decodeMode\":1,\"reason\":\"Still images use CPU decoding for immediate, device-independent loading.\"}");
    else if (hardwareFallback)
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"video\",\"decodeMode\":1,\"fallback\":true,\"reason\":\"" +
            EscapeJson(hardwareFailureReason.empty()
                ? "The GPU rejected the video stream; playback is using CPU decoding."
                : hardwareFailureReason) + "\"}");
    if (forcedSdrOutput)
        Emit(FFF3FPEvent::ColorModeChanged, "{\"requested\":0,\"actual\":0,\"reason\":\"True HDR output is only available for HDR source video.\"}");
    else if (snapshot_.requestedColorMode == FFF3FPColorMode::MapToHdr && snapshot_.actualColorMode != snapshot_.requestedColorMode)
        Emit(FFF3FPEvent::ColorModeChanged, "{\"requested\":2,\"actual\":0,\"reason\":\"" + EscapeJson(videoRenderer_.FallbackReason()) + "\"}");
}

FFFResult PlayerSession::DecodeInitialStillImage() noexcept {
    if (!staticImage_ || format_ == nullptr || videoDecoder_ == nullptr || videoStream_ < 0)
        return FFFResult::InvalidState;
    if (playbackPacket_ == nullptr) playbackPacket_ = av_packet_alloc();
    if (playbackPacket_ == nullptr) return FFFResult::NativeFailure;
    const auto before = videoRenderer_.PresentedVideoFrames();
    int readResult = 0;
    while (videoRenderer_.PresentedVideoFrames() == before &&
        (readResult = av_read_frame(format_, playbackPacket_)) >= 0) {
        if (playbackPacket_->stream_index == videoStream_) {
            const auto decodeResult = DecodePacket(videoDecoder_, playbackPacket_, true, format_);
            av_packet_unref(playbackPacket_);
            if (decodeResult != FFFResult::Success) return decodeResult;
        } else {
            av_packet_unref(playbackPacket_);
        }
    }
    if (videoRenderer_.PresentedVideoFrames() == before) {
        const auto drainResult = DecodePacket(videoDecoder_, nullptr, true, format_);
        if (drainResult != FFFResult::Success) return drainResult;
    }
    const auto* stream = format_->streams[videoStream_];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    if (av_seek_frame(format_, videoStream_, start, AVSEEK_FLAG_BACKWARD) >= 0)
        avcodec_flush_buffers(videoDecoder_);
    if (state_.load() == FFF3FPState::Failed) return FFFResult::DeviceFailure;
    return videoRenderer_.PresentedVideoFrames() > before
        ? FFFResult::Success : FFFResult::FfmpegFailure;
}

FFFResult PlayerSession::ProbeHardwareVideo(AVFormatContext* owner, AVCodecContext* decoder,
    const std::int32_t streamIndex, const std::int32_t hardwarePixelFormat) noexcept {
    if (owner == nullptr || decoder == nullptr || streamIndex < 0) return FFFResult::InvalidState;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr) { av_packet_free(&packet); av_frame_free(&frame); return FFFResult::NativeFailure; }
    FFFResult result = FFFResult::NotSupported;
    int videoPacketCount = 0;
    int readResult = 0;
    while (videoPacketCount < 512 && (readResult = av_read_frame(owner, packet)) >= 0) {
        if (packet->stream_index != streamIndex) { av_packet_unref(packet); continue; }
        ++videoPacketCount;
        FilterAv1HardwareTimecodeMetadata(decoder, packet);
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
    if (result != FFFResult::Success && readResult < 0) {
        avcodec_send_packet(decoder, nullptr);
        while (avcodec_receive_frame(decoder, frame) >= 0) {
            result = frame->format == hardwarePixelFormat ? FFFResult::Success : FFFResult::NotSupported;
            av_frame_unref(frame);
            if (result == FFFResult::Success) break;
        }
    }
    av_packet_free(&packet); av_frame_free(&frame);
    const auto* stream = owner->streams[streamIndex];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    av_seek_frame(owner, streamIndex, start, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(decoder);
    return result;
}

FFFResult PlayerSession::FallbackToSoftwareVideoDecoder(const char* reason) noexcept {
    if (format_ == nullptr || videoStream_ < 0 || snapshot_.decodeMode != FFF3FPDecodeMode::Gpu)
        return FFFResult::FfmpegFailure;
    AVCodecContext* softwareDecoder = nullptr;
    const auto openResult = OpenDecoder(format_, videoStream_, true, &softwareDecoder,
        -1, nullptr, false);
    if (openResult != FFFResult::Success) return openResult;
    const auto resumePosition = std::max<std::int64_t>(0, snapshot_.position100ns);
    avcodec_free_context(&videoDecoder_);
    videoDecoder_ = softwareDecoder;
    if (videoDecodeFrame_ != nullptr) av_frame_unref(videoDecodeFrame_);
    if (videoTransferFrame_ != nullptr) av_frame_unref(videoTransferFrame_);
    ClearVideoQueue();
    snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
    DoSeek(resumePosition);
    RebuildMediaInfo();
    Emit(FFF3FPEvent::DeviceChanged,
        "{\"type\":\"video\",\"decodeMode\":1,\"fallback\":true,\"reason\":\"" + EscapeJson(reason == nullptr ?
            "The hardware decoder failed." : reason) + "\"}");
    return FFFResult::Success;
}

void PlayerSession::PumpPlayback() noexcept {
    if (format_ == nullptr) { SetState(FFF3FPState::Failed); return; }
    UpdateBitRateForPosition(ClockPosition());
    if (PumpVideoPresentation()) return;
    if (videoStream_ < 0 && audioStream_ >= 0 && !audioRenderer_) {
        // Windows can block both shared and exclusive initialization while a
        // different process owns the endpoint. Keep audio-only media on its
        // normal wall clock instead of draining the entire file immediately.
        snapshot_.position100ns = std::min(ClockPosition(), snapshot_.duration100ns);
        PublishSnapshot();
        if (snapshot_.duration100ns > 0 && snapshot_.position100ns >= snapshot_.duration100ns)
            FlushAtEnd();
        else
            Sleep(10);
        return;
    }
    const auto audioBuffered = audioRenderer_ ? audioRenderer_->Buffered100ns() : 0;
    const auto videoSaturated = [&] {
        if (videoFrameQueue_.empty()) return false;
        const auto lookAhead = VideoFramePosition(videoFrameQueue_.back()) - ClockPosition();
        const auto queueLimit = VideoFrameQueueLimit(videoFrameQueue_);
        return videoFrameQueue_.size() >= std::max<std::size_t>(1, queueLimit - 1) ||
            (videoFrameQueue_.size() >= MinimumQueuedVideoFrames &&
                lookAhead >= TargetVideoLookAhead100ns);
    };
    if (!pendingAudioPackets_.empty() && audioBuffered < TargetAudioBuffer100ns) {
        auto* packet = pendingAudioPackets_.front();
        pendingAudioPackets_.pop_front();
        pendingAudioPacketBytes_ -= static_cast<std::size_t>(std::max(packet->size, 0));
        DecodePacket(audioDecoder_, packet, false, format_);
        av_packet_free(&packet);
        return;
    }
    if (!pendingVideoPackets_.empty() && !videoSaturated()) {
        auto* packet = pendingVideoPackets_.front();
        pendingVideoPackets_.pop_front();
        pendingVideoPacketBytes_ -= static_cast<std::size_t>(std::max(packet->size, 0));
        DecodePacket(videoDecoder_, packet, true, format_);
        av_packet_free(&packet);
        return;
    }
    if (videoStream_ < 0 && audioRenderer_ && audioBuffered >= TargetAudioBuffer100ns) { Sleep(2); return; }
    if (!draining_ && externalFormat_ != nullptr && audioRenderer_ &&
        audioBuffered < TargetAudioBuffer100ns) PumpExternalAudio();
    const auto videoPacketsFull = pendingVideoPackets_.size() >= MaximumPendingVideoPackets ||
        pendingVideoPacketBytes_ >= MaximumPendingVideoPacketBytes;
    const auto audioPacketsFull = pendingAudioPackets_.size() >= MaximumPendingAudioPackets ||
        pendingAudioPacketBytes_ >= MaximumPendingAudioPacketBytes;
    if (videoSaturated() &&
        ((!audioRenderer_ || audioBuffered >= TargetAudioBuffer100ns) || videoPacketsFull)) {
        Sleep(1);
        return;
    }
    if (audioRenderer_ && audioBuffered >= TargetAudioBuffer100ns && audioPacketsFull &&
        pendingVideoPackets_.empty()) {
        Sleep(1);
        return;
    }
    if (playbackPacket_ == nullptr) playbackPacket_ = av_packet_alloc();
    if (playbackPacket_ == nullptr) { Fail(FFFResult::NativeFailure, "Could not allocate a playback packet."); return; }
    const auto result = av_read_frame(format_, playbackPacket_);
    if (result < 0) {
        av_packet_unref(playbackPacket_);
        if (!pendingVideoPackets_.empty() || !pendingAudioPackets_.empty()) { Sleep(1); return; }
        FlushAtEnd(); return;
    }
    TrackPacketBitRate(playbackPacket_, format_);
    if (playbackPacket_->stream_index == videoStream_) {
        if (videoSaturated()) {
            auto* retained = av_packet_clone(playbackPacket_);
            if (retained == nullptr) {
                av_packet_unref(playbackPacket_);
                Fail(FFFResult::NativeFailure, "Could not retain a bounded compressed video packet.", "decode");
                return;
            }
            pendingVideoPacketBytes_ += static_cast<std::size_t>(std::max(retained->size, 0));
            pendingVideoPackets_.push_back(retained);
        } else {
            DecodePacket(videoDecoder_, playbackPacket_, true, format_);
        }
    }
    else if (playbackPacket_->stream_index == audioStream_ && externalFormat_ == nullptr) {
        if (audioRenderer_ && audioBuffered >= TargetAudioBuffer100ns) {
            auto* retained = av_packet_clone(playbackPacket_);
            if (retained == nullptr) {
                av_packet_unref(playbackPacket_);
                Fail(FFFResult::NativeFailure, "Could not retain a bounded compressed audio packet.", "decode");
                return;
            }
            pendingAudioPacketBytes_ += static_cast<std::size_t>(std::max(retained->size, 0));
            pendingAudioPackets_.push_back(retained);
        } else {
            DecodePacket(audioDecoder_, playbackPacket_, false, format_);
        }
    }
    av_packet_unref(playbackPacket_);
}

FFFResult PlayerSession::DecodePacket(AVCodecContext* decoder, AVPacket* packet, const bool video,
    AVFormatContext* owner) noexcept {
    if (decoder == nullptr) return FFFResult::Success;
    if (packet != nullptr && packet->size == 0 && packet->side_data_elems == 0)
        return FFFResult::Success;
    AVFrame*& reusableFrame = video ? videoDecodeFrame_ :
        (owner == externalFormat_ ? externalAudioDecodeFrame_ : audioDecodeFrame_);
    if (reusableFrame == nullptr) reusableFrame = av_frame_alloc();
    if (reusableFrame == nullptr) return FFFResult::NativeFailure;
    auto* frame = reusableFrame;
    av_frame_unref(frame);
    const auto handleFrame = [this, video, owner](AVFrame* decoded) {
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
            if (hardwareFallbackPending_ || internalAudioFailurePending_) break;
        }
        return receiveResult;
    };
    if (video) FilterAv1HardwareTimecodeMetadata(decoder, packet);
    auto result = avcodec_send_packet(decoder, packet);
    if (result == AVERROR(EAGAIN)) {
        const auto receiveResult = receiveFrames();
        if (hardwareFallbackPending_) {
            hardwareFallbackPending_ = false;
            auto reason = pendingHardwareFallbackReason_.empty()
                ? std::string("The GPU frame could not be transferred for presentation; playback continued with CPU decoding.")
                : std::move(pendingHardwareFallbackReason_);
            pendingHardwareFallbackReason_.clear();
            const auto fallbackResult = FallbackToSoftwareVideoDecoder(reason.c_str());
            if (fallbackResult != FFFResult::Success)
                Fail(fallbackResult, "Could not fall back to CPU decoding after a GPU frame-transfer failure.");
            return fallbackResult;
        }
        if (!video && owner == format_ && internalAudioFailurePending_) {
            const auto failureResult = internalAudioFailureResult_;
            auto message = audioRenderer_ ? audioRenderer_->LastError() : std::string{};
            DisableFailedInternalAudio(failureResult,
                message.empty() ? "The selected audio track could not be rendered." : std::move(message));
            return failureResult;
        }
        if (receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF) {
            if (video && snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
                FallbackToSoftwareVideoDecoder(("The GPU stopped decoding the video stream: " +
                    FfmpegError(receiveResult) + "; playback continued with CPU decoding.").c_str()) == FFFResult::Success)
                return FFFResult::Success;
            const auto message = std::string(video
                ? "Video decoder failed while making room for a packet: "
                : "Audio decoder failed while making room for a packet: ") + FfmpegError(receiveResult);
            if (!video && owner == format_) HandleInternalAudioDecodeFailure(FFFResult::FfmpegFailure, message);
            else Fail(FFFResult::FfmpegFailure, message);
            return FFFResult::FfmpegFailure;
        }
        result = avcodec_send_packet(decoder, packet);
    }
    if (result < 0 && result != AVERROR_EOF) {
        if (video && snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
            FallbackToSoftwareVideoDecoder(("The GPU rejected a video packet: " +
                FfmpegError(result) + "; playback continued with CPU decoding.").c_str()) == FFFResult::Success)
            return FFFResult::Success;
        const auto message = "Decoder rejected packet: " + FfmpegError(result);
        if (!video && owner == format_) HandleInternalAudioDecodeFailure(FFFResult::FfmpegFailure, message);
        else Fail(FFFResult::FfmpegFailure, message);
        return FFFResult::FfmpegFailure;
    }
    result = receiveFrames();
    if (!video && owner == format_ && internalAudioFailurePending_) {
        const auto failureResult = internalAudioFailureResult_;
        auto message = audioRenderer_ ? audioRenderer_->LastError() : std::string{};
        DisableFailedInternalAudio(failureResult,
            message.empty() ? "The selected audio track could not be rendered." : std::move(message));
        return failureResult;
    }
    if (hardwareFallbackPending_) {
        hardwareFallbackPending_ = false;
        auto reason = pendingHardwareFallbackReason_.empty()
            ? std::string("The GPU frame could not be transferred for presentation; playback continued with CPU decoding.")
            : std::move(pendingHardwareFallbackReason_);
        pendingHardwareFallbackReason_.clear();
        const auto fallbackResult = FallbackToSoftwareVideoDecoder(reason.c_str());
        if (fallbackResult != FFFResult::Success)
            Fail(fallbackResult, "Could not fall back to CPU decoding after a GPU frame-transfer failure.");
        return fallbackResult;
    }
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF && video &&
        snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
        FallbackToSoftwareVideoDecoder(("The GPU stopped decoding the video stream: " +
            FfmpegError(result) + "; playback continued with CPU decoding.").c_str()) == FFFResult::Success)
        return FFFResult::Success;
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF && !video && owner == format_) {
        HandleInternalAudioDecodeFailure(FFFResult::FfmpegFailure,
            "The selected audio track stopped decoding: " + FfmpegError(result));
        return FFFResult::FfmpegFailure;
    }
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
        av_frame_unref(frame);
        videoFramePool_.push_back(frame);
        if (hardwareFallbackPending_) {
            hardwareFallbackPending_ = false;
            auto reason = pendingHardwareFallbackReason_.empty()
                ? std::string("The GPU frame could not be transferred for presentation; playback continued with CPU decoding.")
                : std::move(pendingHardwareFallbackReason_);
            pendingHardwareFallbackReason_.clear();
            const auto result = FallbackToSoftwareVideoDecoder(reason.c_str());
            if (result != FFFResult::Success)
                Fail(result, "Could not fall back to CPU decoding after a GPU frame-transfer failure.");
        }
        return true;
    }
    const auto lookAhead = VideoFramePosition(videoFrameQueue_.back()) - now;
    const auto queueLimit = VideoFrameQueueLimit(videoFrameQueue_);
    const auto saturated = videoFrameQueue_.size() >= std::max<std::size_t>(1, queueLimit - 1) ||
        (videoFrameQueue_.size() >= MinimumQueuedVideoFrames &&
            lookAhead >= TargetVideoLookAhead100ns);
    // Continue demuxing toward audio while video is ahead, retaining compressed
    // video packets instead of several additional full decoded 4K frames.
    const auto audioNeedsData = audioRenderer_ &&
        audioRenderer_->Buffered100ns() < TargetAudioBuffer100ns;
    const auto packetCapacityAvailable =
        pendingVideoPackets_.size() < MaximumPendingVideoPackets &&
        pendingVideoPacketBytes_ < MaximumPendingVideoPacketBytes;
    if (saturated && (!audioNeedsData || !packetCapacityAvailable)) {
        std::unique_lock lock(mutex_);
        // The media clock owns presentation.  Once the bounded queue is full,
        // sleep until its front frame becomes due (or a command arrives) instead
        // of polling every millisecond and spending an idle core on look-ahead.
        const auto wait100ns = std::max<std::int64_t>(0, position - now - 20'000);
        const auto waitMilliseconds = std::clamp<std::int64_t>((wait100ns + 9'999) / 10'000, 1, 20);
        if (commands_.empty() && !terminate_)
            commandCondition_.wait_for(lock, std::chrono::milliseconds(waitMilliseconds));
        return true;
    }
    return false;
}

void PlayerSession::QueueVideoFrame(AVFrame* frame) noexcept {
    AVFrame* queued = nullptr;
    if (videoFramePool_.empty()) queued = av_frame_alloc();
    else { queued = videoFramePool_.back(); videoFramePool_.pop_back(); }
    if (queued == nullptr || av_frame_ref(queued, frame) < 0) {
        if (queued != nullptr) { av_frame_unref(queued); videoFramePool_.push_back(queued); }
        Fail(FFFResult::NativeFailure, "Could not queue the decoded video frame.", "decode");
        return;
    }
    videoFrameQueue_.push_back(queued);
    ++snapshot_.decodedVideoFrames;
    snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
}

void PlayerSession::ClearVideoQueue() noexcept {
    for (auto* frame : videoFrameQueue_) {
        av_frame_unref(frame);
        videoFramePool_.push_back(frame);
    }
    videoFrameQueue_.clear();
    snapshot_.queuedVideoFrames = 0;
    ClearPendingPackets();
}

void PlayerSession::ClearPendingPackets() noexcept {
    for (auto*& packet : pendingVideoPackets_) av_packet_free(&packet);
    pendingVideoPackets_.clear();
    pendingVideoPacketBytes_ = 0;
    for (auto*& packet : pendingAudioPackets_) av_packet_free(&packet);
    pendingAudioPackets_.clear();
    pendingAudioPacketBytes_ = 0;
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
    if (pts != AV_NOPTS_VALUE) {
        if (rebuildingFrameIndex_) {
            // During an out-of-window SeekFrame rebuild, the rolling PTS table
            // is rebuilt from the stream start. Its absolute index must follow
            // the decoded ordinal rather than the deque's local size.
            nextIndex = std::max<std::int64_t>(0, snapshot_.frameIndex + 1);
            auto indexed = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), pts);
            if (indexed == framePtsIndex_.end() || *indexed != pts)
                framePtsIndex_.insert(indexed, pts);
        } else if (framePtsIndex_.empty() || pts > framePtsIndex_.back()) {
            framePtsIndex_.push_back(pts);
            nextIndex = framePtsIndexBase_ + static_cast<std::int64_t>(framePtsIndex_.size() - 1);
        } else {
            auto indexed = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), pts);
            if (indexed == framePtsIndex_.end() || *indexed != pts)
                indexed = framePtsIndex_.insert(indexed, pts);
            nextIndex = framePtsIndexBase_ +
                static_cast<std::int64_t>(std::distance(framePtsIndex_.begin(), indexed));
        }
        while (framePtsIndex_.size() > MaximumIndexedVideoFrames) {
            framePtsIndex_.pop_front();
            ++framePtsIndexBase_;
        }
    }
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
    if (IsHardwareFrame(frame) && frame->format != AV_PIX_FMT_D3D11) {
        if (videoTransferFrame_ == nullptr) videoTransferFrame_ = av_frame_alloc();
        if (videoTransferFrame_ == nullptr) {
            Fail(FFFResult::NativeFailure, "Could not allocate the reusable hardware-transfer frame.");
            return;
        }
        av_frame_unref(videoTransferFrame_);
        const auto transferStart = std::chrono::steady_clock::now();
        const auto transferResult = av_hwframe_transfer_data(videoTransferFrame_, frame, 0);
        if (transferResult < 0) {
            if (snapshot_.decodeMode == FFF3FPDecodeMode::Gpu) {
                hardwareFallbackPending_ = true;
                pendingHardwareFallbackReason_ = "The GPU frame transfer failed: " +
                    FfmpegError(transferResult) + "; playback continued with CPU decoding.";
                return;
            }
            Fail(FFFResult::FfmpegFailure, "Could not transfer the hardware-decoded frame for presentation.");
            return;
        }
        snapshot_.hardwareTransfer100ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - transferStart).count() / 100);
        av_frame_copy_props(videoTransferFrame_, frame);
        renderFrame = videoTransferFrame_;
    }
    const auto previousColorMode = snapshot_.actualColorMode;
    const auto firstFrameForMedia = snapshot_.framePts == AV_NOPTS_VALUE;
    if (staticImage_ && owner == format_) {
        if (stillImageFrame_ == nullptr) stillImageFrame_ = av_frame_alloc();
        if (stillImageFrame_ == nullptr) {
            Fail(FFFResult::NativeFailure, "Could not retain the still-image frame for graphics recovery.");
            return;
        }
        av_frame_unref(stillImageFrame_);
        const auto retainResult = av_frame_ref(stillImageFrame_, renderFrame);
        if (retainResult < 0) {
            Fail(FFFResult::NativeFailure,
                "Could not retain the still-image frame for graphics recovery: " +
                FfmpegError(retainResult));
            return;
        }
    }
    const auto renderResult = videoRenderer_.Render(renderFrame);
    if (renderResult != FFFResult::Success) {
        if (renderResult == FFFResult::DeviceFailure &&
            videoRenderer_.RequestRecoveryIfDeviceLost()) return;
        if (renderResult == FFFResult::NotSupported &&
            snapshot_.decodeMode == FFF3FPDecodeMode::Gpu) {
            hardwareFallbackPending_ = true;
            pendingHardwareFallbackReason_ = videoRenderer_.LastError();
            if (!pendingHardwareFallbackReason_.empty())
                pendingHardwareFallbackReason_ += " Playback continued with CPU decoding.";
            return;
        }
        Fail(renderResult, videoRenderer_.LastError(), "render"); return;
    }
    snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
    snapshot_.position100ns = position; snapshot_.frameIndex = nextIndex; snapshot_.framePts = pts;
    snapshot_.frameTimeBaseNumerator = stream->time_base.num;
    snapshot_.frameTimeBaseDenominator = stream->time_base.den;
    snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    snapshot_.sourcePeakNits = static_cast<std::uint32_t>(std::lround(videoRenderer_.SourcePeakNits()));
    ApplyHdrState(snapshot_, videoRenderer_.HdrState());
    UpdateAudioDiagnostics();
    PublishSnapshot();
    if (firstFrameForMedia) RebuildMediaInfo();
    if (snapshot_.actualColorMode != previousColorMode) {
        std::ostringstream json; json << "{\"requested\":" << static_cast<unsigned>(snapshot_.requestedColorMode)
            << ",\"actual\":" << static_cast<unsigned>(snapshot_.actualColorMode) << ",\"reason\":\""
            << EscapeJson(videoRenderer_.FallbackReason()) << "\"}";
        Emit(FFF3FPEvent::ColorModeChanged, json.str());
    }
    seekTarget100ns_ = -1; seekTargetFrame_ = -1; keyframeSeekPending_ = false;
    rebuildingFrameIndex_ = false;
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
    ++snapshot_.decodedAudioFrames;
    const auto result = audioRenderer_->Enqueue(frame, position);
    if (result == FFFResult::Success && owner == format_) internalAudioDecodeErrorCount_ = 0;
    if (result == FFFResult::BufferTooSmall) ++snapshot_.audioRejectedFrames;
    else if (result != FFFResult::Success) {
        if (owner == format_) {
            internalAudioFailureResult_ = result;
            internalAudioFailurePending_ = true;
        } else {
            Fail(result, audioRenderer_->LastError(), "audio-render");
        }
    }
}

bool PlayerSession::HandleInternalAudioDecodeFailure(const FFFResult result, std::string message) noexcept {
    // A stream switch seeks on the video timeline. MPEG-TS can land on a partial
    // DTS/AC-3 access unit before the next audio sync word; reject that boundary
    // packet and let the decoder lock onto subsequent complete packets.
    if (++internalAudioDecodeErrorCount_ < 32) {
        if (audioDecoder_ != nullptr) avcodec_flush_buffers(audioDecoder_);
        return false;
    }
    DisableFailedInternalAudio(result, std::move(message));
    return true;
}

void PlayerSession::DisableFailedInternalAudio(const FFFResult result, std::string message) noexcept {
    try {
        const auto failedStream = audioStream_;
        const auto position = std::max<std::int64_t>(0, snapshot_.position100ns);
        if (audioDecoder_ != nullptr) avcodec_free_context(&audioDecoder_);
        if (audioDecodeFrame_ != nullptr) av_frame_unref(audioDecodeFrame_);
        audioStream_ = -1;
        snapshot_.selectedAudioStream = -1;
        internalAudioFailurePending_ = false;
        internalAudioFailureResult_ = FFFResult::Success;
        internalAudioDecodeErrorCount_ = 0;
        for (auto*& packet : pendingAudioPackets_) av_packet_free(&packet);
        pendingAudioPackets_.clear();
        pendingAudioPacketBytes_ = 0;
        if (audioRenderer_) audioRenderer_->Reset(position);
        ResetClock(position);
        UpdateAudioDiagnostics();
        PublishSnapshot();
        if (message.empty()) message = "The selected audio track failed.";
        message += " Playback continued without this audio track; select another track to restore audio.";
        ReportError(result, std::move(message), "audio-track");
        Emit(FFF3FPEvent::OperationCompleted,
            "{\"operation\":\"disable-failed-audio\",\"stream\":" +
            std::to_string(failedStream) + "}");
    } catch (...) {
        ReportError(FFFResult::NativeFailure,
            "The failed audio track could not be disabled cleanly, but the playback session remains available.",
            "audio-track");
    }
}

void PlayerSession::UpdateAudioDiagnostics() noexcept {
    if (audioRenderer_ == nullptr) {
        snapshot_.audioPosition100ns = 0;
        snapshot_.bufferedAudio100ns = 0;
        snapshot_.audioUnderruns = 0;
        snapshot_.audioTimestampJitterFrames = 0;
        snapshot_.audioDiscontinuities = 0;
        snapshot_.audioInsertedSilenceFrames = 0;
        snapshot_.audioDroppedOverlapFrames = 0;
        return;
    }
    snapshot_.audioPosition100ns = audioRenderer_->Position100ns();
    snapshot_.bufferedAudio100ns = audioRenderer_->Buffered100ns();
    snapshot_.audioUnderruns = audioRenderer_->UnderrunCount();
    snapshot_.audioTimestampJitterFrames = audioRenderer_->TimestampJitterCount();
    snapshot_.audioDiscontinuities = audioRenderer_->DiscontinuityCount();
    snapshot_.audioInsertedSilenceFrames = audioRenderer_->InsertedSilenceFrames();
    snapshot_.audioDroppedOverlapFrames = audioRenderer_->DroppedOverlapFrames();
}

void PlayerSession::ResetBitRateTracking() noexcept {
    videoBitRateBuckets_.clear();
    audioBitRateBuckets_.clear();
    publishedBitRateSecond_ = -1;
    snapshot_.videoBitRate = snapshot_.audioBitRate = 0;
}

void PlayerSession::TrackPacketBitRate(const AVPacket* packet, AVFormatContext* owner) noexcept {
    if (packet == nullptr || owner == nullptr || packet->size <= 0 ||
        packet->stream_index < 0 || packet->stream_index >= static_cast<int>(owner->nb_streams)) return;
    const auto* stream = owner->streams[packet->stream_index];
    if (stream == nullptr || stream->codecpar == nullptr) return;
    const bool video = stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    const bool audio = stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
    if ((!video || packet->stream_index != videoStream_) &&
        (!audio || (owner == format_ && packet->stream_index != audioStream_) ||
            (owner == externalFormat_ && packet->stream_index != externalAudioStream_))) return;

    const auto timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
    if (timestamp == AV_NOPTS_VALUE) return;
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    auto position = av_rescale_q(timestamp - start, stream->time_base,
        AVRational{1, static_cast<int>(TicksPerSecond)});
    if (owner == externalFormat_) position += externalAudioOffset100ns_;
    const auto second = std::max<std::int64_t>(0, position) / TicksPerSecond;
    auto& buckets = video ? videoBitRateBuckets_ : audioBitRateBuckets_;
    auto found = std::find_if(buckets.rbegin(), buckets.rend(), [second](const BitRateBucket& item) {
        return item.secondIndex == second;
    });
    if (found != buckets.rend()) {
        found->bytes += static_cast<std::uint32_t>(packet->size);
    } else {
        const auto insertion = std::lower_bound(buckets.begin(), buckets.end(), second,
            [](const BitRateBucket& item, const std::int64_t value) {
                return item.secondIndex < value;
            });
        buckets.insert(insertion, {second, static_cast<std::uint32_t>(packet->size)});
    }
    constexpr std::size_t MaximumBitRateBuckets = 16;
    while (buckets.size() > MaximumBitRateBuckets) buckets.pop_front();
}

void PlayerSession::UpdateBitRateForPosition(const std::int64_t position) noexcept {
    const auto currentSecond = std::max<std::int64_t>(0, position) / TicksPerSecond;
    if (currentSecond == publishedBitRateSecond_) return;
    publishedBitRateSecond_ = currentSecond;
    const auto completedSecond = currentSecond - 1;
    const auto rateFor = [completedSecond](const std::deque<BitRateBucket>& buckets) {
        if (completedSecond < 0) return std::uint64_t{0};
        const auto found = std::find_if(buckets.rbegin(), buckets.rend(),
            [completedSecond](const BitRateBucket& item) {
                return item.secondIndex == completedSecond;
            });
        if (found == buckets.rend() || found->bytes > std::numeric_limits<std::uint64_t>::max() / 8)
            return std::uint64_t{0};
        return found->bytes * 8;
    };
    snapshot_.videoBitRate = rateFor(videoBitRateBuckets_);
    snapshot_.audioBitRate = rateFor(audioBitRateBuckets_);
    PublishSnapshot();
}

void PlayerSession::PumpExternalAudio() noexcept {
    if (externalFormat_ == nullptr || externalAudioDecoder_ == nullptr) return;
    if (snapshot_.position100ns < externalAudioOffset100ns_) return;
    if (externalAudioPacket_ == nullptr) externalAudioPacket_ = av_packet_alloc();
    if (externalAudioPacket_ == nullptr) return;
    while (av_read_frame(externalFormat_, externalAudioPacket_) >= 0) {
        if (externalAudioPacket_->stream_index == externalAudioStream_) {
            TrackPacketBitRate(externalAudioPacket_, externalFormat_);
            DecodePacket(externalAudioDecoder_, externalAudioPacket_, false, externalFormat_);
            av_packet_unref(externalAudioPacket_);
            break;
        }
        av_packet_unref(externalAudioPacket_);
    }
}

void PlayerSession::FlushAtEnd() noexcept {
    if (!draining_) {
        draining_ = true;
        if (videoDecoder_) DecodePacket(videoDecoder_, nullptr, true, format_);
        if (audioDecoder_ && externalFormat_ == nullptr) DecodePacket(audioDecoder_, nullptr, false, format_);
    }
    // Some single-frame image decoders publish their only frame while draining.
    // Do not enter Ended before that queued frame has reached the renderer.
    if (!videoFrameQueue_.empty()) {
        if (!PumpVideoPresentation()) Sleep(1);
        return;
    }
    if (audioRenderer_ && audioRenderer_->Buffered100ns() > 0) { Sleep(2); return; }
    auto endPosition = snapshot_.position100ns;
    if (videoStream_ >= 0 && lastVideoFrameDuration100ns_ > 0)
        endPosition += lastVideoFrameDuration100ns_;
    if (audioRenderer_) endPosition = std::max(endPosition, audioRenderer_->Position100ns());
    snapshot_.duration100ns = std::max(snapshot_.duration100ns, endPosition);
    snapshot_.position100ns = snapshot_.duration100ns;
    RebuildMediaInfo();
    SuspendAudioRenderer(true);
    SetState(FFF3FPState::Ended, "end"); Emit(FFF3FPEvent::PlaybackEnded, "{}");
}

void PlayerSession::DoSeek(std::int64_t position, const std::int64_t targetFrame,
    const bool exact) noexcept {
    if (!format_) return; position = std::clamp<std::int64_t>(position, 0, snapshot_.duration100ns > 0 ? snapshot_.duration100ns : position);
    const auto referenceStream = videoStream_ >= 0 ? videoStream_ : audioStream_;
    auto timestamp = av_rescale_q(position, AVRational{1, static_cast<int>(TicksPerSecond)}, format_->streams[referenceStream]->time_base);
    if (format_->streams[referenceStream]->start_time != AV_NOPTS_VALUE) timestamp += format_->streams[referenceStream]->start_time;
    if (av_seek_frame(format_, referenceStream, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
        ReportError(FFFResult::FfmpegFailure,
            "FFmpeg could not seek to the requested position; playback remained active.", "seek");
        return;
    }
    ClearVideoQueue();
    ResetBitRateTracking();
    if (videoDecoder_) avcodec_flush_buffers(videoDecoder_); if (audioDecoder_) avcodec_flush_buffers(audioDecoder_);
    internalAudioDecodeErrorCount_ = 0;
    seekTarget100ns_ = position; seekTargetFrame_ = targetFrame;
    keyframeSeekPending_ = !exact && videoStream_ >= 0; draining_ = false;
    lastVideoFrameDuration100ns_ = 0;
    snapshot_.position100ns = position;
    snapshot_.frameIndex = targetFrame >= 0 && position > 0 ? targetFrame - 1 : -1;
    ++snapshot_.timelineGeneration;
    ResetClock(position); if (audioRenderer_) audioRenderer_->Reset(position);
    PublishSnapshot();
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
    if (playbackPacket_ == nullptr) playbackPacket_ = av_packet_alloc();
    if (playbackPacket_ == nullptr) return;
    while ((seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_) && !terminate_) {
        if (av_read_frame(format_, playbackPacket_) < 0) {
            // Delayed B-frames may contain the target nearest EOF. Drain once,
            // then clear any still-unfulfilled seek so later playback does not
            // remain permanently on the seeking path.
            DecodePacket(videoDecoder_, nullptr, true, format_);
            if (seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_) {
                seekTarget100ns_ = -1;
                seekTargetFrame_ = -1;
                keyframeSeekPending_ = false;
                rebuildingFrameIndex_ = false;
                PublishSnapshot();
            }
            break;
        }
        TrackPacketBitRate(playbackPacket_, format_);
        if (playbackPacket_->stream_index == videoStream_)
            DecodePacket(videoDecoder_, playbackPacket_, true, format_);
        av_packet_unref(playbackPacket_);
    }
}

void PlayerSession::DoSelectStream(const std::int32_t index, const bool video) noexcept {
    if (!format_ || index < 0 || index >= static_cast<std::int32_t>(format_->nb_streams) ||
        format_->streams[index]->codecpar->codec_type != (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO) ||
        (video && (format_->streams[index]->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0)) { ReportError(FFFResult::InvalidArgument, "The requested media stream is invalid.", "select-stream"); return; }
    AVCodecContext* replacement = nullptr;
    const auto previousDecodeMode = snapshot_.decodeMode;
    auto result = video && decodeMode_ == FFF3FPDecodeMode::D3D11
        ? OpenHardwareVideoDecoder(format_, index, &replacement)
        : OpenDecoder(format_, index, video, &replacement);
    if (result != FFFResult::Success && video && decodeMode_ == FFF3FPDecodeMode::D3D11) {
        result = OpenDecoder(format_, index, true, &replacement, -1, nullptr, false);
        if (result == FFFResult::Success) snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
    } else if (result == FFFResult::Success && video) {
        snapshot_.decodeMode = decodeMode_;
    }
    if (result != FFFResult::Success) { ReportError(result, "Could not open the selected media stream.", "select-stream"); return; }
    if (video) { if (videoDecoder_) avcodec_free_context(&videoDecoder_); videoDecoder_ = replacement; videoStream_ = index; snapshot_.selectedVideoStream = index; videoRenderer_.ConfigureHdrStream(format_->streams[index]->codecpar); ApplyHdrState(snapshot_, videoRenderer_.HdrState()); framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = false; }
    else {
        if (audioDecoder_) avcodec_free_context(&audioDecoder_);
        audioDecoder_ = replacement; audioStream_ = index; snapshot_.selectedAudioStream = index;
        internalAudioFailurePending_ = false;
        internalAudioFailureResult_ = FFFResult::Success;
        internalAudioDecodeErrorCount_ = 0;
    }
    DoSeek(snapshot_.position100ns); RebuildMediaInfo();
    if (video && snapshot_.decodeMode != previousDecodeMode)
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"decodeMode\":" + std::to_string(static_cast<unsigned>(snapshot_.decodeMode)) +
            ",\"reason\":\"The selected video stream changed the active decoder.\"}");
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
    if (!audioRenderer_ && (!audioExclusive_ || snapshot_.state == FFF3FPState::Playing)) {
        std::string audioError;
        const auto audioResult = RecreateAudioRenderer(audioEndpointId_, audioExclusive_,
            snapshot_.state != FFF3FPState::Playing, audioError);
        if (audioResult != FFFResult::Success) {
            avcodec_free_context(&replacementDecoder);
            avformat_close_input(&replacementFormat);
            ReportError(audioResult, std::move(audioError), "external-audio");
            return;
        }
    }
    if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); if (externalFormat_) avformat_close_input(&externalFormat_);
    externalFormat_ = replacementFormat; externalAudioDecoder_ = replacementDecoder; externalAudioStream_ = index;
    externalAudioOffset100ns_ = offset; externalAudioPath_ = std::move(path);
    snapshot_.isExternalAudio = 1; snapshot_.externalAudioOffset100ns = offset; DoSeek(snapshot_.position100ns);
    if (audioRenderer_) audioRenderer_->SetPaused(snapshot_.state != FFF3FPState::Playing);
    Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"load-external-audio\",\"stream\":" + std::to_string(index) + "}");
}

void PlayerSession::DoClose(const FFF3FPState finalState, const bool preserveVideoOutput) noexcept {
    if (audioRenderer_) { audioRenderer_->Stop(); audioRenderer_.reset(); }
    // SetTimedTextLayer is intentionally callable outside the session command
    // queue. Gate renderer teardown with its content lock so a final timer tick
    // cannot restart the presenter while this close is joining it.
    std::unique_lock contentLock(timedTextContentMutex_);
    if (preserveVideoOutput) videoRenderer_.ResetMedia();
    else videoRenderer_.Close();
    if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); if (externalFormat_) avformat_close_input(&externalFormat_);
    if (videoDecoder_) avcodec_free_context(&videoDecoder_); if (audioDecoder_) avcodec_free_context(&audioDecoder_);
    if (coverArtFrame_) av_frame_free(&coverArtFrame_);
    if (stillImageFrame_) av_frame_free(&stillImageFrame_);
    if (videoDecodeFrame_) av_frame_free(&videoDecodeFrame_);
    if (videoTransferFrame_) av_frame_free(&videoTransferFrame_);
    if (audioDecodeFrame_) av_frame_free(&audioDecodeFrame_);
    if (externalAudioDecodeFrame_) av_frame_free(&externalAudioDecodeFrame_);
    if (playbackPacket_) av_packet_free(&playbackPacket_);
    if (externalAudioPacket_) av_packet_free(&externalAudioPacket_);
    ClearVideoQueue();
    for (auto*& frame : videoFramePool_) av_frame_free(&frame);
    videoFramePool_.clear();
    if (format_) avformat_close_input(&format_);
    videoStream_ = audioStream_ = coverArtStream_ = externalAudioStream_ = -1; externalAudioPath_.clear(); framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = false;
    timedTextContentCache_.clear();
    externalAudioOffset100ns_ = 0; seekTarget100ns_ = seekTargetFrame_ = -1;
    keyframeSeekPending_ = false; lastVideoFrameDuration100ns_ = 0; draining_ = false;
    staticImage_ = false;
    hardwareFallbackPending_ = false;
    pendingHardwareFallbackReason_.clear();
    internalAudioFailurePending_ = false;
    internalAudioFailureResult_ = FFFResult::Success;
    internalAudioDecodeErrorCount_ = 0;
    ResetBitRateTracking();
    {
        std::lock_guard lock(mutex_); snapshot_.state = finalState; snapshot_.position100ns = 0; snapshot_.duration100ns = 0;
        snapshot_.frameIndex = -1; snapshot_.selectedVideoStream = -1; snapshot_.selectedAudioStream = -1;
        snapshot_.videoWidth = snapshot_.videoHeight = 0; snapshot_.isHdrSource = 0;
        snapshot_.isExternalAudio = 0; snapshot_.externalAudioOffset100ns = 0;
        snapshot_.decodedVideoFrames = snapshot_.presentedVideoFrames = snapshot_.droppedVideoFrames = 0;
        snapshot_.decodedAudioFrames = snapshot_.audioUnderruns = 0;
        snapshot_.audioPosition100ns = snapshot_.bufferedAudio100ns = 0;
        snapshot_.audioTimestampJitterFrames = snapshot_.audioDiscontinuities = 0;
        snapshot_.audioInsertedSilenceFrames = snapshot_.audioDroppedOverlapFrames = 0;
        snapshot_.coalescedVideoFrames = snapshot_.audioRejectedFrames = 0;
        snapshot_.swapChainPresents = snapshot_.presentWait100ns = 0;
        snapshot_.deviceLockWait100ns = snapshot_.hardwareTransfer100ns = 0;
        snapshot_.softwareConvert100ns = 0;
        snapshot_.videoBitRate = snapshot_.audioBitRate = 0;
        snapshot_.queuedVideoFrames = 0; snapshot_.sourcePeakNits = 0;
        ApplyHdrState(snapshot_, {});
        // During same-HWND media replacement the flip chain is intentionally
        // retained. Keep reporting its real mode while Opening; the next source
        // commits SDR only after ForceSdrOutputForSdrSource has reconfigured DXGI.
        snapshot_.actualColorMode = preserveVideoOutput
            ? videoRenderer_.ActualColorMode() : FFF3FPColorMode::MapToSdr;
        mediaInfoJson_.clear();
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
    std::ostringstream json;
    const auto formatName = format_->iformat && format_->iformat->name ? format_->iformat->name : "";
    const auto formatLongName = format_->iformat && format_->iformat->long_name ? format_->iformat->long_name : "";
    const auto* majorBrand = av_dict_get(format_->metadata, "major_brand", nullptr, 0);
    const auto* compatibleBrands = av_dict_get(format_->metadata, "compatible_brands", nullptr, 0);
    json << "{\"format\":\"" << EscapeJson(formatName)
        << "\",\"formatLongName\":\"" << EscapeJson(formatLongName)
        << "\",\"formatCodecId\":\"" << EscapeJson(majorBrand ? majorBrand->value : "")
        << "\",\"compatibleBrands\":\"" << EscapeJson(compatibleBrands ? compatibleBrands->value : "")
        << "\",\"duration100ns\":" << snapshot_.duration100ns
        << ",\"startTime100ns\":" << (format_->start_time == AV_NOPTS_VALUE ? 0 :
            av_rescale_q(format_->start_time, AV_TIME_BASE_Q, AVRational{1, static_cast<int>(TicksPerSecond)}))
        << ",\"bitRate\":" << std::max<std::int64_t>(0, format_->bit_rate)
        << ",\"fileSize\":" << std::max<std::int64_t>(0, format_->pb ? avio_size(format_->pb) : 0)
        << ",\"probeScore\":" << format_->probe_score
        << ",\"metadata\":";
    AppendDictionaryJson(json, format_->metadata);
    json << ",\"streams\":[";
    for (unsigned index = 0; index < format_->nb_streams; ++index) {
        if (index) json << ',';
        auto* stream = format_->streams[index];
        const auto* parameters = stream->codecpar;
        const auto* descriptor = avcodec_descriptor_get(parameters->codec_id);
        const auto frameRate = av_guess_frame_rate(format_, stream, nullptr);
        const auto nominalFrameRate = stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0
            ? stream->r_frame_rate : frameRate;
        const auto sampleAspect = av_guess_sample_aspect_ratio(format_, stream, nullptr);
        const auto displayAspect = (sampleAspect.num > 0 && sampleAspect.den > 0)
            ? av_mul_q(AVRational{std::max(1, parameters->width), std::max(1, parameters->height)}, sampleAspect)
            : AVRational{std::max(1, parameters->width), std::max(1, parameters->height)};
        const auto streamBitRate = std::max<std::int64_t>(0, parameters->bit_rate);
        std::int64_t streamSize = 0;
        if (streamBitRate > 0 && stream->duration != AV_NOPTS_VALUE && stream->time_base.den != 0) {
            const auto seconds = static_cast<long double>(stream->duration) * stream->time_base.num / stream->time_base.den;
            const auto bytes = seconds > 0 ? seconds * streamBitRate / 8.0L : 0.0L;
            streamSize = bytes >= static_cast<long double>(INT64_MAX) ? INT64_MAX :
                static_cast<std::int64_t>(std::llround(std::max(0.0L, bytes)));
        }
        const auto* pixelDescriptor = parameters->codec_type == AVMEDIA_TYPE_VIDEO
            ? av_pix_fmt_desc_get(static_cast<AVPixelFormat>(parameters->format)) : nullptr;
        const auto sourceBitDepth = pixelDescriptor == nullptr ? parameters->bits_per_raw_sample :
            std::max(parameters->bits_per_raw_sample, PixelFormatBitDepth(static_cast<AVPixelFormat>(parameters->format)));
        const auto isLossless = descriptor != nullptr &&
            (descriptor->props & AV_CODEC_PROP_LOSSLESS) != 0 && (descriptor->props & AV_CODEC_PROP_LOSSY) == 0;
        json << "{\"index\":" << index << ",\"type\":\"" << MediaTypeName(parameters->codec_type)
             << "\",\"streamId\":" << stream->id
             << ",\"codec\":\"" << EscapeJson(descriptor ? descriptor->name : "unknown")
             << "\",\"codecLongName\":\"" << EscapeJson(descriptor && descriptor->long_name ? descriptor->long_name : "")
             << "\",\"codecTag\":\"" << EscapeJson(CodecTagName(parameters->codec_tag))
             << "\",\"timeBaseNumerator\":"
             << stream->time_base.num << ",\"timeBaseDenominator\":" << stream->time_base.den
             << ",\"bitRate\":" << streamBitRate
             << ",\"streamSize\":" << streamSize
             << ",\"lossless\":" << (isLossless ? "true" : "false")
             << ",\"startTime100ns\":" << (stream->start_time == AV_NOPTS_VALUE ? 0 :
                 av_rescale_q(stream->start_time, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}))
             << ",\"duration100ns\":" << (stream->duration == AV_NOPTS_VALUE ? 0 :
                 av_rescale_q(stream->duration, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}))
             << ",\"frames\":" << std::max<std::int64_t>(0, stream->nb_frames)
             << ",\"extradataSize\":" << std::max(0, parameters->extradata_size)
             << ",\"default\":" << ((stream->disposition & AV_DISPOSITION_DEFAULT) != 0 ? "true" : "false")
             << ",\"forced\":" << ((stream->disposition & AV_DISPOSITION_FORCED) != 0 ? "true" : "false")
             << ",\"disposition\":\"";
        std::vector<std::string> dispositions;
        const auto addDisposition = [&](const int flag, const char* name) {
            if ((stream->disposition & flag) != 0) dispositions.emplace_back(name);
        };
        addDisposition(AV_DISPOSITION_DEFAULT, "default"); addDisposition(AV_DISPOSITION_DUB, "dub");
        addDisposition(AV_DISPOSITION_ORIGINAL, "original"); addDisposition(AV_DISPOSITION_COMMENT, "comment");
        addDisposition(AV_DISPOSITION_LYRICS, "lyrics"); addDisposition(AV_DISPOSITION_KARAOKE, "karaoke");
        addDisposition(AV_DISPOSITION_FORCED, "forced"); addDisposition(AV_DISPOSITION_HEARING_IMPAIRED, "hearing_impaired");
        addDisposition(AV_DISPOSITION_VISUAL_IMPAIRED, "visual_impaired"); addDisposition(AV_DISPOSITION_CLEAN_EFFECTS, "clean_effects");
        addDisposition(AV_DISPOSITION_ATTACHED_PIC, "attached_pic"); addDisposition(AV_DISPOSITION_TIMED_THUMBNAILS, "timed_thumbnails");
        for (std::size_t item = 0; item < dispositions.size(); ++item) {
            if (item) json << ',';
            json << EscapeJson(dispositions[item]);
        }
        json << "\",\"metadata\":";
        AppendDictionaryJson(json, stream->metadata);
        const auto* profile = avcodec_profile_name(parameters->codec_id, parameters->profile);
        if (profile != nullptr) json << ",\"profile\":\"" << EscapeJson(profile) << "\"";
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            json << ",\"width\":" << parameters->width << ",\"height\":" << parameters->height
                 << ",\"averageFrameRateNumerator\":" << frameRate.num
                 << ",\"averageFrameRateDenominator\":" << frameRate.den
                 << ",\"nominalFrameRateNumerator\":" << nominalFrameRate.num
                 << ",\"nominalFrameRateDenominator\":" << nominalFrameRate.den
                 << ",\"frameRateMode\":\"" <<
                    (av_cmp_q(frameRate, nominalFrameRate) == 0 ? "constant" : "variable") << "\""
                 << ",\"sampleAspectNumerator\":" << sampleAspect.num
                 << ",\"sampleAspectDenominator\":" << sampleAspect.den
                 << ",\"displayAspectNumerator\":" << displayAspect.num
                 << ",\"displayAspectDenominator\":" << displayAspect.den
                  << ",\"hdr\":" << ((parameters->color_trc == AVCOL_TRC_SMPTE2084 ||
                    parameters->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
                    av_packet_side_data_get(parameters->coded_side_data,
                        parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF) != nullptr ||
                    av_packet_side_data_get(parameters->coded_side_data,
                        parameters->nb_coded_side_data, AV_PKT_DATA_DYNAMIC_HDR10_PLUS) != nullptr) ? "true" : "false")
                  << ",\"attachedPicture\":" << ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0 ? "true" : "false");
            const auto pixelFormat = static_cast<AVPixelFormat>(parameters->format);
            const auto* pixelFormatName = av_get_pix_fmt_name(pixelFormat);
            if (pixelFormatName != nullptr)
                json << ",\"pixelFormat\":\"" << EscapeJson(pixelFormatName) << "\"";
            const auto sourceBitDepth = std::max(parameters->bits_per_raw_sample,
                PixelFormatBitDepth(pixelFormat));
            if (sourceBitDepth > 0)
                json << ",\"bitDepth\":" << sourceBitDepth;
            const bool rgb = pixelDescriptor != nullptr && (pixelDescriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;
            const auto colorModel = pixelDescriptor == nullptr ? "" :
                (rgb ? (pixelDescriptor->nb_components == 1 ? "灰度" : "RGB") : "YUV");
            std::string chromaSubsampling;
            if (pixelDescriptor != nullptr && !rgb) {
                if (pixelDescriptor->log2_chroma_w == 1 && pixelDescriptor->log2_chroma_h == 1) chromaSubsampling = "4:2:0";
                else if (pixelDescriptor->log2_chroma_w == 1 && pixelDescriptor->log2_chroma_h == 0) chromaSubsampling = "4:2:2";
                else if (pixelDescriptor->log2_chroma_w == 0 && pixelDescriptor->log2_chroma_h == 0) chromaSubsampling = "4:4:4";
            }
            json << ",\"colorModel\":\"" << EscapeJson(colorModel)
                 << "\",\"chromaSubsampling\":\"" << EscapeJson(chromaSubsampling) << "\"";
            if (static_cast<std::int32_t>(index) == videoStream_ && videoDecoder_ != nullptr) {
                const auto decoderFormat = videoDecoder_->pix_fmt;
                const auto decoderSurfaceFormat = videoDecoder_->sw_pix_fmt;
                const auto* decoderFormatName = av_get_pix_fmt_name(decoderFormat);
                const auto* decoderSurfaceName = av_get_pix_fmt_name(decoderSurfaceFormat);
                if (decoderFormatName != nullptr)
                    json << ",\"decoderPixelFormat\":\"" << EscapeJson(decoderFormatName) << "\"";
                if (decoderSurfaceName != nullptr)
                    json << ",\"decoderSurfaceFormat\":\"" << EscapeJson(decoderSurfaceName) << "\"";
                const auto decoderBitDepth = PixelFormatBitDepth(
                    decoderSurfaceFormat != AV_PIX_FMT_NONE ? decoderSurfaceFormat : decoderFormat);
                if (decoderBitDepth > 0) json << ",\"decoderBitDepth\":" << decoderBitDepth;
                const auto acceleration = HardwareAccelerationName(videoDecoder_);
                if (!acceleration.empty())
                    json << ",\"hardwareAcceleration\":\"" << EscapeJson(acceleration) << "\"";
            }
            json << ",\"colorRange\":" << parameters->color_range
                 << ",\"colorSpace\":" << parameters->color_space
                 << ",\"colorPrimaries\":" << parameters->color_primaries
                 << ",\"colorTransfer\":" << parameters->color_trc
                 << ",\"chromaLocation\":" << parameters->chroma_location
                 << ",\"fieldOrder\":" << parameters->field_order
                 << ",\"level\":" << parameters->level;
            const auto* masteringData = av_packet_side_data_get(parameters->coded_side_data,
                parameters->nb_coded_side_data, AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
            const auto* lightData = av_packet_side_data_get(parameters->coded_side_data,
                parameters->nb_coded_side_data, AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
            HdrProcessor streamHdr;
            streamHdr.ConfigureStream(parameters);
            const auto hdr = static_cast<std::int32_t>(index) == videoStream_
                ? videoRenderer_.HdrState() : streamHdr.State();
            json << ",\"hdrFormat\":\"" << EscapeJson(HdrProcessor::FormatName(hdr.format)) << "\""
                 << ",\"hdrCompatibility\":\"" << EscapeJson(HdrProcessor::CompatibilityNames(hdr.compatibility)) << "\""
                 << ",\"hdrProcessingPath\":\"" << EscapeJson(HdrProcessor::ProcessingPathName(hdr.processingPath)) << "\""
                 << ",\"dolbyVisionProfile\":" << hdr.dolbyVisionProfile
                 << ",\"dolbyVisionLevel\":" << hdr.dolbyVisionLevel
                 << ",\"dolbyVisionRpu\":" << (hdr.hasRpu ? "true" : "false")
                 << ",\"dolbyVisionEnhancementLayer\":\""
                 << EscapeJson(HdrProcessor::EnhancementLayerName(hdr.enhancementLayer)) << "\""
                 << ",\"hdrFallback\":" << (hdr.fallback ? "true" : "false")
                 << ",\"dynamicHdrMetadata\":" << (hdr.dynamicMetadata ? "true" : "false");
            if (masteringData != nullptr && masteringData->size >= sizeof(AVMasteringDisplayMetadata)) {
                const auto* mastering = reinterpret_cast<const AVMasteringDisplayMetadata*>(masteringData->data);
                std::string primaries;
                if (mastering->has_primaries) {
                    const auto redX = av_q2d(mastering->display_primaries[0][0]);
                    const auto redY = av_q2d(mastering->display_primaries[0][1]);
                    const auto greenX = av_q2d(mastering->display_primaries[1][0]);
                    const auto greenY = av_q2d(mastering->display_primaries[1][1]);
                    if (std::abs(redX - 0.68) < 0.015 && std::abs(redY - 0.32) < 0.015 &&
                        std::abs(greenX - 0.265) < 0.015 && std::abs(greenY - 0.69) < 0.015) primaries = "Display P3";
                    else primaries = "自定义";
                }
                json << ",\"masteringPrimaries\":\"" << EscapeJson(primaries) << "\"";
                if (mastering->has_luminance)
                    json << ",\"masteringMinLuminance\":" << std::setprecision(12) << av_q2d(mastering->min_luminance)
                         << ",\"masteringMaxLuminance\":" << std::setprecision(12) << av_q2d(mastering->max_luminance);
            }
            if (lightData != nullptr && lightData->size >= sizeof(AVContentLightMetadata)) {
                const auto* light = reinterpret_cast<const AVContentLightMetadata*>(lightData->data);
                json << ",\"maxCLL\":" << light->MaxCLL << ",\"maxFALL\":" << light->MaxFALL;
            }
            const char* codecConfiguration = nullptr;
            if (parameters->codec_id == AV_CODEC_ID_AV1) codecConfiguration = "av1C";
            else if (parameters->codec_id == AV_CODEC_ID_H264) codecConfiguration = "avcC";
            else if (parameters->codec_id == AV_CODEC_ID_HEVC) codecConfiguration = "hvcC";
            if (codecConfiguration != nullptr)
                json << ",\"codecConfigurationBox\":\"" << codecConfiguration << "\"";
        }
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            const auto* sampleFormat = av_get_sample_fmt_name(static_cast<AVSampleFormat>(parameters->format));
            json << ",\"sampleRate\":" << parameters->sample_rate << ",\"channels\":" << parameters->ch_layout.nb_channels
                 << ",\"channelLayout\":\"" << EscapeJson(ChannelLayoutName(parameters->ch_layout)) << "\""
                 << ",\"sampleFormat\":\"" << EscapeJson(sampleFormat ? sampleFormat : "") << "\""
                 << ",\"bitsPerCodedSample\":" << parameters->bits_per_coded_sample
                 << ",\"frameSize\":" << parameters->frame_size
                 << ",\"initialPadding\":" << parameters->initial_padding
                 << ",\"trailingPadding\":" << parameters->trailing_padding
                 << ",\"seekPreroll\":" << parameters->seek_preroll;
            const auto rawBits = std::max(parameters->bits_per_raw_sample, parameters->bits_per_coded_sample);
            json << ",\"rawSampleBits\":" << rawBits
                 << ",\"compressionMode\":\"" << (isLossless ? "无损" : "有损") << "\"";
            if (parameters->codec_id == AV_CODEC_ID_FLAC && parameters->extradata != nullptr && parameters->extradata_size >= 16) {
                static constexpr char Hex[] = "0123456789ABCDEF";
                std::string md5;
                md5.reserve(32);
                for (int byte = parameters->extradata_size - 16; byte < parameters->extradata_size; ++byte) {
                    md5.push_back(Hex[parameters->extradata[byte] >> 4]);
                    md5.push_back(Hex[parameters->extradata[byte] & 0x0f]);
                }
                json << ",\"md5\":\"" << md5 << "\"";
            }
            if (static_cast<std::int32_t>(index) == audioStream_ && audioRenderer_ != nullptr) {
                const auto validBits = audioRenderer_->OutputValidBitsPerSample();
                json << ",\"outputSampleRate\":" << audioRenderer_->OutputSampleRate()
                     << ",\"outputChannels\":" << audioRenderer_->OutputChannels()
                     << ",\"outputBitsPerSample\":" << audioRenderer_->OutputBitsPerSample()
                     << ",\"outputValidBitsPerSample\":" << validBits
                     << ",\"outputFloat\":" << (audioRenderer_->OutputIsFloat() ? "true" : "false");
            }
        }
        AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0);
        AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
        json << ",\"language\":\"" << EscapeJson(language ? language->value : "") << "\",\"title\":\"" << EscapeJson(title ? title->value : "") << "\"}";
    }
    json << "]}"; std::lock_guard lock(mutex_); mediaInfoJson_ = json.str();
}

void PlayerSession::SetState(const FFF3FPState state, const char* operation) noexcept {
    const auto previous = state_.exchange(state);
    snapshot_.state = state;
    PublishSnapshot();
    if (previous == state) return;
    std::string json = "{\"state\":" + std::to_string(static_cast<unsigned>(state));
    if (operation) json += ",\"operation\":\"" + EscapeJson(operation) + "\""; json += '}';
    Emit(FFF3FPEvent::StateChanged, json);
}

void PlayerSession::Fail(const FFFResult result, std::string message, const char* operation) noexcept {
    try { SuspendAudioRenderer(true); snapshot_.state = FFF3FPState::Failed; state_.store(FFF3FPState::Failed); PublishSnapshot();
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
        const auto buffered = audioRenderer_->Buffered100ns();
        if (externalFormat_ == nullptr || audioPosition > clockOriginPosition100ns_.load() ||
            buffered > 0) {
            PublishPlaybackClock(audioPosition, audioRenderer_->TimelineLimit100ns());
            return audioPosition;
        }
    }
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const auto origin = clockOriginQpc_.load(); if (origin == 0 || qpcFrequency_ <= 0) return clockOriginPosition100ns_.load();
    const auto position = clockOriginPosition100ns_.load() + (now.QuadPart - origin) * TicksPerSecond / qpcFrequency_;
    PublishPlaybackClock(position, snapshot_.duration100ns > 0
        ? snapshot_.duration100ns : (std::numeric_limits<std::int64_t>::max)());
    return position;
}

void PlayerSession::PublishPlaybackClock(const std::int64_t position,
    const std::int64_t limit) const noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    playbackClockSequence_.fetch_add(1, std::memory_order_acq_rel);
    playbackPosition100ns_.store(position, std::memory_order_relaxed);
    playbackClockLimit100ns_.store(std::max(position, limit), std::memory_order_relaxed);
    playbackClockSampleQpc_.store(now.QuadPart, std::memory_order_relaxed);
    playbackClockSequence_.fetch_add(1, std::memory_order_release);
}

void PlayerSession::ResetClock(const std::int64_t position) noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    clockOriginPosition100ns_ = position;
    clockOriginQpc_ = now.QuadPart;
    PublishPlaybackClock(position, position);
}

void PlayerSession::PublishSnapshot() noexcept {
    snapshot_.presentedVideoFrames = videoRenderer_.PresentedVideoFrames();
    snapshot_.coalescedVideoFrames = videoRenderer_.CoalescedVideoFrames();
    snapshot_.swapChainPresents = videoRenderer_.SwapChainPresents();
    snapshot_.presentWait100ns = videoRenderer_.PresentWait100ns();
    snapshot_.deviceLockWait100ns = videoRenderer_.DeviceLockWait100ns();
    snapshot_.softwareConvert100ns = videoRenderer_.SoftwareConvert100ns();
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
