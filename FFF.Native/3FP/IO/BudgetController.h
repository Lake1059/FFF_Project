#pragma once

#include <cstdint>
#include <optional>

#include "IoBudgetMath.h"

// Phase-0 dynamic budget controller. It owns the 500 ms sampling gate and the
// 25% single-step clamp; it deliberately does not own a thread or a timer.
// PlayerSession calls Sample() from its existing pump, so a slow input can
// never leave a second budget loop running against it.
namespace FFFPlayer::Io {

struct BudgetControllerConfig final {
    std::uint32_t sampleIntervalMs = 500;
    std::uint32_t maxStepPercent = 25;
    std::uint32_t hysteresisPercent = 10;
    std::uint32_t largeWindowCeilingMiB = 384;
};

class BudgetController final {
public:
    explicit BudgetController(BudgetControllerConfig config = {}) noexcept
        : config_(config) {}

    void Reset(const SessionBudget& initial, std::uint64_t nowMs) noexcept {
        current_ = initial;
        lastSampleMs_ = nowMs;
        hasCurrent_ = true;
    }

    // Returns a new target only when the sampling gate opens. The returned value
    // is the rate-limited target; callers must not apply the raw ComputeSessionBudget
    // result directly, or a transient RTT spike becomes a cache resize storm.
    std::optional<SessionBudget> Sample(std::uint64_t nowMs,
        const BudgetInputs& inputs) noexcept {
        if (hasCurrent_ && nowMs >= lastSampleMs_ &&
            nowMs - lastSampleMs_ < config_.sampleIntervalMs) {
            return std::nullopt;
        }
        const auto raw = ComputeSessionBudget(inputs);
        lastSampleMs_ = nowMs;
        if (!hasCurrent_) {
            current_ = raw;
            hasCurrent_ = true;
            return current_;
        }
        const auto next = RateLimit(current_, raw);
        if (!Changed(current_, next)) return std::nullopt;
        current_ = next;
        return current_;
    }

    const SessionBudget& Current() const noexcept { return current_; }
    bool HasCurrent() const noexcept { return hasCurrent_; }

private:
    static std::uint64_t LimitStep(std::uint64_t previous, std::uint64_t desired,
        std::uint32_t percent) noexcept {
        const auto maxStep = SaturatingMultiply(previous, percent) / 100;
        if (desired > previous) return previous + (std::min)(desired - previous, maxStep);
        if (desired < previous) return previous - (std::min)(previous - desired, maxStep);
        return previous;
    }

    static std::uint64_t LimitByteField(std::uint64_t previous, std::uint64_t desired,
        std::uint32_t percent) noexcept {
        // Disabling a cache is a safety action, not a bandwidth adjustment: let it
        // reach zero immediately rather than decaying over four samples.
        if (desired == 0) return 0;
        return LimitStep(previous, desired, percent);
    }

    static bool Near(std::uint64_t a, std::uint64_t b, std::uint32_t percent) noexcept {
        if (a == b) return true;
        const auto larger = (std::max)(a, b);
        const auto delta = a > b ? a - b : b - a;
        return delta <= larger / 100 * percent;
    }

    static bool Changed(const SessionBudget& a, const SessionBudget& b) noexcept {
        return a.byteWindowTargetBytes != b.byteWindowTargetBytes ||
            a.packetForwardBytes != b.packetForwardBytes ||
            a.packetBackBytes != b.packetBackBytes ||
            a.segmentCacheBytes != b.segmentCacheBytes ||
            a.prefetchBytes != b.prefetchBytes ||
            a.resumeWatermarkBytes != b.resumeWatermarkBytes ||
            a.byteWindowCeilingBytes != b.byteWindowCeilingBytes ||
            a.avioBufferBytes != b.avioBufferBytes ||
            a.frameQueueDepth != b.frameQueueDepth ||
            a.readIdleTimeoutMs != b.readIdleTimeoutMs ||
            a.openDeadlineMs != b.openDeadlineMs ||
            a.seekDeadlineMs != b.seekDeadlineMs ||
            a.stallTimeoutMs != b.stallTimeoutMs ||
            a.hardStallMs != b.hardStallMs ||
            a.canPromiseContinuousPlayback != b.canPromiseContinuousPlayback;
    }

    SessionBudget RateLimit(const SessionBudget& previous, const SessionBudget& desired) const noexcept {
        SessionBudget next = desired;
        next.byteWindowTargetBytes = LimitByteField(previous.byteWindowTargetBytes,
            desired.byteWindowTargetBytes, config_.maxStepPercent);
        next.byteWindowCeilingBytes = LimitByteField(previous.byteWindowCeilingBytes,
            desired.byteWindowCeilingBytes, config_.maxStepPercent);
        next.packetForwardBytes = LimitByteField(previous.packetForwardBytes,
            desired.packetForwardBytes, config_.maxStepPercent);
        next.packetBackBytes = LimitByteField(previous.packetBackBytes,
            desired.packetBackBytes, config_.maxStepPercent);
        next.segmentCacheBytes = LimitByteField(previous.segmentCacheBytes,
            desired.segmentCacheBytes, config_.maxStepPercent);
        next.prefetchBytes = LimitByteField(previous.prefetchBytes,
            desired.prefetchBytes, config_.maxStepPercent);
        next.resumeWatermarkBytes = LimitByteField(previous.resumeWatermarkBytes,
            desired.resumeWatermarkBytes, config_.maxStepPercent);
        if (Near(previous.avioBufferBytes, desired.avioBufferBytes,
            config_.hysteresisPercent)) {
            next.avioBufferBytes = previous.avioBufferBytes;
        }
        return next;
    }

    BudgetControllerConfig config_{};
    SessionBudget current_{};
    std::uint64_t lastSampleMs_ = 0;
    bool hasCurrent_ = false;
};

} // namespace FFFPlayer::Io
