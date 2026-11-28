#include "3FP/Api/FFF.Player.Api.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <windows.h>

using Clock = std::chrono::steady_clock;

namespace {
bool Snapshot(FFF3FPHandle player, FFF3FPSnapshot& snapshot) {
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    return FFF3FP_GetSnapshot(player, &snapshot) == FFFResult::Success;
}

bool WaitState(FFF3FPHandle player, FFF3FPState wanted, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (snapshot.state == wanted) return true;
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool WaitPosition(FFF3FPHandle player, std::int64_t low, std::int64_t high,
    int timeoutMs, std::int64_t& generation) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (snapshot.state == FFF3FPState::Failed) return false;
        if (snapshot.position100ns >= low && snapshot.position100ns <= high) {
            generation = static_cast<std::int64_t>(snapshot.timelineGeneration);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool WaitFrames(FFF3FPHandle player, std::uint64_t baseline, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (snapshot.state == FFF3FPState::Failed) return false;
        if (snapshot.presentedVideoFrames > baseline) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    SetEnvironmentVariableW(L"FFF_SEEK_TRACE", L"1");
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
    if (FFF3FP_Create(&config, &player) != FFFResult::Success || player == nullptr) {
        std::fprintf(stderr, "create failed\n");
        return 1;
    }
    std::fprintf(stderr, "created\n");
    if (FFF3FP_Open(player, argv[1]) != FFFResult::Success || !WaitState(player, FFF3FPState::Ready, 10000)) {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    if (FFF3FP_SetPlaybackRate(player, 5.0) != FFFResult::Success) {
        std::fprintf(stderr, "rate failed\n");
        return 1;
    }
    std::fprintf(stderr, "rate ok\n");
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    std::int64_t generation = 0;
    if (!WaitPosition(player, 20ll * 10'000'000ll, 60ll * 10'000'000ll, 10000, generation)) {
        std::fprintf(stderr, "did not reach 20 s\n");
        return 1;
    }
    if (FFF3FP_Pause(player) != FFFResult::Success || !WaitState(player, FFF3FPState::Paused, 1000)) {
        std::fprintf(stderr, "pause failed\n");
        return 1;
    }
    FFF3FPSnapshot before{};
    if (!Snapshot(player, before)) return 1;
    const auto baselineFrames = before.presentedVideoFrames;

    const auto seekStart = Clock::now();
    if (FFF3FP_Seek(player, 10ll * 10'000'000ll) != FFFResult::Success ||
        !WaitPosition(player, 9ll * 10'000'000ll, 11ll * 10'000'000ll, 3000, generation) ||
        !WaitFrames(player, baselineFrames, 2000)) {
        std::fprintf(stderr, "backward seek did not land and present a new frame\n");
        return 1;
    }
    const auto seekMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - seekStart).count();
    if (seekMs > 1500) {
        std::fprintf(stderr, "cache seek too slow: %lld ms\n", static_cast<long long>(seekMs));
        return 1;
    }
    FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    std::printf("PASS cache-backed backward seek 20s->10s seek=%lldms generation=%lld\n",
        static_cast<long long>(seekMs), static_cast<long long>(generation));
    return 0;
}
