#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <memory>
#include <string>

class IocpMediaReader;
struct SharedFileInputEventReadRequest;

class SharedFileInput final {
public:
    SharedFileInput();
    ~SharedFileInput();
    SharedFileInput(const SharedFileInput&) = delete;
    SharedFileInput& operator=(const SharedFileInput&) = delete;

    static std::unique_ptr<SharedFileInput> Open(const char* pathUtf8,
        std::string& error, ULONGLONG deadlineTick = 0,
        bool probe = false, const AVIOInterruptCB* interrupt = nullptr) noexcept;

    // Serves a copy of `data` instead of a file on disk, for the callers that
    // have to hand FFmpeg bytes they had to repair first (see AvifIlocShim).
    static std::unique_ptr<SharedFileInput> OpenMemory(const std::uint8_t* data,
        std::size_t size, std::string& error) noexcept;

    AVIOContext* Context() const noexcept { return context_; }
    bool HasWriter() const noexcept;
    void SetInterrupt(AVIOInterruptCB interrupt) noexcept { interrupt_ = interrupt; }
    void DetectUnwrittenData(bool enabled) noexcept { detectUnwrittenData_ = enabled; }

    // Feed-forward hint for the IOCP read-ahead window. `bitsPerSecond` is the
    // container/stream bitrate; the reader converts it to a slot target.
    void SetNetworkWindowHint(std::int64_t bitsPerSecond) noexcept;

private:
    static constexpr DWORD NetworkReadTimeoutMs = 8000;
    static constexpr DWORD NetworkOpenTimeoutMs = 15000;
    static constexpr DWORD NetworkSeekTimeoutMs = 3000;
    static constexpr int NetworkReadRetryCount = 2;

    bool InitializeContext(std::string& error);
    static int Read(void* opaque, std::uint8_t* buffer, int bufferSize) noexcept;
    static std::int64_t Seek(void* opaque, std::int64_t offset, int whence) noexcept;
    static bool IsNetworkPath(const std::wstring& path) noexcept;
    static bool IsTransientNetworkError(DWORD error) noexcept;
    static bool Interrupted(const AVIOInterruptCB& interrupt) noexcept;

    int ReadFileOnce(std::uint8_t* buffer, int bufferSize,
        ULONGLONG deadlineTick) noexcept;
    bool IsUnwrittenBlock(const std::uint8_t* buffer, int bytes) const noexcept;
    void CloseHandles() noexcept;

    HANDLE file_{INVALID_HANDLE_VALUE};
    std::unique_ptr<SharedFileInputEventReadRequest> eventRead_;
    std::unique_ptr<IocpMediaReader> iocpReader_;
    AVIOContext* context_{};
    std::uint8_t* data_{};      // owned copy of the in-memory media, when used
    std::int64_t position_{};   // logical read cursor for file and memory media
    std::int64_t size_{};
    bool sizeKnown_{false};
    bool network_{false};
    bool overlapped_{false};
    DWORD lastError_{0};
    std::wstring path_;
    bool detectUnwrittenData_ = false;
    AVIOInterruptCB interrupt_{};
};
