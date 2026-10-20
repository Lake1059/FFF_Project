#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <memory>
#include <string>

class SharedFileInput final {
public:
    SharedFileInput() = default;
    ~SharedFileInput();
    SharedFileInput(const SharedFileInput&) = delete;
    SharedFileInput& operator=(const SharedFileInput&) = delete;

    static std::unique_ptr<SharedFileInput> Open(const char* pathUtf8,
        std::string& error) noexcept;

    // Serves a copy of `data` instead of a file on disk, for the callers that
    // have to hand FFmpeg bytes they had to repair first (see AvifIlocShim).
    static std::unique_ptr<SharedFileInput> OpenMemory(const std::uint8_t* data,
        std::size_t size, std::string& error) noexcept;

    AVIOContext* Context() const noexcept { return context_; }

private:
    static int Read(void* opaque, std::uint8_t* buffer, int bufferSize) noexcept;
    static std::int64_t Seek(void* opaque, std::int64_t offset, int whence) noexcept;

    HANDLE file_{INVALID_HANDLE_VALUE};
    AVIOContext* context_{};
    std::uint8_t* buffer_{};
    std::uint8_t* data_{};      // owned copy of the in-memory media, when used
    std::int64_t position_{};   // read cursor for the in-memory media
    std::int64_t size_{};
};
