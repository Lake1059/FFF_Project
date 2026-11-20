#include "pch.h"
#include "Shared/Ffmpeg/IocpMediaReader.h"

extern "C" {
#include <libavutil/error.h>
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {
constexpr int SlotCount = 4;
constexpr int SlotBytes = 1024 * 1024;
constexpr DWORD PollIntervalMs = 50;
constexpr DWORD RetryDelayMs = 150;
constexpr int RetryCount = 1;

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
        return true;
    default:
        return false;
    }
}
}

struct IocpMediaReader::Impl {
    struct Slot {
        // OVERLAPPED must remain the first member: completions map it back with
        // reinterpret_cast<Slot*>(overlapped).
        OVERLAPPED ov{};
        std::uint8_t* data = nullptr;
        std::int64_t offset = 0;
        int bytes = 0;
        int consumed = 0;
        int attempts = 0;
        std::uint64_t epoch = 0;
        enum class State { Free, Reading, Ready, Eof, Error } state = State::Free;
    };

    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE iocp = nullptr;
    std::int64_t size = -1;
    std::int64_t position = 0;
    std::int64_t nextFill = 0;
    std::int64_t eofOffset = -1;
    std::uint64_t epoch = 0;
    int inFlight = 0;
    bool stop = false;
    DWORD lastError = 0;
    std::vector<std::unique_ptr<Slot>> slots;
    std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;

    ~Impl() {
        for (const auto& slot : slots) {
            if (slot != nullptr && slot->data != nullptr) {
                std::free(slot->data);
                slot->data = nullptr;
            }
        }
    }

    bool Start(std::string& error) {
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
        slots.resize(SlotCount);
        for (auto& slot : slots) {
            slot = std::make_unique<Slot>();
            slot->data = static_cast<std::uint8_t*>(std::malloc(SlotBytes));
            if (slot->data == nullptr) {
                error = "Could not allocate network read-ahead buffers.";
                CloseHandle(iocp);
                iocp = nullptr;
                return false;
            }
        }
        epoch = 1;
        position = 0;
        nextFill = 0;
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
            for (auto& slot : slots) slot->epoch = epoch;
            PostMoreLocked();
        }
        return true;
    }

    void Stop() noexcept {
        {
            std::lock_guard lock(mutex);
            stop = true;
            ++epoch;
            for (const auto& slot : slots) {
                if (slot->state == Slot::State::Reading) CancelIoEx(file, &slot->ov);
            }
        }
        // Let the worker reap cancellation completions before the slots (and
        // their OVERLAPPED blocks) are released. A completion can be queued
        // after CancelIoEx returns; closing the port first would leave the
        // kernel owning memory that the destructor is about to free.
        {
            std::unique_lock lock(mutex);
            const auto deadline = GetTickCount64() + 2000;
            while (inFlight > 0 && GetTickCount64() < deadline)
                changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
        }
        if (iocp != nullptr) PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
        if (worker.joinable()) worker.join();
        if (iocp != nullptr) {
            CloseHandle(iocp);
            iocp = nullptr;
        }
    }

    static bool Interrupted(const AVIOInterruptCB& interrupt) noexcept {
        return interrupt.callback != nullptr && interrupt.callback(interrupt.opaque) != 0;
    }

    void PostMoreLocked() noexcept {
        if (stop) return;
        while (inFlight < SlotCount) {
            if (size >= 0 && nextFill >= size) break;
            if (eofOffset >= 0 && nextFill >= eofOffset) break;
            Slot* slot = nullptr;
            for (const auto& candidate : slots) {
                if (candidate->state == Slot::State::Free) {
                    slot = candidate.get();
                    break;
                }
            }
            if (slot == nullptr) break;
            IssueReadLocked(slot);
            if (slot->state == Slot::State::Error) break;
        }
    }

    void IssueReadLocked(Slot* slot) noexcept {
        IssueReadAtLocked(slot, nextFill, true);
    }

    void RetryReadLocked(Slot* slot) noexcept {
        IssueReadAtLocked(slot, slot->offset, false);
    }

    void IssueReadAtLocked(Slot* slot, const std::int64_t offset,
        const bool advanceFill) noexcept {
        slot->offset = offset;
        slot->bytes = 0;
        slot->consumed = 0;
        slot->state = Slot::State::Reading;
        slot->epoch = epoch;
        std::memset(&slot->ov, 0, sizeof(slot->ov));
        slot->ov.Offset = static_cast<DWORD>(static_cast<std::uint64_t>(offset) & 0xffffffffu);
        slot->ov.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(offset) >> 32);
        if (advanceFill) nextFill = offset + SlotBytes;

        DWORD bytes = 0;
        if (ReadFile(file, slot->data, SlotBytes, &bytes, &slot->ov)) {
            // FILE_SKIP_COMPLETION_PORT_ON_SUCCESS means this operation will not
            // produce a completion packet; publish it directly.
            slot->bytes = static_cast<int>(bytes);
            slot->state = bytes == 0 ? Slot::State::Eof : Slot::State::Ready;
            if (bytes == 0) {
                eofOffset = offset;
            } else {
                // A short read is not necessarily EOF (SMB may cap a single
                // response). Re-arm at the actual end of this range and let a
                // subsequent zero-byte read establish EOF.
                if (bytes < SlotBytes)
                    nextFill = offset + bytes;
                else if (offset + bytes > nextFill)
                    nextFill = offset + bytes;
                if (size >= 0 && nextFill > size) nextFill = size;
            }
            return;
        }
        const auto error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            ++inFlight;
            return;
        }
        lastError = error;
        slot->state = Slot::State::Error;
    }

    void HandleCompletion(const BOOL ok, const DWORD bytes, Slot* slot) noexcept {
        bool retry = false;
        {
            std::lock_guard lock(mutex);
            if (stop || slot->epoch != epoch) {
                slot->state = Slot::State::Free;
                if (inFlight > 0) --inFlight;
                changed.notify_all();
                return;
            }
            if (inFlight > 0) --inFlight;

            if (ok && bytes == 0) {
                slot->bytes = 0;
                slot->state = Slot::State::Eof;
                eofOffset = slot->offset;
            } else if (ok) {
                slot->bytes = static_cast<int>(bytes);
                slot->state = Slot::State::Ready;
                slot->attempts = 0;
                const auto end = slot->offset + static_cast<std::int64_t>(bytes);
                if (bytes < SlotBytes)
                    nextFill = end;
                else if (end > nextFill)
                    nextFill = end;
                if (size >= 0 && nextFill > size) nextFill = size;
            } else {
                lastError = GetLastError();
                if (IsTransientError(lastError) && slot->attempts < RetryCount && !stop) {
                    ++slot->attempts;
                    slot->state = Slot::State::Free;
                    retry = true;
                } else {
                    slot->state = Slot::State::Error;
                }
            }
            changed.notify_all();
        }
        if (retry) {
            Sleep(RetryDelayMs);
            std::lock_guard lock(mutex);
            if (!stop && slot->epoch == epoch && slot->state == Slot::State::Free)
                RetryReadLocked(slot);
            changed.notify_all();
        } else {
            std::lock_guard lock(mutex);
            PostMoreLocked();
        }
    }

    void Worker() noexcept {
        while (true) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const auto ok = GetQueuedCompletionStatus(
                iocp, &bytes, &key, &overlapped, INFINITE);
            if (overlapped == nullptr) {
                std::lock_guard lock(mutex);
                if (stop) break;
                continue;
            }
            auto* slot = reinterpret_cast<Slot*>(overlapped);
            HandleCompletion(ok, bytes, slot);
        }
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
            if (candidate->state == Slot::State::Eof && candidate->offset <= at) return candidate.get();
        }
        return nullptr;
    }

    void CancelPendingLocked() noexcept {
        ++epoch;
        for (const auto& slot : slots) {
            if (slot->state == Slot::State::Reading) CancelIoEx(file, &slot->ov);
            slot->state = Slot::State::Free;
        }
    }

    bool ResetAt(std::int64_t target, DWORD timeoutMs, DWORD& error) noexcept {
        std::unique_lock lock(mutex);
        CancelPendingLocked();
        const auto deadline = GetTickCount64() + timeoutMs;
        while (inFlight > 0 && GetTickCount64() < deadline)
            changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
        if (inFlight > 0) {
            error = ERROR_TIMEOUT;
            return false;
        }
        for (const auto& slot : slots) {
            slot->state = Slot::State::Free;
            slot->bytes = 0;
            slot->consumed = 0;
            slot->attempts = 0;
            slot->epoch = epoch;
        }
        position = target;
        nextFill = target;
        eofOffset = -1;
        PostMoreLocked();
        return true;
    }

    int Read(std::uint8_t* buffer, const int bufferSize,
        const AVIOInterruptCB& interrupt, const DWORD timeoutMs, DWORD& error) noexcept {
        if (buffer == nullptr || bufferSize < 0) {
            error = ERROR_INVALID_PARAMETER;
            return AVERROR(EINVAL);
        }
        std::unique_lock lock(mutex);
        const auto deadline = timeoutMs == 0 ? 0 : GetTickCount64() + timeoutMs;
        while (!stop) {
            if (Interrupted(interrupt)) {
                error = ERROR_OPERATION_ABORTED;
                CancelPendingLocked();
                return AVERROR_EXIT;
            }
            if (deadline != 0 && GetTickCount64() >= deadline) {
                error = ERROR_TIMEOUT;
                CancelPendingLocked();
                return AVERROR(ETIMEDOUT);
            }
            if (size >= 0 && position >= size) return AVERROR_EOF;
            if (eofOffset >= 0 && position >= eofOffset) return AVERROR_EOF;

            if (auto* slot = FindReadySlot(position)) {
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
            changed.wait_for(lock, std::chrono::milliseconds(PollIntervalMs));
        }
        error = ERROR_OPERATION_ABORTED;
        return AVERROR_EXIT;
    }
};

IocpMediaReader::IocpMediaReader(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

IocpMediaReader::~IocpMediaReader() {
    if (impl_ != nullptr) impl_->Stop();
}

std::unique_ptr<IocpMediaReader> IocpMediaReader::Create(const HANDLE file,
    const std::int64_t size, std::string& error) noexcept {
    try {
        if (file == INVALID_HANDLE_VALUE) {
            error = "The network media handle is invalid.";
            return nullptr;
        }
        auto impl = std::make_unique<Impl>();
        impl->file = file;
        impl->size = size;
        if (!impl->Start(error)) return nullptr;
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
