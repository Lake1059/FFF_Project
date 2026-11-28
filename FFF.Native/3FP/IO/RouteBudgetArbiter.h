#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

// P6 route-level budget arbiter. It is the admission gate in front of the
// session budget: nine routes may share a fixed byte pool, and the tenth is
// refused synchronously instead of overcommitting memory and failing later in
// the allocator. Leases are RAII; Release is explicit so a caller can shrink at
// a safe point before re-claiming.
namespace FFFPlayer::Io {

class RouteBudgetArbiter final {
public:
    struct Snapshot final {
        std::uint64_t capacity = 0;
        std::uint64_t used = 0;
        std::uint32_t activeRoutes = 0;
        std::uint64_t configEpoch = 0;
    };

    explicit RouteBudgetArbiter(const std::uint64_t capacity = 0) noexcept
        : capacity_(capacity) {}

    bool TryAcquire(const std::uint64_t bytes) noexcept {
        std::lock_guard lock(mutex_);
        if (bytes > capacity_ || used_ > capacity_ - bytes) return false;
        used_ += bytes;
        ++activeRoutes_;
        return true;
    }

    void Release(const std::uint64_t bytes) noexcept {
        std::lock_guard lock(mutex_);
        used_ = bytes >= used_ ? 0 : used_ - bytes;
        if (activeRoutes_ != 0) --activeRoutes_;
    }

    // P0 VBR: the lease starts at a small admission probe, then grows or shrinks
    // once the stream bitrate is known. Resizing in place keeps the route slot
    // (the session is never briefly un-admitted and does not lose its active
    // route count between the old and new claim).
    bool Resize(const std::uint64_t oldBytes, const std::uint64_t newBytes) noexcept {
        std::lock_guard lock(mutex_);
        if (oldBytes > used_) return false;
        const auto base = used_ - oldBytes;
        if (newBytes > capacity_ || base > capacity_ - newBytes) return false;
        used_ = base + newBytes;
        ++configEpoch_;
        return true;
    }

    bool SetCapacity(const std::uint64_t capacity) noexcept {
        std::lock_guard lock(mutex_);
        if (capacity < used_) return false;
        capacity_ = capacity;
        ++configEpoch_;
        return true;
    }

    Snapshot Current() const noexcept {
        std::lock_guard lock(mutex_);
        return Snapshot{capacity_, used_, activeRoutes_, configEpoch_};
    }

    bool TryAcquireLease(const std::uint64_t bytes, std::uint64_t& leaseBytes) noexcept {
        if (!TryAcquire(bytes)) return false;
        leaseBytes = bytes;
        return true;
    }

private:
    mutable std::mutex mutex_;
    std::uint64_t capacity_ = 0;
    std::uint64_t used_ = 0;
    std::uint32_t activeRoutes_ = 0;
    std::uint64_t configEpoch_ = 0;
};

class RouteBudgetLease final {
public:
    RouteBudgetLease() = default;
    RouteBudgetLease(RouteBudgetArbiter& arbiter, const std::uint64_t bytes) noexcept
        : arbiter_(&arbiter), bytes_(bytes) {}
    RouteBudgetLease(const RouteBudgetLease&) = delete;
    RouteBudgetLease& operator=(const RouteBudgetLease&) = delete;
    RouteBudgetLease(RouteBudgetLease&& other) noexcept
        : arbiter_(other.arbiter_), bytes_(other.bytes_) {
        other.arbiter_ = nullptr;
        other.bytes_ = 0;
    }
    RouteBudgetLease& operator=(RouteBudgetLease&& other) noexcept {
        if (this != &other) {
            Release();
            arbiter_ = other.arbiter_;
            bytes_ = other.bytes_;
            other.arbiter_ = nullptr;
            other.bytes_ = 0;
        }
        return *this;
    }
    ~RouteBudgetLease() { Release(); }

    void Release() noexcept {
        if (arbiter_ != nullptr) {
            arbiter_->Release(bytes_);
            arbiter_ = nullptr;
            bytes_ = 0;
        }
    }

    bool Active() const noexcept { return arbiter_ != nullptr; }
    std::uint64_t Bytes() const noexcept { return bytes_; }

    bool Resize(const std::uint64_t newBytes) noexcept {
        if (arbiter_ == nullptr || newBytes == bytes_) return arbiter_ != nullptr;
        if (!arbiter_->Resize(bytes_, newBytes)) return false;
        bytes_ = newBytes;
        return true;
    }

private:
    RouteBudgetArbiter* arbiter_ = nullptr;
    std::uint64_t bytes_ = 0;
};

} // namespace FFFPlayer::Io
