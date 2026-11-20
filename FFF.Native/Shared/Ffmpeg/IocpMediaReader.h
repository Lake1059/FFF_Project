#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <memory>
#include <string>

// A bounded read-ahead pipeline for UNC/SMB media. The file handle is associated
// with an I/O completion port; a dedicated worker keeps several ReadFile
// operations in flight while the demux worker drains completed slots. This is
// deliberately not an IOCP abstraction over a synchronous read: the point is
// overlap between network latency and decode, plus a finite cancellation point.
class IocpMediaReader final {
public:
    static std::unique_ptr<IocpMediaReader> Create(HANDLE file, std::int64_t size,
        std::string& error) noexcept;

    ~IocpMediaReader();
    IocpMediaReader(const IocpMediaReader&) = delete;
    IocpMediaReader& operator=(const IocpMediaReader&) = delete;

    // Returns bytes copied, AVERROR_EOF, AVERROR_EXIT, AVERROR(ETIMEDOUT), or
    // another negative FFmpeg error. `lastError` carries the Win32 error when
    // applicable. The caller owns `position_`; this reader keeps it in sync.
    int Read(std::uint8_t* buffer, int bufferSize, const AVIOInterruptCB& interrupt,
        DWORD timeoutMs, DWORD& lastError) noexcept;

    // Cancels outstanding reads, waits for the completion port to observe them,
    // then re-arms the pipeline at `position`. Returns false on timeout/stop.
    bool Seek(std::int64_t position, DWORD timeoutMs, DWORD& lastError) noexcept;

    std::int64_t Position() const noexcept;

private:
    struct Impl;
    explicit IocpMediaReader(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
