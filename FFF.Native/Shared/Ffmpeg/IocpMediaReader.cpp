#include "pch.h"
#include "Shared/Ffmpeg/IocpMediaReader.h"

extern "C" {
#include <libavutil/error.h>
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <stdio.h>
#include <thread>
#include <vector>

namespace {
constexpr int SlotBytes = 1024 * 1024;
constexpr int MinSlots = 2;
// Playback may grow beyond the original 12-slot ceiling: a high-RTT share
// needs more bytes in flight than a local loopback SMB test ever did. 24 MiB
// per playback reader is the default ceiling; FFF_NET_IO_MAX_SLOTS can lower
// or raise it up to 48 for diagnostics.
constexpr int MaxSlots = 24;
constexpr int AbsoluteMaxSlots = 48;
constexpr int DefaultSlots = 4;
constexpr DWORD PollIntervalMs = 25;
constexpr DWORD RetryDelayMs = 150;
constexpr int RetryCount = 1;
constexpr ULONGLONG StopWaitMs = 3000;
constexpr DWORD TrimDelayMs = 2000;
constexpr int MaxHoles = 64;
constexpr std::int64_t MaxHoleBytes = 16 * 1024 * 1024;
// Reject new readers above this abandoned-byte watermark. Already admitted
// readers may still time out safely within the hard resident/worker caps.
constexpr std::int64_t MaxAbandonedBytes = 64 * 1024 * 1024;
constexpr int MaxIocpWorkers = 128;
constexpr ULONGLONG AdaptiveIntervalMs = 1000;
constexpr ULONGLONG LatencyWindowMs = 8000;
constexpr ULONGLONG AdaptiveHoldMs = 1000;
constexpr int AdaptiveGrowStreak = 2;
constexpr int AdaptiveShrinkStreak = 8;
constexpr double AdaptiveMinCoverageMs = 300.0;
constexpr double AdaptiveMaxCoverageMs = 4000.0;
constexpr int LatencySampleCapacity = 64;
constexpr std::int64_t DefaultResidentBudgetMb = 192;

struct LatencySample {
    ULONGLONG tick = 0;
    std::uint32_t ms = 0;
};

struct LatencyPercentiles {
    std::uint32_t p50 = 0;
    std::uint32_t p90 = 0;
    std::uint32_t p95 = 0;
};

bool IsTransientError(const DWORD error) noexcept {
    switch (error) {
    case ERROR_BAD_NETPATH:
    case ERROR_NETNAME_DELETED:
    case ERROR_NETWORK_BUSY:
    case ERROR_SEM_TIMEOUT:
    case ERROR_IO_DEVICE:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_CONNECTION_ABORTED:
    case ERROR_NOT_READY:
    case ERROR_REM_NOT_LIST:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_HOST_UNREACHABLE:
    case ERROR_NOT_ENOUGH_QUOTA:
    case ERROR_NO_SYSTEM_RESOURCES:
        return true;
    default:
        return false;
    }
}

int EnvironmentInt(const wchar_t* name, const int fallback,
    const int minimum, const int maximum) noexcept {
    wchar_t text[32]{};
    const auto length = GetEnvironmentVariableW(name, text, 32);
    if (length == 0 || length >= 32) return fallback;
    wchar_t* end = nullptr;
    const auto value = wcstol(text, &end, 10);
    if (end == text || *end != L'\0' || value < minimum || value > maximum)
        return fallback;
    return static_cast<int>(value);
}

bool TraceEnabled() noexcept {
    wchar_t text[16]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO_TRACE", text, 16);
    return length > 0 && length < 16 && !(text[0] == L'0' && text[1] == L'\0');
}

bool SeekProfileEnabled() noexcept {
    static const bool enabled = []() noexcept {
        wchar_t text[8]{};
        const auto length = GetEnvironmentVariableW(L"FFF_SEEK_PROFILE", text, 8);
        return length > 0 && length < 8 && !(text[0] == L'0' && text[1] == L'\0');
    }();
    return enabled;
}

inline double ElapsedMs(const std::chrono::steady_clock::time_point& begin,
    const std::chrono::steady_clock::time_point& end) noexcept {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::wstring TraceFilePath() {
    wchar_t text[32768]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO_TRACE_FILE", text, 32768);
    if (length == 0 || length >= 32768) return {};
    return std::wstring(text);
}

// Process-wide admission for IOCP slot buffers. The per-reader 2..24 slot cap
// is not a process cap: a playback reader, a probe reader, a scanner and
// subtitle readers can otherwise grow independently. This is a blunt but
// honest ceiling; it is charged when a slot buffer is allocated and released
// when the slot (or its abandoned holder) is finally freed.
std::atomic<std::int64_t> ResidentSlotBytes{0};

std::int64_t ResidentSlotBudgetBytes() noexcept {
    static const std::int64_t budget = static_cast<std::int64_t>(EnvironmentInt(
        L"FFF_NET_IO_BUDGET_MB", static_cast<int>(DefaultResidentBudgetMb), 32, 1024)) *
        1024 * 1024;
    return budget;
}

bool ReserveSlotBudget() noexcept {
    const auto budget = ResidentSlotBudgetBytes();
    auto current = ResidentSlotBytes.load(std::memory_order_relaxed);
    while (current + SlotBytes <= budget) {
        if (ResidentSlotBytes.compare_exchange_weak(current, current + SlotBytes,
            std::memory_order_acq_rel, std::memory_order_relaxed))
            return true;
    }
    return false;
}

void ReleaseSlotBudget() noexcept {
    ResidentSlotBytes.fetch_sub(SlotBytes, std::memory_order_acq_rel);
}

struct Slot {
    enum class State { Free, Reading, Ready, RetryWait, Eof, Error };

    // OVERLAPPED must remain the first member: completions map it back with
    // reinterpret_cast<Slot*>(overlapped).
    OVERLAPPED ov{};
    std::uint8_t* data = nullptr;
    std::int64_t offset = 0;
    int requestBytes = 0;
    int bytes = 0;
    int consumed = 0;
    int attempts = 0;
    std::uint64_t epoch = 0;
    DWORD win32Error = 0;
    bool pending = false;
    bool budgetCharged = false;
    ULONGLONG issueTick = 0;
#ifdef FFF_SECURITY_REGRESSION_TESTS
    ULONGLONG testCompletionDue = 0;
#endif
    State state = State::Free;

    ~Slot() {
        std::free(data);
        data = nullptr;
        if (budgetCharged) {
            budgetCharged = false;
            ReleaseSlotBudget();
        }
    }
};

struct Hole {
    std::int64_t begin = 0;
    std::int64_t end = 0;
};

struct Retry {
    Slot* slot = nullptr;
    ULONGLONG due = 0;
    std::uint64_t epoch = 0;
};

// A timed-out worker stays counted until it drains the last pending packet.
// Admission can enforce the cap before any read is issued, so Stop never has
// to start a new thread or reject ownership of a live request.
std::atomic<int> IocpWorkers{0};
std::atomic<std::int64_t> AbandonedCount{0};

bool ReserveIocpWorker() noexcept {
    auto current = IocpWorkers.load(std::memory_order_relaxed);
    while (current < MaxIocpWorkers) {
        if (IocpWorkers.compare_exchange_weak(current, current + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed))
            return true;
    }
    return false;
}

#ifdef FFF_SECURITY_REGRESSION_TESTS
std::atomic<int> TestShortReadBytes{0};
std::atomic<int> TestShortReadCount{0};
std::atomic<int> TestCompletionDelayMs{0};
std::atomic<int> TestPendingReadDelayMs{0};
#endif

void TraceLine(const char* line) noexcept {
    if (line == nullptr) return;
    std::fprintf(stderr, "%s\n", line);
    const auto path = TraceFilePath();
    if (path.empty()) return;
    FILE* file = _wfopen(path.c_str(), L"a");
    if (file == nullptr) return;
    std::fprintf(file, "%s\n", line);
    std::fclose(file);
}
} // namespace

struct IocpMediaReader::Impl {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE iocp = nullptr;
    std::int64_t size = -1;
    std::int64_t position = 0;
    std::int64_t issuedEnd = 0;
    std::int64_t eofOffset = -1;
    std::uint64_t epoch = 0;
    int inFlight = 0;
    int targetSlots = DefaultSlots;
    int initialTargetSlots = DefaultSlots;
    int hintTargetSlots = DefaultSlots;
    int maxSlots = MaxSlots;
    int highWaitStreak = 0;
    int lowWaitStreak = 0;
    bool fixedTarget = false;
    bool adaptive = false;
    bool stop = false;
    bool workerReserved = false;
    bool trace = false;
    bool trimPending = false;
    bool holeLimitHit = false;
    bool resetting = false;
    bool resetFailed = false;
    DWORD resetError = 0;
    bool eofReached = false;
    ULONGLONG trimEligibleTick = 0;
    DWORD trimDelayMs = TrimDelayMs;
    ULONGLONG lastAdaptTick = 0;
    ULONGLONG lastTargetChangeTick = 0;
    std::int64_t hintBytes = 0;
    std::int64_t bitRateBitsPerSecond = 0;
    std::int64_t servedMarkBytes = 0;
    double consumeBytesPerSecond = 0.0;
    double intervalWaitMs = 0.0;
    DWORD lastError = 0;
    std::array<LatencySample, LatencySampleCapacity> recentLatency{};
    int recentLatencyCount = 0;
    int recentLatencyNext = 0;
    std::vector<std::unique_ptr<Slot>> slots;
    std::deque<Hole> holes;
    std::vector<Retry> retries;
    Metrics metrics;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;

    ~Impl() {
        slots.clear();
        if (iocp != nullptr) CloseHandle(iocp);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (workerReserved) IocpWorkers.fetch_sub(1, std::memory_order_acq_rel);
    }

    int AllocationSlotsLocked() const noexcept {
        int limit = targetSlots;
        if (AtEndLocked()) limit = MinSlots;
        if (size >= 0) {
            const auto sizeSlots = size / SlotBytes + (size % SlotBytes != 0);
            limit = (std::min)(limit, (std::max)(MinSlots,
                static_cast<int>((std::min<std::int64_t>)(sizeSlots, maxSlots))));
        }
        return (std::max)(MinSlots, (std::min)(maxSlots, limit));
    }

    std::int64_t HoleBytesLocked() const noexcept {
        std::int64_t total = 0;
        for (const auto& hole : holes) total += hole.end - hole.begin;
        return total;
    }

    bool EnsureSlotsLocked() noexcept {
        const int limit = AllocationSlotsLocked();
        while (slots.size() < static_cast<std::size_t>(limit) &&
            slots.size() < static_cast<std::size_t>(maxSlots)) {
            auto slot = std::unique_ptr<Slot>(new (std::nothrow) Slot());
            if (slot == nullptr || !ReserveSlotBudget()) break;
            slot->budgetCharged = true;
            slot->data = static_cast<std::uint8_t*>(std::malloc(SlotBytes));
            if (slot->data == nullptr) break;
            slot->epoch = epoch;
            // Start reserves maxSlots entries before issuing any reads.
            slots.push_back(std::move(slot));
        }
        metrics.allocatedSlots = static_cast<int>(slots.size());
        if (metrics.peakAllocatedSlots < metrics.allocatedSlots)
            metrics.peakAllocatedSlots = metrics.allocatedSlots;
        return metrics.allocatedSlots >= MinSlots;
    }

    void AddHoleLocked(std::int64_t begin, std::int64_t end) noexcept {
        if (begin >= end) return;
        if (size >= 0 && begin >= size) return;
        if (size >= 0 && end > size) end = size;
        if (eofOffset >= 0 && begin >= eofOffset) return;
        if (eofOffset >= 0 && end > eofOffset) end = eofOffset;
        if (begin >= end) return;

        auto it = holes.begin();
        while (it != holes.end() && it->end < begin) ++it;
        if (it != holes.end() && it->begin <= end) {
            begin = (std::min)(begin, it->begin);
            end = (std::max)(end, it->end);
            it = holes.erase(it);
        }
        while (it != holes.end() && it->begin <= end) {
            end = (std::max)(end, it->end);
            it = holes.erase(it);
        }
        holes.insert(it, Hole{begin, end});
        ++metrics.holesCreated;
        if (static_cast<int>(holes.size()) > MaxHoles ||
            HoleBytesLocked() > MaxHoleBytes) {
            holeLimitHit = true;
            lastError = ERROR_NOT_ENOUGH_QUOTA;
        }
    }

    void RecordLatencyLocked(const ULONGLONG now, const ULONGLONG latency) noexcept {
        if (metrics.maxReadLatencyMs < static_cast<std::int64_t>(latency))
            metrics.maxReadLatencyMs = static_cast<std::int64_t>(latency);
        if (latency >= 40) ++metrics.highLatencyReads;
        if (recentLatencyCount < LatencySampleCapacity) ++recentLatencyCount;
        recentLatency[recentLatencyNext] = {now, static_cast<std::uint32_t>(
            (std::min<ULONGLONG>)(latency, 0xffffffffu))};
        recentLatencyNext = (recentLatencyNext + 1) % LatencySampleCapacity;
    }

    LatencyPercentiles RecentLatencyLocked(const ULONGLONG now) const noexcept {
        std::array<std::uint32_t, LatencySampleCapacity> sorted{};
        int count = 0;
        for (int i = 0; i < recentLatencyCount; ++i) {
            const auto& sample = recentLatency[i];
            if (now - sample.tick <= LatencyWindowMs) sorted[count++] = sample.ms;
        }
        if (count == 0) return {};
        std::sort(sorted.begin(), sorted.begin() + count);
        const auto at = [&](const int percentile) {
            return sorted[((count - 1) * percentile + 99) / 100];
        };
        return {at(50), at(90), at(95)};
    }

    void MarkEofLocked() noexcept {
        eofReached = true;
        if (size >= 0 && (eofOffset < 0 || eofOffset > size)) eofOffset = size;
        trimPending = true;
        trimEligibleTick = GetTickCount64() + trimDelayMs;
    }

    void PublishEofLocked(Slot* slot) noexcept {
        slot->pending = false;
        slot->bytes = 0;
        slot->state = Slot::State::Eof;
        slot->win32Error = 0;
        ++metrics.eofCount;
        if (eofOffset < 0 || slot->offset < eofOffset) eofOffset = slot->offset;
        trimPending = true;
        trimEligibleTick = GetTickCount64() + trimDelayMs;
    }

    void PublishErrorLocked(Slot* slot, const DWORD error) noexcept {
        slot->pending = false;
        slot->bytes = 0;
        slot->state = Slot::State::Error;
        slot->win32Error = error;
        lastError = error;
        ++metrics.errorCount;
    }

    void PublishSuccessLocked(Slot* slot, DWORD bytes) noexcept {
        slot->pending = false;
        slot->bytes = static_cast<int>(bytes);
        slot->win32Error = 0;
        slot->state = bytes == 0 ? Slot::State::Eof : Slot::State::Ready;
        slot->attempts = 0;
        const auto now = GetTickCount64();
        const auto latency = now - slot->issueTick;
        metrics.readLatencyMs = metrics.readLatencyMs == 0
            ? static_cast<std::int64_t>(latency)
            : (metrics.readLatencyMs * 7 + static_cast<std::int64_t>(latency)) / 8;
        RecordLatencyLocked(now, latency);
#ifdef FFF_SECURITY_REGRESSION_TESTS
        const int shortCap = TestShortReadBytes.load(std::memory_order_relaxed);
        if (shortCap > 0 && bytes > static_cast<DWORD>(shortCap) &&
            TestShortReadCount.load(std::memory_order_relaxed) > 0) {
            TestShortReadCount.fetch_sub(1, std::memory_order_relaxed);
            bytes = static_cast<DWORD>(shortCap);
        }
#endif
        slot->bytes = static_cast<int>(bytes);
        if (bytes == 0) {
            ++metrics.eofCount;
            if (eofOffset < 0 || slot->offset < eofOffset) eofOffset = slot->offset;
            trimPending = true;
            trimEligibleTick = GetTickCount64() + trimDelayMs;
            return;
        }

        metrics.bytesRead += static_cast<std::int64_t>(bytes);
        if (static_cast<int>(bytes) > metrics.maxObservedReadBytes)
            metrics.maxObservedReadBytes = static_cast<std::int64_t>(bytes);
        if (bytes < static_cast<DWORD>(slot->requestBytes)) {
            ++metrics.shortReads;
            AddHoleLocked(slot->offset + static_cast<std::int64_t>(bytes),
                slot->offset + static_cast<std::int64_t>(slot->requestBytes));
        }
    }

    bool IssueReadAtLocked(Slot* slot, const std::int64_t offset, const int request,
        const bool advanceFill, const int attempts = 0) noexcept {
        if (slot == nullptr || request <= 0) return false;
        slot->offset = offset;
        slot->requestBytes = request;
        slot->bytes = 0;
        slot->consumed = 0;
        slot->attempts = attempts;
        slot->pending = true;
        slot->state = Slot::State::Reading;
        slot->epoch = epoch;
        slot->win32Error = 0;
        slot->issueTick = GetTickCount64();
#ifdef FFF_SECURITY_REGRESSION_TESTS
        const int testDelay = TestPendingReadDelayMs.load(std::memory_order_relaxed);
        slot->testCompletionDue = testDelay > 0 ? slot->issueTick + testDelay : 0;
#endif
        std::memset(&slot->ov, 0, sizeof(slot->ov));
        slot->ov.Offset = static_cast<DWORD>(static_cast<std::uint64_t>(offset) & 0xffffffffu);
        slot->ov.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(offset) >> 32);
        if (advanceFill) issuedEnd = offset + request;

        DWORD bytes = 0;
        if (ReadFile(file, slot->data, static_cast<DWORD>(request), &bytes, &slot->ov)) {
#ifdef FFF_SECURITY_REGRESSION_TESTS
            if (testDelay > 0 && PostQueuedCompletionStatus(iocp, bytes, 0, &slot->ov)) {
                metrics.maxInFlight = (std::max)(metrics.maxInFlight, ++inFlight);
                return true;
            }
#endif
            // FILE_SKIP_COMPLETION_PORT_ON_SUCCESS means this operation will not
            // produce a completion packet; publish it directly.
            PublishSuccessLocked(slot, bytes);
            return true;
        }
        const auto error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            ++inFlight;
            if (inFlight > metrics.maxInFlight) metrics.maxInFlight = inFlight;
            return true;
        }
        slot->pending = false;
        lastError = error;
        if (error == ERROR_HANDLE_EOF) {
            PublishEofLocked(slot);
            return true;
        }
        if (IsTransientError(error) && attempts < RetryCount && !stop) {
            ++metrics.retries;
            slot->attempts = attempts + 1;
            slot->state = Slot::State::RetryWait;
            slot->win32Error = error;
            try {
                retries.push_back({slot, GetTickCount64() + RetryDelayMs, slot->epoch});
            } catch (...) {
                PublishErrorLocked(slot, error);
                return false;
            }
            return true;
        }
        PublishErrorLocked(slot, error);
        return false;
    }

    Slot* FindFreeLocked() noexcept {
        for (const auto& slot : slots) {
            if (slot->state == Slot::State::Free && !slot->pending) return slot.get();
        }
        return nullptr;
    }

    bool IssueNextHoleLocked(Slot* slot) noexcept {
        while (!holes.empty()) {
            auto& hole = holes.front();
            std::int64_t limit = size;
            if (eofOffset >= 0 && (limit < 0 || eofOffset < limit)) limit = eofOffset;
            if (limit >= 0 && hole.begin >= limit) {
                holes.pop_front();
                continue;
            }
            std::int64_t end = hole.end;
            if (limit >= 0 && end > limit) end = limit;
            if (end <= hole.begin) {
                holes.pop_front();
                continue;
            }
            const auto start = hole.begin;
            const auto length = end - start;
            const auto request = static_cast<int>((std::min<std::int64_t>)(length, SlotBytes));
            if (request <= 0) {
                holes.pop_front();
                continue;
            }
            if (request == length) holes.pop_front();
            else hole.begin += request;
            if (!IssueReadAtLocked(slot, start, request, false)) {
                AddHoleLocked(start, start + request);
                return false;
            }
            return true;
        }
        return true;
    }

    bool AtEndLocked() const noexcept {
        if (size >= 0 && issuedEnd >= size) return true;
        if (eofOffset >= 0 && issuedEnd >= eofOffset) return true;
        return false;
    }

    void PostMoreLocked() noexcept {
        if (stop || resetting || resetFailed || holeLimitHit) return;
        if (AtEndLocked() && holes.empty() && !trimPending) {
            trimPending = true;
            trimEligibleTick = GetTickCount64() + trimDelayMs;
        }
        MaybeTrimLocked();
        EnsureSlotsLocked();
        while (inFlight < targetSlots) {
            auto* slot = FindFreeLocked();
            if (slot == nullptr) break;
            if (!holes.empty()) {
                if (!IssueNextHoleLocked(slot)) break;
                continue;
            }
            if (AtEndLocked()) break;
            std::int64_t remaining = static_cast<std::int64_t>(SlotBytes);
            if (size >= 0) remaining = (std::min)(remaining, size - issuedEnd);
            if (remaining <= 0) break;
            const auto request = static_cast<int>(remaining);
            if (!IssueReadAtLocked(slot, issuedEnd, request, true)) break;
        }
    }

    // Cancels in-flight reads. `preserveRanges` turns their byte ranges into
    // holes so a later retry cannot skip data; `discardAll` also drops ready,
    // EOF and error slots for a seek/reset or close.
    void CancelReadsLocked(const bool preserveRanges, const bool discardAll) noexcept {
        ++epoch;
        for (const auto& slot : slots) {
            if (slot->state == Slot::State::Reading) {
                if (preserveRanges)
                    AddHoleLocked(slot->offset,
                        slot->offset + static_cast<std::int64_t>(slot->requestBytes));
                CancelIoEx(file, &slot->ov);
                slot->state = Slot::State::Free;
                slot->win32Error = 0;
            } else if (slot->state == Slot::State::RetryWait) {
                if (preserveRanges)
                    AddHoleLocked(slot->offset,
                        slot->offset + static_cast<std::int64_t>(slot->requestBytes));
                slot->state = Slot::State::Free;
                slot->pending = false;
                slot->bytes = 0;
                slot->win32Error = 0;
            } else if (discardAll) {
                slot->state = Slot::State::Free;
                // Do not clear pending here: a previously cancelled slot still
                // has a kernel request in flight until HandleCompletion sees it.
                // ResetAt reuses slots only after inFlight reaches zero.
                slot->bytes = 0;
                slot->consumed = 0;
                slot->win32Error = 0;
            }
        }
        retries.clear();
        if (discardAll) holes.clear();
        changed.notify_all();
    }

    void TrimToSlotsLocked(const int requested) noexcept {
        const int keep = (std::max)(MinSlots, (std::min)(maxSlots, requested));
        while (slots.size() > static_cast<std::size_t>(keep)) {
            auto found = slots.end();
            for (auto it = slots.begin(); it != slots.end(); ++it) {
                if (!(*it)->pending && (*it)->state == Slot::State::Free) {
                    found = it;
                    break;
                }
            }
            if (found == slots.end()) break;
            slots.erase(found);
        }
        metrics.allocatedSlots = static_cast<int>(slots.size());
    }

    void TrimSlotsLocked() noexcept {
        TrimToSlotsLocked(AllocationSlotsLocked());
    }

    void SetAdaptiveLocked(const bool enabled) noexcept {
        adaptive = enabled;
        highWaitStreak = 0;
        lowWaitStreak = 0;
        lastAdaptTick = GetTickCount64();
        servedMarkBytes = metrics.bytesServed;
        intervalWaitMs = 0.0;
        if (!enabled) ApplyTargetLocked(hintTargetSlots);
        else RecomputeTargetLocked(true, 0.0);
    }

    void ResetAdaptiveHistoryLocked() noexcept {
        recentLatencyCount = 0;
        recentLatencyNext = 0;
        highWaitStreak = 0;
        lowWaitStreak = 0;
        lastAdaptTick = GetTickCount64();
        servedMarkBytes = metrics.bytesServed;
        intervalWaitMs = 0.0;
        consumeBytesPerSecond = 0.0;
        metrics.consumeBytesPerSecond = 0;
    }

    void SampleAdaptiveWindowLocked(const ULONGLONG elapsedMs) noexcept {
        const auto servedDelta = metrics.bytesServed - servedMarkBytes;
        servedMarkBytes = metrics.bytesServed;
        const double elapsed = static_cast<double>(elapsedMs);
        const double observed = static_cast<double>(servedDelta) * 1000.0 / elapsed;
        const double historyWeight = std::pow(0.65, elapsed / AdaptiveIntervalMs);
        consumeBytesPerSecond = consumeBytesPerSecond <= 0.0 ? observed :
            consumeBytesPerSecond * historyWeight + observed * (1.0 - historyWeight);
    }

    void RecomputeTargetLocked(const bool immediate, const double waitedMs) noexcept {
        if (!adaptive || fixedTarget || stop || resetting || resetFailed) return;

        const auto now = GetTickCount64();
        const auto recent = RecentLatencyLocked(now);
        const double p50 = recent.p50;
        const double p90 = recent.p90;
        const double p95 = recent.p95;
        const auto jitter = (std::max)(0.0, p90 - p50);
        double coverageMs = p95 + jitter + (std::max)(AdaptiveMinCoverageMs, p95 * 0.5);
        coverageMs = (std::min)(coverageMs, AdaptiveMaxCoverageMs);

        double hintedBytesPerSecond = 0.0;
        if (bitRateBitsPerSecond > 0) {
            hintedBytesPerSecond = static_cast<double>(bitRateBitsPerSecond) / 8.0;
        } else if (hintBytes > 0) {
            // SetTargetBytes is also a test/diagnostic entry; infer a
            // conservative consume rate from "half a second plus one slot".
            hintedBytesPerSecond = (std::max)(1.0 * 1024 * 1024,
                (static_cast<double>(hintBytes) - 1024.0 * 1024.0) * 2.0);
        }
        double consume = consumeBytesPerSecond > 0.0
            ? consumeBytesPerSecond : hintedBytesPerSecond;
        if (hintedBytesPerSecond > 0.0 && consume > 0.0)
            consume = (std::max)(hintedBytesPerSecond,
                (std::min)(consume, hintedBytesPerSecond * 2.0));
        if (consume <= 0.0) consume = 4.0 * 1024 * 1024;

        const auto requiredBytes = consume * coverageMs / 1000.0;
        const auto baseBytes = hintBytes > 0
            ? hintBytes
            : static_cast<std::int64_t>(DefaultSlots) * SlotBytes;
        const auto wantedBytes = (std::max)(requiredBytes, static_cast<double>(baseBytes));
        int desired = (std::max)(MinSlots, static_cast<int>((std::min)(
            static_cast<double>(maxSlots), std::ceil(wantedBytes / SlotBytes))));
        // Consumer starvation is a separate signal from the coverage estimate:
        // at least one extra slot is warranted until the wait disappears. This
        // is what lets a two-slot probe/diagnostic window recover on a high-RTT
        // share without over-provisioning when the formula already has enough.
        if (waitedMs >= 25.0 && desired <= targetSlots && targetSlots < maxSlots)
            desired = (std::min)(maxSlots, targetSlots + 1);
        metrics.consumeBytesPerSecond = static_cast<std::int64_t>(consume);
        if (desired > targetSlots) {
            ++highWaitStreak;
            lowWaitStreak = 0;
            const bool urgent = immediate || waitedMs >= 50.0 || p95 >= 150.0;
            if (highWaitStreak >= (urgent ? 1 : AdaptiveGrowStreak) &&
                (now - lastTargetChangeTick >= AdaptiveHoldMs || immediate)) {
                highWaitStreak = 0;
                ApplyTargetLocked(desired);
            }
        } else if (desired < targetSlots) {
            highWaitStreak = 0;
            if (waitedMs > 0.0) lowWaitStreak = 0;
            else if (!immediate) ++lowWaitStreak;
            if (!immediate && lowWaitStreak >= AdaptiveShrinkStreak &&
                now - lastTargetChangeTick >= AdaptiveHoldMs) {
                lowWaitStreak = 0;
                ApplyTargetLocked(desired);
            }
        } else {
            highWaitStreak = 0;
            lowWaitStreak = 0;
        }
    }

    void ProcessAdaptiveLocked() noexcept {
        if (!adaptive || fixedTarget || stop || resetting || resetFailed) return;
        const auto now = GetTickCount64();
        if (now - lastAdaptTick < AdaptiveIntervalMs) return;
        const auto elapsed = now - lastAdaptTick;
        lastAdaptTick = now;
        SampleAdaptiveWindowLocked(elapsed);
        const auto waitedMs = intervalWaitMs;
        intervalWaitMs = 0.0;
        RecomputeTargetLocked(false, waitedMs);
    }

    void MaybeTrimLocked() noexcept {
        if (!trimPending) return;
        if (GetTickCount64() < trimEligibleTick) return;
        const int keep = (AtEndLocked() || eofReached) ? MinSlots : AllocationSlotsLocked();
        TrimToSlotsLocked(keep);
        trimPending = slots.size() > static_cast<std::size_t>(keep);
    }

    void RetryReadLocked(Slot* slot) noexcept {
        const auto attempts = slot->attempts;
        if (!IssueReadAtLocked(slot, slot->offset, slot->requestBytes, false, attempts))
            changed.notify_all();
    }

    void ProcessRetriesLocked() noexcept {
        const auto now = GetTickCount64();
        for (std::size_t index = 0; index < retries.size();) {
            auto* slot = retries[index].slot;
            if (stop || slot->epoch != epoch || slot->state != Slot::State::RetryWait) {
                retries.erase(retries.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            if (retries[index].due > now) {
                ++index;
                continue;
            }
            const auto retry = retries[index];
            retries.erase(retries.begin() + static_cast<std::ptrdiff_t>(index));
            RetryReadLocked(retry.slot);
        }
    }

    void HandleCompletion(const BOOL ok, const DWORD bytes, Slot* slot,
        const DWORD completionError) noexcept {
#ifdef FFF_SECURITY_REGRESSION_TESTS
        ULONGLONG due = 0;
        {
            std::lock_guard lock(mutex);
            due = slot->testCompletionDue;
        }
        const auto now = GetTickCount64();
        if (due > now) Sleep(static_cast<DWORD>(due - now));
        const int delay = TestCompletionDelayMs.load(std::memory_order_relaxed);
        if (delay > 0) Sleep(static_cast<DWORD>(delay));
#endif
        {
            std::lock_guard lock(mutex);
            slot->pending = false;
            --inFlight;
            if (stop) {
                const auto it = std::find_if(slots.begin(), slots.end(),
                    [slot](const auto& candidate) { return candidate.get() == slot; });
                slots.erase(it); // GQCS has delivered this slot's final packet.
                if (metrics.abandoned) AbandonedCount.fetch_sub(1, std::memory_order_relaxed);
                changed.notify_all();
                return;
            }
            if (slot->epoch != epoch) {
                slot->state = Slot::State::Free;
                changed.notify_all();
                return;
            }

            if (ok) {
                PublishSuccessLocked(slot, bytes);
            } else if (completionError == ERROR_HANDLE_EOF) {
                PublishEofLocked(slot);
            } else if (completionError == ERROR_OPERATION_ABORTED) {
                PublishErrorLocked(slot, completionError);
            } else {
                lastError = completionError;
                if (IsTransientError(completionError) &&
                    slot->attempts < RetryCount && !stop) {
                    ++slot->attempts;
                    ++metrics.retries;
                    slot->state = Slot::State::RetryWait;
                    slot->win32Error = completionError;
                    retries.push_back({slot, GetTickCount64() + RetryDelayMs, slot->epoch});
                } else {
                    PublishErrorLocked(slot, completionError);
                }
            }
            changed.notify_all();
        }
        {
            std::lock_guard lock(mutex);
            if (!stop) {
                ProcessAdaptiveLocked();
                PostMoreLocked();
            }
        }
    }

    void Worker() noexcept {
        while (true) {
            DWORD waitMs = PollIntervalMs;
            {
                std::lock_guard lock(mutex);
                if (stop) {
                    if (inFlight == 0) return;
                    // No scheduling timers remain once only completion drain is left.
                    waitMs = INFINITE;
                }
            }
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            SetLastError(ERROR_SUCCESS);
            const auto ok = GetQueuedCompletionStatus(
                iocp, &bytes, &key, &overlapped, waitMs);
            const auto completionError = ok ? ERROR_SUCCESS : GetLastError();
            (void)key;
            if (overlapped == nullptr) {
                std::lock_guard lock(mutex);
                if (stop) continue;
                ProcessRetriesLocked();
                ProcessAdaptiveLocked();
                PostMoreLocked();
                continue;
            }
            HandleCompletion(ok, bytes, reinterpret_cast<Slot*>(overlapped),
                completionError);
        }
    }

    bool Start(const std::shared_ptr<Impl>& owner, const HANDLE callerFile,
        std::string& error, const int requestedInitialSlots) {
        trace = TraceEnabled();
        maxSlots = EnvironmentInt(L"FFF_NET_IO_MAX_SLOTS", MaxSlots,
            MinSlots, AbsoluteMaxSlots);
        fixedTarget = false;
        targetSlots = (std::min)(maxSlots, DefaultSlots);
        if (requestedInitialSlots >= MinSlots) {
            targetSlots = (std::max)(MinSlots, (std::min)(maxSlots, requestedInitialSlots));
            fixedTarget = true;
        } else {
            const auto depth = EnvironmentInt(L"FFF_NET_IO_DEPTH", 0, 0, maxSlots);
            if (depth >= MinSlots) {
                targetSlots = depth;
                fixedTarget = true;
            }
        }
        initialTargetSlots = targetSlots;
        metrics.initialTargetSlots = initialTargetSlots;
        hintTargetSlots = targetSlots;
        metrics.targetBytes = static_cast<std::int64_t>(targetSlots) * SlotBytes;
        trimDelayMs = static_cast<DWORD>(EnvironmentInt(
            L"FFF_NET_IO_TRIM_DELAY_MS", static_cast<int>(TrimDelayMs), 0, 30000));
        lastTargetChangeTick = GetTickCount64();
        lastAdaptTick = lastTargetChangeTick;

        if (!ReserveIocpWorker()) {
            error = "The network media I/O worker limit is exhausted.";
            return false;
        }
        workerReserved = true;
        try {
            slots.reserve(static_cast<std::size_t>(maxSlots));
        } catch (...) {
            error = "Could not reserve network read-ahead slots.";
            return false;
        }
        epoch = 1;
        if (!EnsureSlotsLocked()) {
            error = "Could not allocate the minimum network read-ahead buffers.";
            return false;
        }
        const HANDLE process = GetCurrentProcess();
        if (!DuplicateHandle(process, callerFile, process, &file, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
            error = "Could not duplicate the network media handle.";
            return false;
        }
        iocp = CreateIoCompletionPort(file, nullptr, 0, 0);
        if (iocp == nullptr) {
            error = "Could not create the media I/O completion port.";
            return false;
        }
        if (!SetFileCompletionNotificationModes(file, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
            error = "Could not enable completion-port mode for network media.";
            return false;
        }
        char line[256]{};
        {
            std::lock_guard lock(mutex);
            try {
                worker = std::thread([owner] { owner->Worker(); });
            } catch (...) {
                error = "Could not start the media I/O worker.";
                return false;
            }
            PostMoreLocked();
            if (trace)
                std::snprintf(line, sizeof(line),
                    "[net-io] start id=%llu target=%d slots=%d size=%lld fixed=%d max=%d",
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)),
                    targetSlots, static_cast<int>(slots.size()),
                    static_cast<long long>(size), fixedTarget ? 1 : 0, maxSlots);
        }
        if (trace) TraceLine(line);
        return true;
    }

    void Stop() noexcept {
        if (!worker.joinable()) return;
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(StopWaitMs);
        char line[768]{};
        bool timedOut = false;
        {
            std::unique_lock lock(mutex);
            stop = true;
            const auto holeBytes = HoleBytesLocked();
            retries.clear();
            holes.clear();
            for (const auto& slot : slots) {
                if (slot->pending) CancelIoEx(file, &slot->ov);
            }
            slots.erase(std::remove_if(slots.begin(), slots.end(),
                [](const auto& slot) { return !slot->pending; }), slots.end());
            changed.notify_all();
            changed.wait_until(lock, deadline, [this] { return inFlight == 0; });
            timedOut = inFlight > 0;
            metrics.abandoned = timedOut;
            if (timedOut) {
                AbandonedCount.fetch_add(inFlight, std::memory_order_relaxed);
                // The worker's shared reference now owns the port, duplicate
                // file handle, and only slots whose packets it still owes.
                worker.detach();
            }
            if (trace) {
                const auto recent = RecentLatencyLocked(GetTickCount64());
                std::snprintf(line, sizeof(line),
                    "[net-io] stop id=%llu target=%d slots=%d peak=%d inFlight=%d maxInFlight=%d "
                    "bytes=%lld served=%lld short=%lld holes=%lld holeBytes=%lld retries=%lld "
                    "resets=%lld resetFail=%lld waits=%lld waitMs=%lld maxWaitMs=%lld "
                    "maxRead=%lld latency=%lldms p50=%ums p90=%ums p95=%ums "
                    "highLatency=%lld errors=%lld eofs=%lld abandoned=%d abandonedBytes=%lld",
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)),
                    targetSlots, static_cast<int>(slots.size()), metrics.peakAllocatedSlots,
                    inFlight, metrics.maxInFlight,
                    static_cast<long long>(metrics.bytesRead),
                    static_cast<long long>(metrics.bytesServed),
                    static_cast<long long>(metrics.shortReads),
                    static_cast<long long>(metrics.holesCreated),
                    static_cast<long long>(holeBytes),
                    static_cast<long long>(metrics.retries),
                    static_cast<long long>(metrics.resets),
                    static_cast<long long>(metrics.resetFailures),
                    static_cast<long long>(metrics.waitEvents),
                    static_cast<long long>(metrics.waitMs),
                    static_cast<long long>(metrics.maxWaitMs),
                    static_cast<long long>(metrics.maxObservedReadBytes),
                    static_cast<long long>(metrics.readLatencyMs),
                    static_cast<unsigned>(recent.p50), static_cast<unsigned>(recent.p90),
                    static_cast<unsigned>(recent.p95),
                    static_cast<long long>(metrics.highLatencyReads),
                    static_cast<long long>(metrics.errorCount),
                    static_cast<long long>(metrics.eofCount),
                    timedOut ? 1 : 0,
                    static_cast<long long>(AbandonedCount.load(std::memory_order_relaxed) * SlotBytes));
            }
        }
        if (!timedOut) {
            PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
            worker.join();
        }
        if (trace) TraceLine(line);
    }

    void ApplyTargetLocked(const int requested) noexcept {
        const int next = (std::max)(MinSlots, (std::min)(maxSlots, requested));
        if (next == targetSlots) return;
        targetSlots = next;
        ++metrics.targetChanges;
        metrics.targetBytes = static_cast<std::int64_t>(targetSlots) * SlotBytes;
        lastTargetChangeTick = GetTickCount64();
        if (next < static_cast<int>(slots.size())) {
            trimPending = true;
            trimEligibleTick = GetTickCount64() + trimDelayMs;
        } else {
            trimPending = false;
        }
        EnsureSlotsLocked();
        PostMoreLocked();
        changed.notify_all();
        if (trace) {
            char line[192]{};
            std::snprintf(line, sizeof(line),
                "[net-io] target id=%llu target=%d slots=%d trim=%d consume=%lldB/s",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)),
                targetSlots, static_cast<int>(slots.size()), trimPending ? 1 : 0,
                static_cast<long long>(metrics.consumeBytesPerSecond));
            TraceLine(line);
        }
    }

    bool ResetAt(const std::int64_t target, const DWORD timeoutMs, DWORD& error,
        const AVIOInterruptCB* interrupt) noexcept {
        std::unique_lock lock(mutex);
        if (stop) {
            error = ERROR_OPERATION_ABORTED;
            return false;
        }
        ++metrics.resets;
        const bool profile = SeekProfileEnabled();
        const int inFlightAtStart = inFlight;
        const int targetSlotsAtStart = targetSlots;
        const int slotsAtStart = static_cast<int>(slots.size());
        const auto resetStart = std::chrono::steady_clock::now();
        resetFailed = false;
        resetError = 0;
        resetting = true;
        if (interrupt != nullptr && Interrupted(*interrupt)) {
            resetting = false;
            resetFailed = true;
            resetError = ERROR_OPERATION_ABORTED;
            error = resetError;
            ++metrics.resetFailures;
            changed.notify_all();
            return false;
        }
        CancelReadsLocked(false, true);
        const auto cancelEnd = std::chrono::steady_clock::now();
        const auto deadline = GetTickCount64() + timeoutMs;
        while (inFlight > 0 && GetTickCount64() < deadline) {
            if (interrupt != nullptr && Interrupted(*interrupt)) {
                resetting = false;
                resetFailed = true;
                resetError = ERROR_OPERATION_ABORTED;
                error = resetError;
                ++metrics.resetFailures;
                changed.notify_all();
                return false;
            }
            changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
        }
        const auto waitEnd = std::chrono::steady_clock::now();
        if (inFlight > 0) {
            resetting = false;
            resetFailed = true;
            resetError = ERROR_TIMEOUT;
            error = resetError;
            ++metrics.resetFailures;
            changed.notify_all();
            if (profile)
                std::fprintf(stderr,
                    "[seek-profile] iocp reset target=%lld inFlight=%d/%d targetSlots=%d slots=%d "
                    "cancel=%.2fms wait=%.2fms result=timeout\n",
                    static_cast<long long>(target), inFlight, inFlightAtStart,
                    targetSlotsAtStart, slotsAtStart, ElapsedMs(resetStart, cancelEnd),
                    ElapsedMs(cancelEnd, waitEnd));
            return false;
        }
        retries.clear();
        holes.clear();
        for (const auto& slot : slots) {
            slot->state = Slot::State::Free;
            slot->pending = false;
            slot->bytes = 0;
            slot->consumed = 0;
            slot->attempts = 0;
            slot->win32Error = 0;
            slot->epoch = epoch;
        }
        position = target;
        issuedEnd = target;
        eofOffset = -1;
        eofReached = false;
        holeLimitHit = false;
        lastError = 0;
        resetFailed = false;
        resetError = 0;
        trimPending = false;
        ResetAdaptiveHistoryLocked();
        TrimSlotsLocked();
        resetting = false;
        const auto clearEnd = std::chrono::steady_clock::now();
        PostMoreLocked();
        changed.notify_all();
        const auto refillEnd = std::chrono::steady_clock::now();
        if (profile)
            std::fprintf(stderr,
                "[seek-profile] iocp reset target=%lld inFlight=%d/%d targetSlots=%d slots=%d "
                "cancel=%.2fms wait=%.2fms clear=%.2fms refill=%.2fms total=%.2fms result=ok\n",
                static_cast<long long>(target), inFlight, inFlightAtStart,
                targetSlotsAtStart, slotsAtStart, ElapsedMs(resetStart, cancelEnd),
                ElapsedMs(cancelEnd, waitEnd), ElapsedMs(waitEnd, clearEnd),
                ElapsedMs(clearEnd, refillEnd), ElapsedMs(resetStart, refillEnd));
        return true;
    }

    static int ErrorResult(const DWORD error) noexcept {
        if (error == ERROR_HANDLE_EOF) return AVERROR_EOF;
        if (error == ERROR_OPERATION_ABORTED) return AVERROR_EXIT;
        if (error == ERROR_TIMEOUT) return AVERROR(ETIMEDOUT);
        return AVERROR(EIO);
    }

    static bool Interrupted(const AVIOInterruptCB& interrupt) noexcept {
        return interrupt.callback != nullptr && interrupt.callback(interrupt.opaque) != 0;
    }

    Slot* FindReadySlot(std::int64_t at) noexcept {
        Slot* best = nullptr;
        for (const auto& candidate : slots) {
            if (candidate->state != Slot::State::Ready || candidate->bytes <= 0) continue;
            if (candidate->offset <= at &&
                at < candidate->offset + static_cast<std::int64_t>(candidate->bytes)) {
                if (best == nullptr || candidate->offset < best->offset) best = candidate.get();
            }
        }
        return best;
    }

    Slot* FindEofSlot(std::int64_t at) noexcept {
        for (const auto& candidate : slots) {
            if (candidate->state == Slot::State::Eof && candidate->offset <= at)
                return candidate.get();
        }
        return nullptr;
    }

    Slot* FindErrorSlot(std::int64_t at) noexcept {
        Slot* best = nullptr;
        for (const auto& candidate : slots) {
            if (candidate->state != Slot::State::Error) continue;
            const auto end = candidate->offset +
                static_cast<std::int64_t>(candidate->requestBytes);
            if (candidate->offset <= at && at < end) {
                if (best == nullptr || candidate->offset < best->offset)
                    best = candidate.get();
            }
        }
        return best;
    }

    int Read(std::uint8_t* buffer, const int bufferSize,
        const AVIOInterruptCB& interrupt, const DWORD timeoutMs, DWORD& error) noexcept {
        if (buffer == nullptr || bufferSize < 0) {
            error = ERROR_INVALID_PARAMETER;
            return AVERROR(EINVAL);
        }
        if (bufferSize == 0) return 0;
        std::unique_lock lock(mutex);
        ++metrics.readCalls;
        double readWaitMs = 0.0;
        if (holeLimitHit) {
            error = lastError != 0 ? lastError : ERROR_NOT_ENOUGH_QUOTA;
            return AVERROR(EIO);
        }
        if (resetFailed) {
            error = resetError != 0 ? resetError : ERROR_TIMEOUT;
            return ErrorResult(error);
        }
        PostMoreLocked();
        const auto deadline = timeoutMs == 0 ? 0 : GetTickCount64() + timeoutMs;
        while (!stop) {
            if (Interrupted(interrupt)) {
                error = ERROR_OPERATION_ABORTED;
                CancelReadsLocked(false, true);
                return AVERROR_EXIT;
            }
            if (resetFailed) {
                error = resetError != 0 ? resetError : ERROR_TIMEOUT;
                return ErrorResult(error);
            }
            if (holeLimitHit) {
                error = lastError != 0 ? lastError : ERROR_NOT_ENOUGH_QUOTA;
                return AVERROR(EIO);
            }
            if (deadline != 0 && GetTickCount64() >= deadline) {
                error = ERROR_TIMEOUT;
                CancelReadsLocked(true, false);
                return AVERROR(ETIMEDOUT);
            }
            if (size >= 0 && position >= size) {
                MarkEofLocked();
                return AVERROR_EOF;
            }
            if (eofOffset >= 0 && position >= eofOffset) {
                MarkEofLocked();
                return AVERROR_EOF;
            }

            auto* slot = FindReadySlot(position);
            if (slot != nullptr) {
                const auto offsetInSlot = position - slot->offset;
                const auto available = slot->bytes - static_cast<int>(offsetInSlot);
                if (available > 0) {
                    const auto count = (std::min)(bufferSize, available);
                    std::memcpy(buffer, slot->data + offsetInSlot, static_cast<std::size_t>(count));
                    position += count;
                    slot->consumed += count;
                    metrics.bytesServed += count;
                    if (slot->consumed >= slot->bytes) slot->state = Slot::State::Free;
                    PostMoreLocked();
                    return count;
                }
            }
            if (auto* failed = FindErrorSlot(position)) {
                error = failed->win32Error != 0 ? failed->win32Error : ERROR_IO_DEVICE;
                return ErrorResult(error);
            }
            if (FindEofSlot(position) != nullptr) {
                MarkEofLocked();
                return AVERROR_EOF;
            }
            ++metrics.waitEvents;
            const auto waitBegin = std::chrono::steady_clock::now();
            changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
            const auto waited = ElapsedMs(waitBegin, std::chrono::steady_clock::now());
            const auto waitedWhole = static_cast<std::int64_t>(waited + 0.5);
            metrics.waitMs += waitedWhole;
            intervalWaitMs += waited;
            readWaitMs += waited;
            metrics.maxWaitMs = (std::max)(metrics.maxWaitMs,
                static_cast<std::int64_t>(readWaitMs + 0.5));
        }
        error = ERROR_OPERATION_ABORTED;
        return AVERROR_EXIT;
    }
};

IocpMediaReader::IocpMediaReader(std::shared_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

IocpMediaReader::~IocpMediaReader() {
    if (impl_ != nullptr) impl_->Stop();
}

std::unique_ptr<IocpMediaReader> IocpMediaReader::Create(const HANDLE file,
    const std::int64_t size, std::string& error,
    const int initialTargetSlots) noexcept {
    try {
        if (file == INVALID_HANDLE_VALUE) {
            error = "The network media handle is invalid.";
            return nullptr;
        }
        if (AbandonedCount.load(std::memory_order_relaxed) * SlotBytes >= MaxAbandonedBytes) {
            error = "The abandoned network read-ahead budget is exhausted.";
            return nullptr;
        }
        auto impl = std::make_shared<Impl>();
        impl->size = size;
        auto reader = std::unique_ptr<IocpMediaReader>(new IocpMediaReader(impl));
        if (!impl->Start(impl, file, error, initialTargetSlots)) return nullptr;
        return reader;
    } catch (...) {
        error = "Could not prepare the IOCP media reader.";
        return nullptr;
    }
}

int IocpMediaReader::Read(std::uint8_t* buffer, const int bufferSize,
    const AVIOInterruptCB& interrupt, const DWORD timeoutMs,
    DWORD& lastError) noexcept {
    return impl_->Read(buffer, bufferSize, interrupt, timeoutMs, lastError);
}

bool IocpMediaReader::Seek(const std::int64_t position, const DWORD timeoutMs,
    DWORD& lastError, const AVIOInterruptCB* interrupt) noexcept {
    if (position < 0) {
        lastError = ERROR_INVALID_PARAMETER;
        return false;
    }
    return impl_->ResetAt(position, timeoutMs, lastError, interrupt);
}

std::int64_t IocpMediaReader::Position() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->position;
}

void IocpMediaReader::SetTargetBytes(const std::int64_t targetBytes) noexcept {
    std::lock_guard lock(impl_->mutex);
    if (impl_->fixedTarget || impl_->stop) return;
    impl_->bitRateBitsPerSecond = 0;
    impl_->hintBytes = targetBytes > 0 ? targetBytes : 0;
    int slots = DefaultSlots;
    if (targetBytes > 0) {
        const auto capped = (std::min<std::int64_t>)(targetBytes,
            static_cast<std::int64_t>(impl_->maxSlots) * SlotBytes);
        slots = static_cast<int>((capped + SlotBytes - 1) / SlotBytes);
    }
    impl_->hintTargetSlots = (std::max)(MinSlots,
        (std::min)(impl_->maxSlots, slots));
    if (impl_->trace) {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[net-io] hint id=%llu bytes=%lld target=%d adaptive=%d",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(impl_.get())),
            static_cast<long long>(targetBytes), impl_->hintTargetSlots,
            impl_->adaptive ? 1 : 0);
        TraceLine(line);
    }
    if (impl_->adaptive) impl_->RecomputeTargetLocked(true, 0.0);
    else impl_->ApplyTargetLocked(impl_->hintTargetSlots);
}

void IocpMediaReader::SetStreamBitRate(const std::int64_t bitsPerSecond) noexcept {
    std::lock_guard lock(impl_->mutex);
    if (impl_->fixedTarget || impl_->stop) return;
    impl_->bitRateBitsPerSecond = bitsPerSecond > 0 ? bitsPerSecond : 0;
    impl_->hintBytes = bitsPerSecond > 0
        ? bitsPerSecond / 16 + 1024 * 1024
        : 0;
    int slots = DefaultSlots;
    if (impl_->hintBytes > 0) {
        const auto capped = (std::min<std::int64_t>)(impl_->hintBytes,
            static_cast<std::int64_t>(impl_->maxSlots) * SlotBytes);
        slots = static_cast<int>((capped + SlotBytes - 1) / SlotBytes);
    }
    impl_->hintTargetSlots = (std::max)(MinSlots,
        (std::min)(impl_->maxSlots, slots));
    if (impl_->trace) {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[net-io] bitrate id=%llu bps=%lld bytes=%lld target=%d adaptive=%d",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(impl_.get())),
            static_cast<long long>(bitsPerSecond),
            static_cast<long long>(impl_->hintBytes), impl_->hintTargetSlots,
            impl_->adaptive ? 1 : 0);
        TraceLine(line);
    }
    if (impl_->adaptive) impl_->RecomputeTargetLocked(true, 0.0);
    else impl_->ApplyTargetLocked(impl_->hintTargetSlots);
}

void IocpMediaReader::SetAdaptiveReadAhead(const bool enabled) noexcept {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stop || impl_->fixedTarget) return;
    impl_->SetAdaptiveLocked(enabled);
}

void IocpMediaReader::SetTargetSlots(const int slots) noexcept {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stop) return;
    impl_->fixedTarget = true;
    impl_->adaptive = false;
    impl_->ApplyTargetLocked(slots);
}

int IocpMediaReader::TargetSlots() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->targetSlots;
}

IocpMediaReader::Metrics IocpMediaReader::MetricsSnapshot() const noexcept {
    std::lock_guard lock(impl_->mutex);
    Metrics metrics = impl_->metrics;
    metrics.initialTargetSlots = impl_->initialTargetSlots;
    metrics.targetSlots = impl_->targetSlots;
    metrics.allocatedSlots = static_cast<int>(impl_->slots.size());
    metrics.peakAllocatedSlots = (std::max)(metrics.peakAllocatedSlots, metrics.allocatedSlots);
    metrics.allocatedBytes = static_cast<std::int64_t>(metrics.allocatedSlots) * SlotBytes;
    metrics.inFlight = impl_->inFlight;
    if (metrics.maxInFlight < metrics.inFlight) metrics.maxInFlight = metrics.inFlight;
    metrics.holeCount = static_cast<std::int64_t>(impl_->holes.size());
    metrics.holeBytes = impl_->HoleBytesLocked();
    const auto recent = impl_->RecentLatencyLocked(GetTickCount64());
    metrics.readLatencyP95Ms = recent.p95;
    metrics.readLatencyP50Ms = recent.p50;
    metrics.readLatencyP90Ms = recent.p90;
    metrics.holeLimitHit = impl_->holeLimitHit;
    metrics.abandoned = impl_->metrics.abandoned;
    metrics.abandonedSlots = AbandonedCount.load(std::memory_order_relaxed);
    metrics.abandonedBytes = metrics.abandonedSlots * SlotBytes;
    metrics.targetBytes = static_cast<std::int64_t>(impl_->targetSlots) * SlotBytes;
    return metrics;
}

#ifdef FFF_SECURITY_REGRESSION_TESTS
void IocpMediaReader::TestSetShortRead(const int bytes, const int count) noexcept {
    TestShortReadBytes.store((std::max)(0, bytes), std::memory_order_relaxed);
    TestShortReadCount.store((std::max)(0, count), std::memory_order_relaxed);
}

void IocpMediaReader::TestSetCompletionDelayMs(const int delayMs) noexcept {
    TestCompletionDelayMs.store((std::max)(0, delayMs), std::memory_order_relaxed);
}

void IocpMediaReader::TestSetPendingReadDelayMs(const int delayMs) noexcept {
    TestPendingReadDelayMs.store((std::max)(0, delayMs), std::memory_order_relaxed);
}

std::int64_t IocpMediaReader::TestResidentSlotBytes() noexcept {
    return ResidentSlotBytes.load(std::memory_order_relaxed);
}
#endif
