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

// Time->bytes rule (kept here so nobody re-invents the truncated version):
// seconds x rate / 1e7, multiply FIRST then divide. Dividing first makes the
// per-100ns quantity zero for every real format -- the bug this file used to
// carry. The shared form lives in PlayerSession::AudioBytesPerSecond.

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

// Sample the machine, run the raw formula, then apply the installed one-directional
// test cap. Every ledger-side budget assignment goes through this helper. The
// precheck and any probe do NOT need this helper: they call ComputeTotalBudget,
// which applies the same installed cap on their side.
std::uint64_t ComputeBudgetNow() noexcept {
    const std::uint64_t computed = MemBudgetMath::ComputeTotalBudgetRaw(SampleMemory());
    const std::uint64_t cap = MemBudgetMath::TestCapBytesSlot();
    const std::uint64_t budget = MemBudgetMath::ApplyTestCapBytes(computed, cap);
    if (budget != computed &&
            GetEnvironmentVariableW(L"FFF_MEMARB_TRACE", nullptr, 0) != 0) {
        std::fprintf(stderr, "[arbiter] cap-test computed=%llumib cap=%llumib\n",
            static_cast<unsigned long long>(computed / MemBudgetMath::MiB),
            static_cast<unsigned long long>(cap / MemBudgetMath::MiB));
    }
    return budget;
}

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
    return overcommitted_;
}

std::uint32_t MemoryBudgetArbiter::InvariantViolations() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return violations_;
}

void MemoryBudgetArbiter::NoteReplanSuppressed() noexcept {
    // Relaxed RMW on the playback thread; the detach summary reads it later.
    suppressCount_.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t MemoryBudgetArbiter::DeadZoneFor(std::uint32_t index) const noexcept {
    return MemBudgetMath::DeadZoneBytes(claims_[index].floorBytes,
        claims_[index].ceilingBytes);
}

void MemoryBudgetArbiter::AttachSession(const SessionBudgetContext& context) noexcept {
    if (!Active()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    context_ = context;
    attached_ = true;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) {
        granted_[i] = 0;
        previousGranted_[i] = 0;
        borrowed_[i] = 0;
        claims_[i] = MemClaim{};
    }
    budgetSamples_ = 0;
    limitChanges_ = 0;
    borrowOverruns_ = 0;
    overcommitted_ = false;
    flapCount_ = 0;
    pendingRaise_ = false;
    suppressCount_.store(0, std::memory_order_relaxed);
    totalBudget_ = ComputeBudgetNow();
    std::fprintf(stderr, "[arbiter] attach budget=%llu MiB\n",
        static_cast<unsigned long long>(totalBudget_ / MemBudgetMath::MiB));
}

void MemoryBudgetArbiter::DetachSession() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    // Sum inline: TotalGranted() takes the same mutex and would self-deadlock.
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) sum += granted_[i];
    // End-of-session summary. Chosen over a 2 s timer: detach already owns the
    // "how did this session end" line, so it costs zero extra events and no
    // thread. samples = budget resamples (≈ the 250 ms tick); limitChanges = how
    // many consumer grants actually moved past their dead zone; suppress counts
    // the grow-gate rejections PlayerSession reported via NoteReplanSuppressed();
    // flap counts up->down oscillation rings; borrowOverrun counts Σgranted +
    // Σborrowed exceeding the budget after a recompute.
    std::fprintf(stderr,
        "[arbiter] budget total=%llumib granted=%llumib samples=%u limitChanges=%u "
        "suppress=%u flap=%u borrowOverrun=%u\n",
        static_cast<unsigned long long>(totalBudget_ / MemBudgetMath::MiB),
        static_cast<unsigned long long>(sum / MemBudgetMath::MiB),
        static_cast<unsigned>(budgetSamples_),
        static_cast<unsigned>(limitChanges_),
        static_cast<unsigned>(suppressCount_.load(std::memory_order_relaxed)),
        static_cast<unsigned>(flapCount_),
        static_cast<unsigned>(borrowOverruns_));
    attached_ = false;
    flapCount_ = 0;
    pendingRaise_ = false;
    budgetSamples_ = 0;
    limitChanges_ = 0;
    borrowOverruns_ = 0;
    suppressCount_.store(0, std::memory_order_relaxed);
    totalBudget_ = 0;
    overcommitted_ = false;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) {
        granted_[i] = 0;
        previousGranted_[i] = 0;
        borrowed_[i] = 0;
    }
}

std::uint64_t MemoryBudgetArbiter::Claim(MemConsumer who, const MemClaim& claim) noexcept {
    if (!Active() || who >= MemConsumer::Count) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    // Replacement, never accumulation: a resolution change re-claims the same
    // consumer and must not stack its old floor on top of the new one.
    claims_[Index(who)] = claim;
    if (!attached_) {
        attached_ = true;
        totalBudget_ = ComputeBudgetNow();
    }
    DistributeLocked();
    return granted_[Index(who)];
}

void MemoryBudgetArbiter::Release(MemConsumer who, std::uint64_t bytes) noexcept {
    if (who >= MemConsumer::Count) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto index = Index(who);
    const std::uint64_t held = granted_[index] + borrowed_[index];
    const std::uint64_t release = bytes < held ? bytes : held;
    if (borrowed_[index] >= release) {
        borrowed_[index] -= release;
    } else {
        const std::uint64_t fromGranted = release - borrowed_[index];
        borrowed_[index] = 0;
        granted_[index] = granted_[index] > fromGranted ? granted_[index] - fromGranted : 0;
        // A release below the floor means the consumer shrank by itself; keep the
        // ledger honest and let the next Evaluate restore the floor.
        previousGranted_[index] = granted_[index];
    }
}

bool MemoryBudgetArbiter::TryBorrow(MemConsumer who, std::uint64_t extraBytes) noexcept {
    if (!Active() || who >= MemConsumer::Count || extraBytes == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!attached_ || overcommitted_) return false;
    // Summed inline; TotalGranted() would re-enter this mutex.
    std::uint64_t grantedSum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) grantedSum += granted_[i];
    const std::uint64_t used = grantedSum + BorrowedLocked();
    const std::uint64_t headroom = totalBudget_ > used ? totalBudget_ - used : 0;
    if (extraBytes > headroom) return false;
    borrowed_[Index(who)] += extraBytes;
    return true;
}

void MemoryBudgetArbiter::ReturnExtra(MemConsumer who, std::uint64_t bytes) noexcept {
    if (who >= MemConsumer::Count) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto index = Index(who);
    borrowed_[index] = borrowed_[index] > bytes ? borrowed_[index] - bytes : 0;
}

std::uint64_t MemoryBudgetArbiter::Granted(MemConsumer who) const noexcept {
    if (!Active() || who >= MemConsumer::Count) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!attached_) return 0;
    return granted_[Index(who)];
}

std::uint64_t MemoryBudgetArbiter::TotalBudget() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalBudget_;
}

std::uint64_t MemoryBudgetArbiter::TotalGranted() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) sum += granted_[i];
    return sum;
}

std::uint64_t MemoryBudgetArbiter::TotalGrantedWithBorrow() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i)
        sum += granted_[i] + borrowed_[i];
    return sum;
}

FFFPrefetch::PressureTier MemoryBudgetArbiter::Tier() const noexcept {
    return FFFPrefetch::MemoryGuard::Instance().Tier();
}

std::uint32_t MemoryBudgetArbiter::Evaluate() noexcept {
    if (!Active()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!attached_) return 0;
    // The guard's tier is read, not driven: a tier change reaches us through the
    // prefetch pump, which calls Evaluate right after observing it.
    return RecomputeLocked();
}

std::uint32_t MemoryBudgetArbiter::RecomputeLocked() noexcept {
    const std::uint64_t previousBudget = totalBudget_;
    totalBudget_ = ComputeBudgetNow();
    // Flap ring detection on the wall clock, only on this path: an up-step arms
    // the clock, a down-step inside kFlapWindowMs counts one oscillation. The
    // clock read rides the existing event, so no thread and no timer is added.
    const std::uint64_t nowMs = GetTickCount64();
    if (totalBudget_ > previousBudget) {
        pendingRaise_ = true;
        lastRaiseTickMs_ = nowMs;
    } else if (totalBudget_ < previousBudget && pendingRaise_) {
        if (nowMs - lastRaiseTickMs_ <= kFlapWindowMs) ++flapCount_;
        pendingRaise_ = false;   // consumed (counted) or expired: either way disarm
    } else if (pendingRaise_ && nowMs - lastRaiseTickMs_ > kFlapWindowMs) {
        pendingRaise_ = false;
    }
    DistributeLocked();
    std::uint32_t mask = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) {
        const std::uint64_t delta = granted_[i] > previousGranted_[i]
            ? granted_[i] - previousGranted_[i] : previousGranted_[i] - granted_[i];
        if (delta > DeadZoneFor(i)) mask |= (1u << i);
        previousGranted_[i] = granted_[i];
    }
    // samples: every resample counts (the pump ticks ~4/s regardless of whether
    // anything moved); limitChanges: only consumers whose grant actually moved
    // past their dead zone. Previously both were one counter named "recompute",
    // which read as "we reshaped the limit 3.5 times a second" when the truth
    // was "we sampled 4 times a second and almost never moved a limit".
    ++budgetSamples_;
    limitChanges_ += static_cast<std::uint32_t>(std::bitset<32>(mask).count());
    return mask;
}

std::uint64_t MemoryBudgetArbiter::DistributeLocked() noexcept {
    const auto count = Index(MemConsumer::Count);
    MemBudgetMath::FloorSet floors{};
    floors.audioBytes = claims_[Index(MemConsumer::AudioPcmQueue)].floorBytes;
    floors.videoBytes = claims_[Index(MemConsumer::VideoFrameQueue)].floorBytes;
    floors.decodeBytes = claims_[Index(MemConsumer::DecodeSurfaces)].floorBytes;
    floors.renderBytes = claims_[Index(MemConsumer::RenderSlot)].floorBytes;
    floors.prefetchBytes = claims_[Index(MemConsumer::PrefetchCache)].floorBytes;
    const std::uint64_t floorSum = MemBudgetMath::SumFloors(floors);
    overcommitted_ = floorSum > totalBudget_;

    for (std::uint32_t i = 0; i < count; ++i) granted_[i] = 0;
    if (floorSum == 0) {
        // Nothing registered yet: hand everything to nobody. Consumers that
        // claim later re-trigger a distribution through Claim().
        return 0;
    }

    if (overcommitted_) {
        // Proportional shrink, never below zero and never more than the budget.
        // The session is expected to degrade and re-claim; until it does, this
        // keeps ΣGranted ≤ TotalBudget true instead of handing out phantom bytes.
        for (std::uint32_t i = 0; i < count; ++i) {
            granted_[i] = claims_[i].floorBytes * totalBudget_ / floorSum;
        }
    } else {
        std::uint64_t remaining = totalBudget_ - floorSum;
        for (std::uint32_t i = 0; i < count; ++i) granted_[i] = claims_[i].floorBytes;
        // Raise in the fixed order: audio, video, the never-yielding pair, prefetch.
        const auto raise = [&](MemConsumer who) {
            const auto index = Index(who);
            const std::uint64_t target = (std::min)(claims_[index].targetBytes,
                claims_[index].ceilingBytes);
            if (target <= granted_[index] || remaining == 0) return;
            const std::uint64_t want = target - granted_[index];
            const std::uint64_t give = want < remaining ? want : remaining;
            granted_[index] += give;
            remaining -= give;
        };
        raise(MemConsumer::AudioPcmQueue);
        raise(MemConsumer::VideoFrameQueue);
        raise(MemConsumer::DecodeSurfaces);
        raise(MemConsumer::RenderSlot);
        // Prefetch gets the leftovers only, capped by its own ceiling.
        {
            const auto index = Index(MemConsumer::PrefetchCache);
            const std::uint64_t cap = claims_[index].ceilingBytes;
            if (remaining != 0 && cap > granted_[index]) {
                const std::uint64_t give = (std::min)(remaining, cap - granted_[index]);
                granted_[index] += give;
                remaining -= give;
            }
        }
    }

    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < count; ++i) sum += granted_[i];
    if (sum > totalBudget_) {
        // Should be unreachable; counted rather than clamped so a trace shows it.
        ++violations_;
        std::fprintf(stderr, "[arbiter] INVARIANT VIOLATED sum=%llu budget=%llu\n",
            static_cast<unsigned long long>(sum),
            static_cast<unsigned long long>(totalBudget_));
    }
    // The REAL invariant is Σgranted + Σborrowed ≤ totalBudget_. The borrow side
    // is NOT redistributed here: TryBorrow's headroom check counted borrowed_ at
    // borrow time, but a recompute can shrink totalBudget_ afterwards, so the
    // old borrow can briefly push the sum over the new budget (about one device
    // buffer) until it expires. Counted, never clamped -- borrowOverrun>0 after a
    // long session would prove the window exists, 0 proves it does not.
    const std::uint64_t borrowedSum = BorrowedLocked();
    if (sum + borrowedSum > totalBudget_) {
        ++borrowOverruns_;
        if (GetEnvironmentVariableW(L"FFF_MEMARB_TRACE", nullptr, 0) != 0) {
            std::fprintf(stderr,
                "[arbiter] invariant borrowed-overrun total=%llu granted=%llu borrowed=%llu\n",
                static_cast<unsigned long long>(totalBudget_),
                static_cast<unsigned long long>(sum),
                static_cast<unsigned long long>(borrowedSum));
        }
    }
    return sum;
}

std::uint64_t MemoryBudgetArbiter::BorrowedLocked() const noexcept {
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < Index(MemConsumer::Count); ++i) sum += borrowed_[i];
    return sum;
}

} // namespace FFFPlayer
