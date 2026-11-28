#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <memory>
#include <string>

// HTTP/HTTPS byte source for the custom AVIO path.
//
// One instance owns one logical media URL and one WinHTTP request at a time.
// Every Range response is validated against the requested offset before a byte
// is handed to FFmpeg; a short/truncated response is never reported as EOF.
// The class is worker-thread affine: Open/Read/Seek/Close all run on the
// session worker, and cancellation is observed through AVIOInterruptCB.
class HttpMediaReader final {
public:
    HttpMediaReader();
    ~HttpMediaReader();
    HttpMediaReader(const HttpMediaReader&) = delete;
    HttpMediaReader& operator=(const HttpMediaReader&) = delete;

    static std::unique_ptr<HttpMediaReader> Open(const std::string& urlUtf8,
        ULONGLONG deadlineTick, const AVIOInterruptCB* interrupt,
        std::string& error, DWORD responseTimeoutMs = 0) noexcept;

    // `position` is the session's logical read cursor. Returns >= 0 on success,
    // AVERROR_EOF at a validated end, AVERROR(ETIMEDOUT) on a retryable
    // transport timeout, or AVERROR(EIO) on a ledger/HTTP failure.
    int Read(std::uint8_t* buffer, int bufferSize, DWORD timeoutMs,
        const AVIOInterruptCB& interrupt, std::int64_t position) noexcept;

    bool Seek(std::int64_t target, DWORD timeoutMs, const AVIOInterruptCB& interrupt,
        std::string& error) noexcept;

    // P2 cancellation: abort a WinHTTP read that is currently blocked in
    // WinHttpReadData. Safe to call from the session worker while the input
    // thread owns the reader; subsequent Read/Seek calls return AVERROR_EXIT.
    void Abort() noexcept;
    // Clear the cancel latch after the direct seek path has been entered and the
    // AVIO is once again owned by the session worker.
    void ClearAbort() noexcept;

    std::int64_t Size() const noexcept;
    std::int64_t Position() const noexcept;
    bool Seekable() const noexcept;
    bool SizeKnown() const noexcept;
    DWORD LastError() const noexcept;
    bool IsEof() const noexcept;
    std::uint32_t RttMs() const noexcept;
    std::uint64_t GoodputBps() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
