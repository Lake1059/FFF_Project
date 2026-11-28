#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "IoBudgetMath.h"

// A short-lived lease of the controller's current target. Consumers hold a lease
// (not the global budget) so shrinking is always done at a safe point: when a
// lease expires, the holder must cancel/wait in-flight work before re-claiming.
namespace FFFPlayer::Io {

inline constexpr std::uint32_t kLeaseTtlMs = 2000;

class SessionBudgetLease final {
public:
    void Grant(std::uint64_t nowMs, const SessionBudget& budget,
        std::uint64_t configEpoch, std::uint32_t ttlMs = kLeaseTtlMs) noexcept {
        std::lock_guard lock(mutex_);
        budget_ = budget;
        grantedAtMs_ = nowMs;
        expiresAtMs_ = nowMs + ttlMs;
        configEpoch_ = configEpoch;
        active_ = true;
    }

    bool Expired(std::uint64_t nowMs) const noexcept {
        std::lock_guard lock(mutex_);
        return !active_ || nowMs >= expiresAtMs_;
    }

    std::optional<SessionBudget> Effective(std::uint64_t nowMs) const noexcept {
        std::lock_guard lock(mutex_);
        if (!active_ || nowMs >= expiresAtMs_) return std::nullopt;
        return budget_;
    }

    void Release() noexcept {
        std::lock_guard lock(mutex_);
        active_ = false;
    }

    std::uint64_t ConfigEpoch() const noexcept {
        std::lock_guard lock(mutex_);
        return configEpoch_;
    }

private:
    mutable std::mutex mutex_;
    SessionBudget budget_{};
    std::uint64_t grantedAtMs_ = 0;
    std::uint64_t expiresAtMs_ = 0;
    std::uint64_t configEpoch_ = 0;
    bool active_ = false;
};

} // namespace FFFPlayer::Io
