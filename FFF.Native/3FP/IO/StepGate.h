#pragma once

#include <atomic>
#include <cstdint>

// P2 step gate: every pipeline step carries the generation it was scheduled
// under. A seek/cancel bumps the generation; old steps that have not entered
// yet fail TryEnter instead of racing the new controller state. The gate is
// lock-free and does not wait: the caller decides when to drain.
namespace FFFPlayer::Io {

class StepGate final {
public:
    explicit StepGate(const std::uint32_t maxInFlight = 1) noexcept
        : maxInFlight_(maxInFlight == 0 ? 1 : maxInFlight) {}

    std::uint64_t BeginGeneration() noexcept {
        inFlight_.store(0, std::memory_order_release);
        return generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    std::uint64_t Cancel() noexcept {
        inFlight_.store(0, std::memory_order_release);
        return generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    std::uint64_t Generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

    bool IsCurrent(const std::uint64_t generation) const noexcept {
        return generation == Generation();
    }

    bool TryEnter(const std::uint64_t generation) noexcept {
        if (!IsCurrent(generation)) return false;
        std::uint32_t expected = inFlight_.load(std::memory_order_acquire);
        do {
            if (expected >= maxInFlight_) return false;
        } while (!inFlight_.compare_exchange_weak(expected, expected + 1,
            std::memory_order_acq_rel, std::memory_order_acquire));
        if (!IsCurrent(generation)) {
            Leave();
            return false;
        }
        return true;
    }

    void Leave() noexcept {
        std::uint32_t current = inFlight_.load(std::memory_order_acquire);
        while (current != 0 &&
            !inFlight_.compare_exchange_weak(current, current - 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
        }
    }

    std::uint32_t InFlight() const noexcept {
        return inFlight_.load(std::memory_order_acquire);
    }
    std::uint32_t MaxInFlight() const noexcept { return maxInFlight_; }

private:
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint32_t> inFlight_{0};
    std::uint32_t maxInFlight_ = 1;
};

} // namespace FFFPlayer::Io
