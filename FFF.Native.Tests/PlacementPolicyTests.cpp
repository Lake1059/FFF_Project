// D-04 SMT-brother dedup unit tests.
//
// Feeds synthetic CpuSetEntry vectors into placement_detail::DedupSmtSiblings
// (the pure inline function in PlacementPolicy.h) and checks the partition.
// No Windows API calls, no OS topology: the dedup boundary is deliberately a
// POD-vector function so it can be tested on any box.
//
// Build (standalone, no link against PlacementPolicy.cpp -- the function is inline):
//   cl /std:c++17 /EHsc /I ..\FFF.Native PlacementPolicyTests.cpp /Fe:placement_policy_tests.exe
//
// Prints one PASS/FAIL line per check and "checks=N failures=M"; exits 1 on
// any failure.

#include "3FP/Core/PlacementPolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void Check(bool value, const std::string& name) {
    ++checks;
    if (value) {
        std::printf("PASS %s\n", name.c_str());
    } else {
        ++failures;
        std::printf("FAIL %s\n", name.c_str());
    }
}

using placement_detail::CpuSetEntry;
using placement_detail::DedupSmtSiblings;

// Helper: build a CpuSetEntry with sensible defaults.
CpuSetEntry Make(std::uint32_t id, std::uint32_t coreIndex,
                 std::uint8_t efficiencyClass = 2,
                 std::uint16_t group = 0,
                 std::uint32_t lp = 0,
                 std::uint32_t cacheIndex = 0) {
    return CpuSetEntry{ id, lp, group, coreIndex, efficiencyClass, cacheIndex };
}

// Count how many distinct (group, coreIndex) pairs appear in a vector.
std::size_t DistinctCores(const std::vector<CpuSetEntry>& v) {
    std::vector<std::pair<std::uint16_t, std::uint32_t>> seen;
    for (const auto& e : v) {
        auto key = std::make_pair(e.group, e.coreIndex);
        if (std::find(seen.begin(), seen.end(), key) == seen.end())
            seen.push_back(key);
    }
    return seen.size();
}

// Find the physical entry retained for a given (group, coreIndex).
const CpuSetEntry* FindPhysical(const std::vector<CpuSetEntry>& phys,
                                std::uint16_t group, std::uint32_t coreIndex) {
    for (const auto& e : phys)
        if (e.group == group && e.coreIndex == coreIndex) return &e;
    return nullptr;
}

void TestDedupOnePerCore() {
    // 4 physical cores, each with 2 SMT brothers = 8 LPs.
    std::vector<CpuSetEntry> input;
    for (std::uint32_t core = 0; core < 4; ++core) {
        input.push_back(Make(core * 2 + 0, core, 2, 0, core * 2 + 0));
        input.push_back(Make(core * 2 + 1, core, 2, 0, core * 2 + 1));
    }
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);
    Check(phys.size() == 4, "dedup-one-per-core: 8 LPs -> 4 physical");
    Check(DistinctCores(phys) == 4, "dedup-one-per-core: physical has 4 distinct CoreIndex");
    Check(sibs.size() == 4, "dedup-one-per-core: 4 brothers parked as siblings");
    Check(phys.size() + sibs.size() == input.size(),
          "dedup-one-per-core: no entries lost (phys+sib == input)");
}

void TestNoSmtNoDedup() {
    // 3 cores, no SMT (one LP per CoreIndex).
    std::vector<CpuSetEntry> input;
    for (std::uint32_t core = 0; core < 3; ++core)
        input.push_back(Make(core, core, 2, 0, core));
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);
    Check(phys.size() == 3, "no-smt: 3 LPs -> 3 physical");
    Check(sibs.empty(), "no-smt: no siblings");
}

void TestHigherEfficiencyClassWins() {
    // Two brothers on the same core with different EfficiencyClass (odd topology,
    // but the tiebreaker must still be deterministic). Higher class wins.
    std::vector<CpuSetEntry> input = {
        Make(10, 5, /*eff=*/1, 0, 10),
        Make(11, 5, /*eff=*/3, 0, 11),
    };
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);
    Check(phys.size() == 1, "eff-class-wins: one physical retained");
    if (!phys.empty()) {
        Check(phys[0].id == 11 && phys[0].efficiencyClass == 3,
              "eff-class-wins: higher EfficiencyClass brother kept");
    }
    Check(sibs.size() == 1 && sibs[0].id == 10,
          "eff-class-wins: lower class brother parked as sibling");
}

void TestSmallerIdWinsOnTie() {
    // Two brothers, same EfficiencyClass: smaller CPU set Id is kept.
    std::vector<CpuSetEntry> input = {
        Make(21, 7, 2, 0, 21),
        Make(20, 7, 2, 0, 20),
    };
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);
    Check(phys.size() == 1, "id-tiebreak: one physical retained");
    if (!phys.empty()) {
        Check(phys[0].id == 20, "id-tiebreak: smaller Id kept on equal EfficiencyClass");
    }
    Check(sibs.size() == 1 && sibs[0].id == 21,
          "id-tiebreak: larger Id parked as sibling");
}

void TestMultiGroupIsolatesDedup() {
    // Two groups, each with 2 cores and SMT. Dedup is per-group: same CoreIndex in
    // different groups must NOT be collapsed into one.
    std::vector<CpuSetEntry> input = {
        Make(0, 0, 2, /*group=*/0, 0),
        Make(1, 0, 2, /*group=*/0, 1),
        Make(2, 0, 2, /*group=*/1, 0),
        Make(3, 0, 2, /*group=*/1, 1),
    };
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);
    Check(phys.size() == 2, "multi-group: 2 groups x 1 core = 2 physical");
    Check(sibs.size() == 2, "multi-group: 2 brothers (one per group)");
    Check(FindPhysical(phys, 0, 0) != nullptr && FindPhysical(phys, 1, 0) != nullptr,
          "multi-group: CoreIndex 0 present in both groups separately");
}

void TestOverflowSiblingsAvailable() {
    // 4 physical P-cores (with 4 brothers), worker budget = 6.
    // Dedup gives 4 physical + 4 siblings. The caller (BuildTiers) seats 4 physical
    // first, then needs 2 more -- siblings must supply them.
    std::vector<CpuSetEntry> input;
    for (std::uint32_t core = 0; core < 4; ++core) {
        input.push_back(Make(core * 2 + 0, core, /*eff=*/2, 0, core * 2 + 0));
        input.push_back(Make(core * 2 + 1, core, /*eff=*/2, 0, core * 2 + 1));
    }
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);

    const std::size_t workers = 6;
    const std::size_t physicalSeats = phys.size();
    const std::size_t overflowNeeded = workers > physicalSeats ? workers - physicalSeats : 0;
    Check(overflowNeeded == 2, "overflow: 4 physical < 6 workers -> need 2 brother seats");
    Check(sibs.size() >= overflowNeeded,
          "overflow: siblings supply enough brother seats (4 >= 2)");

    // Simulate the BuildTiers seating: physical first, then siblings.
    std::size_t seated = 0;
    std::vector<std::uint32_t> tier0;
    for (const auto& e : phys) {
        if (seated >= workers) break;
        tier0.push_back(e.id);
        ++seated;
    }
    for (const auto& e : sibs) {
        if (seated >= workers) break;
        tier0.push_back(e.id);
        ++seated;
    }
    Check(tier0.size() == workers, "overflow: tier0 reaches 6 (4 physical + 2 brothers)");
    Check(seated == workers, "overflow: all 6 workers seated");
}

void TestHybridClassesSurviveDedup() {
    // 4 P-cores (eff=3, SMT) + 4 E-cores (eff=0, no SMT).
    // Dedup must keep 4 P physical + 4 E physical = 8 physical, 4 P brothers.
    std::vector<CpuSetEntry> input;
    for (std::uint32_t core = 0; core < 4; ++core) {
        input.push_back(Make(core * 2 + 0, core, /*eff=*/3, 0, core * 2 + 0));
        input.push_back(Make(core * 2 + 1, core, /*eff=*/3, 0, core * 2 + 1));
    }
    for (std::uint32_t core = 0; core < 4; ++core) {
        input.push_back(Make(16 + core, 100 + core, /*eff=*/0, 0, 16 + core));
    }
    std::vector<CpuSetEntry> phys, sibs;
    DedupSmtSiblings(input, phys, sibs);
    Check(phys.size() == 8, "hybrid: 4 P-cores (deduped) + 4 E-cores = 8 physical");
    Check(sibs.size() == 4, "hybrid: 4 P-core brothers as siblings");
    // All physical P-cores should have eff=3, all E-cores eff=0.
    int pCount = 0, eCount = 0;
    for (const auto& e : phys) {
        if (e.efficiencyClass == 3) ++pCount;
        else if (e.efficiencyClass == 0) ++eCount;
    }
    Check(pCount == 4 && eCount == 4, "hybrid: physical split 4 P + 4 E");
}

}  // namespace

int main() {
    TestDedupOnePerCore();
    TestNoSmtNoDedup();
    TestHigherEfficiencyClassWins();
    TestSmallerIdWinsOnTie();
    TestMultiGroupIsolatesDedup();
    TestOverflowSiblingsAvailable();
    TestHybridClassesSurviveDedup();

    std::printf("checks=%d failures=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
