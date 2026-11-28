#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

// A validated byte window shared by local, UNC, HTTP and segment readers.
// It tracks only a contiguous range that has already passed ReadLedger
// validation; it never turns a gap or a timeout into EOF. A window may be
// re-based (resized) without invalidating already validated bytes, which is the
// configuration-epoch / source-epoch split from the I/O 2.0 contract.
namespace FFFPlayer::Io {

class ByteWindow final {
public:
    ByteWindow() = default;
    explicit ByteWindow(const std::uint64_t capacity) noexcept : capacity_(capacity) {}

    void Reset(const std::int64_t start, const std::uint64_t capacity) noexcept {
        start_ = start < 0 ? 0 : start;
        end_ = start_;
        capacity_ = capacity;
    }

    void SetCapacity(const std::uint64_t capacity) noexcept {
        capacity_ = capacity;
        if (Length() > capacity_) {
            // Keep the newest bytes. This is a configuration change, not a
            // source change: no ledger epoch is touched.
            start_ = end_ - static_cast<std::int64_t>(capacity_);
        }
    }

    bool Empty() const noexcept { return end_ <= start_; }
    std::uint64_t Length() const noexcept {
        return end_ <= start_ ? 0 : static_cast<std::uint64_t>(end_ - start_);
    }
    std::uint64_t Capacity() const noexcept { return capacity_; }
    std::int64_t Start() const noexcept { return start_; }
    std::int64_t End() const noexcept { return end_; }

    // Appends bytes that begin exactly at the current end, or overlap the tail
    // already held. A gap is rejected: the caller must seek/refill instead of
    // pretending the missing bytes arrived.
    bool Append(const std::int64_t offset, const std::uint64_t length) noexcept {
        if (length == 0) return true;
        if (offset < start_ || offset > end_) return false;
        const auto newEnd = offset > (std::numeric_limits<std::int64_t>::max)() -
            static_cast<std::int64_t>(length)
            ? (std::numeric_limits<std::int64_t>::max)()
            : offset + static_cast<std::int64_t>(length);
        if (newEnd <= end_) return true;
        end_ = newEnd;
        if (capacity_ != 0 && Length() > capacity_) {
            start_ = end_ - static_cast<std::int64_t>(capacity_);
        }
        return true;
    }

    bool Contains(const std::int64_t offset, const std::uint64_t length) const noexcept {
        if (length == 0) return offset >= start_ && offset <= end_;
        if (offset < start_) return false;
        const auto limit = offset > (std::numeric_limits<std::int64_t>::max)() -
            static_cast<std::int64_t>(length)
            ? (std::numeric_limits<std::int64_t>::max)()
            : offset + static_cast<std::int64_t>(length);
        return limit <= end_;
    }

    // Drops bytes strictly before `offset`; returns the number dropped. A
    // consumer that still needs an older offset must seek/refill, not reuse a
    // stale pointer.
    std::uint64_t EvictBefore(const std::int64_t offset) noexcept {
        if (offset <= start_) return 0;
        const auto dropped = static_cast<std::uint64_t>(std::min(offset, end_) - start_);
        start_ = std::min(offset, end_);
        if (start_ >= end_) {
            start_ = end_;
        }
        return dropped;
    }

private:
    std::int64_t start_ = 0;
    std::int64_t end_ = 0;
    std::uint64_t capacity_ = 0;
};

} // namespace FFFPlayer::Io
