#include "pch.h"
#include "3FP/Subtitle/SubtitleProbe.h"
#include "Shared/Ffmpeg/SharedFileInput.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr std::int64_t TicksPerSecond = 10'000'000;

std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    return av_strerror(error, buffer, sizeof(buffer)) == 0
        ? buffer : "FFmpeg error " + std::to_string(error);
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

const char* MediaTypeName(const AVMediaType type) noexcept {
    switch (type) { case AVMEDIA_TYPE_VIDEO: return "video"; case AVMEDIA_TYPE_AUDIO: return "audio";
    case AVMEDIA_TYPE_SUBTITLE: return "subtitle"; default: return "other"; }
}

// Container start time in seconds, mirroring the top-level startTime100ns of
// FFF3FP_GetMediaInfo (only AV_NOPTS_VALUE maps to zero; a negative start
// time is reported as-is so the host can offer the matching correction).
std::string FfmpegResultSeconds(const AVFormatContext* format) {
    const auto startSeconds = format->start_time == AV_NOPTS_VALUE
        ? 0.0 : static_cast<double>(format->start_time) / static_cast<double>(AV_TIME_BASE);
    std::ostringstream value;
    value << std::fixed << std::setprecision(6) << startSeconds;
    return value.str();
}

// Mirrors the per-stream JSON of PlayerSession::RebuildMediaInfo so clients
// can reuse the same deserialization for both. Only the fields that exist for
// subtitle streams are emitted; clients fill the rest with defaults.
void AppendSubtitleStreamJson(std::ostringstream& json, const unsigned index,
    const AVStream* stream) {
    const auto* parameters = stream->codecpar;
    const auto* descriptor = avcodec_descriptor_get(parameters->codec_id);
    const auto streamBitRate = std::max<std::int64_t>(0, parameters->bit_rate);
    std::int64_t streamSize = 0;
    if (streamBitRate > 0 && stream->duration != AV_NOPTS_VALUE && stream->time_base.den != 0) {
        const auto seconds = static_cast<long double>(stream->duration) *
            stream->time_base.num / stream->time_base.den;
        const auto bytes = seconds > 0 ? seconds * streamBitRate / 8.0L : 0.0L;
        streamSize = bytes >= static_cast<long double>(INT64_MAX) ? INT64_MAX :
            static_cast<std::int64_t>(std::llround(std::max(0.0L, bytes)));
    }
    const auto isLossless = descriptor != nullptr &&
        (descriptor->props & AV_CODEC_PROP_LOSSLESS) != 0 &&
        (descriptor->props & AV_CODEC_PROP_LOSSY) == 0;
    if (index) json << ',';
    json << "{\"index\":" << stream->index
         << ",\"type\":\"" << MediaTypeName(parameters->codec_type)
         << "\",\"streamId\":" << stream->id
         << ",\"codec\":\"" << EscapeJson(descriptor ? descriptor->name : "unknown")
         << "\",\"codecLongName\":\"" << EscapeJson(descriptor && descriptor->long_name ? descriptor->long_name : "")
         << "\",\"codecTag\":\"" << EscapeJson(CodecTagName(parameters->codec_tag))
         << "\",\"timeBaseNumerator\":" << stream->time_base.num
         << ",\"timeBaseDenominator\":" << stream->time_base.den
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
    const auto* language = av_dict_get(stream->metadata, "language", nullptr, 0);
    const auto* title = av_dict_get(stream->metadata, "title", nullptr, 0);
    json << ",\"language\":\"" << EscapeJson(language ? language->value : "")
         << "\",\"title\":\"" << EscapeJson(title ? title->value : "") << "\"}";
}

FFFResult CopyUtf8(const std::string& value, char* output, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto bytes = value.size() + 1;
    if (bytes > UINT32_MAX) return FFFResult::NativeFailure;
    if (requiredSize != nullptr) *requiredSize = static_cast<std::uint32_t>(bytes);
    if (output == nullptr || outputSize < bytes) return FFFResult::BufferTooSmall;
    std::memcpy(output, value.c_str(), bytes);
    return FFFResult::Success;
}
}  // namespace

FFFResult ProbeSubtitleStreams(const char* localPathUtf8, char* outputUtf8,
    const std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept {
    if (localPathUtf8 == nullptr || *localPathUtf8 == '\0') return FFFResult::InvalidArgument;
    AVFormatContext* format = nullptr;
    std::unique_ptr<SharedFileInput> sharedInput;
    // avformat_open_input only frees the context when it fails, so every
    // other exit must close it; with CUSTOM_IO the close does not touch the
    // AVIOContext, which stays owned by sharedInput (released after close).
    const auto cleanup = [&] {
        if (format != nullptr) avformat_close_input(&format);
        sharedInput.reset();
    };
    try {
        std::string openError;
        sharedInput = SharedFileInput::Open(localPathUtf8, openError);
        if (sharedInput == nullptr) return FFFResult::FfmpegFailure;
        format = avformat_alloc_context();
        if (format == nullptr) {
            cleanup();
            return FFFResult::NativeFailure;
        }
        format->pb = sharedInput->Context();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        auto result = avformat_open_input(&format, localPathUtf8, nullptr, nullptr);
        if (result < 0) {
            cleanup();
            return FFFResult::FfmpegFailure;
        }
        result = avformat_find_stream_info(format, nullptr);
        if (result < 0) {
            cleanup();
            return FFFResult::FfmpegFailure;
        }

        std::ostringstream json;
        const auto formatName = format->iformat && format->iformat->name ? format->iformat->name : "";
        const auto formatLongName = format->iformat && format->iformat->long_name ?
            format->iformat->long_name : "";
        json << "{\"format\":\"" << EscapeJson(formatName)
             << "\",\"formatLongName\":\"" << EscapeJson(formatLongName)
             << "\",\"startTimeSeconds\":" << FfmpegResultSeconds(format)
             << ",\"streams\":[";
        unsigned emitted = 0;
        for (unsigned index = 0; index < format->nb_streams; ++index) {
            const auto* stream = format->streams[index];
            if (stream == nullptr || stream->codecpar == nullptr ||
                stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
            AppendSubtitleStreamJson(json, emitted, stream);
            ++emitted;
        }
        json << "]}";
        auto output = json.str();
        cleanup();
        return CopyUtf8(output, outputUtf8, outputSize, requiredSize);
    } catch (const std::bad_alloc&) {
        cleanup();
        return FFFResult::NativeFailure;
    } catch (const std::exception&) {
        cleanup();
        return FFFResult::NativeFailure;
    } catch (...) {
        cleanup();
        return FFFResult::NativeFailure;
    }
}
