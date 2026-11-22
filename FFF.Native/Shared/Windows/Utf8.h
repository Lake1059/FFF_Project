#pragma once

#include <Windows.h>
#include <string>
#include <utility>

namespace FFFText {

inline std::wstring FromUtf8(const char* value, bool* valid = nullptr) {
    if (valid != nullptr) *valid = true;
    if (value == nullptr || *value == '\0') return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (length > 1) {
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), length) == length) {
            result.resize(static_cast<std::size_t>(length - 1));
            return result;
        }
    }
    if (valid != nullptr) *valid = false;
    return {};
}

inline bool TryFromUtf8(const char* value, std::wstring& output) noexcept {
    try {
        bool valid;
        auto converted = FromUtf8(value, &valid);
        if (!valid) return false;
        output = std::move(converted);
        return true;
    } catch (...) { return false; }
}

}
