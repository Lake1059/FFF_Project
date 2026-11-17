#pragma once
#include <algorithm>
#include <cstdint>
#include <initializer_list>

// Pure arithmetic of the memory budget: no Windows headers, no locks, no state.
// Split out of MemoryBudgetArbiter.cpp so the numbers the scheme quotes can be
// re-derived and unit-tested offline (see FFF.Native.Tests/membudget_math_test.cpp).
// Every value here is bytes; every ratio is an integer division of bytes.
namespace FFFPlayer::MemBudgetMath {

inline constexpr std::uint64_t MiB = 1024ull * 1024ull;
inline constexpr std::uint64_t GiB = 1024ull * MiB;

// Clamp the pool so a starved box still gets a usable minimum and a workstation
// cannot turn the pool into a second cache. The upper clamp was raised from
// 4 GiB to 8 GiB on 2026-11-17: on every box past ~54 GiB of RAM the old 4 GiB
// ceiling -- not the anti-thrashing gates -- was the binding term, so the pool
// never grew even when available physical and commit headroom were plentiful.
inline constexpr std::uint64_t kTotalBudgetMinBytes = 8 * MiB;
inline constexpr std::uint64_t kTotalBudgetMaxBytes = 8 * GiB;

// Physical-share tier (2026-11-17 relaxation). The share of total RAM the pool
// may claim is deliberately NOT one global number:
//   * boxes with < kRamTierThresholdBytes of RAM keep spending 1/10, exactly as
//     before -- a 4 GiB laptop stays at ~409 MiB and nothing about its budget
//     moves;
//   * boxes with >= kRamTierThresholdBytes may spend 15/100, because on those
//     machines the old 1/10 was the binding term even when the system had headroom.
// Widening the small-box share too would grow the 4 GiB box from ~409 to ~614
// MiB (+50%): the "small box dragged down" case the relaxation exists to avoid.
inline constexpr std::uint64_t kRamTierThresholdBytes = 8 * GiB;
inline constexpr std::uint64_t kPhysicalShareDenominatorSmall = 10;   // < tier: old /10
inline constexpr std::uint64_t kPhysicalSharePercentLarge = 15;       // >= tier: /100

struct MemorySample final {
    std::uint64_t physicalBytes = 0;        // GlobalMemoryStatusEx ullTotalPhys
    std::uint64_t availablePhysBytes = 0;   // GlobalMemoryStatusEx ullAvailPhys
    std::uint64_t commitAvailableBytes = 0; // commit limit - committed total
};

// Shared clamp used by every formula variant below. Kept in one place so the
// what-if functions cannot drift from the production clamp rules.
constexpr std::uint64_t ClampBudget(std::uint64_t byAvailable,
                                    std::uint64_t byCommit,
                                    std::uint64_t byPhysical,
                                    std::uint64_t maxBytes) noexcept {
    std::uint64_t budget = (std::min)(byAvailable, (std::min)(byCommit, byPhysical));
    if (budget < kTotalBudgetMinBytes) budget = kTotalBudgetMinBytes;
    if (budget > maxBytes) budget = maxBytes;
    return budget;
}

// ── TEST-ONLY budget ceiling (FFF3FP_MEMARB_CAP_MB) ──────────────────────────
// The arbiter .cpp reads the environment variable ONCE (at process startup,
// before any session opens) and installs the parsed MiB count into the slot
// below. The override is deliberately one-directional: it may only SHRINK the
// computed pool, never raise it above what the two anti-thrashing gates allow --
// a two-way override would be a back-door around them. Its sole purpose is to
// jiggle the budget on demand when measuring recompute oscillation; it is for
// testing only and is not advertised in release builds.
//
// The slot lives here, not in the .cpp, so that EVERY public budget read -- the
// ledger, the open-time Σfloor precheck, any probe -- observes the same squeeze
// without each caller remembering to wrap its own call. Default 0 = no cap,
// which keeps offline unit tests (which link only this header) unaffected.
constexpr std::uint64_t ApplyTestCapBytes(std::uint64_t computedBytes,
                                          std::uint64_t capBytes) noexcept {
    if (capBytes == 0 || capBytes >= computedBytes) return computedBytes;
    return capBytes;
}

// Parse the raw environment string: a positive whole number of MiB. nullptr,
// empty, zero, negative, non-numeric input or trailing junk all mean "not set"
// (0), so a typo can never silently squeeze a session. A cap above 16 GiB is
// likewise treated as unset: it could never bind against an 8 GiB pool anyway.
constexpr std::uint64_t ParseTestCapBytes(const wchar_t* text) noexcept {
    if (text == nullptr) return 0;
    std::uint64_t value = 0;
    const wchar_t* p = text;
    while (*p >= L'0' && *p <= L'9') {
        if (value > 16384ull) return 0;   // overflow guard: no 20-digit inputs
        value = value * 10 + static_cast<std::uint64_t>(*p - L'0');
        ++p;
    }
    if (value == 0 || value > 16384ull || *p != 0) return 0;
    return value * MiB;
}

// Process-lifetime test-cap slot. Written exactly once at process startup
// (InstallTestCapBytes, from the arbiter .cpp); read-only afterwards. A
// function-local Meyers static so no static-init order can bite.
inline std::uint64_t& TestCapBytesSlot() noexcept {
    static std::uint64_t slot = 0;
    return slot;
}

inline void InstallTestCapBytes(std::uint64_t capBytes) noexcept {
    TestCapBytesSlot() = capBytes;
}

// TotalBudget, RAW formula (no test cap): clamp(min(available*1/4,
// commitAvail*1/2, physical*share), 8 MiB, 8 GiB).
// The first two factors are the anti-thrashing gates and never move: the first
// keeps 75% of free RAM for the system and other processes, the second keeps us
// under the commit ceiling. The third caps us at a share of the machine, and the
// share is tiered by machine size (kRamTierThresholdBytes).
constexpr std::uint64_t ComputeTotalBudgetRaw(const MemorySample& sample) noexcept {
    const std::uint64_t byAvailable = sample.availablePhysBytes / 4;
    const std::uint64_t byCommit = sample.commitAvailableBytes / 2;
    const std::uint64_t byPhysical = sample.physicalBytes >= kRamTierThresholdBytes
        ? sample.physicalBytes * kPhysicalSharePercentLarge / 100
        : sample.physicalBytes / kPhysicalShareDenominatorSmall;
    return ClampBudget(byAvailable, byCommit, byPhysical, kTotalBudgetMaxBytes);
}

// The ONLY public total-budget entry: raw formula, then the one-directional test
// cap. Every caller (ledger, precheck, probe) goes through this, so a test cap
// squeezes every observation at once. Not constexpr: the installed slot is
// runtime state by design.
inline std::uint64_t ComputeTotalBudget(const MemorySample& sample) noexcept {
    return ApplyTestCapBytes(ComputeTotalBudgetRaw(sample), TestCapBytesSlot());
}

// ── What-if variants, offline table harness only ─────────────────────────────
// Production always calls ComputeTotalBudget. These two exist so the comparison
// table in the 2026-11-17 report is generated by executing code instead of by
// hand:
//   * ComputeTotalBudgetLegacy() is the formula BEFORE the relaxation: flat 1/10
//     physical share, 4 GiB upper clamp.
//   * ComputeTotalBudgetGlobalFifteen() is the thought experiment "what if 15%
//     were applied to every box, no tier" -- with the new 8 GiB clamp, so the
//     only difference from the tiered formula is the missing threshold.
constexpr std::uint64_t ComputeTotalBudgetLegacy(const MemorySample& sample) noexcept {
    return ClampBudget(sample.availablePhysBytes / 4,
                       sample.commitAvailableBytes / 2,
                       sample.physicalBytes / 10,
                       4 * GiB);
}

constexpr std::uint64_t ComputeTotalBudgetGlobalFifteen(const MemorySample& sample) noexcept {
    return ClampBudget(sample.availablePhysBytes / 4,
                       sample.commitAvailableBytes / 2,
                       sample.physicalBytes * 15 / 100,
                       kTotalBudgetMaxBytes);
}

// Dead zone: a grant change smaller than this must not trigger a recompute.
// Two consumers pull in opposite directions here -- the prefetch cache has no
// floor (so floor/8 collapses to zero and ceiling/16 carries it), the audio queue
// has ceiling == 2*floor (so the two terms are equal) -- hence max() rather than
// any single fraction.
constexpr std::uint64_t DeadZoneBytes(std::uint64_t floorBytes,
                                      std::uint64_t ceilingBytes) noexcept {
    const std::uint64_t byFloor = floorBytes / 8;
    const std::uint64_t byCeiling = ceilingBytes / 16;
    return byFloor > byCeiling ? byFloor : byCeiling;
}

// ── Σfloor over-commit ladder ─────────────────────────────────────────────────
// When the floors of all five consumers exceed the total budget, the session
// degrades in a fixed order and never by cutting audio or the render slot:
//   1. video floor from 3 frames to 2 frames (the hard lower bound)
//   2. decode surfaces from the thread ladder's 8 to 4
//   3. still over budget -> refuse to open (the caller must pick hardware decode)
// A 4 GiB machine opening 8K 10-bit with software decode lands on step 3, which
// is the expected verdict, not a bug: 2 frames + 4 threads still does not fit.
enum class DegradeStep : std::uint8_t {
    None = 0,
    VideoTwoFrames = 1,
    DecodeFourThreads = 2,
    Reject = 3,
};

struct FloorSet final {
    std::uint64_t audioBytes = 0;
    std::uint64_t videoBytes = 0;   // 3 x bytesPerFrame at entry
    std::uint64_t decodeBytes = 0;  // 8 x bytesPerFrame at entry
    std::uint64_t renderBytes = 0;
    std::uint64_t prefetchBytes = 0;
};

constexpr std::uint64_t SumFloors(const FloorSet& floors) noexcept {
    return floors.audioBytes + floors.videoBytes + floors.decodeBytes +
        floors.renderBytes + floors.prefetchBytes;
}

struct DegradeResult final {
    DegradeStep step = DegradeStep::None;
    FloorSet floors{};        // the floors actually used at this step
    std::uint64_t sumBytes = 0;
};

constexpr DegradeResult ResolveFloors(std::uint64_t totalBudgetBytes,
                                      const FloorSet& floors) noexcept {
    DegradeResult result{};
    FloorSet candidate = floors;
    result.step = DegradeStep::None;
    result.sumBytes = SumFloors(candidate);
    if (result.sumBytes <= totalBudgetBytes) {
        result.floors = candidate;
        return result;
    }
    // Step 1: 3 frames -> 2 frames. Integer math keeps the 2/3 relation exact
    // enough that both sides of the scheme's example table agree.
    candidate.videoBytes = floors.videoBytes * 2 / 3;
    result.step = DegradeStep::VideoTwoFrames;
    result.sumBytes = SumFloors(candidate);
    if (result.sumBytes <= totalBudgetBytes) {
        result.floors = candidate;
        return result;
    }
    // Step 2: decode surfaces 8 -> 4 threads. Only the decode floor moves; audio
    // and the render slot are never trimmed (underrun and Present failure are
    // worse than a shallower decode pipeline).
    candidate.decodeBytes = floors.decodeBytes / 2;
    result.step = DegradeStep::DecodeFourThreads;
    result.sumBytes = SumFloors(candidate);
    if (result.sumBytes <= totalBudgetBytes) {
        result.floors = candidate;
        return result;
    }
    result.step = DegradeStep::Reject;
    result.floors = candidate;
    return result;
}

// ── Grant distribution ───────────────────────────────────────────────────────
// Needing order for the slack above the floors. Prefetch is last because it is
// the only consumer that can go to zero without a visible defect; audio is first
// because every millisecond it is short is an audible hole.
inline constexpr int kRaiseOrderAudio = 0;
inline constexpr int kRaiseOrderVideo = 1;
inline constexpr int kRaiseOrderFixed = 2;   // decode surfaces + render slot
inline constexpr int kRaiseOrderPrefetch = 3;

} // namespace FFFPlayer::MemBudgetMath

// Pin the numbers the relaxation decision quotes: the tier boundary is exactly
// 8 GiB (the tests feed 8 GiB and 7.9 GiB into it), the upper clamp is 8 GiB,
// and the small-box denominator is still the old /10.
static_assert(FFFPlayer::MemBudgetMath::kRamTierThresholdBytes == 8 * FFFPlayer::MemBudgetMath::GiB,
    "tier boundary must stay exactly 8 GiB");
static_assert(FFFPlayer::MemBudgetMath::kTotalBudgetMaxBytes == 8 * FFFPlayer::MemBudgetMath::GiB,
    "upper clamp must stay 8 GiB");
static_assert(FFFPlayer::MemBudgetMath::kPhysicalShareDenominatorSmall == 10,
    "the small-box share must stay the old 1/10");
static_assert(FFFPlayer::MemBudgetMath::kPhysicalSharePercentLarge == 15,
    "the large-box share must stay 15/100");
