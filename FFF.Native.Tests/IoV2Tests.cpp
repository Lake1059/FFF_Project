#include "3FP/IO/BudgetController.h"
#include "3FP/IO/Deadline.h"
#include "3FP/IO/IMediaSource.h"
#include "3FP/IO/IoBudgetMath.h"
#include "3FP/IO/ProgressWatchdog.h"
#include "3FP/IO/ReadLedger.h"
#include "3FP/IO/SessionBudgetLease.h"
#include "3FP/IO/SourceCapabilities.h"

#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;

void Check(bool value, const char* expression, int line) {
    if (value) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

void TestDeadline() {
    using namespace FFFPlayer::Io;
    CHECK(ReadIdleTimeoutMs(1000, 0) == 3000);
    CHECK(ReadIdleTimeoutMs(30000, 0) == 30000);
    CHECK(StallTimeoutMs(30000) == 120000);
    CHECK(StallTimeoutMs(600000) == 120000);
    CHECK(HardStallMs(1000) == 300000);
    CHECK(OpenDeadlineMs(30000, 128 * 1024, 25'000'000) == 60000);
    CHECK(SeekDeadlineMs(30000, 50ull * 1024 * 1024, 25'000'000) == 30000);
    CHECK(LatencyClass(49) == 0);
    CHECK(LatencyClass(300) == 1);
    CHECK(LatencyClass(2000) == 2);
    CHECK(LatencyClass(30000) == 3);
    CHECK(LatencyClass(30001) == 4);
    CHECK(ContinuousReadWindowMs(30000) == 60000);
}

void TestBudgetMath() {
    using namespace FFFPlayer::Io;
    CHECK(AvioBufferBytes(200'000'000) == 125000);
    CHECK(BdpBytes(200'000'000, 300) == 7'500'000);
    CHECK(ByteWindowTargetBytes(200'000'000, 300, 64 * MiB) == 9'597'152);
    CHECK(CanPromiseContinuousPlayback(240'000'000, 200'000'000, 300, 64 * MiB));
    CHECK(!CanPromiseContinuousPlayback(219'000'000, 200'000'000, 300, 64 * MiB));
    CHECK(CanPromiseContinuousPlayback(220'000'000, 200'000'000, 300, 64 * MiB));
    CHECK(!CanPromiseContinuousPlayback(240'000'000, 200'000'000, 5000, 64 * MiB));
    CHECK(!CanPromiseContinuousPlayback(240'000'000, 200'000'000, 300, 20 * MiB));
    // A high measured rate cannot bypass a small TCP receive window.
    const auto tcpLimited = EffectiveGoodputBps(240'000'000, 1 * MiB, 300);
    CHECK(tcpLimited < 40'000'000);
    CHECK(!CanPromiseContinuousPlayback(tcpLimited, 20'000'000, 300, 64 * MiB));
    CHECK(PacketBackBytes(200'000'000, 8 * MiB) == 0); // 200 Mbps > 50 Mbps
    CHECK(PacketBackBytes(20'000'000, 8 * MiB) == 8 * MiB);
    CHECK(SegmentPrefetchCount(30000, 6000) == 4);
    CHECK(PrefetchBytes(1 * GiB) == (1 * GiB) / 10);
    CHECK(PrefetchBytes(32 * GiB) == 1 * GiB);
    CHECK(PrefetchBytes(32 * GiB, true) == 256 * MiB);

    BudgetInputs inputs{};
    inputs.bitrateBps = 200'000'000;
    inputs.measuredGoodputBps = 240'000'000;
    inputs.tcpReceiveWindowBytes = 16 * MiB;
    inputs.rttMs = 300;
    inputs.chunkTimeMs = 1000;
    inputs.decodeJitterMs = 100;
    inputs.presentJitterMs = 100;
    inputs.frameIntervalMs = 16;
    inputs.segmentDurationMs = 6000;
    inputs.gopBytes = 8 * MiB;
    inputs.availablePhysicalBytes = 16 * GiB;
    inputs.sessionCount = 9;
    const auto budget = ComputeSessionBudget(inputs);
    CHECK(budget.byteWindowTargetBytes >= 4 * MiB);
    CHECK(budget.byteWindowTargetBytes <= 64 * MiB);
    CHECK(budget.byteWindowCeilingBytes == (192 * MiB) / 9);
    CHECK(budget.byteWindowTargetBytes <= budget.byteWindowCeilingBytes);
    CHECK(budget.avioBufferBytes == 125000);
    CHECK(budget.frameQueueDepth == 6);
    CHECK(budget.stallTimeoutMs == 30000);
    CHECK(!budget.canPromiseContinuousPlayback); // 9-way ceiling cannot hold 1 s of 200 Mbps
}

void TestReadLedger() {
    using namespace FFFPlayer::Io;
    ReadLedger ledger;
    ledger.ResetSource(1, 100, 0x1234);
    ReadObservation observation{};
    observation.sourceEpoch = 1;
    observation.offset = 0;
    observation.bytes = 10;
    observation.etagHash = 0x1234;
    CHECK(ledger.Observe(observation) == LedgerVerdict::Ok);
    observation.offset = 10;
    CHECK(ledger.Observe(observation) == LedgerVerdict::Ok);
    observation.offset = 5;
    CHECK(ledger.Observe(observation) == LedgerVerdict::OffsetMismatch);
    observation.offset = 20;
    observation.sourceEpoch = 2;
    CHECK(ledger.Observe(observation) == LedgerVerdict::StaleEpoch);
    observation.offset = 20;
    observation.sourceEpoch = 1;
    observation.etagHash = 0x9999;
    CHECK(ledger.Observe(observation) == LedgerVerdict::SourceChanged);
    observation.offset = 20;
    observation.etagHash = 0x1234;
    observation.rangeResponse = true;
    observation.contentRangeStart = 0;
    CHECK(ledger.Observe(observation) == LedgerVerdict::ContentRangeMismatch);
    ledger.ResetExpectedOffset(100);
    observation.offset = 100;
    observation.bytes = 0;
    observation.rangeResponse = false;
    observation.eof = true;
    CHECK(ledger.Observe(observation) == LedgerVerdict::Ok);
    ledger.ResetExpectedOffset(20);
    observation.offset = 20;
    observation.eof = true;
    CHECK(ledger.Observe(observation) == LedgerVerdict::TruncatedEof);
}

void TestContracts() {
    using namespace FFFPlayer::Io;
    CHECK(MakeEof().status == Status::Eof);
    CHECK(MakeRetryable(Error::Timeout).status == Status::Retryable);
    CHECK(IsPermanent(Error::NotFound));
    CHECK(!IsPermanent(Error::SegmentExpired));
    CHECK(IsSegmentRecoverable(Error::SegmentExpired));
    CHECK(IsAuthRetryable(Error::AuthRequired));

    SourceCapabilities capabilities{};
    capabilities.domain = SourceDomain::ByteRandomAccess;
    capabilities.features = SourceFeature::ByteExact | SourceFeature::RandomAccess;
    CHECK(capabilities.ByteExact());
    CHECK(capabilities.RandomAccess());
    CHECK(!capabilities.IsRealtime());

    SessionBudgetLease lease;
    SessionBudget budget{};
    budget.byteWindowTargetBytes = 1 * MiB;
    lease.Grant(0, budget, 7, 2000);
    CHECK(!lease.Expired(1000));
    CHECK(lease.Effective(1000).has_value());
    CHECK(lease.ConfigEpoch() == 7);
    CHECK(lease.Expired(2000));
    CHECK(!lease.Effective(2000).has_value());
}

void TestProgressWatchdog() {
    using namespace FFFPlayer::Io;
    ProgressWatchdogConfig config{};
    config.stallTimeoutMs = 30000;
    config.hardStallMs = 300000;
    config.resumeWatermarkBytes = 8 * MiB;
    ProgressWatchdog watchdog(config);
    watchdog.Reset(0);
    watchdog.Begin(0);
    watchdog.OnBufferLevel(8 * MiB);
    watchdog.SetResumeEligible(true);
    watchdog.MarkPlaying();
    CHECK(watchdog.Snapshot(0).state == HealthState::Playing);
    watchdog.OnBufferLevel(0); // low-water mark: pause the clock but keep reading

    watchdog.Tick(30000);
    CHECK(watchdog.Snapshot(30000).state == HealthState::Stalled);
    CHECK(watchdog.Snapshot(30000).stallCount == 1);
    watchdog.OnProgress(31000, 1);
    CHECK(watchdog.Snapshot(31000).state == HealthState::Recovering);
    watchdog.OnBufferLevel(8 * MiB);
    CHECK(watchdog.Snapshot(31000).state == HealthState::Playing);

    watchdog.OnBufferLevel(0);
    CHECK(watchdog.Snapshot(31000).state == HealthState::Buffering);
    watchdog.SetPaused(true, 32000);
    CHECK(watchdog.Snapshot(32000).state == HealthState::Paused);
    watchdog.Tick(60000);
    CHECK(watchdog.Snapshot(60000).state == HealthState::Paused);
    watchdog.SetPaused(false, 61000);
    CHECK(watchdog.Snapshot(61000).state == HealthState::Buffering);

    watchdog.OnPermanentError(62000);
    CHECK(watchdog.Snapshot(62000).state == HealthState::FailedPermanent);
    watchdog.OnCancel(63000);
    CHECK(watchdog.Snapshot(63000).state == HealthState::Cancelled);

    // A stalled source with a valid progress point must recover, never fail,
    // even when the watchdog fires long before the next progress arrives.
    ProgressWatchdog slow(config);
    slow.Reset(0);
    slow.Begin(0);
    slow.MarkPlaying();
    slow.Tick(120000);
    CHECK(slow.Snapshot(120000).state == HealthState::Stalled);
    slow.OnProgress(130000, 1);
    CHECK(slow.Snapshot(130000).state == HealthState::Recovering);
    slow.Tick(900000);
    CHECK(!IsTerminal(slow.Snapshot(900000).state));

    ProgressWatchdogConfig highLatency{};
    highLatency.stallTimeoutMs = 120000;
    highLatency.hardStallMs = 300000;
    highLatency.resumeWatermarkBytes = 8 * MiB;
    ProgressWatchdog high(highLatency);
    high.Reset(0);
    high.Begin(0);
    high.MarkPlaying();
    high.Tick(130000);
    CHECK(high.Snapshot(130000).state == HealthState::Stalled);
    high.OnProgress(150000, 1);
    CHECK(high.Snapshot(150000).state == HealthState::Recovering);
    high.Tick(1000000);
    CHECK(!IsTerminal(high.Snapshot(1000000).state));
    CHECK(high.Snapshot(1000000).waitingNetwork);
}

void TestBudgetController() {
    using namespace FFFPlayer::Io;
    BudgetInputs inputs{};
    inputs.bitrateBps = 200'000'000;
    inputs.measuredGoodputBps = 240'000'000;
    inputs.tcpReceiveWindowBytes = 16 * MiB;
    inputs.rttMs = 300;
    inputs.chunkTimeMs = 1000;
    inputs.frameIntervalMs = 16;
    inputs.segmentDurationMs = 6000;
    inputs.availablePhysicalBytes = 16 * GiB;
    inputs.sessionCount = 9;
    const auto initial = ComputeSessionBudget(inputs);
    BudgetController controller;
    controller.Reset(initial, 0);
    CHECK(!controller.Sample(100, inputs).has_value());
    BudgetInputs hot = inputs;
    hot.rttMs = 1000;
    const auto first = controller.Sample(500, hot);
    CHECK(first.has_value());
    CHECK(first->byteWindowTargetBytes <= initial.byteWindowTargetBytes * 5 / 4);
    CHECK(!controller.Sample(600, hot).has_value());
}

void TestVbrBudget() {
    using namespace FFFPlayer::Io;
    VbrInputs in{};
    in.declaredAverageBps = 200'000'000;
    in.declaredPeakBps = 240'000'000;
    in.measuredAverageBps = 200'000'000;
    in.measuredP95Bps = 250'000'000;
    in.goodputBps = 400'000'000;
    in.levelMaxBps = 240'000'000;
    in.rttMs = 300;
    in.segmentDurationMs = 2000;
    in.gopDurationMs = 2000;
    in.lookAheadMs = 6000;
    auto b = ComputeVbrBudget(in);
    CHECK(b.admissionPeakBps == 275'000'000);          // measured p95 x 1.10
    CHECK(b.steadyPeakBps == 275'000'000);
    CHECK(b.burstWindowMs == 2000);
    CHECK(b.burstBytes == 68'750'000);                  // 2 s at admission peak
    CHECK(b.routeBytes > 150ull * MiB);
    CHECK(!b.routeBudgetInsufficient);
    CHECK(b.levelViolation);                            // 275 > declared 240
    CHECK(!b.manifestMismatch);
    CHECK(b.packetForwardBytes <= kPacketForwardMaxBytes);

    // A 30 s RTT segmented source cannot be made continuous with three
    // prefetches: the formula exposes the shortfall instead of a safe-looking
    // small cache.
    in.segmented = true;
    in.rttMs = 30000;
    in.goodputBps = 600'000'000;
    const auto segmented = ComputeVbrBudget(in);
    CHECK(segmented.requiredParallel >= 20);
    CHECK(segmented.routeBudgetInsufficient);
    CHECK(segmented.routeBytes == kVbrRouteCeilingBytes);

    // Level is not a cache target. A low level with a high measured peak keeps
    // the measured network budget and only raises the decoder flag.
    in.segmented = false;
    in.rttMs = 300;
    in.declaredPeakBps = 20'000'000;
    in.declaredAverageBps = 20'000'000;
    in.levelMaxBps = 20'000'000;
    const auto lowLevel = ComputeVbrBudget(in);
    CHECK(lowLevel.admissionPeakBps >= 250'000'000);
    CHECK(lowLevel.levelViolation);
    CHECK(lowLevel.manifestMismatch);                   // measured > declared x1.30

    // Unknown bitrate still has a bounded packet queue; it must never be zero.
    VbrInputs unknown{};
    const auto empty = ComputeVbrBudget(unknown);
    CHECK(empty.packetForwardBytes >= 4ull * MiB);
    CHECK(empty.packetCacheBytes >= 2ull * MiB);
    CHECK(empty.routeBytes >= kVbrRouteFloorBytes);
}

} // namespace

int main() {
    TestDeadline();
    TestBudgetMath();
    TestReadLedger();
    TestProgressWatchdog();
    TestBudgetController();
    TestVbrBudget();
    TestContracts();
    if (failures != 0) {
        std::printf("FAILED %d checks\n", failures);
        return 1;
    }
    std::puts("PASS I/O 2.0 contract arithmetic and state machine");
    return 0;
}
