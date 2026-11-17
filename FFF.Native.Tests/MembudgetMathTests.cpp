// Offline arithmetic tests for the memory budget (docs/108 §4, §5, §12).
//
// No Windows dependency: only MemoryBudgetMath.h, which is pure constexpr math.
// That is the point -- the four budget tiers, the dead zone and the Σfloor
// degradation ladder can be re-derived on any machine, without a playback session
// and without a GPU, so a regression in the numbers fails here instead of on a
// user's box.
//
// Build/run: python FFF.Native.Tests/run_membudget_math_tests.py --log <path>
//   cl /std:c++17 /EHsc /W4 /I ..\FFF.Native MembudgetMathTests.cpp /Fe:...
//
// Prints one PASS/FAIL line per check and "checks=N failures=M"; exit 1 on failure.
//
// Extra mode: membudget_math_tests.exe --table [hostPhysMiB]
//   Prints the 5-tier x 3-occupancy comparison table (legacy / tiered / global-15
//   thought experiment) used by the 2026-11-17 relaxation report. Not a test;
//   exits 0 after printing.

#include "3FP/Core/MemoryBudgetMath.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using namespace FFFPlayer::MemBudgetMath;

int g_checks = 0;
int g_failures = 0;

void Check(const char* name, bool ok, const char* detail) {
    ++g_checks;
    if (ok) {
        std::printf("PASS %s\n", name);
    } else {
        ++g_failures;
        std::printf("FAIL %s :: %s\n", name, detail);
    }
}

void CheckBudget(const char* name, MemorySample sample, std::uint64_t expected) {
    const auto actual = ComputeTotalBudget(sample);
    char detail[256];
    std::snprintf(detail, sizeof(detail),
        "expected=%llu MiB actual=%llu MiB",
        static_cast<unsigned long long>(expected / MiB),
        static_cast<unsigned long long>(actual / MiB));
    Check(name, actual == expected, detail);
}

// ---------------------------------------------------------------------------
// 1. The four tiers the scheme quotes. Deliberately NOT one fixed availability
//    fraction: the point of the sample is that whichever of the three terms binds
//    is visible, so each tier names the term it exercises.
//    Post-relaxation (2026-11-17): 4 GiB and 8 GiB boxes are UNCHANGED; on
//    16/32 GiB boxes the physical share went from /10 to 15%, so the avail/commit
//    gates -- not the physical tenth -- now bind there.
// ---------------------------------------------------------------------------
void TestBudgetTiers() {
    // 4 GiB: available and commit tie at 256 MiB, physical still allows 409 MiB.
    // This box is below the tier boundary, so the relaxation must not move it.
    MemorySample small{};
    small.physicalBytes = 4 * GiB;
    small.availablePhysBytes = 1 * GiB;          // 25% free
    small.commitAvailableBytes = 512 * MiB;      // 12.5% headroom
    CheckBudget("tier 4GiB -> 256MiB (available binds, unchanged by relaxation)",
        small, 256 * MiB);

    // 8 GiB: commit binds at 768 MiB. The box sits exactly on the tier boundary
    // and now spends 15% of RAM = 1.2 GiB on the physical term, which still does
    // not bind -- commit does.
    MemorySample medium{};
    medium.physicalBytes = 8 * GiB;
    medium.availablePhysBytes = 3 * GiB;
    medium.commitAvailableBytes = 1536 * MiB;
    CheckBudget("tier 8GiB -> 768MiB (commit binds, unchanged by relaxation)",
        medium, 768 * MiB);

    // 16 GiB: availPhys/4 and commitAvail/2 tie at 2048 MiB; the widened
    // physical term (2.4 GiB) no longer binds.
    MemorySample large{};
    large.physicalBytes = 16 * GiB;
    large.availablePhysBytes = 8 * GiB;
    large.commitAvailableBytes = 4 * GiB;
    CheckBudget("tier 16GiB -> 2048MiB (avail/commit bind, widened phys no longer binds)",
        large, 2048 * MiB);

    // 32 GiB: same shape, 4096 MiB.
    MemorySample huge{};
    huge.physicalBytes = 32 * GiB;
    huge.availablePhysBytes = 16 * GiB;
    huge.commitAvailableBytes = 8 * GiB;
    CheckBudget("tier 32GiB -> 4096MiB (avail/commit bind)", huge, 4096 * MiB);
}

// ---------------------------------------------------------------------------
// 2. Clamps: the floor keeps a starved box usable, the ceiling keeps a big box
//    from turning the pool into a second cache. The ceiling moved 4 GiB -> 8 GiB.
// ---------------------------------------------------------------------------
void TestClamps() {
    MemorySample starved{};
    starved.physicalBytes = 4 * GiB;
    starved.availablePhysBytes = 4 * MiB;     // nearly nothing free
    starved.commitAvailableBytes = 2 * MiB;
    CheckBudget("floor clamps to 8MiB", starved, kTotalBudgetMinBytes);

    MemorySample massive{};
    massive.physicalBytes = 256 * GiB;
    massive.availablePhysBytes = 200 * GiB;
    massive.commitAvailableBytes = 200 * GiB;
    CheckBudget("ceiling clamps to 8GiB", massive, kTotalBudgetMaxBytes);

    // A zeroed sample (GlobalMemoryStatusEx failing) must not produce a zero pool
    // that silently disables every consumer.
    MemorySample empty{};
    CheckBudget("zero sample still yields the floor", empty, kTotalBudgetMinBytes);
}

// ---------------------------------------------------------------------------
// 3. The physical-share tier boundary, pinned exactly: 8 GiB switches to 15%,
//    7.9 GiB stays at the old /10. The gates are fed 100 GiB of headroom so the
//    physical term is the only thing that can bind.
// ---------------------------------------------------------------------------
void TestRamTierBoundary() {
    MemorySample onTheLine{};
    onTheLine.physicalBytes = 8 * GiB;
    onTheLine.availablePhysBytes = 100 * GiB;
    onTheLine.commitAvailableBytes = 100 * GiB;
    CheckBudget("tier boundary: exactly 8GiB -> 15% share",
        onTheLine, 8 * GiB * 15 / 100);

    MemorySample justBelow{};
    justBelow.physicalBytes = 79 * GiB / 10;    // 7.9 GiB
    justBelow.availablePhysBytes = 100 * GiB;
    justBelow.commitAvailableBytes = 100 * GiB;
    CheckBudget("tier boundary: 7.9GiB -> old /10 share",
        justBelow, justBelow.physicalBytes / 10);

    // Differential regression: below the threshold, the tiered formula must be
    // byte-identical to the legacy one on the same samples (the "4 GiB machine
    // behavior is unchanged" claim is a real proposition, not wording).
    MemorySample smallBox{};
    smallBox.physicalBytes = 4 * GiB;
    smallBox.availablePhysBytes = 100 * GiB;
    smallBox.commitAvailableBytes = 100 * GiB;
    char detail[256];
    std::snprintf(detail, sizeof(detail),
        "tiered=%llu MiB legacy=%llu MiB",
        static_cast<unsigned long long>(ComputeTotalBudget(smallBox) / MiB),
        static_cast<unsigned long long>(ComputeTotalBudgetLegacy(smallBox) / MiB));
    Check("differential: 4GiB box == legacy formula byte-for-byte",
        ComputeTotalBudget(smallBox) == ComputeTotalBudgetLegacy(smallBox), detail);

    std::snprintf(detail, sizeof(detail),
        "tiered=%llu MiB legacy=%llu MiB",
        static_cast<unsigned long long>(ComputeTotalBudget(justBelow) / MiB),
        static_cast<unsigned long long>(ComputeTotalBudgetLegacy(justBelow) / MiB));
    Check("differential: 7.9GiB box == legacy formula byte-for-byte",
        ComputeTotalBudget(justBelow) == ComputeTotalBudgetLegacy(justBelow), detail);

    // And above the threshold it must actually differ: the relaxation is a no-op
    // test if the tier never engages.
    std::snprintf(detail, sizeof(detail),
        "tiered=%llu MiB legacy=%llu MiB",
        static_cast<unsigned long long>(ComputeTotalBudget(onTheLine) / MiB),
        static_cast<unsigned long long>(ComputeTotalBudgetLegacy(onTheLine) / MiB));
    Check("differential: 8GiB box differs from legacy (tier engaged)",
        ComputeTotalBudget(onTheLine) != ComputeTotalBudgetLegacy(onTheLine) &&
        ComputeTotalBudget(onTheLine) == onTheLine.physicalBytes * 15 / 100, detail);
}

// ---------------------------------------------------------------------------
// 4. TEST-ONLY cap (FFF3FP_MEMARB_CAP_MB): one-directional squeeze only.
// ---------------------------------------------------------------------------
void TestTestCap() {
    const std::uint64_t computed = 1000 * MiB;
    char detail[256];

    std::snprintf(detail, sizeof(detail),
        "computed=1000MiB cap=500MiB -> %llu MiB",
        static_cast<unsigned long long>(ApplyTestCapBytes(computed, 500 * MiB) / MiB));
    Check("cap: cap < computed squeezes down to the cap",
        ApplyTestCapBytes(computed, 500 * MiB) == 500 * MiB, detail);

    std::snprintf(detail, sizeof(detail),
        "computed=1000MiB cap=2000MiB -> %llu MiB",
        static_cast<unsigned long long>(ApplyTestCapBytes(computed, 2000 * MiB) / MiB));
    Check("cap: cap > computed does NOT raise the budget",
        ApplyTestCapBytes(computed, 2000 * MiB) == computed, detail);

    Check("cap: cap == computed and cap unset both keep computed",
        ApplyTestCapBytes(computed, computed) == computed &&
        ApplyTestCapBytes(computed, 0) == computed, detail);

    std::snprintf(detail, sizeof(detail),
        "parse(\"512\")=%llu MiB",
        static_cast<unsigned long long>(ParseTestCapBytes(L"512") / MiB));
    Check("cap: parser accepts a plain positive number of MiB",
        ParseTestCapBytes(L"512") == 512 * MiB &&
        ParseTestCapBytes(L"16384") == 16384 * MiB, detail);

    Check("cap: parser rejects empty/zero/negative/junk/absurd values",
        ParseTestCapBytes(nullptr) == 0 && ParseTestCapBytes(L"") == 0 &&
        ParseTestCapBytes(L"0") == 0 && ParseTestCapBytes(L"-5") == 0 &&
        ParseTestCapBytes(L"12abc") == 0 && ParseTestCapBytes(L"12.5") == 0 &&
        ParseTestCapBytes(L" 12") == 0 && ParseTestCapBytes(L"99999") == 0,
        "invalid cap strings must mean \"not set\"");

    // The installed slot squeezes even the PUBLIC entry the open-time precheck
    // calls (ComputeTotalBudget): this is the guarantee that a test cap reaches
    // every reader at once, instead of only the ledger's internal wrapper.
    MemorySample big{};
    big.physicalBytes = 16 * GiB;
    big.availablePhysBytes = 16 * GiB;
    big.commitAvailableBytes = 16 * GiB;
    const std::uint64_t raw = ComputeTotalBudgetRaw(big);
    InstallTestCapBytes(500 * MiB);
    const std::uint64_t squeezed = ComputeTotalBudget(big);
    InstallTestCapBytes(0);   // uninstall at once: later tests must see no cap
    std::snprintf(detail, sizeof(detail),
        "raw=%llu MiB squeezed=%llu MiB expected=500 MiB",
        static_cast<unsigned long long>(raw / MiB),
        static_cast<unsigned long long>(squeezed / MiB));
    Check("cap: installed slot squeezes the public entry the precheck calls",
        raw > 500 * MiB && squeezed == 500 * MiB, detail);
}

// ---------------------------------------------------------------------------
// 5. The 8 GiB hard ceiling is reachable ONLY when the two anti-thrashing gates
//    also allow it: with gates wide open the physical 15% term (9.6 GiB on a
//    64 GiB box) gets clamped to 8 GiB; with availPhys tight, the avail gate
//    alone holds the budget at 4 GiB, below the ceiling.
// ---------------------------------------------------------------------------
void TestHardCapReachability() {
    MemorySample wideOpen{};
    wideOpen.physicalBytes = 64 * GiB;
    wideOpen.availablePhysBytes = 64 * GiB;
    wideOpen.commitAvailableBytes = 64 * GiB;
    char detail[256];
    std::snprintf(detail, sizeof(detail),
        "phys*15%%=%llu MiB final=%llu MiB",
        static_cast<unsigned long long>(64 * GiB * 15 / 100 / MiB),
        static_cast<unsigned long long>(ComputeTotalBudget(wideOpen) / MiB));
    Check("hard cap: 64GiB box with open gates clamps at exactly 8GiB",
        ComputeTotalBudget(wideOpen) == kTotalBudgetMaxBytes &&
        ComputeTotalBudget(wideOpen) == 8 * GiB, detail);

    MemorySample gated{};
    gated.physicalBytes = 64 * GiB;
    gated.availablePhysBytes = 16 * GiB;        // /4 = 4 GiB, the binding term
    gated.commitAvailableBytes = 64 * GiB;
    std::snprintf(detail, sizeof(detail),
        "avail/4=%llu MiB final=%llu MiB (must be < 8GiB)",
        static_cast<unsigned long long>(16 * GiB / 4 / MiB),
        static_cast<unsigned long long>(ComputeTotalBudget(gated) / MiB));
    Check("hard cap: tight availPhys gate holds budget at 4GiB < 8GiB ceiling",
        ComputeTotalBudget(gated) == 4 * GiB &&
        ComputeTotalBudget(gated) < kTotalBudgetMaxBytes, detail);
}

// ---------------------------------------------------------------------------
// 6. Guard survives the relaxation: a 4 GiB machine opening 8192x8192
//    yuv444p10le software decode (384 MiB/frame) must STILL land on Reject.
//    The relaxation deliberately does not touch small boxes, so the rejection
//    reason (Σfloor after both degradations vs. the real budget) must hold.
// ---------------------------------------------------------------------------
void TestGuardSurvivesRelax() {
    const std::uint64_t bytesPerFrame8k = 384 * MiB;   // 8192x8192 yuv444p10le
    FloorSet floors{};
    floors.audioBytes = 1 * MiB;
    floors.videoBytes = 3 * bytesPerFrame8k;           // 1152 MiB
    floors.decodeBytes = 8 * bytesPerFrame8k;          // 3072 MiB
    floors.renderBytes = 16 * MiB;
    floors.prefetchBytes = 0;

    MemorySample box4g{};
    box4g.physicalBytes = 4 * GiB;
    box4g.availablePhysBytes = 1 * GiB;
    box4g.commitAvailableBytes = 512 * MiB;
    const std::uint64_t budget = ComputeTotalBudget(box4g);

    auto verdict = ResolveFloors(budget, floors);
    char detail[256];
    std::snprintf(detail, sizeof(detail),
        "budget=%llu MiB entry-sum=%llu MiB after-degrade-sum=%llu MiB step=%u",
        static_cast<unsigned long long>(budget / MiB),
        static_cast<unsigned long long>(SumFloors(floors) / MiB),
        static_cast<unsigned long long>(verdict.sumBytes / MiB),
        static_cast<unsigned>(verdict.step));
    Check("guard: 4GiB box + 8192x8192 yuv444p10le soft decode -> Reject (step 3)",
        verdict.step == DegradeStep::Reject &&
        verdict.sumBytes > budget &&
        verdict.sumBytes == 2321 * MiB, detail);
}

// ---------------------------------------------------------------------------
// 7. Dead zone max(floor/8, ceiling/16) at the boundary that matters:
//    prefetch has no floor, audio has ceiling == 2*floor and the two terms tie.
//    Untouched by this round -- kept as a regression.
// ---------------------------------------------------------------------------
void TestDeadZone() {
    const std::uint64_t prefetchCeiling = 256 * MiB;
    const auto prefetch = DeadZoneBytes(0, prefetchCeiling);
    char detail[256];
    std::snprintf(detail, sizeof(detail), "prefetch dead zone=%llu B expected=%llu B",
        static_cast<unsigned long long>(prefetch),
        static_cast<unsigned long long>(prefetchCeiling / 16));
    Check("dead zone: prefetch floor=0 -> ceiling/16 == 16 MiB",
        prefetch == prefetchCeiling / 16, detail);

    // Stereo 48 kHz float32 audio: 240 ms ceiling = 180 KiB, floor = 90 KiB.
    const std::uint64_t audioFloor = 90 * 1024;
    const std::uint64_t audioCeiling = 180 * 1024;
    const auto audio = DeadZoneBytes(audioFloor, audioCeiling);
    std::snprintf(detail, sizeof(detail),
        "audio dead zone=%llu B floor/8=%llu ceiling/16=%llu",
        static_cast<unsigned long long>(audio),
        static_cast<unsigned long long>(audioFloor / 8),
        static_cast<unsigned long long>(audioCeiling / 16));
    Check("dead zone: audio floor/8 == ceiling/16 (the tie the scheme relies on)",
        audio == audioFloor / 8 && audio == audioCeiling / 16, detail);

    // Video: floor 128 MiB, ceiling 512 MiB -> floor/8 = 16 MiB, ceiling/16 = 32 MiB.
    const auto video = DeadZoneBytes(128 * MiB, 512 * MiB);
    std::snprintf(detail, sizeof(detail), "video dead zone=%llu MiB expected=32 MiB",
        static_cast<unsigned long long>(video / MiB));
    Check("dead zone: video floor/8=16MiB vs ceiling/16=32MiB -> 32MiB",
        video == 32 * MiB, detail);
}

// ---------------------------------------------------------------------------
// 8. Σfloor three-step ladder, with a real 8K frame size.
//    8K 10-bit 4:2:0 (P010) = 7680*4320*3 bytes ~= 95 MiB. Rounded to 96 MiB so
//    the arithmetic below stays exact and readable.
//    Untouched by this round -- kept as a regression.
// ---------------------------------------------------------------------------
void TestDegradeLadder() {
    const std::uint64_t bytesPerFrame8k10bit = 96 * MiB;
    FloorSet floors{};
    floors.audioBytes = 1 * MiB;                               // 240 ms 7.1.4-ish
    floors.videoBytes = 3 * bytesPerFrame8k10bit;              // 288 MiB
    floors.decodeBytes = 8 * bytesPerFrame8k10bit;             // 768 MiB
    floors.renderBytes = 16 * MiB;
    floors.prefetchBytes = 0;

    char detail[256];

    // (a) no degradation: 1200 MiB holds all floors.
    auto fits = ResolveFloors(1200 * MiB, floors);
    Check("ladder: 1200MiB holds every floor (Step None)",
        fits.step == DegradeStep::None && fits.sumBytes == SumFloors(floors), "expected Step None");

    // (b) 1000 MiB: 3 frames do not fit, 2 frames do (977 MiB).
    auto step1 = ResolveFloors(1000 * MiB, floors);
    std::snprintf(detail, sizeof(detail), "step=%u sum=%llu MiB",
        static_cast<unsigned>(step1.step),
        static_cast<unsigned long long>(step1.sumBytes / MiB));
    Check("ladder: 1000MiB -> video 3 frames to 2 frames",
        step1.step == DegradeStep::VideoTwoFrames && step1.sumBytes == 977 * MiB, detail);

    // (c) 800 MiB: even 2 frames overflow, decode 8->4 threads fits (593 MiB).
    auto step2 = ResolveFloors(800 * MiB, floors);
    std::snprintf(detail, sizeof(detail), "step=%u sum=%llu MiB",
        static_cast<unsigned>(step2.step),
        static_cast<unsigned long long>(step2.sumBytes / MiB));
    Check("ladder: 800MiB -> decode 8 to 4 threads",
        step2.step == DegradeStep::DecodeFourThreads && step2.sumBytes == 593 * MiB, detail);

    // (d) 256 MiB (4 GiB machine): still 593 MiB after both steps -> refuse.
    auto reject = ResolveFloors(256 * MiB, floors);
    std::snprintf(detail, sizeof(detail), "step=%u sum=%llu MiB",
        static_cast<unsigned>(reject.step),
        static_cast<unsigned long long>(reject.sumBytes / MiB));
    Check("ladder: 4GiB box + 8K 10-bit -> Reject (must use hardware decode)",
        reject.step == DegradeStep::Reject && reject.sumBytes == 593 * MiB, detail);

    // The ladder must never cut audio or the render slot: both stay at their entry
    // value at every step.
    Check("ladder: audio floor never trimmed",
        step1.floors.audioBytes == floors.audioBytes &&
        step2.floors.audioBytes == floors.audioBytes &&
        reject.floors.audioBytes == floors.audioBytes, "audio floor changed");
    Check("ladder: render slot floor never trimmed",
        step1.floors.renderBytes == floors.renderBytes &&
        step2.floors.renderBytes == floors.renderBytes &&
        reject.floors.renderBytes == floors.renderBytes, "render floor changed");
}

// ---------------------------------------------------------------------------
// --table mode: the 5-tier x 3-occupancy comparison. Every number below is
// computed by executing the real formula variants; nothing is hand-typed.
// Assumptions (stated in the report):
//   idle   -> availPhys = 0.85 * phys, commitAvail = 0.90 * phys
//   normal -> availPhys = 0.55 * phys, commitAvail = 0.60 * phys
//   tight  -> availPhys = 0.30 * phys, commitAvail = 0.35 * phys
// ---------------------------------------------------------------------------
struct Occupancy { const char* name; std::uint64_t availPct; std::uint64_t commitPct; };

void DescribeBinding(std::uint64_t byA, std::uint64_t byC, std::uint64_t byP,
                     std::uint64_t final, std::uint64_t maxBytes, char* out, std::size_t n) {
    const bool hard = final == maxBytes &&
        (byA > maxBytes || byC > maxBytes || byP > maxBytes);
    if (hard) { std::snprintf(out, n, "hardcap"); return; }
    bool a = byA == final, c = byC == final, p = byP == final;
    if (a && c && p) std::snprintf(out, n, "avail/commit/phys");
    else if (a && c) std::snprintf(out, n, "avail/commit");
    else if (a && p) std::snprintf(out, n, "avail/phys");
    else if (c && p) std::snprintf(out, n, "commit/phys");
    else if (a) std::snprintf(out, n, "avail");
    else if (c) std::snprintf(out, n, "commit");
    else std::snprintf(out, n, "phys");
}

void PrintVariant(const char* tag, std::uint64_t byA, std::uint64_t byC,
                  std::uint64_t byP, std::uint64_t final, std::uint64_t maxBytes) {
    char binding[32];
    DescribeBinding(byA, byC, byP, final, maxBytes, binding, sizeof(binding));
    std::printf("  %-9s avail/4=%6llu commit/2=%6llu phys=%6llu MiB -> %6llu MiB  [%s]\n",
        tag,
        static_cast<unsigned long long>(byA / MiB),
        static_cast<unsigned long long>(byC / MiB),
        static_cast<unsigned long long>(byP / MiB),
        static_cast<unsigned long long>(final / MiB), binding);
}

int RunTable(std::uint64_t hostPhysMiB) {
    const std::uint64_t physTiers[5] = {4, 8, 16, 32, 64};
    const Occupancy occ[3] = {
        {"idle",   85, 90},
        {"normal", 55, 60},
        {"tight",  30, 35},
    };
    std::printf("== 5-tier x 3-occupancy budget comparison (MiB, computed by code) ==\n");
    std::printf("assumptions: availPhys=%.2f*phys, commitAvail=%.2f*phys per row\n",
        0.85, 0.90);
    for (std::size_t i = 0; i < 5; ++i) {
        const std::uint64_t phys = physTiers[i] * GiB;
        for (std::size_t j = 0; j < 3; ++j) {
            MemorySample s{};
            s.physicalBytes = phys;
            s.availablePhysBytes = phys * occ[j].availPct / 100;
            s.commitAvailableBytes = phys * occ[j].commitPct / 100;
            std::printf("-- phys=%llu GiB occupancy=%-6s (availPhys=%llu MiB commitAvail=%llu MiB)\n",
                static_cast<unsigned long long>(physTiers[i]), occ[j].name,
                static_cast<unsigned long long>(s.availablePhysBytes / MiB),
                static_cast<unsigned long long>(s.commitAvailableBytes / MiB));
            const std::uint64_t leg = ComputeTotalBudgetLegacy(s);
            const std::uint64_t tiered = ComputeTotalBudget(s);
            const std::uint64_t glob = ComputeTotalBudgetGlobalFifteen(s);
            PrintVariant("(a)legacy", s.availablePhysBytes / 4, s.commitAvailableBytes / 2,
                s.physicalBytes / 10, leg, 4 * GiB);
            PrintVariant("(b)tiered", s.availablePhysBytes / 4, s.commitAvailableBytes / 2,
                s.physicalBytes >= kRamTierThresholdBytes
                    ? s.physicalBytes * 15 / 100 : s.physicalBytes / 10,
                tiered, kTotalBudgetMaxBytes);
            PrintVariant("(c)glob15", s.availablePhysBytes / 4, s.commitAvailableBytes / 2,
                s.physicalBytes * 15 / 100, glob, kTotalBudgetMaxBytes);
        }
    }
    if (hostPhysMiB != 0) {
        const std::uint64_t phys = hostPhysMiB * MiB;
        std::printf("== THIS HOST: physical RAM = %llu MiB ==\n",
            static_cast<unsigned long long>(hostPhysMiB));
        for (std::size_t j = 0; j < 3; ++j) {
            MemorySample s{};
            s.physicalBytes = phys;
            s.availablePhysBytes = phys * occ[j].availPct / 100;
            s.commitAvailableBytes = phys * occ[j].commitPct / 100;
            const std::uint64_t leg = ComputeTotalBudgetLegacy(s);
            const std::uint64_t tiered = ComputeTotalBudget(s);
            std::printf("  host %-6s legacy=%llu MiB  tiered(new)=%llu MiB\n",
                occ[j].name,
                static_cast<unsigned long long>(leg / MiB),
                static_cast<unsigned long long>(tiered / MiB));
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--table") {
        std::uint64_t hostPhysMiB = 0;
        if (argc > 2) hostPhysMiB = std::strtoull(argv[2], nullptr, 10);
        return RunTable(hostPhysMiB);
    }
    std::printf("== memory budget math tests ==\n");
    TestBudgetTiers();
    TestClamps();
    TestRamTierBoundary();
    TestTestCap();
    TestHardCapReachability();
    TestGuardSurvivesRelax();
    TestDeadZone();
    TestDegradeLadder();
    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
