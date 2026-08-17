#include "pch.h"
#include "3FP/Api/FFF.Player.Api.h"
#include "Shared/Ffmpeg/SharedFileInput.h"

#include <ass/ass.h>
#include <mlang.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
constexpr std::uint32_t ApiVersion = 1;
constexpr std::int64_t TicksPerMillisecond = 10'000;

std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    return av_strerror(error, buffer, sizeof(buffer)) == 0
        ? buffer : "FFmpeg error " + std::to_string(error);
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count) != count) return {};
    return result;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) != count) return {};
    return result;
}

bool IsFontFile(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".ttf" || extension == L".otf" || extension == L".ttc";
}

std::vector<char> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Could not open the file.");
    const auto end = stream.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw std::runtime_error("The file is too large.");
    std::vector<char> result(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!result.empty() && !stream.read(result.data(), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("Could not read the file.");
    return result;
}

enum class UnicodeEncoding {
    None,
    Utf16Le,
    Utf16Be,
    Utf32Le,
    Utf32Be
};

std::uint8_t ByteAt(const std::vector<char>& value, const std::size_t index) noexcept {
    return static_cast<std::uint8_t>(value[index]);
}

UnicodeEncoding DetectBomlessUnicode(const std::vector<char>& script) noexcept {
    const auto sampleSize = std::min<std::size_t>(script.size(), 4096);
    const auto quads = sampleSize / 4;
    if (quads >= 4) {
        std::array<std::size_t, 4> zeroCounts{};
        for (std::size_t index = 0; index < quads * 4; ++index)
            if (script[index] == '\0') ++zeroCounts[index % 4];
        const auto mostlyZero = [quads](const std::size_t count) { return count * 5 >= quads * 4; };
        if (mostlyZero(zeroCounts[2]) && mostlyZero(zeroCounts[3])) return UnicodeEncoding::Utf32Le;
        if (mostlyZero(zeroCounts[0]) && mostlyZero(zeroCounts[1])) return UnicodeEncoding::Utf32Be;
    }

    const auto pairs = sampleSize / 2;
    if (pairs >= 4) {
        std::array<std::size_t, 2> zeroCounts{};
        for (std::size_t index = 0; index < pairs * 2; ++index)
            if (script[index] == '\0') ++zeroCounts[index % 2];
        const auto mostlyZero = [pairs](const std::size_t count) { return count * 5 >= pairs * 3; };
        const auto mostlyNonZero = [pairs](const std::size_t count) { return count * 5 <= pairs; };
        if (mostlyNonZero(zeroCounts[0]) && mostlyZero(zeroCounts[1])) return UnicodeEncoding::Utf16Le;
        if (mostlyZero(zeroCounts[0]) && mostlyNonZero(zeroCounts[1])) return UnicodeEncoding::Utf16Be;
    }
    return UnicodeEncoding::None;
}

std::wstring DecodeUtf16(const std::vector<char>& script, const std::size_t offset,
    const bool littleEndian) {
    if ((script.size() - offset) % 2 != 0)
        throw std::runtime_error("The UTF-16 ASS subtitle has an incomplete code unit.");
    std::wstring result;
    result.reserve((script.size() - offset) / 2);
    for (std::size_t index = offset; index < script.size(); index += 2) {
        const auto first = ByteAt(script, index);
        const auto second = ByteAt(script, index + 1);
        result.push_back(static_cast<wchar_t>(littleEndian
            ? static_cast<std::uint16_t>(first | (second << 8))
            : static_cast<std::uint16_t>((first << 8) | second)));
    }
    return result;
}

std::wstring DecodeUtf32(const std::vector<char>& script, const std::size_t offset,
    const bool littleEndian) {
    if ((script.size() - offset) % 4 != 0)
        throw std::runtime_error("The UTF-32 ASS subtitle has an incomplete code point.");
    std::wstring result;
    result.reserve((script.size() - offset) / 2);
    for (std::size_t index = offset; index < script.size(); index += 4) {
        std::uint32_t codePoint = 0;
        if (littleEndian) {
            codePoint = ByteAt(script, index) |
                (static_cast<std::uint32_t>(ByteAt(script, index + 1)) << 8) |
                (static_cast<std::uint32_t>(ByteAt(script, index + 2)) << 16) |
                (static_cast<std::uint32_t>(ByteAt(script, index + 3)) << 24);
        } else {
            codePoint = (static_cast<std::uint32_t>(ByteAt(script, index)) << 24) |
                (static_cast<std::uint32_t>(ByteAt(script, index + 1)) << 16) |
                (static_cast<std::uint32_t>(ByteAt(script, index + 2)) << 8) |
                ByteAt(script, index + 3);
        }
        if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            throw std::runtime_error("The UTF-32 ASS subtitle contains an invalid code point.");
        if (codePoint <= 0xFFFF) {
            result.push_back(static_cast<wchar_t>(codePoint));
        } else {
            codePoint -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
        }
    }
    return result;
}

class ScopedComInitialization final {
public:
    ScopedComInitialization() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedComInitialization() { if (SUCCEEDED(result_)) CoUninitialize(); }
    bool IsAvailable() const noexcept { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT result_;
};

std::wstring DetectAndDecodeLegacyText(std::vector<char>& script) {
    ScopedComInitialization com;
    if (!com.IsAvailable()) return {};
    Microsoft::WRL::ComPtr<IMultiLanguage2> multiLanguage;
    if (FAILED(CoCreateInstance(CLSID_CMultiLanguage, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(multiLanguage.ReleaseAndGetAddressOf())))) return {};

    std::array<DetectEncodingInfo, 10> candidates{};
    auto sourceSize = static_cast<int>(script.size());
    auto candidateCount = static_cast<int>(candidates.size());
    if (FAILED(multiLanguage->DetectInputCodepage(MLDETECTCP_NONE, GetACP(), script.data(),
        &sourceSize, candidates.data(), &candidateCount))) return {};

    for (int index = 0; index < candidateCount; ++index) {
        const auto codePage = candidates[index].nCodePage;
        if (codePage == CP_UTF8 || codePage == CP_UTF7 || codePage == 1200 || codePage == 1201 ||
            codePage == 12000 || codePage == 12001) continue;
        DWORD mode = 0;
        auto inputSize = static_cast<UINT>(script.size());
        auto outputSize = static_cast<UINT>(script.size() + 1);
        std::wstring result(outputSize, L'\0');
        const auto conversion = multiLanguage->ConvertStringToUnicode(&mode, codePage, script.data(),
            &inputSize, result.data(), &outputSize);
        if (SUCCEEDED(conversion) && inputSize == script.size()) {
            result.resize(outputSize);
            return result;
        }
    }
    return {};
}

std::vector<char> NormalizeAssScriptEncoding(std::vector<char> script) {
    if (script.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("The ASS subtitle file is too large to convert.");

    UnicodeEncoding encoding = UnicodeEncoding::None;
    std::size_t offset = 0;
    bool hasUtf8Bom = false;
    if (script.size() >= 4 && ByteAt(script, 0) == 0xFF && ByteAt(script, 1) == 0xFE &&
        ByteAt(script, 2) == 0x00 && ByteAt(script, 3) == 0x00) {
        encoding = UnicodeEncoding::Utf32Le;
        offset = 4;
    } else if (script.size() >= 4 && ByteAt(script, 0) == 0x00 && ByteAt(script, 1) == 0x00 &&
        ByteAt(script, 2) == 0xFE && ByteAt(script, 3) == 0xFF) {
        encoding = UnicodeEncoding::Utf32Be;
        offset = 4;
    } else if (script.size() >= 3 && ByteAt(script, 0) == 0xEF && ByteAt(script, 1) == 0xBB &&
        ByteAt(script, 2) == 0xBF) {
        script.erase(script.begin(), script.begin() + 3);
        hasUtf8Bom = true;
    } else if (script.size() >= 2 && ByteAt(script, 0) == 0xFF && ByteAt(script, 1) == 0xFE) {
        encoding = UnicodeEncoding::Utf16Le;
        offset = 2;
    } else if (script.size() >= 2 && ByteAt(script, 0) == 0xFE && ByteAt(script, 1) == 0xFF) {
        encoding = UnicodeEncoding::Utf16Be;
        offset = 2;
    } else {
        encoding = DetectBomlessUnicode(script);
    }

    std::wstring wide;
    switch (encoding) {
    case UnicodeEncoding::Utf16Le: wide = DecodeUtf16(script, offset, true); break;
    case UnicodeEncoding::Utf16Be: wide = DecodeUtf16(script, offset, false); break;
    case UnicodeEncoding::Utf32Le: wide = DecodeUtf32(script, offset, true); break;
    case UnicodeEncoding::Utf32Be: wide = DecodeUtf32(script, offset, false); break;
    case UnicodeEncoding::None: break;
    }
    if (encoding == UnicodeEncoding::None) {
        const std::string utf8(script.begin(), script.end());
        wide = Utf8ToWide(utf8);
        if (wide.empty() && !script.empty() && !hasUtf8Bom) wide = DetectAndDecodeLegacyText(script);
    }
    if (wide.empty() && !script.empty())
        throw std::runtime_error("Could not determine the ASS subtitle text encoding.");
    auto utf8 = WideToUtf8(wide);
    if (utf8.empty() && !wide.empty())
        throw std::runtime_error("The ASS subtitle contains invalid Unicode text.");
    return std::vector<char>(utf8.begin(), utf8.end());
}

class AssSubtitleRenderer final {
public:
    AssSubtitleRenderer() = default;
    ~AssSubtitleRenderer() { Close(); }
    AssSubtitleRenderer(const AssSubtitleRenderer&) = delete;
    AssSubtitleRenderer& operator=(const AssSubtitleRenderer&) = delete;

    FFFResult Open(const char* path, const char* fontDirectories,
        const std::int32_t requestedStream) noexcept {
        if (path == nullptr || *path == '\0') return FFFResult::InvalidArgument;
        try {
            const auto widePath = Utf8ToWide(path);
            if (widePath.empty())
                return Fail("The ASS subtitle path is not valid UTF-8.", FFFResult::InvalidArgument);

            library_ = ass_library_init();
            if (library_ == nullptr)
                return Fail("Could not initialize libass.");
            ass_set_message_cb(library_, &AssMessageCallback, this);
            ass_set_extract_fonts(library_, 1);

            ParseFontDirectories(fontDirectories);
            AddMediaFonts();

            lastLibassMessage_.clear();
            if (requestedStream >= 0) {
                track_ = ReadContainerTrack(path, requestedStream);
            } else {
                auto script = NormalizeAssScriptEncoding(ReadFile(std::filesystem::path(widePath)));
                if (script.empty()) return Fail("The ASS subtitle file is empty.");
                track_ = ass_read_memory(library_, script.data(), script.size(), nullptr);
            }
            if (track_ == nullptr)
                return Fail("Could not parse the ASS subtitle." + LibassDetail());

            renderer_ = ass_renderer_init(library_);
            if (renderer_ == nullptr)
                return Fail("Could not initialize the libass renderer." + LibassDetail());
            ass_set_cache_limits(renderer_, 1000, 128);
            ass_set_fonts(renderer_, nullptr, "Arial", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
            return FFFResult::Success;
        } catch (const std::bad_alloc&) {
            return Fail("Could not allocate the ASS subtitle renderer.");
        } catch (const std::exception& error) {
            return Fail(std::string("Could not initialize the ASS subtitle renderer: ") + error.what());
        } catch (...) {
            return Fail("Could not initialize the ASS subtitle renderer.");
        }
    }

    FFFResult Render(const std::int64_t position, const std::int32_t width,
        const std::int32_t height, FFF3FPBitmapSubtitleFrame& output) noexcept {
        if (position < 0 || width <= 0 || height <= 0 ||
            width > 16'384 || height > 16'384 || renderer_ == nullptr || track_ == nullptr ||
            output.size < sizeof(FFF3FPBitmapSubtitleFrame) || output.version != ApiVersion)
            return FFFResult::InvalidArgument;
        try {
            const auto geometryChanged = width != canvasWidth_ || height != canvasHeight_;
            auto contentChanged = geometryChanged || !hasRenderedFrame_;
            if (geometryChanged) {
                ass_set_frame_size(renderer_, width, height);
                ass_set_storage_size(renderer_, width, height);
                ass_set_pixel_aspect(renderer_, 1.0);
                canvasWidth_ = width;
                canvasHeight_ = height;
            }

            if (geometryChanged || position != lastPosition_ || !hasRenderedFrame_) {
                int changed = 0;
                const auto timestamp = position / TicksPerMillisecond;
                ASS_Image* images = ass_render_frame(renderer_, track_, timestamp, &changed);
                if (geometryChanged || changed != 0 || !hasRenderedFrame_) {
                    Composite(images);
                    contentChanged = true;
                }
                lastPosition_ = position;
                hasRenderedFrame_ = true;
            }
            FillOutput(output, position);
            if (!contentChanged) {
                output.flags = static_cast<FFF3FPBitmapSubtitleFlags>(
                    static_cast<std::uint32_t>(output.flags) |
                    static_cast<std::uint32_t>(FFF3FPBitmapSubtitleFlags::Unchanged));
            }
            // Keep Copy valid for callers that do not yet use the additive
            // Unchanged flag; updated callers can skip the transfer entirely.
            hasPendingCopy_ = true;
            return FFFResult::Success;
        } catch (const std::bad_alloc&) {
            return Fail("Could not allocate the rendered ASS subtitle bitmap.");
        } catch (const std::exception& error) {
            return Fail(std::string("Could not render the ASS subtitle: ") + error.what());
        } catch (...) {
            return Fail("Could not render the ASS subtitle.");
        }
    }

    FFFResult Copy(void* output, const std::uint32_t outputSize) noexcept {
        if (!hasPendingCopy_) return FFFResult::InvalidState;
        if (pixels_.size() > outputSize || (!pixels_.empty() && output == nullptr))
            return FFFResult::BufferTooSmall;
        if (!pixels_.empty()) std::memcpy(output, pixels_.data(), pixels_.size());
        hasPendingCopy_ = false;
        return FFFResult::Success;
    }

    const std::string& LastError() const noexcept { return lastError_; }

private:
    ASS_Track* ReadContainerTrack(const char* path, const std::int32_t requestedStream) {
        AVFormatContext* format = nullptr;
        std::unique_ptr<SharedFileInput> sharedInput;
        AVCodecContext* decoder = nullptr;
        AVPacket* packet = nullptr;
        ASS_Track* track = nullptr;
        const auto cleanup = [&] {
            if (packet != nullptr) av_packet_free(&packet);
            if (decoder != nullptr) avcodec_free_context(&decoder);
            if (format != nullptr) avformat_close_input(&format);
            sharedInput.reset();
        };

        try {
            std::string openError;
            sharedInput = SharedFileInput::Open(path, openError);
            if (sharedInput == nullptr)
                throw std::runtime_error(openError);
            format = avformat_alloc_context();
            if (format == nullptr)
                throw std::runtime_error("Could not allocate the subtitle input context.");
            format->pb = sharedInput->Context();
            format->flags |= AVFMT_FLAG_CUSTOM_IO;
            auto result = avformat_open_input(&format, path, nullptr, nullptr);
            if (result < 0)
                throw std::runtime_error("Could not open the subtitle container: " + FfmpegError(result));
            result = avformat_find_stream_info(format, nullptr);
            if (result < 0)
                throw std::runtime_error("Could not inspect subtitle streams: " + FfmpegError(result));
            if (requestedStream < 0 || requestedStream >= static_cast<std::int32_t>(format->nb_streams) ||
                format->streams[requestedStream]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
                throw std::runtime_error("The requested embedded subtitle stream does not exist.");

            auto* stream = format->streams[requestedStream];
            std::int64_t timelineOriginMilliseconds = 0;
            auto timelineStream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (timelineStream < 0)
                timelineStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (timelineStream >= 0) {
                const auto* reference = format->streams[timelineStream];
                if (reference->start_time != AV_NOPTS_VALUE)
                    timelineOriginMilliseconds = av_rescale_q(reference->start_time,
                        reference->time_base, AVRational{1, 1000});
            }
            const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (codec == nullptr)
                throw std::runtime_error("FFmpeg has no decoder for the embedded subtitle stream.");
            decoder = avcodec_alloc_context3(codec);
            if (decoder == nullptr)
                throw std::runtime_error("Could not allocate the embedded subtitle decoder.");
            result = avcodec_parameters_to_context(decoder, stream->codecpar);
            if (result < 0)
                throw std::runtime_error("Could not configure the embedded subtitle decoder: " + FfmpegError(result));
            result = avcodec_open2(decoder, codec, nullptr);
            if (result < 0)
                throw std::runtime_error("Could not start the embedded subtitle decoder: " + FfmpegError(result));

            track = ass_new_track(library_);
            if (track == nullptr) throw std::runtime_error("Could not allocate a libass subtitle track.");
            if (stream->codecpar->codec_id == AV_CODEC_ID_ASS || stream->codecpar->codec_id == AV_CODEC_ID_SSA) {
                if (stream->codecpar->extradata != nullptr && stream->codecpar->extradata_size > 0)
                    ass_process_codec_private(track,
                        reinterpret_cast<char*>(stream->codecpar->extradata),
                        stream->codecpar->extradata_size);
            } else {
                static constexpr char DefaultHeader[] =
                    "[Script Info]\nScriptType: v4.00+\nPlayResX: 384\nPlayResY: 288\n"
                    "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
                    "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,18,1\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
                ass_process_data(track, const_cast<char*>(DefaultHeader),
                    static_cast<int>(sizeof(DefaultHeader) - 1));
            }

            packet = av_packet_alloc();
            if (packet == nullptr) throw std::runtime_error("Could not allocate a subtitle packet.");
            std::int64_t eventOrder = 0;
            while ((result = av_read_frame(format, packet)) >= 0) {
                if (packet->stream_index != requestedStream) {
                    av_packet_unref(packet);
                    continue;
                }

                const auto packetPts = packet->pts;
                const auto packetDuration = packet->duration;
                AVSubtitle subtitle{};
                int gotSubtitle = 0;
                const auto decodeResult = avcodec_decode_subtitle2(decoder, &subtitle, &gotSubtitle, packet);
                av_packet_unref(packet);
                if (decodeResult < 0) {
                    avsubtitle_free(&subtitle);
                    throw std::runtime_error("Could not decode an embedded subtitle packet: " +
                        FfmpegError(decodeResult));
                }
                if (!gotSubtitle) continue;

                std::int64_t baseMilliseconds = 0;
                if (subtitle.pts != AV_NOPTS_VALUE)
                    baseMilliseconds = subtitle.pts / 1000;
                else if (packetPts != AV_NOPTS_VALUE)
                    baseMilliseconds = av_rescale_q(packetPts, stream->time_base, AVRational{1, 1000});
                const auto startMilliseconds = std::max<std::int64_t>(0,
                    baseMilliseconds - timelineOriginMilliseconds + subtitle.start_display_time);
                auto durationMilliseconds = subtitle.end_display_time > subtitle.start_display_time &&
                    subtitle.end_display_time != UINT32_MAX
                    ? static_cast<std::int64_t>(subtitle.end_display_time - subtitle.start_display_time)
                    : (packetDuration > 0
                        ? av_rescale_q(packetDuration, stream->time_base, AVRational{1, 1000})
                        : 5000);
                durationMilliseconds = std::max<std::int64_t>(1, durationMilliseconds);

                for (unsigned index = 0; index < subtitle.num_rects; ++index) {
                    const auto* rectangle = subtitle.rects[index];
                    if (rectangle == nullptr) continue;
                    if (rectangle->ass != nullptr && *rectangle->ass != '\0') {
                        ass_process_chunk(track, rectangle->ass,
                            static_cast<int>(std::strlen(rectangle->ass)),
                            startMilliseconds, durationMilliseconds);
                    } else if (rectangle->text != nullptr && *rectangle->text != '\0') {
                        std::string text;
                        for (const char* current = rectangle->text; *current != '\0'; ++current) {
                            if (*current == '\r') continue;
                            if (*current == '\n') text += "\\N";
                            else text += *current;
                        }
                        auto chunk = std::to_string(eventOrder++) + ",0,Default,,0,0,0,," + text;
                        ass_process_chunk(track, chunk.data(), static_cast<int>(chunk.size()),
                            startMilliseconds, durationMilliseconds);
                    }
                }
                avsubtitle_free(&subtitle);
            }
            if (result != AVERROR_EOF)
                throw std::runtime_error("Could not finish reading embedded subtitles: " + FfmpegError(result));

            cleanup();
            return track;
        } catch (...) {
            cleanup();
            if (track != nullptr) ass_free_track(track);
            throw;
        }
    }

    static void AssMessageCallback(const int level, const char* format, va_list arguments,
        void* data) noexcept {
        if (data == nullptr || format == nullptr || level > 2) return;
        auto* self = static_cast<AssSubtitleRenderer*>(data);
        try {
            std::array<char, 2048> buffer{};
            std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
            self->lastLibassMessage_ = buffer.data();
            while (!self->lastLibassMessage_.empty() &&
                (self->lastLibassMessage_.back() == '\r' || self->lastLibassMessage_.back() == '\n'))
                self->lastLibassMessage_.pop_back();
        } catch (...) {}
    }

    void ParseFontDirectories(const char* value) {
        if (value == nullptr) return;
        std::string all(value);
        std::size_t start = 0;
        while (start <= all.size()) {
            const auto end = all.find('\n', start);
            auto directory = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!directory.empty() && directory.back() == '\r') directory.pop_back();
            if (!directory.empty()) fontDirectories_.push_back(std::move(directory));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    void AddMediaFonts() {
        for (const auto& directoryUtf8 : fontDirectories_) {
            const auto directoryWide = Utf8ToWide(directoryUtf8);
            if (directoryWide.empty()) continue;
            std::error_code error;
            const std::filesystem::path directory(directoryWide);
            if (!std::filesystem::is_directory(directory, error)) continue;
            for (std::filesystem::directory_iterator iterator(directory,
                     std::filesystem::directory_options::skip_permission_denied, error), end;
                 iterator != end; iterator.increment(error)) {
                if (error) { error.clear(); continue; }
                if (!iterator->is_regular_file(error) || !IsFontFile(iterator->path())) continue;
                auto font = ReadFile(iterator->path());
                if (font.empty() || font.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    continue;
                auto name = WideToUtf8(iterator->path().filename().wstring());
                if (name.empty()) name = "media-font";
                ass_add_font(library_, name.c_str(), font.data(), static_cast<int>(font.size()));
            }
        }
    }

    static bool VisibleBounds(const ASS_Image& image, const std::int32_t canvasWidth,
        const std::int32_t canvasHeight, std::int32_t& left, std::int32_t& top,
        std::int32_t& right, std::int32_t& bottom) noexcept {
        if (image.bitmap == nullptr || image.w <= 0 || image.h <= 0 || image.stride < image.w ||
            static_cast<std::uint8_t>(image.color) == 255)
            return false;
        const auto imageRight = static_cast<std::int64_t>(image.dst_x) + image.w;
        const auto imageBottom = static_cast<std::int64_t>(image.dst_y) + image.h;
        left = static_cast<std::int32_t>(std::max<std::int64_t>(0, image.dst_x));
        top = static_cast<std::int32_t>(std::max<std::int64_t>(0, image.dst_y));
        right = static_cast<std::int32_t>(std::min<std::int64_t>(canvasWidth, imageRight));
        bottom = static_cast<std::int32_t>(std::min<std::int64_t>(canvasHeight, imageBottom));
        return left < right && top < bottom;
    }

    void Composite(ASS_Image* images) {
        auto left = canvasWidth_;
        auto top = canvasHeight_;
        auto right = 0;
        auto bottom = 0;
        for (auto* image = images; image != nullptr; image = image->next) {
            std::int32_t imageLeft{}, imageTop{}, imageRight{}, imageBottom{};
            if (!VisibleBounds(*image, canvasWidth_, canvasHeight_,
                imageLeft, imageTop, imageRight, imageBottom)) continue;
            left = std::min(left, imageLeft);
            top = std::min(top, imageTop);
            right = std::max(right, imageRight);
            bottom = std::max(bottom, imageBottom);
        }

        if (left >= right || top >= bottom) {
            cropLeft_ = cropTop_ = cropWidth_ = cropHeight_ = cropStride_ = 0;
            pixels_.clear();
            AdvanceSequence();
            return;
        }

        cropLeft_ = std::max(0, left - 1);
        cropTop_ = std::max(0, top - 1);
        right = std::min(canvasWidth_, right + 1);
        bottom = std::min(canvasHeight_, bottom + 1);
        cropWidth_ = right - cropLeft_;
        cropHeight_ = bottom - cropTop_;
        cropStride_ = cropWidth_ * 4;
        pixels_.assign(static_cast<std::size_t>(cropStride_) * cropHeight_, 0);

        for (auto* image = images; image != nullptr; image = image->next)
            CompositeImage(*image);
        AdvanceSequence();
    }

    void CompositeImage(const ASS_Image& image) noexcept {
        std::int32_t left{}, top{}, right{}, bottom{};
        if (!VisibleBounds(image, canvasWidth_, canvasHeight_, left, top, right, bottom)) return;

        const auto red = static_cast<std::uint8_t>(image.color >> 24);
        const auto green = static_cast<std::uint8_t>(image.color >> 16);
        const auto blue = static_cast<std::uint8_t>(image.color >> 8);
        const auto opacity = 255u - static_cast<std::uint8_t>(image.color);
        if (opacity == 0) return;

        const auto sourceLeft = left - image.dst_x;
        const auto sourceTop = top - image.dst_y;
        for (auto y = top; y < bottom; ++y) {
            const auto* source = image.bitmap +
                static_cast<std::ptrdiff_t>(sourceTop + y - top) * image.stride + sourceLeft;
            auto* destination = pixels_.data() +
                static_cast<std::ptrdiff_t>(y - cropTop_) * cropStride_ + (left - cropLeft_) * 4;
            for (auto x = left; x < right; ++x, ++source, destination += 4) {
                const auto alpha = (static_cast<unsigned>(*source) * opacity + 127u) / 255u;
                if (alpha == 0) continue;
                const auto inverse = 255u - alpha;
                const auto sourceBlue = (static_cast<unsigned>(blue) * alpha + 127u) / 255u;
                const auto sourceGreen = (static_cast<unsigned>(green) * alpha + 127u) / 255u;
                const auto sourceRed = (static_cast<unsigned>(red) * alpha + 127u) / 255u;
                destination[0] = static_cast<std::uint8_t>(sourceBlue +
                    (static_cast<unsigned>(destination[0]) * inverse + 127u) / 255u);
                destination[1] = static_cast<std::uint8_t>(sourceGreen +
                    (static_cast<unsigned>(destination[1]) * inverse + 127u) / 255u);
                destination[2] = static_cast<std::uint8_t>(sourceRed +
                    (static_cast<unsigned>(destination[2]) * inverse + 127u) / 255u);
                destination[3] = static_cast<std::uint8_t>(alpha +
                    (static_cast<unsigned>(destination[3]) * inverse + 127u) / 255u);
            }
        }
    }

    void AdvanceSequence() noexcept {
        sequence_ = sequence_ == std::numeric_limits<std::int64_t>::max() ? 1 : sequence_ + 1;
    }

    void FillOutput(FFF3FPBitmapSubtitleFrame& output, const std::int64_t position) const noexcept {
        output = {};
        output.size = sizeof(FFF3FPBitmapSubtitleFrame);
        output.version = ApiVersion;
        output.flags = pixels_.empty() ? FFF3FPBitmapSubtitleFlags::Clear : FFF3FPBitmapSubtitleFlags::None;
        output.start100ns = position;
        output.canvasWidth = canvasWidth_;
        output.canvasHeight = canvasHeight_;
        output.x = cropLeft_;
        output.y = cropTop_;
        output.width = cropWidth_;
        output.height = cropHeight_;
        output.stride = cropStride_;
        output.pixelBytes = static_cast<std::uint32_t>(pixels_.size());
        output.sequence = pixels_.empty() ? 0 : sequence_;
    }

    std::string LibassDetail() const {
        return lastLibassMessage_.empty() ? std::string{} : " " + lastLibassMessage_;
    }

    FFFResult Fail(std::string message,
        const FFFResult result = FFFResult::NativeFailure) noexcept {
        try { lastError_ = std::move(message); } catch (...) {}
        return result;
    }

    void Close() noexcept {
        if (renderer_ != nullptr) ass_renderer_done(renderer_);
        if (track_ != nullptr) ass_free_track(track_);
        if (library_ != nullptr) ass_library_done(library_);
        renderer_ = nullptr;
        track_ = nullptr;
        library_ = nullptr;
        pixels_.clear();
    }

    ASS_Library* library_{};
    ASS_Renderer* renderer_{};
    ASS_Track* track_{};
    std::string lastError_;
    std::string lastLibassMessage_;
    std::vector<std::string> fontDirectories_;
    std::vector<std::uint8_t> pixels_;
    std::int64_t lastPosition_{std::numeric_limits<std::int64_t>::min()};
    std::int64_t sequence_{};
    std::int32_t canvasWidth_{};
    std::int32_t canvasHeight_{};
    std::int32_t cropLeft_{};
    std::int32_t cropTop_{};
    std::int32_t cropWidth_{};
    std::int32_t cropHeight_{};
    std::int32_t cropStride_{};
    bool hasRenderedFrame_{};
    bool hasPendingCopy_{};
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

FFFResult FFF3FP_OpenAssSubtitle(const char* path, const char* fontDirectories,
    const std::int32_t stream, FFF3FPAssSubtitleHandle* output) noexcept {
    if (output == nullptr) return FFFResult::InvalidArgument;
    *output = nullptr;
    try {
        auto renderer = std::make_unique<AssSubtitleRenderer>();
        const auto result = renderer->Open(path, fontDirectories, stream);
        *output = renderer.release();
        return result;
    } catch (...) { return FFFResult::NativeFailure; }
}

FFFResult FFF3FP_RenderAssSubtitle(const FFF3FPAssSubtitleHandle renderer,
    const std::int64_t position, const std::int32_t width, const std::int32_t height,
    FFF3FPBitmapSubtitleFrame* frame) noexcept {
    return renderer != nullptr && frame != nullptr
        ? static_cast<AssSubtitleRenderer*>(renderer)->Render(position, width, height, *frame)
        : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_CopyAssSubtitlePixels(const FFF3FPAssSubtitleHandle renderer,
    void* output, const std::uint32_t outputSize) noexcept {
    return renderer != nullptr
        ? static_cast<AssSubtitleRenderer*>(renderer)->Copy(output, outputSize)
        : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_GetAssSubtitleLastError(const FFF3FPAssSubtitleHandle renderer,
    char* output, const std::uint32_t size, std::uint32_t* required) noexcept {
    return renderer != nullptr
        ? CopyUtf8(static_cast<AssSubtitleRenderer*>(renderer)->LastError(), output, size, required)
        : FFFResult::InvalidArgument;
}

void FFF3FP_DestroyAssSubtitle(const FFF3FPAssSubtitleHandle renderer) noexcept {
    delete static_cast<AssSubtitleRenderer*>(renderer);
}
