#pragma once

#include <cstdint>
#include <mutex>

// Session health for the G1-G4 contract. It is intentionally independent of
// FFmpeg and Windows: the caller passes a monotonic tick and a buffer level,
// so the same state machine can be unit-tested with a fake clock.
//
// Hard rule: Tick() can produce Stalled or WaitingNetwork, never Failed.
// FailedPermanent exists only for a source-level permanent error delivered by
// OnPermanentError(); a timeout is not one.
namespace FFFPlayer::Io {

enum class HealthState : std::uint8_t {
    Opening = 0,
    Buffering = 1,
    Playing = 2,
    Paused = 3,
    Stalled = 4,
    Recovering = 5,
    Degraded = 6,
    FailedPermanent = 7,
    Cancelled = 8,
};

constexpr bool IsTerminal(HealthState state) noexcept {
    return state == HealthState::FailedPermanent || state == HealthState::Cancelled;
}

constexpr const char* HealthStateName(HealthState state) noexcept {
    switch (state) {
    case HealthState::Opening: return "opening";
    case HealthState::Buffering: return "buffering";
    case HealthState::Playing: return "playing";
    case HealthState::Paused: return "paused";
    case HealthState::Stalled: return "stalled";
    case HealthState::Recovering: return "recovering";
    case HealthState::Degraded: return "degraded";
    case HealthState::FailedPermanent: return "failed_permanent";
    case HealthState::Cancelled: return "cancelled";
    }
    return "unknown";
}

struct ProgressWatchdogConfig final {
    std::uint32_t stallTimeoutMs = 30000;
    std::uint64_t hardStallMs = 300000;
    std::uint64_t resumeWatermarkBytes = 8 * 1024 * 1024;
};

struct HealthSnapshot final {
    HealthState state = HealthState::Opening;
    std::uint64_t lastProgressMs = 0;
    std::uint64_t progressGapMs = 0;
    std::uint64_t bufferedBytes = 0;
    std::uint32_t stallCount = 0;
    std::uint32_t resumeCount = 0;
    bool waitingNetwork = false;
    bool resumeEligible = false;
};

class ProgressWatchdog final {
public:
    explicit ProgressWatchdog(ProgressWatchdogConfig config = {}) noexcept
        : config_(config) {}

    void Reset(std::uint64_t nowMs) noexcept {
        std::lock_guard lock(mutex_);
        started_ = true;
        lastProgressMs_ = nowMs;
        state_ = HealthState::Opening;
        resumeState_ = HealthState::Buffering;
        bufferedBytes_ = 0;
        stallCount_ = 0;
        resumeCount_ = 0;
        waitingNetwork_ = false;
        resumeEligible_ = false;
    }

    void Begin(std::uint64_t nowMs) noexcept {
        std::lock_guard lock(mutex_);
        started_ = true;
        lastProgressMs_ = nowMs;
        state_ = HealthState::Buffering;
        resumeState_ = HealthState::Buffering;
    }

    // `progress` covers a successful read, a completed segment, or consumption
    // of a packet/frame from an existing cache. `bytes` may be zero for the
    // cache-consumption case.
    void OnProgress(std::uint64_t nowMs, std::uint64_t bytes = 0,
        bool progress = true) noexcept {
        std::lock_guard lock(mutex_);
        if (!started_ || IsTerminal(state_)) return;
        if (!progress) return;
        lastProgressMs_ = nowMs;
        bufferedBytes_ += bytes;
        waitingNetwork_ = false;
        if (state_ == HealthState::Stalled) {
            state_ = HealthState::Recovering;
            ++resumeCount_;
        }
        if ((state_ == HealthState::Buffering || state_ == HealthState::Recovering) &&
            resumeEligible_ && bufferedBytes_ >= config_.resumeWatermarkBytes) {
            state_ = HealthState::Playing;
        }
    }

    void OnBufferLevel(std::uint64_t bytes) noexcept {
        std::lock_guard lock(mutex_);
        if (IsTerminal(state_) || state_ == HealthState::Paused) {
            bufferedBytes_ = bytes;
            return;
        }
        bufferedBytes_ = bytes;
        if (state_ == HealthState::Playing && bytes < config_.resumeWatermarkBytes) {
            state_ = HealthState::Buffering;
        }
        if ((state_ == HealthState::Buffering || state_ == HealthState::Recovering) &&
            resumeEligible_ && bytes >= config_.resumeWatermarkBytes) {
            state_ = HealthState::Playing;
        }
    }

    void SetResumeEligible(bool eligible) noexcept {
        std::lock_guard lock(mutex_);
        resumeEligible_ = eligible;
        if (!eligible) return;
        if ((state_ == HealthState::Buffering || state_ == HealthState::Recovering) &&
            bufferedBytes_ >= config_.resumeWatermarkBytes) {
            state_ = HealthState::Playing;
        }
    }

    void MarkPlaying() noexcept {
        std::lock_guard lock(mutex_);
        if (IsTerminal(state_) || state_ == HealthState::Paused) return;
        state_ = HealthState::Playing;
    }

    void MarkDegraded() noexcept {
        std::lock_guard lock(mutex_);
        if (IsTerminal(state_)) return;
        state_ = HealthState::Degraded;
    }

    void SetPaused(bool paused, std::uint64_t nowMs) noexcept {
        std::lock_guard lock(mutex_);
        if (IsTerminal(state_)) return;
        if (paused) {
            if (state_ != HealthState::Paused) resumeState_ = state_;
            state_ = HealthState::Paused;
            lastProgressMs_ = nowMs;
            waitingNetwork_ = false;
            return;
        }
        if (state_ != HealthState::Paused) return;
        if (bufferedBytes_ < config_.resumeWatermarkBytes) {
            state_ = HealthState::Buffering;
        } else {
            state_ = resumeState_ == HealthState::Stalled
                ? HealthState::Stalled : HealthState::Playing;
        }
        lastProgressMs_ = nowMs;
    }

    // The only function that can move a live session to a non-recoverable state.
    // Callers must pass a source-level permanent error (404/410, DRM, unsupported
    // format), not a read timeout.
    void OnPermanentError(std::uint64_t nowMs) noexcept {
        std::lock_guard lock(mutex_);
        if (state_ == HealthState::Cancelled) return;
        state_ = HealthState::FailedPermanent;
        lastProgressMs_ = nowMs;
        waitingNetwork_ = false;
    }

    void OnCancel(std::uint64_t nowMs) noexcept {
        std::lock_guard lock(mutex_);
        state_ = HealthState::Cancelled;
        lastProgressMs_ = nowMs;
        waitingNetwork_ = false;
    }

    void Tick(std::uint64_t nowMs) noexcept {
        std::lock_guard lock(mutex_);
        if (!started_ || IsTerminal(state_) || state_ == HealthState::Paused) return;
        const auto gap = nowMs >= lastProgressMs_ ? nowMs - lastProgressMs_ : 0;
        if (state_ == HealthState::Playing || state_ == HealthState::Buffering ||
            state_ == HealthState::Recovering || state_ == HealthState::Degraded) {
            if (gap >= config_.stallTimeoutMs) {
                state_ = HealthState::Stalled;
                ++stallCount_;
                waitingNetwork_ = gap >= config_.hardStallMs;
            }
        } else if (state_ == HealthState::Stalled) {
            // Stalled stays non-terminal forever; hardStall only changes the UI
            // from "buffering" to "waiting for network".
            waitingNetwork_ = gap >= config_.hardStallMs;
        }
    }

    HealthSnapshot Snapshot(std::uint64_t nowMs) const noexcept {
        std::lock_guard lock(mutex_);
        HealthSnapshot snapshot{};
        snapshot.state = state_;
        snapshot.lastProgressMs = lastProgressMs_;
        snapshot.progressGapMs = nowMs >= lastProgressMs_ ? nowMs - lastProgressMs_ : 0;
        snapshot.bufferedBytes = bufferedBytes_;
        snapshot.stallCount = stallCount_;
        snapshot.resumeCount = resumeCount_;
        snapshot.waitingNetwork = waitingNetwork_;
        snapshot.resumeEligible = resumeEligible_;
        return snapshot;
    }

private:
    mutable std::mutex mutex_;
    ProgressWatchdogConfig config_{};
    HealthState state_ = HealthState::Opening;
    HealthState resumeState_ = HealthState::Buffering;
    std::uint64_t lastProgressMs_ = 0;
    std::uint64_t bufferedBytes_ = 0;
    std::uint32_t stallCount_ = 0;
    std::uint32_t resumeCount_ = 0;
    bool waitingNetwork_ = false;
    bool resumeEligible_ = false;
    bool started_ = false;
};

} // namespace FFFPlayer::Io
