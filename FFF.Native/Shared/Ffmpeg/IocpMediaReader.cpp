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
constexpr int MaxSlots = 12;
constexpr int DefaultSlots = 4;
constexpr DWORD PollIntervalMs = 50;
constexpr DWORD RetryDelayMs = 150;
constexpr int RetryCount = 1;
constexpr ULONGLONG StopWaitMs = 3000;
constexpr DWORD TrimDelayMs = 2000;
constexpr int MaxHoles = 64;
constexpr std::int64_t MaxHoleBytes = 16 * 1024 * 1024;
constexpr std::int64_t MaxAbandonedBytes = 64 * 1024 * 1024;
constexpr ULONGLONG AdaptiveIntervalMs = 1000;
constexpr ULONGLONG AdaptiveHoldMs = 2000;
constexpr int AdaptiveMaxBias = 4;
constexpr std::int64_t AdaptiveLatencyMs = 40;
constexpr std::array<ULONGLONG, 16> LatencyBounds = {
    0, 1, 2, 3, 5, 8, 12, 18, 25, 40, 60, 90, 140, 220, 500, 1000
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
    bool pending = false;
    ULONGLONG issueTick = 0;
    State state = State::Free;

    ~Slot() {
        std::free(data);
        data = nullptr;
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

// Slots that could not be reaped within StopWaitMs are deliberately leaked.
// The kernel may still own their OVERLAPPED/data pointers; freeing them here
// would turn a stuck share into a use-after-free. This path should be rare and
// only happens when the completion port cannot observe cancellation.
std::mutex AbandonedMutex;
std::vector<std::unique_ptr<Slot>> AbandonedSlots;
std::atomic<std::int64_t> AbandonedBytes{0};
std::atomic<std::int64_t> AbandonedCount{0};

#ifdef FFF_SECURITY_REGRESSION_TESTS
std::atomic<int> TestShortReadBytes{0};
std::atomic<int> TestShortReadCount{0};
std::atomic<int> TestCompletionDelayMs{0};
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
    int adaptiveBias = 0;
    int highWaitStreak = 0;
    int lowWaitStreak = 0;
    bool fixedTarget = false;
    bool adaptive = false;
    bool stop = false;
    bool trace = false;
    bool trimPending = false;
    bool holeLimitHit = false;
    ULONGLONG trimEligibleTick = 0;
    DWORD trimDelayMs = TrimDelayMs;
    ULONGLONG lastAdaptTick = 0;
    ULONGLONG lastTargetChangeTick = 0;
    std::int64_t adaptWaitMark = 0;
    std::int64_t adaptReadMark = 0;
    DWORD lastError = 0;
    std::array<std::uint64_t, 16> latencyBuckets{};
    std::vector<std::unique_ptr<Slot>> slots;
    std::deque<Hole> holes;
    std::vector<Retry> retries;
    Metrics metrics;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;

    ~Impl() {
        slots.clear();
    }

    int AllocationSlotsLocked() const noexcept {
        int limit = targetSlots;
        if (size >= 0) {
            const auto sizeSlots = (size + SlotBytes - 1) / SlotBytes;
            limit = (std::min)(limit, (std::max)(MinSlots,
                static_cast<int>((std::min<std::int64_t>)(sizeSlots, MaxSlots))));
        }
        return limit;
    }

    std::int64_t HoleBytesLocked() const noexcept {
        std::int64_t total = 0;
        for (const auto& hole : holes) total += hole.end - hole.begin;
        return total;
    }

    bool EnsureSlotsLocked() noexcept {
        const int limit = AllocationSlotsLocked();
        while (slots.size() < static_cast<std::size_t>(limit) &&
            slots.size() < static_cast<std::size_t>(MaxSlots)) {
            auto slot = std::unique_ptr<Slot>(new (std::nothrow) Slot());
            if (slot == nullptr) return false;
            slot->data = static_cast<std::uint8_t*>(std::malloc(SlotBytes));
            if (slot->data == nullptr) return false;
            slot->epoch = epoch;
            try {
                slots.push_back(std::move(slot));
            } catch (...) {
                return false;
            }
        }
        metrics.allocatedSlots = static_cast<int>(slots.size());
        if (metrics.peakAllocatedSlots < metrics.allocatedSlots)
            metrics.peakAllocatedSlots = metrics.allocatedSlots;
        return true;
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
            metrics.holeLimitHit = true;
            lastError = ERROR_NOT_ENOUGH_QUOTA;
        }
    }

    void RecordLatencyLocked(const ULONGLONG latency) noexcept {
        std::size_t bucket = 0;
        while (bucket + 1 < LatencyBounds.size() && latency >= LatencyBounds[bucket + 1])
            ++bucket;
        ++latencyBuckets[bucket];
        if (metrics.maxReadLatencyMs < static_cast<std::int64_t>(latency))
            metrics.maxReadLatencyMs = static_cast<std::int64_t>(latency);
    }

    std::int64_t P95LatencyLocked() const noexcept {
        std::uint64_t total = 0;
        for (const auto count : latencyBuckets) total += count;
        if (total == 0) return 0;
        const auto target = (total * 95 + 99) / 100;
        std::uint64_t seen = 0;
        for (std::size_t bucket = 0; bucket < latencyBuckets.size(); ++bucket) {
            seen += latencyBuckets[bucket];
            if (seen >= target)
                return static_cast<std::int64_t>(LatencyBounds[bucket]);
        }
        return static_cast<std::int64_t>(LatencyBounds.back());
    }

    void PublishSuccessLocked(Slot* slot, DWORD bytes) noexcept {
        slot->pending = false;
        slot->bytes = static_cast<int>(bytes);
        slot->state = bytes == 0 ? Slot::State::Eof : Slot::State::Ready;
        slot->attempts = 0;
        if (slot->issueTick != 0) {
            const auto latency = GetTickCount64() - slot->issueTick;
            metrics.readLatencyMs = metrics.readLatencyMs == 0
                ? static_cast<std::int64_t>(latency)
                : (metrics.readLatencyMs * 7 + static_cast<std::int64_t>(latency)) / 8;
            RecordLatencyLocked(latency);
        }
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
            eofOffset = slot->offset;
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
        slot->issueTick = GetTickCount64();
        std::memset(&slot->ov, 0, sizeof(slot->ov));
        slot->ov.Offset = static_cast<DWORD>(static_cast<std::uint64_t>(offset) & 0xffffffffu);
        slot->ov.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(offset) >> 32);
        if (advanceFill) issuedEnd = offset + request;

        DWORD bytes = 0;
        if (ReadFile(file, slot->data, static_cast<DWORD>(request), &bytes, &slot->ov)) {
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
        if (IsTransientError(error) && attempts < RetryCount && !stop) {
            ++metrics.retries;
            slot->attempts = attempts + 1;
            slot->state = Slot::State::RetryWait;
            try {
                retries.push_back({slot, GetTickCount64() + RetryDelayMs, slot->epoch});
            } catch (...) {
                slot->state = Slot::State::Error;
                return false;
            }
            return true;
        }
        slot->state = Slot::State::Error;
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
        if (stop || holeLimitHit) return;
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

    void CancelPendingLocked() noexcept {
        ++epoch;
        for (const auto& slot : slots) {
            if (slot->state == Slot::State::Reading) CancelIoEx(file, &slot->ov);
            slot->state = Slot::State::Free;
        }
        changed.notify_all();
    }

    void TrimToSlotsLocked(const int requested) noexcept {
        const int keep = (std::max)(MinSlots, (std::min)(MaxSlots, requested));
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
        adaptiveBias = 0;
        highWaitStreak = 0;
        lowWaitStreak = 0;
        lastAdaptTick = GetTickCount64();
        adaptWaitMark = metrics.waitEvents;
        adaptReadMark = metrics.readCalls;
    }

    void ProcessAdaptiveLocked() noexcept {
        if (!adaptive || fixedTarget || stop) return;
        const auto now = GetTickCount64();
        if (now - lastAdaptTick < AdaptiveIntervalMs) return;
        const auto waitDelta = metrics.waitEvents - adaptWaitMark;
        const auto readDelta = metrics.readCalls - adaptReadMark;
        adaptWaitMark = metrics.waitEvents;
        adaptReadMark = metrics.readCalls;
        lastAdaptTick = now;

        // A wait event is expensive only when it is a meaningful fraction of
        // the read calls in the interval. P95 catches high-RTT shares where
        // several completions still arrive too late for the current window.
        const bool high = (waitDelta >= 2 &&
            (readDelta == 0 || waitDelta * 8 >= readDelta)) ||
            (waitDelta > 0 && P95LatencyLocked() >= AdaptiveLatencyMs);
        const bool low = waitDelta == 0 && inFlight < targetSlots;
        if (high) {
            ++highWaitStreak;
            lowWaitStreak = 0;
        } else if (low) {
            ++lowWaitStreak;
            highWaitStreak = 0;
        } else {
            highWaitStreak = 0;
            lowWaitStreak = 0;
        }

        bool changed = false;
        if (highWaitStreak >= 2 && adaptiveBias < AdaptiveMaxBias &&
            now - lastTargetChangeTick >= AdaptiveHoldMs) {
            ++adaptiveBias;
            highWaitStreak = 0;
            changed = true;
        } else if (lowWaitStreak >= 4 && adaptiveBias > 0 &&
            now - lastTargetChangeTick >= AdaptiveHoldMs) {
            --adaptiveBias;
            lowWaitStreak = 0;
            changed = true;
        }
        if (!changed) return;
        ApplyTargetLocked((std::max)(MinSlots,
            (std::min)(MaxSlots, hintTargetSlots + adaptiveBias)));
    }

    void MaybeTrimLocked() noexcept {
        if (!trimPending || inFlight > 0) return;
        if (GetTickCount64() < trimEligibleTick) return;
        const int keep = eofOffset >= 0 && issuedEnd >= eofOffset
            ? MinSlots
            : AllocationSlotsLocked();
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

    void HandleCompletion(const BOOL ok, const DWORD bytes, Slot* slot) noexcept {
#ifdef FFF_SECURITY_REGRESSION_TESTS
        const int delay = TestCompletionDelayMs.load(std::memory_order_relaxed);
        if (delay > 0) Sleep(static_cast<DWORD>(delay));
#endif
        {
            std::lock_guard lock(mutex);
            slot->pending = false;
            if (inFlight > 0) --inFlight;
            if (stop || slot->epoch != epoch) {
                slot->state = Slot::State::Free;
                changed.notify_all();
                return;
            }

            if (ok) {
                PublishSuccessLocked(slot, bytes);
            } else {
                lastError = GetLastError();
                if (IsTransientError(lastError) && slot->attempts < RetryCount && !stop) {
                    ++slot->attempts;
                    ++metrics.retries;
                    slot->state = Slot::State::RetryWait;
                    retries.push_back({slot, GetTickCount64() + RetryDelayMs, slot->epoch});
                } else {
                    slot->state = Slot::State::Error;
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
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const auto ok = GetQueuedCompletionStatus(
                iocp, &bytes, &key, &overlapped, PollIntervalMs);
            (void)key;
            if (overlapped == nullptr) {
                std::lock_guard lock(mutex);
                if (stop) break;
                ProcessRetriesLocked();
                ProcessAdaptiveLocked();
                PostMoreLocked();
                continue;
            }
            HandleCompletion(ok, bytes, reinterpret_cast<Slot*>(overlapped));
        }
    }

    bool Start(std::string& error, const int requestedInitialSlots) {
        trace = TraceEnabled();
        fixedTarget = false;
        targetSlots = DefaultSlots;
        if (requestedInitialSlots >= MinSlots) {
            targetSlots = (std::max)(MinSlots, (std::min)(MaxSlots, requestedInitialSlots));
            fixedTarget = true;
        } else {
            const auto depth = EnvironmentInt(L"FFF_NET_IO_DEPTH", 0, 0, MaxSlots);
            if (depth >= MinSlots) {
                targetSlots = depth;
                fixedTarget = true;
            }
        }
        initialTargetSlots = targetSlots;
        metrics.initialTargetSlots = initialTargetSlots;
        hintTargetSlots = targetSlots;
        trimDelayMs = static_cast<DWORD>(EnvironmentInt(
            L"FFF_NET_IO_TRIM_DELAY_MS", static_cast<int>(TrimDelayMs), 0, 30000));
        lastTargetChangeTick = GetTickCount64();

        iocp = CreateIoCompletionPort(file, nullptr, 0, 0);
        if (iocp == nullptr) {
            error = "Could not create the media I/O completion port.";
            return false;
        }
        if (!SetFileCompletionNotificationModes(file, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)) {
            error = "Could not enable completion-port mode for network media.";
            CloseHandle(iocp);
            iocp = nullptr;
            return false;
        }
        try {
            slots.reserve(MaxSlots);
        } catch (...) {
            error = "Could not reserve network read-ahead slots.";
            CloseHandle(iocp);
            iocp = nullptr;
            return false;
        }
        if (!EnsureSlotsLocked()) {
            error = "Could not allocate network read-ahead buffers.";
            CloseHandle(iocp);
            iocp = nullptr;
            return false;
        }
        epoch = 1;
        position = 0;
        issuedEnd = 0;
        try {
            worker = std::thread([this] { Worker(); });
        } catch (...) {
            error = "Could not start the media I/O worker.";
            CloseHandle(iocp);
            iocp = nullptr;
            return false;
        }
        {
            std::lock_guard lock(mutex);
            for (const auto& slot : slots) slot->epoch = epoch;
            PostMoreLocked();
        }
        if (trace) {
            char line[256]{};
            std::snprintf(line, sizeof(line),
                "[net-io] start id=%llu target=%d slots=%d size=%lld fixed=%d",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)),
                targetSlots, static_cast<int>(slots.size()),
                static_cast<long long>(size), fixedTarget ? 1 : 0);
            TraceLine(line);
        }
        return true;
    }

    void Stop() noexcept {
        bool timedOut = false;
        {
            std::lock_guard lock(mutex);
            stop = true;
            ++epoch;
            for (const auto& slot : slots) {
                if (slot->state == Slot::State::Reading) CancelIoEx(file, &slot->ov);
            }
            changed.notify_all();
        }
        {
            std::unique_lock lock(mutex);
            const auto deadline = GetTickCount64() + StopWaitMs;
            while (inFlight > 0 && GetTickCount64() < deadline)
                changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
            if (inFlight > 0) timedOut = true;
            retries.clear();
            holes.clear();
        }
        if (iocp != nullptr) PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
        if (worker.joinable()) worker.join();
        if (iocp != nullptr) {
            CloseHandle(iocp);
            iocp = nullptr;
        }
        if (timedOut) {
            const auto bytes = static_cast<std::int64_t>(slots.size()) * SlotBytes;
            AbandonedBytes.fetch_add(bytes, std::memory_order_relaxed);
            AbandonedCount.fetch_add(static_cast<std::int64_t>(slots.size()),
                std::memory_order_relaxed);
            std::lock_guard lock(AbandonedMutex);
            for (auto& slot : slots) AbandonedSlots.push_back(std::move(slot));
            slots.clear();
        }
        metrics.inFlight = inFlight;
        metrics.abandoned = timedOut;
        if (trace) {
            char line[640]{};
            std::snprintf(line, sizeof(line),
                "[net-io] stop id=%llu target=%d slots=%d peak=%d inFlight=%d maxInFlight=%d "
                "bytes=%lld short=%lld holes=%lld holeBytes=%lld retries=%lld resets=%lld "
                "waits=%lld maxRead=%lld latency=%lldms p95=%lldms abandoned=%d "
                "abandonedBytes=%lld",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)),
                targetSlots, static_cast<int>(slots.size()), metrics.peakAllocatedSlots,
                inFlight, metrics.maxInFlight,
                static_cast<long long>(metrics.bytesRead),
                static_cast<long long>(metrics.shortReads),
                static_cast<long long>(metrics.holesCreated),
                static_cast<long long>(HoleBytesLocked()),
                static_cast<long long>(metrics.retries),
                static_cast<long long>(metrics.resets),
                static_cast<long long>(metrics.waitEvents),
                static_cast<long long>(metrics.maxObservedReadBytes),
                static_cast<long long>(metrics.readLatencyMs),
                static_cast<long long>(P95LatencyLocked()),
                timedOut ? 1 : 0,
                static_cast<long long>(AbandonedBytes.load(std::memory_order_relaxed)));
            TraceLine(line);
        }
    }

    void ApplyTargetLocked(const int requested) noexcept {
        const int next = (std::max)(MinSlots, (std::min)(MaxSlots, requested));
        if (next == targetSlots) return;
        targetSlots = next;
        ++metrics.targetChanges;
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
            char line[160]{};
            std::snprintf(line, sizeof(line),
                "[net-io] target id=%llu target=%d slots=%d trim=%d",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)),
                targetSlots, static_cast<int>(slots.size()), trimPending ? 1 : 0);
            TraceLine(line);
        }
    }

    bool ResetAt(const std::int64_t target, const DWORD timeoutMs, DWORD& error) noexcept {
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
        CancelPendingLocked();
        const auto cancelEnd = std::chrono::steady_clock::now();
        const auto deadline = GetTickCount64() + timeoutMs;
        while (inFlight > 0 && GetTickCount64() < deadline)
            changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
        const auto waitEnd = std::chrono::steady_clock::now();
        if (inFlight > 0) {
            error = ERROR_TIMEOUT;
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
            slot->epoch = epoch;
        }
        TrimSlotsLocked();
        position = target;
        issuedEnd = target;
        eofOffset = -1;
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

    int Read(std::uint8_t* buffer, const int bufferSize,
        const AVIOInterruptCB& interrupt, const DWORD timeoutMs, DWORD& error) noexcept {
        if (buffer == nullptr || bufferSize < 0) {
            error = ERROR_INVALID_PARAMETER;
            return AVERROR(EINVAL);
        }
        if (bufferSize == 0) return 0;
        std::unique_lock lock(mutex);
        ++metrics.readCalls;
        if (holeLimitHit) {
            error = lastError != 0 ? lastError : ERROR_NOT_ENOUGH_QUOTA;
            return AVERROR(EIO);
        }
        PostMoreLocked();
        const auto deadline = timeoutMs == 0 ? 0 : GetTickCount64() + timeoutMs;
        while (!stop) {
            if (Interrupted(interrupt)) {
                error = ERROR_OPERATION_ABORTED;
                CancelPendingLocked();
                return AVERROR_EXIT;
            }
            if (holeLimitHit) {
                error = lastError != 0 ? lastError : ERROR_NOT_ENOUGH_QUOTA;
                return AVERROR(EIO);
            }
            if (deadline != 0 && GetTickCount64() >= deadline) {
                error = ERROR_TIMEOUT;
                CancelPendingLocked();
                return AVERROR(ETIMEDOUT);
            }
            if (size >= 0 && position >= size) return AVERROR_EOF;
            if (eofOffset >= 0 && position >= eofOffset) return AVERROR_EOF;

            auto* slot = FindReadySlot(position);
            if (slot != nullptr) {
                const auto offsetInSlot = position - slot->offset;
                const auto available = slot->bytes - static_cast<int>(offsetInSlot);
                if (available > 0) {
                    const auto count = (std::min)(bufferSize, available);
                    std::memcpy(buffer, slot->data + offsetInSlot, static_cast<std::size_t>(count));
                    position += count;
                    slot->consumed += count;
                    if (slot->consumed >= slot->bytes) slot->state = Slot::State::Free;
                    PostMoreLocked();
                    return count;
                }
            }
            if (FindEofSlot(position) != nullptr) return AVERROR_EOF;
            ++metrics.waitEvents;
            changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
        }
        error = ERROR_OPERATION_ABORTED;
        return AVERROR_EXIT;
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
};

IocpMediaReader::IocpMediaReader(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

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
        if (AbandonedBytes.load(std::memory_order_relaxed) >= MaxAbandonedBytes) {
            error = "The abandoned network read-ahead budget is exhausted.";
            return nullptr;
        }
        auto impl = std::make_unique<Impl>();
        impl->file = file;
        impl->size = size;
        if (!impl->Start(error, initialTargetSlots)) return nullptr;
        return std::unique_ptr<IocpMediaReader>(new IocpMediaReader(std::move(impl)));
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
    DWORD& lastError) noexcept {
    if (position < 0) {
        lastError = ERROR_INVALID_PARAMETER;
        return false;
    }
    return impl_->ResetAt(position, timeoutMs, lastError);
}

std::int64_t IocpMediaReader::Position() const noexcept {
    std::lock_guard lock(impl_->mutex);
    return impl_->position;
}

void IocpMediaReader::SetTargetBytes(const std::int64_t targetBytes) noexcept {
    std::lock_guard lock(impl_->mutex);
    if (impl_->fixedTarget || impl_->stop) return;
    int slots = DefaultSlots;
    if (targetBytes > 0) {
        const auto capped = (std::min<std::int64_t>)(targetBytes,
            static_cast<std::int64_t>(MaxSlots) * SlotBytes);
        slots = static_cast<int>((capped + SlotBytes - 1) / SlotBytes);
    }
    impl_->hintTargetSlots = slots;
    const int requested = impl_->adaptive
        ? (std::max)(MinSlots, (std::min)(MaxSlots, slots + impl_->adaptiveBias))
        : slots;
    if (impl_->trace) {
        char line[160]{};
        std::snprintf(line, sizeof(line),
            "[net-io] hint id=%llu bytes=%lld target=%d bias=%d",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(impl_.get())),
            static_cast<long long>(targetBytes), requested, impl_->adaptiveBias);
        TraceLine(line);
    }
    impl_->ApplyTargetLocked(requested);
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
    metrics.readLatencyP95Ms = impl_->P95LatencyLocked();
    metrics.holeLimitHit = impl_->holeLimitHit;
    metrics.abandoned = impl_->metrics.abandoned;
    metrics.abandonedSlots = AbandonedCount.load(std::memory_order_relaxed);
    metrics.abandonedBytes = AbandonedBytes.load(std::memory_order_relaxed);
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
#endif
