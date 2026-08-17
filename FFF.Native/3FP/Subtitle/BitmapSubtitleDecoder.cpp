#include "pch.h"
#include "3FP/Api/FFF.Player.Api.h"
#include "Shared/Ffmpeg/SharedFileInput.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include <climits>

namespace {
constexpr std::uint32_t ApiVersion = 1;
constexpr std::int64_t TicksPerSecond = 10'000'000;
constexpr int MaximumPacketsPerRead = 4096;

std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    return av_strerror(error, buffer, sizeof(buffer)) == 0
        ? buffer : "FFmpeg error " + std::to_string(error);
}

class BitmapSubtitleDecoder final {
public:
    BitmapSubtitleDecoder() = default;
    ~BitmapSubtitleDecoder() { Close(); }
    BitmapSubtitleDecoder(const BitmapSubtitleDecoder&) = delete;
    BitmapSubtitleDecoder& operator=(const BitmapSubtitleDecoder&) = delete;

    FFFResult Open(const char* path, const std::int32_t requestedStream) noexcept {
        if (path == nullptr || *path == '\0') return FFFResult::InvalidArgument;
        try { path_ = path; } catch (...) { return FFFResult::NativeFailure; }
        sharedInput_ = SharedFileInput::Open(path, lastError_);
        if (sharedInput_ == nullptr) return FFFResult::NativeFailure;
        format_ = avformat_alloc_context();
        if (format_ == nullptr) return Fail("Could not allocate the bitmap subtitle input context.");
        format_->pb = sharedInput_->Context();
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
        const auto openResult = avformat_open_input(&format_, path, nullptr, nullptr);
        if (openResult < 0) return Fail("Could not open the bitmap subtitle: " + FfmpegError(openResult));
        const auto requestedStreamReady = requestedStream >= 0 &&
            requestedStream < static_cast<std::int32_t>(format_->nb_streams) &&
            format_->streams[requestedStream]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE &&
            format_->streams[requestedStream]->codecpar->codec_id != AV_CODEC_ID_NONE;
        if (!requestedStreamReady) {
            const auto infoResult = avformat_find_stream_info(format_, nullptr);
            if (infoResult < 0) return Fail("Could not inspect the bitmap subtitle: " + FfmpegError(infoResult));
        }
        streamIndex_ = requestedStream;
        if (streamIndex_ < 0) streamIndex_ = av_find_best_stream(format_, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
        if (streamIndex_ < 0 || streamIndex_ >= static_cast<std::int32_t>(format_->nb_streams) ||
            format_->streams[streamIndex_]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
            return Fail("The file does not contain the requested subtitle stream.", FFFResult::InvalidArgument);
        auto timelineStream = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (timelineStream < 0)
            timelineStream = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (timelineStream >= 0) {
            const auto* reference = format_->streams[timelineStream];
            if (reference->start_time != AV_NOPTS_VALUE)
                timelineOrigin100ns_ = av_rescale_q(reference->start_time, reference->time_base,
                    AVRational{1, static_cast<int>(TicksPerSecond)});
        }
        const auto codecId = format_->streams[streamIndex_]->codecpar->codec_id;
        if (codecId != AV_CODEC_ID_HDMV_PGS_SUBTITLE && codecId != AV_CODEC_ID_DVD_SUBTITLE &&
            codecId != AV_CODEC_ID_DVB_SUBTITLE && codecId != AV_CODEC_ID_XSUB)
            return Fail("The subtitle stream is not a supported bitmap format.", FFFResult::NotSupported);
        const auto* codec = avcodec_find_decoder(codecId);
        if (codec == nullptr) return Fail("FFmpeg has no decoder for this bitmap subtitle.", FFFResult::NotSupported);
        codec_ = avcodec_alloc_context3(codec);
        if (codec_ == nullptr) return Fail("Could not allocate the bitmap subtitle decoder.");
        const auto parameterResult = avcodec_parameters_to_context(codec_, format_->streams[streamIndex_]->codecpar);
        if (parameterResult < 0) return Fail("Could not configure the bitmap subtitle decoder: " + FfmpegError(parameterResult));
        const auto codecResult = avcodec_open2(codec_, codec, nullptr);
        if (codecResult < 0) return Fail("Could not start the bitmap subtitle decoder: " + FfmpegError(codecResult));
        packet_ = av_packet_alloc();
        if (packet_ == nullptr) return Fail("Could not allocate a subtitle packet.");
        return FFFResult::Success;
    }

    FFFResult Read(FFF3FPBitmapSubtitleFrame& output) noexcept {
        if (output.size < sizeof(FFF3FPBitmapSubtitleFrame) || output.version != ApiVersion)
            return FFFResult::InvalidArgument;
        if (hasPending_) return FFFResult::InvalidState;
        output = {};
        output.size = sizeof(FFF3FPBitmapSubtitleFrame);
        output.version = ApiVersion;
        int inspectedPackets = 0;
        for (;;) {
            const auto readResult = av_read_frame(format_, packet_);
            if (readResult == AVERROR_EOF) {
                output.flags = FFF3FPBitmapSubtitleFlags::EndOfStream;
                output.sequence = sequence_;
                return FFFResult::Success;
            }
            if (readResult < 0) return Fail("Could not read the bitmap subtitle: " + FfmpegError(readResult));
            ++inspectedPackets;
            if (packet_->stream_index != streamIndex_) {
                av_packet_unref(packet_);
                if (inspectedPackets >= MaximumPacketsPerRead) {
                    output.flags = FFF3FPBitmapSubtitleFlags::MoreData;
                    output.sequence = sequence_;
                    return FFFResult::Success;
                }
                continue;
            }

            const auto packetPts = packet_->pts;
            AVSubtitle subtitle{};
            int gotSubtitle = 0;
            const auto decodeResult = avcodec_decode_subtitle2(codec_, &subtitle, &gotSubtitle, packet_);
            av_packet_unref(packet_);
            if (decodeResult < 0) return Fail("Could not decode the bitmap subtitle: " + FfmpegError(decodeResult));
            if (!gotSubtitle) {
                if (inspectedPackets >= MaximumPacketsPerRead) {
                    output.flags = FFF3FPBitmapSubtitleFlags::MoreData;
                    output.sequence = sequence_;
                    return FFFResult::Success;
                }
                continue;
            }
            const auto result = BuildFrame(subtitle, packetPts, output);
            avsubtitle_free(&subtitle);
            return result;
        }
    }

    FFFResult Copy(void* output, const std::uint32_t outputSize) noexcept {
        if (!hasPending_) return FFFResult::InvalidState;
        if (pixels_.size() > outputSize || (!pixels_.empty() && output == nullptr)) return FFFResult::BufferTooSmall;
        if (!pixels_.empty()) std::memcpy(output, pixels_.data(), pixels_.size());
        hasPending_ = false;
        return FFFResult::Success;
    }

    FFFResult Seek(const std::int64_t position) noexcept {
        if (position < 0) return FFFResult::InvalidArgument;
        const auto* stream = format_->streams[streamIndex_];
        const auto absolutePosition = position + timelineOrigin100ns_;
        auto globalTimestamp = av_rescale_q(absolutePosition,
            AVRational{1, static_cast<int>(TicksPerSecond)}, AV_TIME_BASE_Q);
        if (format_->start_time != AV_NOPTS_VALUE)
            globalTimestamp = std::max(globalTimestamp, format_->start_time);
        auto result = avformat_seek_file(format_, -1, INT64_MIN, globalTimestamp,
            globalTimestamp, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            auto streamTimestamp = av_rescale_q(absolutePosition,
                AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base);
            if (stream->start_time != AV_NOPTS_VALUE)
                streamTimestamp = std::max(streamTimestamp, stream->start_time);
            result = av_seek_frame(format_, streamIndex_, streamTimestamp, AVSEEK_FLAG_BACKWARD);
        }
        if (result < 0) return Fail("Could not seek the bitmap subtitle: " + FfmpegError(result));
        avcodec_flush_buffers(codec_);
        avformat_flush(format_);
        av_packet_unref(packet_);
        pixels_.clear();
        hasPending_ = false;
        return FFFResult::Success;
    }

    const std::string& LastError() const noexcept { return lastError_; }

private:
    FFFResult BuildFrame(const AVSubtitle& subtitle, const std::int64_t packetPts,
        FFF3FPBitmapSubtitleFrame& output) noexcept {
        try {
            const auto* stream = format_->streams[streamIndex_];
            auto base = std::int64_t{};
            if (subtitle.pts != AV_NOPTS_VALUE)
                base = av_rescale_q(subtitle.pts, AVRational{1, AV_TIME_BASE}, AVRational{1, static_cast<int>(TicksPerSecond)});
            else if (packetPts != AV_NOPTS_VALUE)
                base = av_rescale_q(packetPts, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)});
            base -= timelineOrigin100ns_;
            output.start100ns = std::max<std::int64_t>(0, base + static_cast<std::int64_t>(subtitle.start_display_time) * 10'000);
            if (subtitle.end_display_time > subtitle.start_display_time && subtitle.end_display_time != UINT32_MAX)
                output.end100ns = std::max(output.start100ns,
                    base + static_cast<std::int64_t>(subtitle.end_display_time) * 10'000);
            output.canvasWidth = codec_->width;
            output.canvasHeight = codec_->height;
            output.sequence = sequence_++;

            int left = INT_MAX, top = INT_MAX, right = 0, bottom = 0;
            bool forced = false;
            for (unsigned index = 0; index < subtitle.num_rects; ++index) {
                const auto* rectangle = subtitle.rects[index];
                if (rectangle == nullptr || rectangle->type != SUBTITLE_BITMAP || rectangle->w <= 0 || rectangle->h <= 0 ||
                    rectangle->data[0] == nullptr || rectangle->data[1] == nullptr) continue;
                left = std::min(left, rectangle->x); top = std::min(top, rectangle->y);
                right = std::max(right, rectangle->x + rectangle->w); bottom = std::max(bottom, rectangle->y + rectangle->h);
                forced = forced || (rectangle->flags & AV_SUBTITLE_FLAG_FORCED) != 0;
            }
            if (left == INT_MAX) {
                output.flags = FFF3FPBitmapSubtitleFlags::Clear;
                pixels_.clear();
                hasPending_ = true;
                return FFFResult::Success;
            }
            if (output.canvasWidth <= 0) output.canvasWidth = right;
            if (output.canvasHeight <= 0) output.canvasHeight = bottom;
            left = std::max(0, left); top = std::max(0, top);
            right = std::min(output.canvasWidth, right); bottom = std::min(output.canvasHeight, bottom);
            if (right <= left || bottom <= top) return Fail("The decoded bitmap subtitle rectangle is invalid.");
            output.x = left; output.y = top; output.width = right - left; output.height = bottom - top;
            output.stride = output.width * 4;
            const auto bytes = static_cast<std::size_t>(output.stride) * output.height;
            if (bytes > UINT32_MAX) return Fail("The decoded bitmap subtitle is too large.");
            output.pixelBytes = static_cast<std::uint32_t>(bytes);
            if (forced) output.flags = FFF3FPBitmapSubtitleFlags::Forced;
            pixels_.assign(bytes, 0);

            for (unsigned index = 0; index < subtitle.num_rects; ++index) {
                const auto* rectangle = subtitle.rects[index];
                if (rectangle == nullptr || rectangle->type != SUBTITLE_BITMAP || rectangle->data[0] == nullptr ||
                    rectangle->data[1] == nullptr) continue;
                const auto* palette = reinterpret_cast<const std::uint32_t*>(rectangle->data[1]);
                for (int sourceY = 0; sourceY < rectangle->h; ++sourceY) {
                    const auto destinationY = rectangle->y + sourceY - top;
                    if (destinationY < 0 || destinationY >= output.height) continue;
                    const auto* source = rectangle->data[0] + static_cast<std::ptrdiff_t>(sourceY) * rectangle->linesize[0];
                    auto* destination = pixels_.data() + static_cast<std::ptrdiff_t>(destinationY) * output.stride;
                    for (int sourceX = 0; sourceX < rectangle->w; ++sourceX) {
                        const auto destinationX = rectangle->x + sourceX - left;
                        if (destinationX < 0 || destinationX >= output.width) continue;
                        const auto color = palette[source[sourceX]];
                        const auto alpha = static_cast<std::uint8_t>(color >> 24);
                        auto* pixel = destination + destinationX * 4;
                        pixel[0] = static_cast<std::uint8_t>(((color) & 0xffu) * alpha / 255u);
                        pixel[1] = static_cast<std::uint8_t>(((color >> 8) & 0xffu) * alpha / 255u);
                        pixel[2] = static_cast<std::uint8_t>(((color >> 16) & 0xffu) * alpha / 255u);
                        pixel[3] = alpha;
                    }
                }
            }
            hasPending_ = true;
            return FFFResult::Success;
        } catch (...) {
            return Fail("Could not allocate the decoded bitmap subtitle.", FFFResult::NativeFailure);
        }
    }

    FFFResult Fail(std::string message, const FFFResult result = FFFResult::FfmpegFailure) noexcept {
        try { lastError_ = std::move(message); } catch (...) {}
        return result;
    }

    void Close() noexcept {
        if (packet_ != nullptr) av_packet_free(&packet_);
        if (codec_ != nullptr) avcodec_free_context(&codec_);
        if (format_ != nullptr) avformat_close_input(&format_);
        sharedInput_.reset();
        pixels_.clear();
    }

    std::string path_;
    std::string lastError_;
    AVFormatContext* format_{};
    std::unique_ptr<SharedFileInput> sharedInput_;
    AVCodecContext* codec_{};
    AVPacket* packet_{};
    std::int32_t streamIndex_{-1};
    std::int64_t timelineOrigin100ns_{};
    std::int64_t sequence_{};
    std::vector<std::uint8_t> pixels_;
    bool hasPending_{};
};

FFFResult CopyUtf8(const std::string& value, char* output, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto bytes = value.size() + 1;
    if (bytes > UINT32_MAX) return FFFResult::NativeFailure;
    if (requiredSize != nullptr) *requiredSize = static_cast<std::uint32_t>(bytes);
    if (output == nullptr || outputSize < bytes) return FFFResult::BufferTooSmall;
    std::memcpy(output, value.c_str(), bytes);
    return FFFResult::Success;
}
}

FFFResult FFF3FP_OpenBitmapSubtitle(const char* path, const std::int32_t stream,
    FFF3FPBitmapSubtitleHandle* output) noexcept {
    if (output == nullptr) return FFFResult::InvalidArgument;
    *output = nullptr;
    try {
        auto decoder = std::make_unique<BitmapSubtitleDecoder>();
        const auto result = decoder->Open(path, stream);
        *output = decoder.release();
        return result;
    } catch (...) { return FFFResult::NativeFailure; }
}

FFFResult FFF3FP_ReadBitmapSubtitle(const FFF3FPBitmapSubtitleHandle decoder,
    FFF3FPBitmapSubtitleFrame* frame) noexcept {
    return decoder != nullptr && frame != nullptr
        ? static_cast<BitmapSubtitleDecoder*>(decoder)->Read(*frame) : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_CopyBitmapSubtitlePixels(const FFF3FPBitmapSubtitleHandle decoder,
    void* output, const std::uint32_t outputSize) noexcept {
    return decoder != nullptr
        ? static_cast<BitmapSubtitleDecoder*>(decoder)->Copy(output, outputSize) : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_SeekBitmapSubtitle(const FFF3FPBitmapSubtitleHandle decoder,
    const std::int64_t position) noexcept {
    return decoder != nullptr
        ? static_cast<BitmapSubtitleDecoder*>(decoder)->Seek(position) : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_GetBitmapSubtitleLastError(const FFF3FPBitmapSubtitleHandle decoder,
    char* output, const std::uint32_t size, std::uint32_t* required) noexcept {
    return decoder != nullptr
        ? CopyUtf8(static_cast<BitmapSubtitleDecoder*>(decoder)->LastError(), output, size, required)
        : FFFResult::InvalidArgument;
}

void FFF3FP_DestroyBitmapSubtitle(const FFF3FPBitmapSubtitleHandle decoder) noexcept {
    delete static_cast<BitmapSubtitleDecoder*>(decoder);
}
