#pragma once

// Prefetch control surface. Appended additively: this header is NOT included
// from FFF.Player.Api.h and adds no FFF3FPSnapshot field, so PlayerApiVersion
// (20), SnapshotVersion (23) and the 608-byte snapshot layout stay unchanged.
// Hosts resolve both exports dynamically and degrade to today's behaviour when
// they are absent (same convention as the audio tap entries).

#include "3FP/Api/FFF.Player.Api.h"

#include <cstdint>

#define FFF3FP_PREFETCH_PLAN_VERSION 1u
#define FFF3FP_PREFETCH_STATUS_VERSION 1u

// Plan flags describe the browsing state at the moment the plan was built.
#define FFF3FP_PREFETCH_FLAG_STILL_BROWSING 0x1u
#define FFF3FP_PREFETCH_FLAG_PLAYING        0x2u

struct FFF3FPPrefetchPlan {
    std::uint32_t size;          // sizeof(FFF3FPPrefetchPlan)
    std::uint32_t version;       // FFF3FP_PREFETCH_PLAN_VERSION
    std::uint32_t generation;    // caller-owned; a larger value cancels older work
    std::uint32_t flags;         // FFF3FP_PREFETCH_FLAG_*
    // UTF-8, '\n'-separated paths. The first entry is the currently opened
    // media; the rest are its neighbours, nearest first. A path cannot contain
    // a newline on Windows, and a single pointer keeps the host P/Invoke simple.
    const char* pathsUtf8;
    std::uint32_t reserved[4];
};

enum FFF3FPPrefetchTier : std::uint32_t {
    FFF3FP_PREFETCH_TIER_NORMAL = 0u,
    FFF3FP_PREFETCH_TIER_TIGHT = 1u,
    FFF3FP_PREFETCH_TIER_CRITICAL = 2u,
};

struct FFF3FPPrefetchStatus {
    std::uint32_t size;          // sizeof(FFF3FPPrefetchStatus)
    std::uint32_t version;       // FFF3FP_PREFETCH_STATUS_VERSION
    std::uint32_t enabled;       // 0 = one-key fallback / unsupported
    std::uint32_t tier;          // FFF3FPPrefetchTier
    std::uint32_t hostRunning;   // helper process alive
    std::uint32_t workerRunning; // in-process decode line alive
    std::uint32_t cachedStillFrames;
    std::uint32_t cachedProbeEntries;
    std::uint64_t cachedBytes;
    std::uint64_t budgetBytes;
    std::uint64_t hits;
    std::uint64_t misses;
    std::uint64_t releasedFrames;
    std::uint64_t helperFailures;
    std::uint32_t reserved[8];
};

// Process-wide, so no FFF3FPHandle: the prefetch service is deliberately not
// owned by a session. A plan with a null/empty path list cancels all work.
// Bad size/version returns InvalidArgument and leaves the previous plan alone.
FFF3FP_API FFFResult FFF3FP_SetPrefetchPlan(const FFF3FPPrefetchPlan* plan) noexcept;

// A caller declaring a smaller struct is rejected with InvalidArgument rather
// than receiving a short write. Bad size/version never disturbs playback.
FFF3FP_API FFFResult FFF3FP_GetPrefetchStatus(FFF3FPPrefetchStatus* status) noexcept;
