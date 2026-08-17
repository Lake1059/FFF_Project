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

    AVIOContext* Context() const noexcept { return context_; }

private:
    static int Read(void* opaque, std::uint8_t* buffer, int bufferSize) noexcept;
    static std::int64_t Seek(void* opaque, std::int64_t offset, int whence) noexcept;

    HANDLE file_{INVALID_HANDLE_VALUE};
    AVIOContext* context_{};
    std::uint8_t* buffer_{};
    std::int64_t size_{};
};
