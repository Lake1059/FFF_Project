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
// The slot count is a demand-driven target (2..24 by default), subject to a
// process-wide resident budget. Slots are stable heap objects because the completion port maps
// OVERLAPPED back to its slot; resizing a vector of slot values would invalidate
// every in-flight request.
//
// `SetTargetBytes` is fed with an approximate stream bitrate by the player. The
// reader turns it into a slot target; `FFF_NET_IO_DEPTH` can pin the target for
// diagnostics and tests.
class IocpMediaReader final {
public:
    struct Metrics {
        int initialTargetSlots = 0;
        int targetSlots = 0; // Requested window; allocation may be budget-limited.
        int allocatedSlots = 0;
        int peakAllocatedSlots = 0;
        int inFlight = 0;
        int maxInFlight = 0;
        std::int64_t allocatedBytes = 0;
        std::int64_t bytesRead = 0;
        std::int64_t readCalls = 0;
        std::int64_t waitEvents = 0;
        std::int64_t shortReads = 0;
        std::int64_t holesCreated = 0;
        std::int64_t holeCount = 0;
        std::int64_t holeBytes = 0;
        std::int64_t retries = 0;
        std::int64_t resets = 0;
        std::int64_t targetChanges = 0;
        std::int64_t maxObservedReadBytes = 0;
        std::int64_t readLatencyMs = 0;
        // Percentiles use the same time-bounded completion window as the stop trace.
        std::int64_t readLatencyP95Ms = 0;
        std::int64_t readLatencyP50Ms = 0;
        std::int64_t readLatencyP90Ms = 0;
        std::int64_t maxReadLatencyMs = 0;
        std::int64_t highLatencyReads = 0;
        std::int64_t bytesServed = 0;
        std::int64_t waitMs = 0;
        std::int64_t maxWaitMs = 0; // Cumulative wait within one Read call.
        std::int64_t errorCount = 0;
        std::int64_t eofCount = 0;
        std::int64_t resetFailures = 0;
        std::int64_t targetBytes = 0;
        std::int64_t consumeBytesPerSecond = 0;
        std::int64_t abandonedSlots = 0;
        std::int64_t abandonedBytes = 0;
        bool holeLimitHit = false; // Active until a successful Seek clears it.
        bool abandoned = false;
    };

    // `initialTargetSlots` is used by short-lived probe readers: they must not
    // allocate the playback default before their first read. Zero keeps the
    // normal adaptive default. The caller retains ownership of `file`.
    static std::unique_ptr<IocpMediaReader> Create(HANDLE file, std::int64_t size,
        std::string& error, int initialTargetSlots = 0) noexcept;

    ~IocpMediaReader();
    IocpMediaReader(const IocpMediaReader&) = delete;
    IocpMediaReader& operator=(const IocpMediaReader&) = delete;

    // Returns bytes copied, AVERROR_EOF, AVERROR_EXIT, AVERROR(ETIMEDOUT), or
    // another negative FFmpeg error. `lastError` carries the Win32 error when
    // applicable. The caller owns `position_`; this reader keeps it in sync.
    int Read(std::uint8_t* buffer, int bufferSize, const AVIOInterruptCB& interrupt,
        DWORD timeoutMs, DWORD& lastError) noexcept;

    // Cancels outstanding reads, waits for the completion port to observe them,
    // trims unused slots, then re-arms the pipeline at `position`. An optional
    // interrupt lets a closing scanner stop the drain instead of waiting the
    // whole timeout.
    bool Seek(std::int64_t position, DWORD timeoutMs, DWORD& lastError,
        const AVIOInterruptCB* interrupt = nullptr) noexcept;

    std::int64_t Position() const noexcept;

    // Demand hint in bytes. 0 restores the default target of four slots.
    void SetTargetBytes(std::int64_t targetBytes) noexcept;

    // Feed-forward stream bitrate. Unlike SetTargetBytes this keeps the real
    // bitrate, which the adaptive window needs to estimate how many bytes a
    // high-latency read must cover.
    void SetStreamBitRate(std::int64_t bitsPerSecond) noexcept;

    // Enables the measured-wait/latency feedback loop on top of the bitrate
    // hint. It is only used for playback IO, not short-lived probe readers.
    void SetAdaptiveReadAhead(bool enabled) noexcept;

    // Explicit slot target, used by diagnostics/tests. Pins the target so a
    // later bitrate hint cannot overwrite it.
    void SetTargetSlots(int slots) noexcept;

    int TargetSlots() const noexcept;
    Metrics MetricsSnapshot() const noexcept;

private:
    struct Impl;
    explicit IocpMediaReader(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> impl_;
};
