#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <type_traits>

#include "3FP/Prefetch/MemoryGuard.h"

// Unified memory budget ledger for the five playback-side consumers.
// Contract v1 is frozen: docs/108-统一内存预算仲裁器设计（2026-11-17） is the spec.
//
// Rules that this header encodes and that callers must not bend around:
//  * Only this object decides how much memory a consumer may hold. Consumers do
//    not shrink on their own pressure readings.
//  * Time values everywhere else in the session are 100 ns; byte values here are
//    plain uint64 counts of bytes. No seconds, no MiB, no ratios at this boundary.
//  * Concession order is fixed: PrefetchCache yields first, then VideoFrameQueue,
//    then AudioPcmQueue. DecodeSurfaces and RenderSlot never yield (they only
//    register their floor).
//  * Evaluate() is the only change notification source. Consumers must not poll
//    a clock; a grant change smaller than max(floorBytes/8, ceilingBytes/16)
//    must not trigger a recompute.
//  * The arbiter owns no thread and no timer. Its only event sources are the
//    MemoryGuard tier change, the session profile switch, a D3D device/window
//    change, and Claim/Release calls.
namespace FFFPlayer {

inline constexpr std::uint32_t MemoryArbiterContractVersion = 1;

enum class MemConsumer : std::uint8_t {
    AudioPcmQueue   = 0,  // L2 chunk deque + reusable blocks (WasapiRenderer)
    VideoFrameQueue = 1,  // decoded frame queue, byte + frame double cap
    DecodeSurfaces  = 2,  // hardware surfaces / software DPB: registered, never trimmed
    RenderSlot      = 3,  // the single texture slot in VideoRenderer: same
    PrefetchCache   = 4,  // derived data, the only consumer that may go to zero
    Count           = 5,
};

struct MemClaim final {
    std::uint64_t floorBytes = 0;    // below this the consumer cannot work correctly
    std::uint64_t targetBytes = 0;   // what it would like to hold
    std::uint64_t ceilingBytes = 0;  // hard upper bound (profile / user choice)
    std::uint8_t priority = 0;       // 0 = most expendable ... 3 = never yields
};

struct SessionBudgetContext final {
    std::uint64_t physicalBytes = 0;
    std::uint64_t commitLimitBytes = 0;
    std::uint64_t commitAvailableBytes = 0;
    std::uint64_t availablePhysBytes = 0;
    std::uint32_t profileVideo = 0;
    std::uint32_t profileAudio = 0;
};

class MemoryBudgetArbiter final {
public:
    static MemoryBudgetArbiter& Instance() noexcept;

    // Once per session, at DoOpen; DetachSession at DoClose. AttachSession samples
    // the context immediately (S1) so the first Claim already has a budget.
    void AttachSession(const SessionBudgetContext& context) noexcept;
    void DetachSession() noexcept;

    // Register (overwrite) this consumer's claim and return the granted byte count.
    // Claim is idempotent-replacing, never additive: re-claiming after a resolution
    // change replaces the previous floor/target instead of stacking on it.
    std::uint64_t Claim(MemConsumer who, const MemClaim& claim) noexcept;

    // Give bytes back (consumer shrank or released). Never returns more than held.
    void Release(MemConsumer who, std::uint64_t bytes) noexcept;

    // Temporary overshoot above the granted value. Succeeds only when the pool has
    // headroom after respecting every higher-priority floor.
    bool TryBorrow(MemConsumer who, std::uint64_t extraBytes) noexcept;
    void ReturnExtra(MemConsumer who, std::uint64_t bytes) noexcept;

    // Recompute and return a bitmask: bit i set means consumer i's grant changed
    // by more than its dead zone. This is the ONLY notification consumers get.
    std::uint32_t Evaluate() noexcept;

    std::uint64_t Granted(MemConsumer who) const noexcept;
    std::uint64_t TotalBudget() const noexcept;
    std::uint64_t TotalGranted() const noexcept;
    // Σgranted + Σborrowed: the run-time invariant is on THIS sum, not on
    // Σgranted alone -- a borrow taken earlier may outlive a recompute.
    std::uint64_t TotalGrantedWithBorrow() const noexcept;
    FFFPrefetch::PressureTier Tier() const noexcept;

    // Rollback switch state: FFF3FP_MEMARB=0 (or the FFF_MEMBUDGET=0 alias) makes
    // every consumer fall back to its own static table. Consumers must ask this
    // before treating Granted() as authoritative, because a disabled arbiter
    // deliberately reports zero grants.
    bool Active() const noexcept;

    // Σfloor exceeded the total budget: the session must degrade (video 3->2
    // frames, then decode 8->4 threads) and re-Claim, or refuse to open.
    bool Overcommitted() const noexcept;

    // Count of times ΣGranted > TotalBudget was observed after a distribution. A
    // non-zero value is a defect, not a policy: it is counted (never clamped
    // silently) so a trace can prove the invariant held.
    std::uint32_t InvariantViolations() const noexcept;

    // Grow-gate suppression accounting, called by PlayerSession when its dead-zone
    // / 2 s interval gate rejects a budget raise. Lock-free by design: it is hit
    // on the playback path, and a lost relaxed RMW would only under-count a debug
    // figure by one -- it never corrupts anything. Read back in the detach
    // summary. Name and signature are the contract the session side relies on.
    void NoteReplanSuppressed() noexcept;

private:
    MemoryBudgetArbiter() noexcept = default;
    ~MemoryBudgetArbiter() noexcept = default;
    MemoryBudgetArbiter(const MemoryBudgetArbiter&) = delete;
    MemoryBudgetArbiter& operator=(const MemoryBudgetArbiter&) = delete;

    // Caller must hold mutex_. Resamples memory, distributes the budget and
    // returns the changed-consumer bitmask.
    std::uint32_t RecomputeLocked() noexcept;
    std::uint64_t DistributeLocked() noexcept;
    std::uint64_t BorrowedLocked() const noexcept;
    std::uint64_t DeadZoneFor(std::uint32_t index) const noexcept;

    mutable std::mutex mutex_;
    SessionBudgetContext context_{};
    MemClaim claims_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
    std::uint64_t granted_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
    std::uint64_t borrowed_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
    std::uint64_t previousGranted_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
    std::uint64_t totalBudget_ = 0;
    bool attached_ = false;
    bool overcommitted_ = false;
    std::uint32_t violations_ = 0;
    // How many times the budget was resampled/redistributed (≈ the 250 ms
    // sample tick), and how many of those actually moved a consumer's grant
    // past its dead zone. Kept apart so the summary line cannot look busy just
    // because the pump ticks.
    std::uint32_t budgetSamples_ = 0;
    std::uint32_t limitChanges_ = 0;
    // Σgranted + Σborrowed briefly exceeded the new budget after a recompute.
    std::uint32_t borrowOverruns_ = 0;
    // Flap accounting: a total-budget up-step arms the wall clock; a down-step
    // inside the flap window counts one oscillation ("flap ring"). Read on the
    // existing recompute path only -- no thread, no timer.
    std::uint32_t flapCount_ = 0;
    std::uint64_t lastRaiseTickMs_ = 0;
    bool pendingRaise_ = false;
    // Written by the playback thread via NoteReplanSuppressed(), read at detach.
    // Atomic so it needs no arbiter mutex on the hot path.
    std::atomic<std::uint32_t> suppressCount_{0};
};

} // namespace FFFPlayer

static_assert(std::is_enum<FFFPlayer::MemConsumer>::value,
    "MemConsumer must stay a scoped enum");
static_assert(sizeof(FFFPlayer::MemConsumer) == 1,
    "MemConsumer is packed into the changed bitmask; widening it changes Evaluate()");
static_assert(sizeof(FFFPlayer::MemClaim) == 32,
    "MemClaim layout is quoted by the scheme (3 x uint64 + padding); check before changing");
static_assert(sizeof(FFFPlayer::SessionBudgetContext) == 40,
    "SessionBudgetContext layout is quoted by the scheme (4 x uint64 + 2 x uint32)");
static_assert(static_cast<std::uint8_t>(FFFPlayer::MemConsumer::Count) == 5,
    "Evaluate() returns a 32-bit mask with one bit per consumer");
