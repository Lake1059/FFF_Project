#pragma once

#include "3FP/Api/FFF.Player.Api.h"
extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
}
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace PlayerMediaText {

inline FFFResult CopyUtf8(const std::string& value, char* output, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto bytes = value.size() + 1;
    if (bytes > UINT32_MAX) return FFFResult::NativeFailure;
    if (requiredSize != nullptr) *requiredSize = static_cast<std::uint32_t>(bytes);
    if (output == nullptr || outputSize < bytes) return FFFResult::BufferTooSmall;
    std::memcpy(output, value.c_str(), bytes);
    return FFFResult::Success;
}

inline std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    return av_strerror(error, buffer, sizeof(buffer)) == 0 ? buffer : "FFmpeg error " + std::to_string(error);
}

inline std::string EscapeJson(const std::string& value) {
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

inline void AppendDictionaryJson(std::ostringstream& json, const AVDictionary* dictionary) {
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

inline std::string CodecTagName(const std::uint32_t tag) {
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

}

