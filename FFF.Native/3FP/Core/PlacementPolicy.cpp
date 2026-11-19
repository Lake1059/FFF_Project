#include "pch.h"
#include "3FP/Core/PlacementPolicy.h"
#include "3FP/Api/FFF.Player.CpuPlacement.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>


namespace {

constexpr ULONG kSystemProcessorPerformanceInformation = 8;

// winternl.h does not declare this information class; the layout has been stable since
// XP and is the one Process Explorer reads. The static_assert pins what we rely on.
struct ProcessorPerformanceInfo {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
};
static_assert(sizeof(ProcessorPerformanceInfo) == 48, "per-processor layout changed");

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

std::uint64_t Now100ns() noexcept {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? f.QuadPart : 1;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart * 10'000'000LL / frequency);
}

bool EnvIs(const wchar_t* name, const wchar_t* value) noexcept {
    wchar_t buffer[32] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, 32);
    return length > 0 && _wcsicmp(buffer, value) == 0;
}

// D-04: SMT brother dedup is on by default. FFF_PLACEMENT_NOSMTFILTER=1 disables it
// and restores the historical "take every CPU set" behavior for A/B comparison.
// Function-local static = lazy, thread-safe one-time init, read once per process.
bool SmtFilterEnabled() noexcept {
    static const bool enabled = [] {
        wchar_t buf[8] = {};
        const DWORD len = GetEnvironmentVariableW(L"FFF_PLACEMENT_NOSMTFILTER", buf, 8);
        // Enabled unless explicitly set to "1" (presence with any other value still enables).
        return len == 0 || _wcsicmp(buf, L"1") != 0;
    }();
    return enabled;
}

// Priority classes are not numerically ordered (NORMAL=0x20, ABOVE_NORMAL=0x8000), so
// compare by rank instead of by value.
int ClassRank(DWORD value) noexcept {
    switch (value) {
        case IDLE_PRIORITY_CLASS: return 0;
        case BELOW_NORMAL_PRIORITY_CLASS: return 1;
        case NORMAL_PRIORITY_CLASS: return 2;
        case ABOVE_NORMAL_PRIORITY_CLASS: return 3;
        case HIGH_PRIORITY_CLASS: return 4;
        case REALTIME_PRIORITY_CLASS: return 5;
        default: return -1;
    }
}

// Process-wide startup placement state (docs/146). The startup assignment is opt-in
// (FFF_STARTUP_PLACEMENT) and is released by the host fallback or by the first
// presented frame. A playback session takes ownership of the process-default CPU set
// as soon as its own tier writes it, and the startup release must not clear that.
std::mutex g_startupPlacementMutex;
bool g_startupPlacementActive = false;
bool g_sessionOwnsProcessDefaultCpuSets = false;

// Startup placement history for the diagnostic snapshot (P3-12): 1 = Begin applied the
// process default, 2 = the last Begin was refused or disabled, 0 = Begin never ran.
// End deliberately leaves this untouched: the flag answers "did the startup window ever
// apply its placement in this process", which stays true after the window closes.
std::atomic<std::uint32_t> g_startupPlacementHistory{0};

void MarkStartupPlacementHistory(const std::uint32_t state) noexcept {
    g_startupPlacementHistory.store(state, std::memory_order_relaxed);
}

std::uint32_t StartupPlacementHistory() noexcept {
    return g_startupPlacementHistory.load(std::memory_order_relaxed);
}

// ABI sentinel for the int32 fields of the snapshot: "cannot be measured".
constexpr std::int32_t kUnknownInt =
    static_cast<std::int32_t>(FFF3FP_CPUPLACEMENT_UNKNOWN);

constexpr std::uint32_t Flag(const FFF3FPCpuPlacementFlags flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}

}  // namespace

PlacementPolicy::PlacementPolicy() {
    if (EnvIs(L"FFF_PLACEMENT", L"off")) {
        disabled_ = true;
    } else if (EnvIs(L"FFF_PLACEMENT", L"prefer")) {
        forced_ = true;
        ceiling_ = Tier::Preferred;
    } else if (EnvIs(L"FFF_PLACEMENT", L"spill")) {
        forced_ = true;
        ceiling_ = Tier::Spilled;
    } else if (EnvIs(L"FFF_PLACEMENT", L"boost")) {
        forced_ = true;
        ceiling_ = Tier::Boosted;
    }
}

PlacementPolicy::~PlacementPolicy() { Release(); }


void PlacementPolicy::Discover() noexcept {
    // The off arm skips discovery unless sampling was asked for: the trace needs the
    // partition to classify samples, and reading topology is not "touching" anything.
    if (discovered_ || disabled_) return;
    discovered_ = true;
    try {

    ULONG needed = 0;
    GetSystemCpuSetInformation(nullptr, 0, &needed, GetCurrentProcess(), 0);
    if (needed == 0 || sizeof(SYSTEM_CPU_SET_INFORMATION) < 32) {
        kind_ = Domain::None;
        return;
    }

    std::vector<BYTE> buffer(needed + sizeof(SYSTEM_CPU_SET_INFORMATION));
    ULONG written = 0;
    if (!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
                                    static_cast<ULONG>(buffer.size()), &written, GetCurrentProcess(), 0)) {
        kind_ = Domain::None;
        return;
    }

    struct Entry { std::uint32_t id; std::uint32_t lp; };
    std::map<unsigned char, std::vector<Entry>> byEfficiencyPhys;
    std::map<unsigned char, std::vector<Entry>> byEfficiencySib;
    std::map<unsigned char, std::vector<Entry>> byCachePhys;
    std::map<unsigned char, std::vector<Entry>> byCacheSib;
    std::set<WORD> groupsSeen;
    const std::size_t count = written / sizeof(SYSTEM_CPU_SET_INFORMATION);
    // Collect raw records for SMT dedup (D-04). all_ still sees every id: the Spilled
    // tier intentionally widens onto all logical processors, brothers included.
    std::vector<placement_detail::CpuSetEntry> raw;
    raw.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& cpu = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data())[i].CpuSet;
        all_.push_back(static_cast<std::uint32_t>(cpu.Id));
        groupsSeen.insert(cpu.Group);
        raw.push_back({ static_cast<std::uint32_t>(cpu.Id),
                        static_cast<std::uint32_t>(cpu.LogicalProcessorIndex),
                        static_cast<std::uint16_t>(cpu.Group),
                        cpu.CoreIndex,
                        static_cast<std::uint8_t>(cpu.EfficiencyClass),
                        cpu.LastLevelCacheIndex });
    }
    groupCount_ = groupsSeen.empty() ? 1u : static_cast<std::uint32_t>(groupsSeen.size());
    if (all_.empty()) {
        kind_ = Domain::None;
        return;
    }

    // D-04: keep one LP per physical core (per group+CoreIndex); SMT brothers share
    // L1/L2 and execution units, so seating a decode worker on the second brother
    // buys no real throughput. Brothers are retained as overflow (BuildTiers spills
    // onto them when the worker budget outgrows the physical core count).
    // FFF_PLACEMENT_NOSMTFILTER=1 skips this and restores the historical "take all".
    std::vector<placement_detail::CpuSetEntry> physical, siblings;
    if (SmtFilterEnabled()) {
        placement_detail::DedupSmtSiblings(raw, physical, siblings);
    } else {
        physical = std::move(raw);
    }
    for (const auto& e : physical) {
        byEfficiencyPhys[e.efficiencyClass].push_back({e.id, e.lp});
        byCachePhys[static_cast<unsigned char>(e.cacheIndex)].push_back({e.id, e.lp});
    }
    for (const auto& e : siblings) {
        byEfficiencySib[e.efficiencyClass].push_back({e.id, e.lp});
        byCacheSib[static_cast<unsigned char>(e.cacheIndex)].push_back({e.id, e.lp});
    }

    if (byEfficiencyPhys.size() >= 2) {
        // Higher value is the faster class. Do not order by count: this machine exposes
        // fewer P CPUs than E (12 vs 8) and counting would pick the wrong side. The
        // groups are retained in class order (fastest first) so the preferred set can
        // later be widened onto the next class when the decode budget outgrows the
        // fastest one -- see EnsurePreferredCapacity.
        kind_ = Domain::EfficiencyClass;
        for (auto group = byEfficiencyPhys.rbegin(); group != byEfficiencyPhys.rend(); ++group) {
            orderedGroups_.push_back(Group{});
            for (const auto& entry : group->second) {
                orderedGroups_.back().ids.push_back(entry.id);
                orderedGroups_.back().lps.push_back(entry.lp);
            }
            // SMT brothers of this class ride along as overflow seats (same efficiency
            // class as their physical representative, so they land in the same bucket).
            const auto sib = byEfficiencySib.find(group->first);
            if (sib != byEfficiencySib.end()) {
                for (const auto& entry : sib->second) {
                    orderedGroups_.back().siblingIds.push_back(entry.id);
                    orderedGroups_.back().siblingLps.push_back(entry.lp);
                }
            }
        }
        BuildTiers(0);
    } else if (byCachePhys.size() >= 2) {
        // One L3 domain per CCD. "Least busy" needs per-processor sampling; without it
        // the policy declines instead of guessing.
        std::vector<std::uint64_t> idle, total;
        if (!ReadProcessorTimes(idle, total)) {
            if (groupCount_ > 1) {
                kind_ = Domain::None;
            } else {
                kind_ = Domain::None;
            }
            return;
        }
        kind_ = Domain::CacheDomain;
        // Keep every domain, head first then by LLC index: the preferred set starts as
        // the head domain alone and widens onto the following domains when the decode
        // budget outgrows one die (EnsurePreferredCapacity). The deterministic index
        // order is what keeps a widened membership stable across windows; the head
        // choice itself stays load-driven with hysteresis.
        for (const auto& domain : byCachePhys) {
            orderedGroups_.push_back(Group{});
            for (const auto& entry : domain.second) {
                orderedGroups_.back().ids.push_back(entry.id);
                orderedGroups_.back().lps.push_back(entry.lp);
            }
            const auto sib = byCacheSib.find(domain.first);
            if (sib != byCacheSib.end()) {
                for (const auto& entry : sib->second) {
                    orderedGroups_.back().siblingIds.push_back(entry.id);
                    orderedGroups_.back().siblingLps.push_back(entry.lp);
                }
            }
        }
        BuildTiers(0);
        prevIdle_ = std::move(idle);
        prevBusy_ = std::move(total);
        havePrevSample_ = true;
        lastDomainSwitch100ns_ = static_cast<std::int64_t>(Now100ns());
        // The first pick is arbitrary; both domains are near idle at playback start, so
        // Tick() corrects it within one window.
    } else {
        kind_ = Domain::None;
        return;
    }

    // The decode ladder uses this as its first-step boundary (see the header).
    preferredDomainThreads_ = static_cast<std::uint32_t>(domainLp0_.size());

    } catch (...) {
        kind_ = Domain::None;
    }
}

void PlacementPolicy::BuildTiers(std::uint32_t minimumWorkers) noexcept {
    // A view over orderedGroups_: seat groups in preference order until the target is
    // met, spilling the remainder to tier1. minimumWorkers == 0 keeps the historical
    // partition -- the first (fastest / head) group alone.
    //
    // D-04 two-pass seating:
    //   Pass 1 seats physical cores only (one LP per SMT pair). These are the real
    //   seats: a decode worker on a physical core gets the whole core.
    //   Pass 2 spills SMT brothers into tier0 as overflow ONLY when the worker budget
    //   outgrows the physical core count. Brothers share L1/L2 and execution units, so
    //   they buy less than a full core -- but they buy something, and a seated worker on
    //   a brother beats an unseated one. Dedup is a preference, not a hard limit.
    //   When FFF_PLACEMENT_NOSMTFILTER=1 the sibling vectors are empty and pass 2 is a
    //   no-op, reproducing the historical "every CPU set is a seat" behavior.
    tier0_.clear(); tier1_.clear();
    domainLp0_.clear(); domainLp1_.clear();
    std::size_t seated = 0;

    // -- Pass 1: physical cores ---------------------------------------------------
    bool firstGroup = true;
    for (const auto& group : orderedGroups_) {
        if (minimumWorkers == 0) {
            if (firstGroup) {
                tier0_.insert(tier0_.end(), group.ids.begin(), group.ids.end());
                domainLp0_.insert(domainLp0_.end(), group.lps.begin(), group.lps.end());
                seated = group.ids.size();
            } else {
                tier1_.insert(tier1_.end(), group.ids.begin(), group.ids.end());
                domainLp1_.insert(domainLp1_.end(), group.lps.begin(), group.lps.end());
            }
            firstGroup = false;
            continue;
        }
        if (seated >= static_cast<std::size_t>(minimumWorkers)) {
            tier1_.insert(tier1_.end(), group.ids.begin(), group.ids.end());
            domainLp1_.insert(domainLp1_.end(), group.lps.begin(), group.lps.end());
            continue;
        }
        if (seated + group.ids.size() <= static_cast<std::size_t>(minimumWorkers)) {
            tier0_.insert(tier0_.end(), group.ids.begin(), group.ids.end());
            domainLp0_.insert(domainLp0_.end(), group.lps.begin(), group.lps.end());
            seated += group.ids.size();
            continue;
        }
        const auto need = static_cast<std::size_t>(minimumWorkers) - seated;
        tier0_.insert(tier0_.end(), group.ids.begin(), group.ids.begin() + need);
        domainLp0_.insert(domainLp0_.end(), group.lps.begin(), group.lps.begin() + need);
        tier1_.insert(tier1_.end(), group.ids.begin() + need, group.ids.end());
        domainLp1_.insert(domainLp1_.end(), group.lps.begin() + need, group.lps.end());
        seated = static_cast<std::size_t>(minimumWorkers);
    }

    // -- Pass 2: SMT brothers as overflow ----------------------------------------
    for (const auto& group : orderedGroups_) {
        if (minimumWorkers == 0) {
            // Default partition: all brothers are extra capacity (tier1).
            tier1_.insert(tier1_.end(), group.siblingIds.begin(), group.siblingIds.end());
            domainLp1_.insert(domainLp1_.end(), group.siblingLps.begin(), group.siblingLps.end());
            continue;
        }
        if (seated >= static_cast<std::size_t>(minimumWorkers)) {
            tier1_.insert(tier1_.end(), group.siblingIds.begin(), group.siblingIds.end());
            domainLp1_.insert(domainLp1_.end(), group.siblingLps.begin(), group.siblingLps.end());
            continue;
        }
        if (seated + group.siblingIds.size() <= static_cast<std::size_t>(minimumWorkers)) {
            tier0_.insert(tier0_.end(), group.siblingIds.begin(), group.siblingIds.end());
            domainLp0_.insert(domainLp0_.end(), group.siblingLps.begin(), group.siblingLps.end());
            seated += group.siblingIds.size();
            continue;
        }
        const auto need = static_cast<std::size_t>(minimumWorkers) - seated;
        tier0_.insert(tier0_.end(), group.siblingIds.begin(), group.siblingIds.begin() + need);
        domainLp0_.insert(domainLp0_.end(), group.siblingLps.begin(), group.siblingLps.begin() + need);
        tier1_.insert(tier1_.end(), group.siblingIds.begin() + need, group.siblingIds.end());
        domainLp1_.insert(domainLp1_.end(), group.siblingLps.begin() + need, group.siblingLps.end());
        seated = static_cast<std::size_t>(minimumWorkers);
    }
    preferredDomainThreads_ = static_cast<std::uint32_t>(domainLp0_.size());
}

void PlacementPolicy::EnsurePreferredCapacity(std::uint32_t minimumWorkers) noexcept {
    Discover();
    if (kind_ == Domain::None || minimumWorkers == 0) return;
    if (preferredDomainThreads_ >= minimumWorkers) return;
    BuildTiers(minimumWorkers);
    capacityTarget_ = minimumWorkers;
    // The OS state follows the partition: an already-applied tier was aimed at the
    // narrower set, and ApplyTier short-circuits on an unchanged tier, so step through
    // None to force the calls back onto the widened set.
    if (started_ && tier_ != Tier::None) {
        ApplyTier(Tier::None);
        ApplyTier(tier_);
    }
}

std::uint32_t PlacementPolicy::PreferredDomainThreads() noexcept {
    Discover();
    return preferredDomainThreads_;
}

bool PlacementPolicy::ReadProcessorTimes(std::vector<std::uint64_t>& idle,
                                         std::vector<std::uint64_t>& total) noexcept {
    static const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (query == nullptr) return false;
    // D-04: multi-group machines (>64 logical processors) are refused for the
    // cache-domain load sampler. Phase 1 keeps this refusal rather than half-applying:
    // CPU Set assignments are honored only inside the thread's affinity group, so a
    // cross-group set is silently ignored by the OS. The future fix is per-group
    // SetThreadGroupAffinity + SetThreadSelectedCpuSets; the Discover() caller turns
    // this false into a multi-group-specific log line.
    if (GetMaximumProcessorGroupCount() > 1) return false;

    const DWORD processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (processors == 0 || processors > 64) return false;

    std::vector<ProcessorPerformanceInfo> info(processors);
    ULONG returned = 0;
    const LONG status = query(kSystemProcessorPerformanceInformation, info.data(),
                              static_cast<ULONG>(info.size() * sizeof(ProcessorPerformanceInfo)), &returned);
    if (status < 0 || returned < info.size() * sizeof(ProcessorPerformanceInfo)) return false;

    idle.assign(processors, 0);
    total.assign(processors, 0);
    for (DWORD i = 0; i < processors; ++i) {
        idle[i] = static_cast<std::uint64_t>(info[i].IdleTime.QuadPart);
        // KernelTime already contains IdleTime.
        total[i] = static_cast<std::uint64_t>(info[i].KernelTime.QuadPart +
                                              info[i].UserTime.QuadPart);
    }
    return true;
}

void PlacementPolicy::ChooseCacheDomain() noexcept {
    if (kind_ != Domain::CacheDomain) return;
    double busy0 = 0.0, busy1 = 0.0;
    if (!ReadTierUtilization(busy0, busy1)) return;
    ApplyDomainSwap(busy0, busy1);
}

bool PlacementPolicy::ReadTierUtilization(double& busy0, double& busy1) noexcept {
    try {
    std::vector<std::uint64_t> idle, total;
    if (!ReadProcessorTimes(idle, total)) return false;
    if (!havePrevSample_) {
        prevIdle_ = std::move(idle);
        prevBusy_ = std::move(total);
        havePrevSample_ = true;
        return false;
    }

    auto utilization = [&](const std::vector<std::uint32_t>& lps, double& value) {
        std::uint64_t busy = 0, whole = 0;
        for (const auto lp : lps) {
            if (lp >= idle.size() || lp >= prevIdle_.size()) return false;
            const std::uint64_t dIdle = idle[lp] >= prevIdle_[lp] ? idle[lp] - prevIdle_[lp] : 0;
            const std::uint64_t dTotal = total[lp] >= prevBusy_[lp] ? total[lp] - prevBusy_[lp] : 0;
            busy += dTotal > dIdle ? dTotal - dIdle : 0;
            whole += dTotal;
        }
        value = whole > 0 ? static_cast<double>(busy) / static_cast<double>(whole) : 0.0;
        return whole > 0;
    };

    const bool ok0 = utilization(domainLp0_, busy0);
    const bool ok1 = utilization(domainLp1_, busy1);
    prevIdle_ = std::move(idle);
    prevBusy_ = std::move(total);
    return ok0 && ok1;
    } catch (...) {
        return false;
    }
}

void PlacementPolicy::ApplyDomainSwap(double busy0, double busy1) noexcept {
    try {
    // Only swap for a clear win: a 20 pp margin keeps the domains from trading the
    // preferred role on noise, and the dwell keeps the swap out of startup transients.
    const std::uint64_t now = Now100ns();
    const bool dwelled = static_cast<std::int64_t>(now) - lastDomainSwitch100ns_ >= 100'000'000LL;
    if (busy0 > busy1 + 0.20 && dwelled) {
        // Rotate rather than swap: the preferred set may have been widened past the
        // head domain (EnsurePreferredCapacity), and a raw swap would trade the whole
        // widened set for the remainder. Moving the head to the back and rebuilding
        // keeps the same width pointed at the newly preferred die.
        if (!orderedGroups_.empty())
            std::rotate(orderedGroups_.begin(), orderedGroups_.begin() + 1, orderedGroups_.end());
        BuildTiers(capacityTarget_);
        lastDomainSwitch100ns_ = static_cast<std::int64_t>(now);
        ApplyTier(Tier::Preferred);
    }
    } catch (...) {}
}

void PlacementPolicy::Tick() noexcept {
    if (!disabled_ && kind_ == Domain::CacheDomain && tier_ == Tier::Preferred) {
        ChooseCacheDomain();
    }
}

void PlacementPolicy::OnPlaybackStart(std::uint32_t workerThreadId) noexcept {
    if (disabled_) {
        // The negative control says so out loud: an off session that stays silent is
        // indistinguishable from a build without the policy. One line, no API call.
        // The off arm applies nothing; with the trace on it still learns the partition so
        // the samples can be classified (Discover itself is read-only).
        Discover();
        return;
    }
    workerThreadId_ = workerThreadId;
    if (!started_) {
        started_ = true;
        const DWORD current = GetPriorityClass(GetCurrentProcess());
        if (current != 0) {
            baselinePriorityClass_ = current;
            prioritySaved_ = true;
        }
        HANDLE worker = workerThreadId_ != 0
            ? OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, workerThreadId_) : nullptr;
        if (worker != nullptr) {
            baselineThreadPriority_ = GetThreadPriority(worker);
            CloseHandle(worker);
        }
        Discover();
    }
    if (!Available()) {
        // Topology is not worth using, but tier 3 still applies: Boost() is independent
        // of the domains, so the state machine keeps a place for it.
        if (tier_ == Tier::None) {
            tier_ = Tier::Preferred;
        }
        return;
    }
    // A pause/resume keeps the tier that was earned: resetting to Preferred on every
    // resume would re-learn the same spill two seconds later, and the pause is the one
    // moment where the pipeline is not under load anyway.
    const Tier wanted = forced_ ? ceiling_ : (tier_ == Tier::None ? Tier::Preferred : tier_);
    ApplyTier(wanted);
}

void PlacementPolicy::OnPlaybackStop() noexcept {
    if (disabled_ || forced_) return;
    if (tier_ == Tier::Boosted) ApplyTier(Tier::Spilled);
}

void PlacementPolicy::Release() noexcept {
    if (disabled_) {
        return;
    }
    const bool touched = tier_ != Tier::None && started_;
    // The priority overlay outlives tier changes; drop it before the teardown so the
    // final restore actually returns the process to its original priority class.
    overlayPriority_ = false;
    ApplyTier(Tier::None);
    tier_ = Tier::None;
    started_ = false;
    (void)touched;
}

bool PlacementPolicy::ApplyTier(Tier wanted) noexcept {
    if (tier_ == wanted) return false;

    const bool restricted = wanted == Tier::Preferred || wanted == Tier::Spilled ||
                            wanted == Tier::Boosted;
    // 1. process-wide default: tier0 while preferred, every set once spilled, none on release
    if (Available()) {
        BOOL ok = FALSE;
        std::uint32_t defaultSetCount = 0;
        if (wanted == Tier::Preferred) {
            ok = SetProcessDefaultCpuSets(GetCurrentProcess(),
                                          reinterpret_cast<const ULONG*>(tier0_.data()),
                                          static_cast<ULONG>(tier0_.size()));
            defaultSetCount = static_cast<std::uint32_t>(tier0_.size());
        } else if (restricted) {
            ok = SetProcessDefaultCpuSets(GetCurrentProcess(),
                                          reinterpret_cast<const ULONG*>(all_.data()),
                                          static_cast<ULONG>(all_.size()));
            defaultSetCount = static_cast<std::uint32_t>(all_.size());
        } else {
            ok = SetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0);
        }
        // The session owns the process default from here on (see the startup placement in
        // docs/146): its release must not be mistaken for the startup one.
        if (ok) {
            g_sessionOwnsProcessDefaultCpuSets = restricted;
            // Diagnostic bookkeeping only (P3-12): what this policy last put there.
            processDefaultSetCount_ = defaultSetCount;
        }
    } else if (wanted == Tier::None) {
        if (SetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0))
            processDefaultSetCount_ = 0;
        g_sessionOwnsProcessDefaultCpuSets = false;
    }

    // 2. the present thread keeps the preferred domain at every tier: it is the one
    //    thread whose latency the user can see, and a process default cannot promise it
    //    once the other threads are allowed to spill.
    HANDLE worker = workerThreadId_ != 0
        ? OpenThread(THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION |
                         THREAD_SET_INFORMATION,
                     FALSE, workerThreadId_)
        : nullptr;
    if (worker != nullptr) {
        (Available() && restricted)
            ? SetThreadSelectedCpuSets(worker, reinterpret_cast<const ULONG*>(tier0_.data()),
                                       static_cast<ULONG>(tier0_.size()))
            : SetThreadSelectedCpuSets(worker, nullptr, 0);
    }

    // 3. priority only at the last tier, and never a demotion of a host that runs higher
    if (wanted == Tier::Boosted) {
        const DWORD current = GetPriorityClass(GetCurrentProcess());
        if (ClassRank(current) >= 0 && ClassRank(current) < ClassRank(ABOVE_NORMAL_PRIORITY_CLASS)) {
            classRaised_ = SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS) != FALSE;
        }
        if (worker != nullptr) SetThreadPriority(worker, THREAD_PRIORITY_HIGHEST);
    } else {
        // The standalone priority overlay (Prioritize) outlives tier changes: while it
        // is held, stepping a tier down must not hand its priority back.
        if (!overlayPriority_ && classRaised_ && prioritySaved_ &&
            GetPriorityClass(GetCurrentProcess()) != baselinePriorityClass_) {
            SetPriorityClass(GetCurrentProcess(), baselinePriorityClass_);
        }
        if (!overlayPriority_) classRaised_ = false;
        if (worker != nullptr && started_) {
            SetThreadPriority(worker, overlayPriority_ ? THREAD_PRIORITY_HIGHEST
                                                       : baselineThreadPriority_);
        }
    }

    if (worker != nullptr) CloseHandle(worker);
    tier_ = wanted;
    return true;
}

bool PlacementPolicy::Spill() noexcept {
    if (disabled_ || tier_ == Tier::None) return false;
    if (tier_ >= ceiling_) return false;
    if (!Available()) return false;          // nothing to lift
    return ApplyTier(Tier::Spilled);
}

// 规则锚点（docs/150「进程优先级写者规则」，P0-2）：进程 priority class 只允许内核按
// 测量升级，并且只恢复到播放开始时的基线（baselinePriorityClass_）；宿主启动期默认
// 不再写进程 class（docs/145 两次实测零收益，P0-1 已删启动 HIGH）。Boost() 是这条规则
// 的升级入口之一，所以它只在最贵的一档动作，且从不把已经更高的宿主降下来。
bool PlacementPolicy::Boost() noexcept {
    if (disabled_ || tier_ == Tier::None) return false;
    if (tier_ >= ceiling_) return false;
    return ApplyTier(Tier::Boosted);
}

bool PlacementPolicy::Recover() noexcept {
    if (disabled_ || forced_) return false;
    if (tier_ == Tier::Boosted) return ApplyTier(Tier::Spilled);
    if (tier_ == Tier::Spilled) return ApplyTier(Tier::Preferred);
    return false;
}

// 规则锚点（docs/150「进程优先级写者规则」，P0-2）：Prioritize() 也只写"进程 class +
// 呈现线程"这一层，由测出的线程饥饿触发；LakeUI PrecisionTimer 的线程级 AboveNormal
// 属于另一层（单个计时线程），两者不能混算成"进程被拉高了"。进程 class 的底线仍是
// 播放开始时的基线：降档 / Release 都按 baselinePriorityClass_ 回退。
bool PlacementPolicy::Prioritize() noexcept {
    if (disabled_ || tier_ == Tier::None) return false;
    if (ceiling_ != Tier::Boosted) return false;   // forced negative-control arms stay pure
    if (overlayPriority_) return false;
    overlayPriority_ = true;
    bool changed = false;
    const DWORD current = GetPriorityClass(GetCurrentProcess());
    if (ClassRank(current) >= 0 && ClassRank(current) < ClassRank(ABOVE_NORMAL_PRIORITY_CLASS) &&
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
        classRaised_ = true;
        changed = true;
    }
    HANDLE worker = workerThreadId_ != 0
        ? OpenThread(THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION |
                         THREAD_SET_INFORMATION,
                     FALSE, workerThreadId_)
        : nullptr;
    if (worker != nullptr) {
        if (SetThreadPriority(worker, THREAD_PRIORITY_HIGHEST)) changed = true;
        CloseHandle(worker);
    }
    return changed;
}

// ---- Process-wide startup placement (docs/146) -------------------------------------
//
// Same discovery as the playback policy, but only the process-default CPU set is
// written: this runs before any session exists. Everything here is a soft preference;
// the scheduler may still use other CPUs. See FFF.Player.CpuPlacement.h for the
// contract and docs/146 for why the domain is chosen this way.

bool PlacementPolicy::BuildStartupCpuSetIds(std::vector<std::uint32_t>& ids) noexcept {
    ids.clear();
    Discover();
    if (kind_ == Domain::None || orderedGroups_.empty()) return false;
    // CPU Set assignments are honored only inside the thread's processor group. The
    // discovery does not model that, so a machine with several groups (more than 64
    // logical processors) is refused instead of half-applied. Phase 1: keep the refusal
    // rather than guessing; the future path is SetThreadGroupAffinity + per-group
    // SetThreadSelectedCpuSets. The caller (BeginProcessStartupPlacement) logs the
    // refusal with a hint.
    if (groupCount_ > 1) return false;
    if (kind_ == Domain::EfficiencyClass) {
        // Fastest class first: the P-core group on Intel hybrid parts.
        ids = orderedGroups_.front().ids;
        return !ids.empty();
    }
    // Multi-CCD AMD: "main CCD" is not exposed by any public Win32 API. The definition
    // that cannot be wrong is "stay on the L3 domain the UI thread was already given" --
    // it still buys locality (no cross-CCD migration, same L3) without guessing which
    // die is "main" or trading cache for clock on X3D parts (docs/146 §四).
    PROCESSOR_NUMBER current{};
    GetCurrentProcessorNumberEx(&current);
    for (const auto& group : orderedGroups_) {
        bool onThisDomain = false;
        for (const auto lp : group.lps) {
            if (lp == current.Number) { onThisDomain = true; break; }
        }
        if (onThisDomain) {
            ids = group.ids;
            return !ids.empty();
        }
    }
    ids = orderedGroups_.front().ids;
    return !ids.empty();
}

bool PlacementPolicy::BeginProcessStartupPlacement() noexcept {
    // Opt-in by name and by value. The host checks the same variable before its first
    // export call, so an absent/off switch never even loads the kernel early.
    wchar_t mode[16] = {};
    const DWORD length = GetEnvironmentVariableW(L"FFF_STARTUP_PLACEMENT", mode, 16);
    if (length == 0) {
        MarkStartupPlacementHistory(2);   // disabled: no opt-in
        return false;
    }
    if (_wcsicmp(mode, L"1") != 0 && _wcsicmp(mode, L"on") != 0 &&
        _wcsicmp(mode, L"true") != 0) {
        MarkStartupPlacementHistory(2);
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(g_startupPlacementMutex);
        if (g_startupPlacementActive) {
            MarkStartupPlacementHistory(1);
            return true;
        }
        if (g_sessionOwnsProcessDefaultCpuSets) {
            MarkStartupPlacementHistory(2);
            return false;
        }
        // Deliberately a function-local static: the startup placement lives for the whole
        // process, and the playback policy stays per session.
        static PlacementPolicy policy;
        if (policy.disabled_) {
            MarkStartupPlacementHistory(2);
            return false;
        }
        std::vector<std::uint32_t> ids;
        if (!policy.BuildStartupCpuSetIds(ids)) {
            MarkStartupPlacementHistory(2);
            return false;
        }
        if (!SetProcessDefaultCpuSets(GetCurrentProcess(),
                                      reinterpret_cast<const ULONG*>(ids.data()),
                                      static_cast<ULONG>(ids.size()))) {
            MarkStartupPlacementHistory(2);
            return false;
        }
        g_startupPlacementActive = true;
        MarkStartupPlacementHistory(1);
        return true;
    } catch (...) {
        MarkStartupPlacementHistory(2);
        return false;
    }
}

void PlacementPolicy::EndProcessStartupPlacement(const char* reason) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_startupPlacementMutex);
        if (!g_startupPlacementActive) return;
        g_startupPlacementActive = false;
        const bool sessionOwned = g_sessionOwnsProcessDefaultCpuSets;
        if (!sessionOwned) SetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0);
    } catch (...) {
    }
}

// ---- CPU placement diagnostics (P3-12, docs/148 §四) -------------------------------
//
// One read-only summary for the host's A/B harness: what the policy decided, and what
// the OS currently says about the worker thread. Nothing here changes placement; the
// thread queries open a THREAD_QUERY_LIMITED_INFORMATION handle and close it at once.

std::uint32_t PlacementPolicy::QueryThreadSelectedCpuSetCount(
    const std::uint32_t threadId) noexcept {
    if (threadId == 0) return 0;   // no such thread: "not running", not "unknown"
    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);
    if (thread == nullptr) return FFF3FP_CPUPLACEMENT_UNKNOWN;
    // A NULL buffer with capacity 0 asks for the count only: the call returns TRUE and
    // reports 0 when the thread has no explicit selection, and fails with
    // ERROR_INSUFFICIENT_BUFFER (putting the required count in `required`) when it has
    // one -- see the GetThreadSelectedCpuSets contract.
    ULONG required = 0;
    std::uint32_t count = FFF3FP_CPUPLACEMENT_UNKNOWN;
    if (GetThreadSelectedCpuSets(thread, nullptr, 0, &required)) count = 0;
    else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) count = required;
    CloseHandle(thread);
    return count;
}

void PlacementPolicy::FillDiagnosticSnapshot(FFF3FPCpuPlacementSnapshot& snapshot) const noexcept {
    FFF3FPCpuPlacementDomain domain = FFF3FPCpuPlacementDomain::NONE;
    if (kind_ == Domain::EfficiencyClass) domain = FFF3FPCpuPlacementDomain::EFFICIENCY_CLASS;
    else if (kind_ == Domain::CacheDomain) domain = FFF3FPCpuPlacementDomain::CACHE_DOMAIN;
    snapshot.domain = static_cast<std::uint32_t>(domain);

    FFF3FPCpuPlacementTier tier = FFF3FPCpuPlacementTier::NONE;
    if (tier_ == Tier::Preferred) tier = FFF3FPCpuPlacementTier::PREFERRED;
    else if (tier_ == Tier::Spilled) tier = FFF3FPCpuPlacementTier::SPILLED;
    else if (tier_ == Tier::Boosted) tier = FFF3FPCpuPlacementTier::BOOSTED;
    snapshot.tier = static_cast<std::uint32_t>(tier);

    std::uint32_t flags = 0;
    if (Available()) flags |= Flag(FFF3FPCpuPlacementFlags::AVAILABLE);
    if (disabled_) flags |= Flag(FFF3FPCpuPlacementFlags::DISABLED);
    if (forced_) flags |= Flag(FFF3FPCpuPlacementFlags::FORCED);
    if (classRaised_) flags |= Flag(FFF3FPCpuPlacementFlags::CLASS_RAISED);
    if (overlayPriority_) flags |= Flag(FFF3FPCpuPlacementFlags::OVERLAY_PRIORITY);
    if (prioritySaved_) flags |= Flag(FFF3FPCpuPlacementFlags::PRIORITY_SAVED);
    // Startup history is process-wide, not session state: Begin applied it before any
    // session existed and End intentionally does not clear it (docs/146).
    if (StartupPlacementHistory() == 1) flags |= Flag(FFF3FPCpuPlacementFlags::STARTUP_APPLIED);
    // The worker holds an explicit set exactly while a restricted tier is in force and a
    // usable domain exists (ApplyTier step 2). This mirrors the apply path; the actual
    // thread selection is reported separately as workerSelectedSetCount.
    if (Available() && started_ && tier_ != Tier::None)
        flags |= Flag(FFF3FPCpuPlacementFlags::WORKER_PINNED);
    snapshot.flags = flags;

    const DWORD processClass = GetPriorityClass(GetCurrentProcess());
    snapshot.processPriorityClass = processClass != 0
        ? static_cast<std::int32_t>(processClass) : kUnknownInt;
    snapshot.baselinePriorityClass = prioritySaved_
        ? static_cast<std::int32_t>(baselinePriorityClass_) : kUnknownInt;

    snapshot.tier0SetCount = static_cast<std::uint32_t>(tier0_.size());
    snapshot.tier1SetCount = static_cast<std::uint32_t>(tier1_.size());
    snapshot.processDefaultSetCount = processDefaultSetCount_;
    snapshot.preferredDomainThreads = preferredDomainThreads_;
    snapshot.processorGroupCount = groupCount_;
    snapshot.workerThreadId = workerThreadId_;

    // The worker may have exited (Close) or never started: OpenThread then fails and both
    // thread-level fields stay at the sentinel instead of a misleading zero.
    if (workerThreadId_ == 0) {
        snapshot.workerThreadPriority = kUnknownInt;
        snapshot.workerSelectedSetCount = FFF3FP_CPUPLACEMENT_UNKNOWN;
    } else {
        snapshot.workerSelectedSetCount = QueryThreadSelectedCpuSetCount(workerThreadId_);
        const HANDLE worker =
            OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, workerThreadId_);
        if (worker == nullptr) {
            snapshot.workerThreadPriority = kUnknownInt;
        } else {
            const int priority = GetThreadPriority(worker);
            snapshot.workerThreadPriority =
                priority != THREAD_PRIORITY_ERROR_RETURN ? priority : kUnknownInt;
            CloseHandle(worker);
        }
    }
}
