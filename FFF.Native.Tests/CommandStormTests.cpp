#include "3FP/Api/FFF.Player.Api.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

using Clock = std::chrono::steady_clock;

static bool Snapshot(FFF3FPHandle player, FFF3FPSnapshot& snapshot) {
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    return FFF3FP_GetSnapshot(player, &snapshot) == FFFResult::Success;
}

static bool WaitState(FFF3FPHandle player, FFF3FPState wanted, int timeoutMs,
    FFF3FPSnapshot* out = nullptr) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (out != nullptr) *out = snapshot;
        if (snapshot.state == wanted) return true;
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static bool WaitPosition(FFF3FPHandle player, std::int64_t low, std::int64_t high,
    int timeoutMs, FFF3FPSnapshot* out = nullptr) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (out != nullptr) *out = snapshot;
        if (snapshot.state == FFF3FPState::Failed) return false;
        if (snapshot.position100ns >= low && snapshot.position100ns <= high) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const int seekCount = argc >= 3 ? std::atoi(argv[2]) : 300;
    FFF3FPConfiguration config{};
    config.size = sizeof(config);
    config.version = 20;
    config.decodeMode = FFF3FPDecodeMode::Cpu;
    config.colorMode = FFF3FPColorMode::MapToSdr;
    config.sdrPeakNits = 100.0f;
    config.hdrPeakNits = 1000.0f;
    config.sdrPaperWhiteNits = 100.0f;
    config.preferredAdapterIndex = -1;
    FFF3FPHandle player = nullptr;
    if (FFF3FP_Create(&config, &player) != FFFResult::Success || player == nullptr) return 1;

    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoAllowNetwork | FFF3FPIoCorrectnessFirst;
    options.openDeadlineMs = 60000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 30000;
    if (FFF3FP_OpenEx(player, argv[1], &options) != FFFResult::Success ||
        !WaitState(player, FFF3FPState::Ready, 30000)) {
        std::fprintf(stderr, "open failed\n");
        FFF3FP_Destroy(player);
        return 1;
    }
    FFF3FPSnapshot initial{};
    if (!Snapshot(player, initial)) return 1;
    if (argc >= 4 && std::strcmp(argv[3], "eof") == 0) {
        // The last seek target is exactly the media duration. There is no
        // packet at that timestamp, so the session must publish Ended (or the
        // clamped end position) without leaving a seek pump spinning.
        if (FFF3FP_Play(player) != FFFResult::Success) return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (FFF3FP_Pause(player) != FFFResult::Success ||
            !WaitState(player, FFF3FPState::Paused, 5000)) {
            std::fprintf(stderr, "eof pause failed\n");
            return 1;
        }
        if (FFF3FP_Seek(player, initial.duration100ns) != FFFResult::Success) {
            std::fprintf(stderr, "eof seek rejected\n");
            return 1;
        }
        bool settled = false;
        FFF3FPSnapshot atEnd{};
        const auto eofDeadline = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < eofDeadline) {
            if (!Snapshot(player, atEnd)) break;
            if (atEnd.state == FFF3FPState::Ended ||
                atEnd.position100ns >= initial.duration100ns - 1000000) {
                settled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto closeStart = Clock::now();
        FFF3FP_Close(player);
        const bool closed = WaitState(player, FFF3FPState::Closed, 3000);
        const auto closeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - closeStart).count();
        FFF3FP_Destroy(player);
        if (!settled || !closed || closeMs > 1000) {
            std::fprintf(stderr,
                "eof seek did not settle settled=%d state=%u pos=%.3fs closed=%d close=%lldms\n",
                settled ? 1 : 0, static_cast<unsigned>(atEnd.state),
                atEnd.position100ns / 10'000'000.0, closed ? 1 : 0,
                static_cast<long long>(closeMs));
            return 1;
        }
        std::printf("PASS eof seek state=%u position=%.3fs close=%lldms\n",
            static_cast<unsigned>(atEnd.state),
            atEnd.position100ns / 10'000'000.0, static_cast<long long>(closeMs));
        return 0;
    }
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // A slider drag emits far more targets than the worker can execute. All
    // calls must be accepted (coalesced), not queued as hundreds of seeks.
    const auto stormStart = Clock::now();
    for (int i = 0; i < seekCount; ++i) {
        const auto target = (i & 1) ? 55ll * 10'000'000ll : 5ll * 10'000'000ll;
        if (FFF3FP_Seek(player, target) != FFFResult::Success) {
            std::fprintf(stderr, "seek storm rejected at %d\n", i);
            return 1;
        }
    }
    if (FFF3FP_Seek(player, 30ll * 10'000'000ll) != FFFResult::Success) return 1;
    const auto stormMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - stormStart).count();
    const bool stalled = argc >= 4 && std::strcmp(argv[3], "stall") == 0;
    if (stalled) {
        // The origin has sent headers plus a few bytes for a Range request and
        // then stopped. Close must abort the blocked reader from the caller
        // thread; waiting for the normal read budget would take 30 s.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto closeStart = Clock::now();
        FFF3FP_Close(player);
        const bool closed = WaitState(player, FFF3FPState::Closed, 5000);
        const auto closeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - closeStart).count();
        FFF3FP_Destroy(player);
        if (!closed || closeMs > 1500) {
            std::fprintf(stderr, "stalled close did not settle closed=%d close=%lldms\n",
                closed ? 1 : 0, static_cast<long long>(closeMs));
            return 1;
        }
        std::printf("PASS stalled close seeks=%d close=%lldms\n",
            seekCount, static_cast<long long>(closeMs));
        return 0;
    }
    FFF3FPSnapshot landed{};
    if (!WaitPosition(player, 29ll * 10'000'000ll, 31ll * 10'000'000ll, 5000, &landed)) {
        std::fprintf(stderr, "storm final target did not land\n");
        return 1;
    }

    // Pause/Play churn must not leave the state machine stuck between commands.
    for (int i = 0; i < 20; ++i) {
        if (FFF3FP_Pause(player) != FFFResult::Success ||
            !WaitState(player, FFF3FPState::Paused, 500)) {
            std::fprintf(stderr, "pause churn failed at %d\n", i);
            return 1;
        }
        if (FFF3FP_Play(player) != FFFResult::Success ||
            !WaitState(player, FFF3FPState::Playing, 500)) {
            std::fprintf(stderr, "play churn failed at %d\n", i);
            return 1;
        }
    }

    // Close while a seek is in flight: the cancel generation must let the
    // worker leave the seek pump and acknowledge Idle promptly.
    if (FFF3FP_Pause(player) != FFFResult::Success ||
        !WaitState(player, FFF3FPState::Paused, 500)) return 1;
    if (FFF3FP_Seek(player, 55ll * 10'000'000ll) != FFFResult::Success) return 1;
    const auto closeStart = Clock::now();
    FFF3FP_Close(player);
    const bool closed = WaitState(player, FFF3FPState::Closed, 3000);
    const auto closeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - closeStart).count();
    FFF3FP_Destroy(player);
    if (!closed || closeMs > 1000) {
        std::fprintf(stderr, "close during seek did not settle closed=%d close=%lldms\n",
            closed ? 1 : 0, static_cast<long long>(closeMs));
        return 1;
    }
    std::printf("PASS command storm seeks=%d storm=%lldms final=%.3fs close=%lldms\n",
        seekCount, static_cast<long long>(stormMs),
        landed.position100ns / 10'000'000.0, static_cast<long long>(closeMs));
    return 0;
}
