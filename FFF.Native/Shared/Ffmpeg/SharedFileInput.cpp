#include "pch.h"
#include "Shared/Ffmpeg/SharedFileInput.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <cerrno>
#include <cstdio>

namespace {
constexpr int AvioBufferSize = 64 * 1024;

std::wstring FromUtf8(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
        result.data(), length);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

std::string LastWin32Error() {
    const auto error = GetLastError();
    char* message = nullptr;
    const auto length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message), 0, nullptr);
    std::string result = "Windows error " + std::to_string(error);
    if (length > 0 && message != nullptr) {
        result.assign(message, message + length);
        while (!result.empty() &&
            (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
            result.pop_back();
    }
    if (message != nullptr) LocalFree(message);
    return result;
}
}

SharedFileInput::~SharedFileInput() {
    if (context_ != nullptr) {
        av_freep(&context_->buffer);
        avio_context_free(&context_);
        buffer_ = nullptr;
    } else if (buffer_ != nullptr) {
        av_free(buffer_);
        buffer_ = nullptr;
    }
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

std::unique_ptr<SharedFileInput> SharedFileInput::Open(const char* pathUtf8,
    std::string& error) noexcept {
    try {
        const auto path = FromUtf8(pathUtf8);
        if (path.empty()) {
            error = "The local path is not valid UTF-8.";
            return nullptr;
        }

        auto input = std::make_unique<SharedFileInput>();
        input->file_ = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input->file_ == INVALID_HANDLE_VALUE) {
            error = "Could not open local media with shared access: " + LastWin32Error();
            return nullptr;
        }

        LARGE_INTEGER fileSize{};
        if (!GetFileSizeEx(input->file_, &fileSize) || fileSize.QuadPart < 0) {
            error = "Could not read the local media size: " + LastWin32Error();
            return nullptr;
        }
        input->size_ = fileSize.QuadPart;

        input->buffer_ = static_cast<std::uint8_t*>(av_malloc(AvioBufferSize));
        if (input->buffer_ == nullptr) {
            error = "Could not allocate the media input buffer.";
            return nullptr;
        }

        input->context_ = avio_alloc_context(input->buffer_, AvioBufferSize, 0,
            input.get(), &SharedFileInput::Read, nullptr, &SharedFileInput::Seek);
        if (input->context_ == nullptr) {
            error = "Could not allocate the FFmpeg media input context.";
            return nullptr;
        }
        input->buffer_ = nullptr;
        input->context_->seekable = AVIO_SEEKABLE_NORMAL;
        return input;
    } catch (...) {
        error = "Could not prepare the shared media input.";
        return nullptr;
    }
}

int SharedFileInput::Read(void* opaque, std::uint8_t* buffer,
    const int bufferSize) noexcept {
    auto* input = static_cast<SharedFileInput*>(opaque);
    if (input == nullptr || input->file_ == INVALID_HANDLE_VALUE ||
        buffer == nullptr || bufferSize < 0)
        return AVERROR(EINVAL);
    if (bufferSize == 0) return 0;

    DWORD bytesRead = 0;
    if (!ReadFile(input->file_, buffer, static_cast<DWORD>(bufferSize),
        &bytesRead, nullptr))
        return AVERROR(EIO);
    return bytesRead == 0 ? AVERROR_EOF : static_cast<int>(bytesRead);
}

std::int64_t SharedFileInput::Seek(void* opaque, const std::int64_t offset,
    const int whence) noexcept {
    auto* input = static_cast<SharedFileInput*>(opaque);
    if (input == nullptr || input->file_ == INVALID_HANDLE_VALUE)
        return AVERROR(EINVAL);
    const auto origin = whence & ~AVSEEK_FORCE;
    if (origin == AVSEEK_SIZE) return input->size_;

    DWORD method = FILE_BEGIN;
    switch (origin) {
    case SEEK_SET: method = FILE_BEGIN; break;
    case SEEK_CUR: method = FILE_CURRENT; break;
    case SEEK_END: method = FILE_END; break;
    default: return AVERROR(EINVAL);
    }

    LARGE_INTEGER distance{};
    distance.QuadPart = offset;
    LARGE_INTEGER position{};
    if (!SetFilePointerEx(input->file_, distance, &position, method))
        return AVERROR(EIO);
    return position.QuadPart;
}
