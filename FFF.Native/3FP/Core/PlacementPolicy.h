#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// ABI struct from 3FP/Api/FFF.Player.CpuPlacement.h. Forward-declared so this header
// does not pull the public API surface into every translation unit; the implementation
// file includes the public header.
struct FFF3FPCpuPlacementSnapshot;

// ---- Testable SMT-dedup boundary (D-04) -----------------------------------------
//
// The enumeration in Discover() collects SYSTEM_CPU_SET_INFORMATION records and
// buckets them by efficiency class / cache domain. On an SMT machine two records
// share one physical core (same CoreIndex) and its L1/L2: seating a decode worker
// on the second brother buys no real throughput, so the preferred set keeps one
// LP per physical core and parks the brother as overflow.
//
// The pure function below does that dedup over a POD mirror of the Windows record
// (no <windows.h> in this header), so the unit test can feed synthetic topologies
// without an OS. The .cpp converts SYSTEM_CPU_SET_INFORMATION -> CpuSetEntry and
// calls it; everything after that (bucketing, tier seating) reads the two outputs.
namespace placement_detail {

/// Fields of SYSTEM_CPU_SET_INFORMATION::CpuSet that the policy consumes. Plain
/// POD so it has no Windows dependency and can be built on the stack in a test.
struct CpuSetEntry {
    std::uint32_t id;              // CPU set Id (system-wide; what SetThreadSelectedCpuSets takes)
    std::uint32_t lp;              // LogicalProcessorIndex (group-local, 0..63)
    std::uint16_t group;           // Processor group number (cross-group = >64 LP machines)
    std::uint32_t coreIndex;       // Physical core index within group; SMT brothers share it
    std::uint8_t  efficiencyClass; // higher = faster (OS-defined: P > E on hybrid parts)
    std::uint32_t cacheIndex;      // LastLevelCacheIndex (one L3 domain per CCD on AMD)
};

/// Deduplicate SMT brothers: keep one logical processor per (group, coreIndex).
///
/// Retention rule for the kept LP on a physical core:
///   1. Higher EfficiencyClass wins (brothers of the same physical core always
///      share it, but this is the safe tiebreaker on odd topologies);
///   2. On a tie, the smaller CPU set Id wins (deterministic; the lower half of
///      an SMT pair tends to be the primary LP on most boot configurations).
///
/// The dropped brothers are returned in `outSiblings` rather than discarded:
/// BuildTiers spills onto them as overflow when the worker budget outgrows the
/// physical core count. Dedup is a preference, not a hard limit.
///
/// Pure: no OS calls, no global state. FFF.Native.Tests/PlacementPolicyTests.cpp
/// feeds synthetic vectors and checks the partition directly.
inline void DedupSmtSiblings(const std::vector<CpuSetEntry>& input,
                             std::vector<CpuSetEntry>& outPhysical,
                             std::vector<CpuSetEntry>& outSiblings) noexcept {
    outPhysical.clear();
    outSiblings.clear();
    // (group, coreIndex) -> index into outPhysical of the current best brother.
    std::map<std::pair<std::uint16_t, std::uint32_t>, std::size_t> best;
    for (const auto& e : input) {
        const auto key = std::make_pair(e.group, e.coreIndex);
        const auto it = best.find(key);
        if (it == best.end()) {
            best.emplace(key, outPhysical.size());
            outPhysical.push_back(e);
            continue;
        }
        CpuSetEntry& retained = outPhysical[it->second];
        const bool eBetter =
            e.efficiencyClass > retained.efficiencyClass ||
            (e.efficiencyClass == retained.efficiencyClass && e.id < retained.id);
        if (eBetter) {
            outSiblings.push_back(retained);  // old best becomes an overflow brother
            retained = e;
        } else {
            outSiblings.push_back(e);         // current record is the brother
        }
    }
}

}  // namespace placement_detail

/// Adaptive CPU placement for machines whose cores are not interchangeable: Intel
/// hybrid parts (performance/efficiency classes) and multi-CCD AMD parts (one L3
/// domain per die).
///
/// Three tiers, cheapest action first, because each one is strictly more intrusive
/// than the last:
///
///   1 Preferred  every player thread is eligible only on tier0. One call.
///   2 Spilled    the process-wide restriction is lifted so decode workers may use the
///                second domain; the present thread keeps its explicit selection.
///   3 Boosted    process priority class and the present thread's priority are raised.
///
/// Which domain is preferred comes from the OS, never from a table of model names:
///   * two or more efficiency classes -> the highest class (values are ordered by the
///     OS: bigger is faster, verified on this machine as SMT P-cores=1, E=0);
///   * otherwise two or more last-level-cache indices -> the L3 domain whose logical
///     processors are least busy overall (sampled, with hysteresis);
///   * otherwise Domain::None, and every call is a documented no-op.
///
/// The decode thread ladder reads the preferred group's size (PreferredDomainThreads)
/// to keep its first step inside one class/domain before crossing onto another.
///
/// Threads that were explicitly selected keep that selection: a process default only
/// governs threads that never picked a set. Measured on Windows 11 26100: child threads
/// do NOT inherit a parent's explicit selection, so pinning the present thread does not
/// pin FFmpeg's frame threads and the spill still reaches them.
///
/// With FFF_PLACEMENT_TRACE set, every evaluation window also emits one
/// PLACEMENT_SAMPLE line with the present thread's landing counts and per-tier busy
/// percentages, and Release() emits a final total. Read-only instrumentation: it adds
/// log lines and counters, never API calls, so the negative control stays negative.
class PlacementPolicy final {
public:
    enum class Domain { None, EfficiencyClass, CacheDomain };

    PlacementPolicy();
    ~PlacementPolicy();

    /// Called when playback starts. Discovers the topology once, reads the process's
    /// current priority class, then applies tier 1. Safe to call repeatedly.
    void OnPlaybackStart(std::uint32_t workerThreadId) noexcept;

    /// Called when playback leaves the playing state: drops tier 3 only. Placement is
    /// kept, because re-applying it on every resume is churn without a benefit.
    void OnPlaybackStop() noexcept;

    /// Called when the session closes: clears the process default, clears the present
    /// thread's selection and restores the priority class. Always safe to call.
    void Release() noexcept;

    /// Re-samples the preferred cache domain. Called once per evaluation window; does
    /// nothing for the efficiency-class kind. Kept separate from Spill/Boost so the
    /// domain choice can change without changing the tier.
    void Tick() noexcept;

    /// Escalation steps. Each returns true only when it changed the machine's state,
    /// so the caller can tell "acted" from "nothing left to do".
    bool Spill() noexcept;
    bool Boost() noexcept;
    /// One step down (boost -> spill -> preferred). Returns true when it changed state.
    bool Recover() noexcept;

    /// Process-wide startup placement (docs/146). Reuses the same discovery as the
    /// playback policy but writes only the process-default CPU set, so it works before
    /// any session exists. Opt-in through FFF_STARTUP_PLACEMENT; returns true when the
    /// process default was actually written. Intel hybrid prefers the fastest
    /// EfficiencyClass group; multi-CCD AMD keeps the L3 domain the calling (UI) thread
    /// is already on, because "main CCD" has no public Windows definition. Multi-group
    /// machines are refused: the OS ignores CPU Set assignments that do not match the
    /// thread's affinity group. See docs/146 for the evidence behind those choices.
    static bool BeginProcessStartupPlacement() noexcept;
    /// Clears the startup process-default assignment. Safe to call repeatedly; leaves a
    /// session-owned assignment alone (the playback policy owns it from then on).
    static void EndProcessStartupPlacement(const char* reason) noexcept;

    /// Standalone priority overlay for the "software threads == CPU threads" rule: raises
    /// the process class and the present thread's priority exactly like Boost(), but
    /// leaves the CPU-set tiers (and hence where the decoder workers may run) where they
    /// are -- a healthy stream at its thread budget must not be pushed onto slower cores
    /// just to hold priority. Idempotent; survives tier walk-backs; dropped by Release().
    /// Refused in the forced negative-control arms (their ceiling is below Boosted).
    bool Prioritize() noexcept;

    /// Trace hook: counts the calling (present/worker) thread's current processor against
    /// the tier partition. Called once per presented frame. No-op unless
    /// FFF_PLACEMENT_TRACE was set at construction; it never changes placement, it only
    /// lets a playback test report where the present thread really ran.
    void SamplePresentThread() noexcept;

    bool Available() const noexcept { return kind_ != Domain::None; }
    bool Spilled() const noexcept { return tier_ == Tier::Spilled || tier_ == Tier::Boosted; }
    bool Boosted() const noexcept { return tier_ == Tier::Boosted; }
    Domain Kind() const noexcept { return kind_; }

    /// One line describing the decision, including why it is Unavailable. Never empty.
    const std::string& Describe() const noexcept { return description_; }

    /// FFF_PLACEMENT_TRACE presence read at construction. Diagnostics only: the session
    /// uses it to gate its PLACEMENT_SNAPSHOT trace line, never a behaviour change.
    bool TraceEnabled() const noexcept { return trace_; }

    /// Diagnostic read-out for FFF3FP_GetCpuPlacementSnapshot (P3-12). Fills the
    /// placement-owned fields of `snapshot` with what the policy believes is in force
    /// plus a live read of the process priority class and the worker thread. It never
    /// takes a lock and never changes placement; the caller zero-fills the struct and
    /// sets size/version. The struct itself is defined in the public API header.
    void FillDiagnosticSnapshot(FFF3FPCpuPlacementSnapshot& snapshot) const noexcept;

    /// Explicit CPU sets currently selected on a thread, or FFF3FP_CPUPLACEMENT_UNKNOWN
    /// (0xFFFFFFFF) when the query fails. Thread id 0 means "no such thread" and reports
    /// 0. Shared by the session-level snapshot so the worker and the present thread are
    /// measured with exactly the same rule.
    static std::uint32_t QueryThreadSelectedCpuSetCount(std::uint32_t threadId) noexcept;

    /// Logical processors in the preferred group: the fastest efficiency class on a
    /// hybrid part, one L3 domain (one CCD) on a multi-CCD part -- widened onto the
    /// next group(s) when a caller asked for more seats than the fastest group alone
    /// has (see EnsurePreferredCapacity). A caller that grows a worker pool uses it as
    /// a step boundary -- fill this set before stepping onto another class/domain.
    /// Zero on a uniform machine (nothing to cross) or when discovery was skipped
    /// (FFF_PLACEMENT=off).
    std::uint32_t PreferredDomainThreads() noexcept;

    /// Widens the preferred group so it can seat at least minimumWorkers logical
    /// processors: the fastest class (or the least-busy cache domain) first, then the
    /// next group in discovery order, until the target is met or the groups run out
    /// (in which case the preferred group is every visible set and that is logged).
    /// Idempotent, and re-aims an already-applied tier so the OS state follows. The
    /// decode ladder calls it once per decoder open with its thread budget, which
    /// keeps the boundary honest on parts where the fastest class is smaller than the
    /// budget (8P+16E and friends) instead of assuming "fast class is the small one".
    void EnsurePreferredCapacity(std::uint32_t minimumWorkers) noexcept;

private:
    enum class Tier { None, Preferred, Spilled, Boosted };

    void Discover() noexcept;
    bool ApplyTier(Tier tier) noexcept;
    void BuildTiers(std::uint32_t minimumWorkers) noexcept;
    /// CPU set ids the startup window should prefer. Calls Discover() first; returns
    /// false when no usable domain exists or the machine spans processor groups.
    bool BuildStartupCpuSetIds(std::vector<std::uint32_t>& ids) noexcept;
    bool ReadProcessorTimes(std::vector<std::uint64_t>& idle,
                            std::vector<std::uint64_t>& busy) noexcept;
    void ChooseCacheDomain() noexcept;
    void Log(const char* transition) noexcept;

    int ClassifyCurrentProcessor() const noexcept;
    bool ReadTierUtilization(double& busy0, double& busy1) noexcept;
    void ApplyDomainSwap(double busy0, double busy1) noexcept;
    void EmitWindowSample(bool busyKnown, double busy0, double busy1) noexcept;
    void EmitFinalSample() noexcept;

    Domain kind_ = Domain::None;
    Tier tier_ = Tier::None;
    Tier ceiling_ = Tier::Boosted;   // forced by FFF_PLACEMENT, otherwise Boosted
    bool discovered_ = false;
    bool disabled_ = false;          // FFF_PLACEMENT=off: negative-control arm
    bool forced_ = false;            // FFF_PLACEMENT=prefer|spill|boost
    bool started_ = false;
    std::uint32_t workerThreadId_ = 0;

    std::vector<std::uint32_t> tier0_;   // cpu set ids, in tier order
    std::vector<std::uint32_t> tier1_;
    std::vector<std::uint32_t> all_;
    // The discovered groups in preference order -- efficiency classes fastest first,
    // cache domains head-first then by LLC index. tier0_/tier1_ are a view over these
    // (see BuildTiers), so widening the preferred set never has to re-read the OS.
    // D-04: ids/lps hold one LP per physical core (SMT-deduped); siblingIds/siblingLps
    // hold the dropped SMT brothers as overflow seats BuildTiers spills onto when the
    // worker budget outgrows the physical core count.
    struct Group {
        std::vector<std::uint32_t> ids;         // physical-core cpu set ids (one per SMT pair)
        std::vector<std::uint32_t> lps;         // physical-core logical processor indices
        std::vector<std::uint32_t> siblingIds; // SMT brother cpu set ids (overflow)
        std::vector<std::uint32_t> siblingLps; // SMT brother logical processor indices
    };
    std::vector<Group> orderedGroups_;
    std::uint32_t capacityTarget_ = 0;   // seats EnsurePreferredCapacity was asked for
    // Logical-processor indices backing each tier: the cache-domain pair for
    // Domain::CacheDomain, the efficiency-class pair for Domain::Hybrid. The present
    // sampling and the per-tier utilization both read these, so both kinds populate them.
    std::vector<std::uint32_t> domainLp0_;
    std::vector<std::uint32_t> domainLp1_;
    // domainLp0_'s size, exposed for the decode ladder's step boundary.
    std::uint32_t preferredDomainThreads_ = 0;
    // Processor groups seen at discovery. CPU Set assignments whose group does not match
    // a thread's affinity group are ignored by the OS, so the startup placement refuses
    // such machines instead of half-applying (docs/146 §四).
    std::uint32_t groupCount_ = 1;

    // cache-domain load sampling
    std::vector<std::uint64_t> prevIdle_;
    std::vector<std::uint64_t> prevBusy_;
    bool havePrevSample_ = false;
    std::int64_t lastDomainSwitch100ns_ = 0;

    // FFF_PLACEMENT_TRACE: presence-gated instrumentation (same convention as
    // FFF_CADENCE_TRACE). Read-only: adds log lines and counters, never API calls.
    bool trace_ = false;
    std::uint64_t sampleTier0_ = 0;   // present-thread samples, whole session
    std::uint64_t sampleTier1_ = 0;
    std::uint64_t sampleOther_ = 0;
    std::uint64_t windowTier0_ = 0;   // since the previous window line
    std::uint64_t windowTier1_ = 0;
    std::uint64_t windowOther_ = 0;
    std::uint64_t windows_ = 0;
    bool finalSampleLogged_ = false;

    // priority baseline, so a host that already runs at HIGH is not demoted
    std::uint32_t baselinePriorityClass_ = 0;
    bool prioritySaved_ = false;
    bool classRaised_ = false;
    bool overlayPriority_ = false;   // Prioritize(): outlives tier changes, cleared on Release
    int baselineThreadPriority_ = 0;   // THREAD_PRIORITY_NORMAL
    // CPU set ids this policy last wrote as the process default (0 again after Release).
    // Maintained at the SetProcessDefaultCpuSets sites; the diagnostic snapshot reports
    // what the policy put there instead of re-querying the OS on every call.
    std::uint32_t processDefaultSetCount_ = 0;

    std::string description_;
};
