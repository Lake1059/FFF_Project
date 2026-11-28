#pragma once
#include <array>
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

    // Attach a session to the process-wide ledger. Every supported concurrent
    // session gets a non-zero token; the process budget is split equally between
    // live sessions, so a 4K route cannot keep the pair-only budget while eight
    // other sessions are open. Consumers call Claim/Granted from their own session
    // worker; the token is thread-affine and does not leak into another session.
    std::uint64_t AttachSession(const SessionBudgetContext& context) noexcept;
    void DetachSession(std::uint64_t token) noexcept;

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
    // Number of live ledgers. The single-route profile uses this to tell a lone
    // session apart from a nine-route batch; it is a race-free snapshot under the
    // same mutex that owns attach/detach.
    std::uint32_t LiveSessions() const noexcept;
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

    // One ledger per live session. The arrays are deliberately fixed: the route
    // arbiter already caps the session count, and a heap allocation on the
    // attach/detach path would be one more failure mode under memory pressure.
    struct SessionState final {
        std::uint64_t token = 0;
        SessionBudgetContext context{};
        MemClaim claims_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
        std::uint64_t granted_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
        std::uint64_t borrowed_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
        std::uint64_t previousGranted_[static_cast<std::uint32_t>(MemConsumer::Count)]{};
        std::uint64_t totalBudget = 0;
        std::uint32_t budgetSamples = 0;
        std::uint32_t limitChanges = 0;
        std::uint32_t borrowOverruns = 0;
        std::uint32_t violations = 0;
        std::uint32_t flapCount = 0;
        std::uint64_t lastRaiseTickMs = 0;
        bool pendingRaise = false;
        bool overcommitted = false;
        std::uint32_t suppressCount = 0;
    };

    // Caller must hold mutex_. Recompute every session's equal share of the
    // process budget and return the OR of changed-consumer masks.
    std::uint32_t RecomputeAllLocked() noexcept;
    std::uint32_t DistributeLocked(SessionState& session) noexcept;
    std::uint64_t BorrowedLocked(const SessionState& session) const noexcept;
    std::uint64_t DeadZoneFor(const SessionState& session, std::uint32_t index) const noexcept;
    SessionState* FindLocked(std::uint64_t token) noexcept;
    const SessionState* FindLocked(std::uint64_t token) const noexcept;
    SessionState* CurrentLocked() noexcept;
    const SessionState* CurrentLocked() const noexcept;

    mutable std::mutex mutex_;
    std::array<SessionState, 9> sessions_{};
    std::uint32_t sessionCount_ = 0;
    std::uint64_t totalBudget_ = 0;
    std::atomic<std::uint64_t> nextToken_{0};
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
