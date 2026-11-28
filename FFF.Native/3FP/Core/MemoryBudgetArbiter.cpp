#include "pch.h"
#include "3FP/Core/MemoryBudgetArbiter.h"

#include "3FP/Core/MemoryBudgetMath.h"
#include "3FP/Prefetch/MemoryGuard.h"

#include <windows.h>

#include <bitset>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace FFFPlayer {
namespace {

// Rollback switch. FFF3FP_MEMARB=0 turns the whole ledger off; FFF_MEMBUDGET is
// accepted as a compatibility alias so an older batch file keeps working. Either
// one set explicitly wins; both unset means the feature is on.
bool ArbiterEnabled() noexcept {
    static const bool enabled = [] {
        wchar_t primary[8] = {};
        wchar_t alias[8] = {};
        const bool hasPrimary = GetEnvironmentVariableW(L"FFF3FP_MEMARB", primary,
            static_cast<DWORD>(sizeof(primary) / sizeof(primary[0]))) != 0;
        const bool hasAlias = GetEnvironmentVariableW(L"FFF_MEMBUDGET", alias,
            static_cast<DWORD>(sizeof(alias) / sizeof(alias[0]))) != 0;
        if (hasPrimary) return primary[0] != L'0';
        if (hasAlias) return alias[0] != L'0';
        return true;
    }();
    return enabled;
}

// One snapshot, three numbers. Commit limit/available come from the same
// MEMORYSTATUSEX call rather than GetPerformanceInfo: identical source the guard
// already reads, no extra PSAPI import, and ullTotalPageFile/ullAvailPageFile are
// exactly "commit limit" and "commit available" on Win10+.
MemBudgetMath::MemorySample SampleMemory() noexcept {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    MemBudgetMath::MemorySample sample{};
    if (!GlobalMemoryStatusEx(&status)) return sample;
    sample.physicalBytes = static_cast<std::uint64_t>(status.ullTotalPhys);
    sample.availablePhysBytes = static_cast<std::uint64_t>(status.ullAvailPhys);
    // Guard against the page-file-less case: available commit can briefly exceed
    // the limit when the system is trimming, and a negative would wrap.
    const std::uint64_t limit = static_cast<std::uint64_t>(status.ullTotalPageFile);
    const std::uint64_t available = static_cast<std::uint64_t>(status.ullAvailPageFile);
    sample.commitAvailableBytes = available < limit ? available : limit;
    return sample;
}

std::uint32_t Index(MemConsumer who) noexcept {
    return static_cast<std::uint32_t>(who);
}

// Flap window: an up-step of the total budget followed by a down-step within
// this many milliseconds counts as one oscillation ring. Chosen from the 2 s
// grow-gate in PlayerSession (a raise cannot even be accepted faster than that)
// plus the sampling jitter seen on box; 4 s is two gate intervals.
constexpr std::uint64_t kFlapWindowMs = 4000;

// TEST-ONLY override. FFF3FP_MEMARB_CAP_MB squeezes the computed budget down to a
// ceiling in MiB. Installed ONCE at process startup (below), long before any
// session opens -- including the DoOpen Σfloor precheck -- so that every public
// budget read sees the same squeeze. One-directional by contract (see
// ApplyTestCapBytes): it may only shrink the pool, never raise it past the two
// anti-thrashing gates. For testing only, not advertised in release builds.
struct TestCapInstaller {
    TestCapInstaller() {
        wchar_t buf[16] = {};
        std::uint64_t parsed = 0;
        if (GetEnvironmentVariableW(L"FFF3FP_MEMARB_CAP_MB", buf,
                static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]))) != 0) {
            parsed = MemBudgetMath::ParseTestCapBytes(buf);
        }
        MemBudgetMath::InstallTestCapBytes(parsed);
    }
};
const TestCapInstaller g_testCapInstaller;

std::uint64_t ComputeBudgetNow() noexcept {
    const std::uint64_t computed = MemBudgetMath::ComputeTotalBudgetRaw(SampleMemory());
    const std::uint64_t cap = MemBudgetMath::TestCapBytesSlot();
    return MemBudgetMath::ApplyTestCapBytes(computed, cap);
}

// Attach/Granted/Claim are called on the session worker. A thread-local token
// lets the frozen five-consumer API stay unchanged while each live session
// claims against its own ledger; the process total is still one number.
thread_local std::uint64_t CurrentSessionToken = 0;

} // namespace

MemoryBudgetArbiter& MemoryBudgetArbiter::Instance() noexcept {
    static MemoryBudgetArbiter instance;
    return instance;
}

bool MemoryBudgetArbiter::Active() const noexcept {
    return ArbiterEnabled();
}

bool MemoryBudgetArbiter::Overcommitted() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : sessions_) {
        if (session.token != 0 && session.overcommitted) return true;
    }
    return false;
}

std::uint32_t MemoryBudgetArbiter::InvariantViolations() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t sum = 0;
    for (const auto& session : sessions_) sum += session.violations;
    return sum;
}

void MemoryBudgetArbiter::NoteReplanSuppressed() noexcept {
    suppressCount_.fetch_add(1, std::memory_order_relaxed);
}

MemoryBudgetArbiter::SessionState* MemoryBudgetArbiter::FindLocked(
    const std::uint64_t token) noexcept {
    for (auto& session : sessions_) {
        if (session.token == token) return &session;
    }
    return nullptr;
}

const MemoryBudgetArbiter::SessionState* MemoryBudgetArbiter::FindLocked(
    const std::uint64_t token) const noexcept {
    for (const auto& session : sessions_) {
        if (session.token == token) return &session;
    }
    return nullptr;
}

MemoryBudgetArbiter::SessionState* MemoryBudgetArbiter::CurrentLocked() noexcept {
    if (CurrentSessionToken == 0) return nullptr;
    return FindLocked(CurrentSessionToken);
}

const MemoryBudgetArbiter::SessionState* MemoryBudgetArbiter::CurrentLocked() const noexcept {
    if (CurrentSessionToken == 0) return nullptr;
    return FindLocked(CurrentSessionToken);
}

std::uint64_t MemoryBudgetArbiter::DeadZoneFor(const SessionState& session,
    const std::uint32_t index) const noexcept {
    return MemBudgetMath::DeadZoneBytes(session.claims_[index].floorBytes,
        session.claims_[index].ceilingBytes);
}

std::uint64_t MemoryBudgetArbiter::AttachSession(
    const SessionBudgetContext& context) noexcept {
    if (!Active()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionCount_ >= sessions_.size()) return 0;
    auto* slot = static_cast<SessionState*>(nullptr);
    for (auto& session : sessions_) {
        if (session.token == 0) {
            slot = &session;
            break;
        }
    }
    if (slot == nullptr) return 0;
    const auto token = nextToken_.fetch_add(1, std::memory_order_relaxed) + 1;
    slot->token = token;
    slot->context = context;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) {
        slot->claims_[i] = MemClaim{};
        slot->granted_[i] = 0;
        slot->previousGranted_[i] = 0;
        slot->borrowed_[i] = 0;
    }
    slot->budgetSamples = 0;
    slot->limitChanges = 0;
    slot->borrowOverruns = 0;
    slot->violations = 0;
    slot->flapCount = 0;
    slot->pendingRaise = false;
    slot->overcommitted = false;
    slot->suppressCount = 0;
    ++sessionCount_;
    CurrentSessionToken = token;
    RecomputeAllLocked();
    return token;
}

void MemoryBudgetArbiter::DetachSession(const std::uint64_t token) noexcept {
    if (token == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = FindLocked(token);
    if (session == nullptr) return;
    // Swap-with-last keeps the fixed array dense; no pointer escapes this lock.
    const auto index = static_cast<std::size_t>(session - sessions_.data());
    if (index + 1 < sessions_.size()) {
        if (sessions_[sessions_.size() - 1].token != 0) {
            // The removed slot must be reset before the move so its token cannot
            // alias a new attach while the array is being compacted.
            sessions_[index] = std::move(sessions_[sessions_.size() - 1]);
        }
    }
    auto& last = sessions_[sessions_.size() - 1];
    last = SessionState{};
    if (sessions_[index].token == token) {
        // Defensive: if the moved slot still points at the removed token, clear it.
        sessions_[index] = SessionState{};
    }
    if (CurrentSessionToken == token) CurrentSessionToken = 0;
    if (sessionCount_ != 0) --sessionCount_;
    RecomputeAllLocked();
}

std::uint64_t MemoryBudgetArbiter::Claim(const MemConsumer who,
    const MemClaim& claim) noexcept {
    if (!Active() || who >= MemConsumer::Count) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = CurrentLocked();
    if (session == nullptr) return 0;
    session->claims_[Index(who)] = claim;
    DistributeLocked(*session);
    return session->granted_[Index(who)];
}

void MemoryBudgetArbiter::Release(const MemConsumer who, const std::uint64_t bytes) noexcept {
    if (who >= MemConsumer::Count) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = CurrentLocked();
    if (session == nullptr) return;
    const auto index = Index(who);
    const std::uint64_t held = session->granted_[index] + session->borrowed_[index];
    const std::uint64_t release = bytes < held ? bytes : held;
    if (session->borrowed_[index] >= release) {
        session->borrowed_[index] -= release;
    } else {
        const std::uint64_t fromGranted = release - session->borrowed_[index];
        session->borrowed_[index] = 0;
        session->granted_[index] = session->granted_[index] > fromGranted
            ? session->granted_[index] - fromGranted : 0;
        session->previousGranted_[index] = session->granted_[index];
    }
}

bool MemoryBudgetArbiter::TryBorrow(const MemConsumer who,
    const std::uint64_t extraBytes) noexcept {
    if (!Active() || who >= MemConsumer::Count || extraBytes == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = CurrentLocked();
    if (session == nullptr || session->overcommitted) return false;
    std::uint64_t grantedSum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) grantedSum += session->granted_[i];
    const std::uint64_t used = grantedSum + BorrowedLocked(*session);
    const std::uint64_t headroom = session->totalBudget > used
        ? session->totalBudget - used : 0;
    if (extraBytes > headroom) return false;
    session->borrowed_[Index(who)] += extraBytes;
    return true;
}

void MemoryBudgetArbiter::ReturnExtra(const MemConsumer who,
    const std::uint64_t bytes) noexcept {
    if (who >= MemConsumer::Count) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = CurrentLocked();
    if (session == nullptr) return;
    const auto index = Index(who);
    session->borrowed_[index] = session->borrowed_[index] > bytes
        ? session->borrowed_[index] - bytes : 0;
}

std::uint64_t MemoryBudgetArbiter::Granted(const MemConsumer who) const noexcept {
    if (!Active() || who >= MemConsumer::Count) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* session = CurrentLocked();
    if (session == nullptr) return 0;
    return session->granted_[Index(who)];
}

std::uint64_t MemoryBudgetArbiter::TotalBudget() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalBudget_;
}

std::uint32_t MemoryBudgetArbiter::LiveSessions() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionCount_;
}

std::uint64_t MemoryBudgetArbiter::TotalGranted() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& session : sessions_) {
        if (session.token == 0) continue;
        for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) sum += session.granted_[i];
    }
    return sum;
}

std::uint64_t MemoryBudgetArbiter::TotalGrantedWithBorrow() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (const auto& session : sessions_) {
        if (session.token == 0) continue;
        for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i)
            sum += session.granted_[i] + session.borrowed_[i];
    }
    return sum;
}

FFFPrefetch::PressureTier MemoryBudgetArbiter::Tier() const noexcept {
    return FFFPrefetch::MemoryGuard::Instance().Tier();
}

std::uint32_t MemoryBudgetArbiter::Evaluate() noexcept {
    if (!Active()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionCount_ == 0) return 0;
    return RecomputeAllLocked();
}

std::uint32_t MemoryBudgetArbiter::RecomputeAllLocked() noexcept {
    totalBudget_ = ComputeBudgetNow();
    const auto perSession = sessionCount_ == 0 ? 0 : totalBudget_ / sessionCount_;
    std::uint32_t mask = 0;
    const std::uint64_t nowMs = GetTickCount64();
    for (auto& session : sessions_) {
        if (session.token == 0) continue;
        const std::uint64_t previousBudget = session.totalBudget;
        session.totalBudget = perSession;
        if (session.totalBudget > previousBudget) {
            session.pendingRaise = true;
            session.lastRaiseTickMs = nowMs;
        } else if (session.totalBudget < previousBudget && session.pendingRaise) {
            if (nowMs - session.lastRaiseTickMs <= kFlapWindowMs) ++session.flapCount;
            session.pendingRaise = false;
        } else if (session.pendingRaise && nowMs - session.lastRaiseTickMs > kFlapWindowMs) {
            session.pendingRaise = false;
        }
        mask |= DistributeLocked(session);
        ++session.budgetSamples;
    }
    return mask;
}

std::uint32_t MemoryBudgetArbiter::DistributeLocked(SessionState& session) noexcept {
    const auto count = Index(MemConsumer::Count);
    MemBudgetMath::FloorSet floors{};
    floors.audioBytes = session.claims_[Index(MemConsumer::AudioPcmQueue)].floorBytes;
    floors.videoBytes = session.claims_[Index(MemConsumer::VideoFrameQueue)].floorBytes;
    floors.decodeBytes = session.claims_[Index(MemConsumer::DecodeSurfaces)].floorBytes;
    floors.renderBytes = session.claims_[Index(MemConsumer::RenderSlot)].floorBytes;
    floors.prefetchBytes = session.claims_[Index(MemConsumer::PrefetchCache)].floorBytes;
    const std::uint64_t floorSum = MemBudgetMath::SumFloors(floors);
    session.overcommitted = floorSum > session.totalBudget;

    for (std::uint32_t i = 0; i < count; ++i) session.granted_[i] = 0;
    if (floorSum == 0) return 0;

    if (session.overcommitted) {
        // Proportional shrink, never below zero and never more than the budget.
        // The session is expected to degrade and re-claim; until it does, this
        // keeps ΣGranted ≤ TotalBudget true instead of handing out phantom bytes.
        for (std::uint32_t i = 0; i < count; ++i) {
            session.granted_[i] = session.claims_[i].floorBytes * session.totalBudget / floorSum;
        }
    } else {
        std::uint64_t remaining = session.totalBudget - floorSum;
        for (std::uint32_t i = 0; i < count; ++i) session.granted_[i] = session.claims_[i].floorBytes;
        // Raise in the fixed order: audio, video, the never-yielding pair, prefetch.
        const auto raise = [&](MemConsumer who) {
            const auto index = Index(who);
            const std::uint64_t target = (std::min)(session.claims_[index].targetBytes,
                session.claims_[index].ceilingBytes);
            if (target <= session.granted_[index] || remaining == 0) return;
            const std::uint64_t want = target - session.granted_[index];
            const std::uint64_t give = want < remaining ? want : remaining;
            session.granted_[index] += give;
            remaining -= give;
        };
        raise(MemConsumer::AudioPcmQueue);
        raise(MemConsumer::VideoFrameQueue);
        raise(MemConsumer::DecodeSurfaces);
        raise(MemConsumer::RenderSlot);
        // Prefetch gets the leftovers only, capped by its own ceiling.
        {
            const auto index = Index(MemConsumer::PrefetchCache);
            const std::uint64_t cap = session.claims_[index].ceilingBytes;
            if (remaining != 0 && cap > session.granted_[index]) {
                const std::uint64_t give = (std::min)(remaining, cap - session.granted_[index]);
                session.granted_[index] += give;
                remaining -= give;
            }
        }
    }

    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < count; ++i) sum += session.granted_[i];
    if (sum > session.totalBudget) ++session.violations;
    if (sum + BorrowedLocked(session) > session.totalBudget) ++session.borrowOverruns;

    std::uint32_t mask = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t delta = session.granted_[i] > session.previousGranted_[i]
            ? session.granted_[i] - session.previousGranted_[i]
            : session.previousGranted_[i] - session.granted_[i];
        if (delta > DeadZoneFor(session, i)) mask |= (1u << i);
        session.previousGranted_[i] = session.granted_[i];
    }
    session.limitChanges += static_cast<std::uint32_t>(std::bitset<32>(mask).count());
    return mask;
}

std::uint64_t MemoryBudgetArbiter::BorrowedLocked(const SessionState& session) const noexcept {
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) sum += session.borrowed_[i];
    return sum;
}

} // namespace FFFPlayer
