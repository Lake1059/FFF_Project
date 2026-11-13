#pragma once

#include "3FP/Api/FFF.Player.Api.h"

// Startup CPU placement control surface (docs/146). Appended additively like the
// prefetch and audio-tap entries: this header is NOT included from FFF.Player.Api.h,
// adds no FFF3FPSnapshot field and does not move PlayerApiVersion (20) or
// SnapshotVersion (23). Hosts resolve both exports dynamically and degrade to today's
// behaviour when they are absent.
//
// What it does: raises the process-default CPU Set for the startup window so the
// threads that open the first frame prefer the fastest domain --
//   * Intel hybrid parts: the fastest EfficiencyClass group (the P-cores);
//   * AMD multi-CCD parts: the L3 domain the calling (UI) thread was already placed on
//     ("main CCD" has no public Windows definition; see docs/146 §四);
//   * machines with more than one processor group: refused, because the OS ignores CPU
//     Set assignments whose group does not match the thread's affinity group.
// CPU Sets are a soft affinity preference, not a hard pin.
//
// Lifecycle: opt-in through FFF_STARTUP_PLACEMENT=1; every other value is a no-op.
// The host calls Begin once from its startup hook and keeps a fallback End for
// sessions that never present a frame. The kernel also ends the placement by itself on
// the first presented frame. If a playback session has already taken ownership of the
// process-default CPU set, End leaves it alone (the playback policy owns it from then
// on). See PlayerSession/PresentVideoFrame and PlacementPolicy.
//
// Both functions are safe to call repeatedly and from any thread.
//   FFF3FP_BeginStartupCpuPlacement: 1 when the process default was applied (or was
//       already applied), 0 when it was disabled, unavailable or refused.
//   FFF3FP_EndStartupCpuPlacement: void; idempotent.
FFF3FP_API int FFF3FP_BeginStartupCpuPlacement() noexcept;
FFF3FP_API void FFF3FP_EndStartupCpuPlacement() noexcept;

// ---- CPU placement diagnostics (P3-12, docs/148 §四) ------------------------------
//
// Read-only snapshot of what the placement policy decided and what the OS currently says
// about the worker / present threads. Appended additively to this header, exactly like
// the startup entries above: it is NOT included from FFF.Player.Api.h, adds no
// FFF3FPSnapshot field, and moves neither PlayerApiVersion (20) nor SnapshotVersion (23).
// Hosts resolve FFF3FP_GetCpuPlacementSnapshot dynamically; a kernel that lacks the
// export degrades to "no snapshot", never to a behaviour change.
//
// Two kinds of value are reported side by side:
//   * what the policy believes it put in force (domain / tier / flags / the set counts
//     it wrote), and
//   * what the OS answers right now about the worker and present threads (priority,
//     explicit CPU set count). Those reads can legitimately fail once a thread exits;
//     such a field is FFF3FP_CPUPLACEMENT_UNKNOWN, never a guessed zero.
//
// The snapshot is advisory: it is filled without taking the session lock, so the fields
// were not all sampled at the same instant. For A/B harnesses, not for control.
#define FFF3FP_CPUPLACEMENT_SNAPSHOT_VERSION 1u
// Sentinel for "cannot be measured" (thread gone, query refused, query unsupported).
#define FFF3FP_CPUPLACEMENT_UNKNOWN 0xFFFFFFFFu

// Which topology split the policy chose. None = uniform machine, or FFF_PLACEMENT=off.
enum class FFF3FPCpuPlacementDomain : std::uint32_t {
    NONE = 0,
    EFFICIENCY_CLASS = 1,   // Intel hybrid P/E classes
    CACHE_DOMAIN = 2,       // AMD multi-CCD L3 domains
};

// How far up the escalation ladder the policy currently stands.
enum class FFF3FPCpuPlacementTier : std::uint32_t {
    NONE = 0,
    PREFERRED = 1,
    SPILLED = 2,
    BOOSTED = 3,
};

// Bits for FFF3FPCpuPlacementSnapshot::flags: what is in force, not what is wanted.
enum class FFF3FPCpuPlacementFlags : std::uint32_t {
    AVAILABLE = 1u << 0,          // a usable domain was discovered
    DISABLED = 1u << 1,           // FFF_PLACEMENT=off negative-control arm
    FORCED = 1u << 2,             // FFF_PLACEMENT=prefer|spill|boost
    CLASS_RAISED = 1u << 3,       // this policy raised the process priority class
    OVERLAY_PRIORITY = 1u << 4,   // Prioritize() overlay is held
    PRIORITY_SAVED = 1u << 5,     // baselinePriorityClass holds a real value
    STARTUP_APPLIED = 1u << 6,    // startup placement applied once in this process
    WORKER_PINNED = 1u << 7,      // worker thread holds an explicit CPU set
};

// 22 x 4-byte fields = 88 bytes, pure POD. Field order and widths are frozen: the host
// mirrors this layout with <StructLayout(LayoutKind.Sequential)> and checks size before
// calling, so any drift has to bump FFF3FP_CPUPLACEMENT_SNAPSHOT_VERSION.
struct FFF3FPCpuPlacementSnapshot {
    std::uint32_t size;                    // caller sets sizeof(...) before the call
    std::uint32_t version;                 // FFF3FP_CPUPLACEMENT_SNAPSHOT_VERSION
    std::uint32_t domain;                  // FFF3FPCpuPlacementDomain
    std::uint32_t tier;                    // FFF3FPCpuPlacementTier
    std::uint32_t flags;                   // FFF3FPCpuPlacementFlags bits
    std::int32_t processPriorityClass;     // GetPriorityClass; unknown sentinel
    std::int32_t baselinePriorityClass;    // class at playback start; same sentinel
    std::uint32_t tier0SetCount;           // preferred CPU set ids the policy wrote
    std::uint32_t tier1SetCount;           // spill candidate set ids
    std::uint32_t processDefaultSetCount;  // ids last written as the process default
    std::uint32_t preferredDomainThreads;  // decode ladder's step boundary
    std::uint32_t processorGroupCount;     // >1 refuses CPU set placement
    std::uint32_t workerThreadId;          // session worker; 0 = not started
    std::int32_t workerThreadPriority;     // GetThreadPriority; unknown sentinel
    std::uint32_t workerSelectedSetCount;  // explicit selection; unknown sentinel
    std::uint32_t presentThreadId;         // timed-text presenter; 0 = not running
    std::uint32_t presentSelectedSetCount; // 0 while the presenter is not running
    std::uint32_t decoderThreadCount;      // software decoder rung; 0 = not chosen
    std::uint32_t reserved[4];             // future fields; zero-filled
};

static_assert(sizeof(FFF3FPCpuPlacementSnapshot) == 88,
    "FFF3FPCpuPlacementSnapshot layout changed: bump the snapshot version and mirror it "
    "in the host's VB structure (the P3-12 ABI is frozen).");

// Fills `snapshot` for a session. InvalidArgument when player or snapshot is null, when
// version != FFF3FP_CPUPLACEMENT_SNAPSHOT_VERSION, or when size is smaller than the
// struct. On success the caller's struct is overwritten end to end (size/version
// included) and reserved[] reads as zero.
FFF3FP_API FFFResult FFF3FP_GetCpuPlacementSnapshot(FFF3FPHandle player,
    FFF3FPCpuPlacementSnapshot* snapshot) noexcept;