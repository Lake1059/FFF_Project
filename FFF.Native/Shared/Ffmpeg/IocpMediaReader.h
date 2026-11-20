#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdint>
#include <memory>
#include <string>

// A bounded, elastic read-ahead pipeline for UNC/SMB media. The file handle is
// associated with an I/O completion port; a dedicated worker keeps several
// ReadFile operations in flight while the demux worker drains completed slots.
//
// The slot count is a demand-driven target (2..12 by default), not a compile-
// time constant. Slots are stable heap objects because the completion port maps
// OVERLAPPED back to its slot; resizing a vector of slot values would invalidate
// every in-flight request.
//
// `SetTargetBytes` is fed with an approximate stream bitrate by the player. The
// reader turns it into a slot target; `FFF_NET_IO_DEPTH` can pin the target for
// diagnostics and tests.
class IocpMediaReader final {
public:
    struct Metrics {
        int targetSlots = 0;
        int allocatedSlots = 0;
        int inFlight = 0;
        int maxInFlight = 0;
        std::int64_t bytesRead = 0;
        std::int64_t waitEvents = 0;
        std::int64_t shortReads = 0;
        std::int64_t holesCreated = 0;
        std::int64_t retries = 0;
        std::int64_t resets = 0;
        std::int64_t targetChanges = 0;
        std::int64_t maxObservedReadBytes = 0;
        std::int64_t readLatencyMs = 0;
        bool abandoned = false;
    };

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
    // trims unused slots, then re-arms the pipeline at `position`.
    bool Seek(std::int64_t position, DWORD timeoutMs, DWORD& lastError) noexcept;

    std::int64_t Position() const noexcept;

    // Demand hint in bytes. 0 restores the default target of four slots.
    void SetTargetBytes(std::int64_t targetBytes) noexcept;

    // Explicit slot target, used by diagnostics/tests. Pins the target so a
    // later bitrate hint cannot overwrite it.
    void SetTargetSlots(int slots) noexcept;

    int TargetSlots() const noexcept;
    Metrics MetricsSnapshot() const noexcept;

private:
    struct Impl;
    explicit IocpMediaReader(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
