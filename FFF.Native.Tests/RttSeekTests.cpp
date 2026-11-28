#include "3FP/Api/FFF.Player.Api.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;

namespace {
void WriteLatency(const char* path, double seconds) {
    std::ofstream out(path, std::ios::trunc);
    out << seconds;
}

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

bool WaitPosition(FFF3FPHandle player, std::int64_t low, std::int64_t high, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (snapshot.state == FFF3FPState::Failed) return false;
        if (snapshot.position100ns >= low && snapshot.position100ns <= high) return true;
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
    if (argc < 4) return 2;
    const double latency = std::atof(argv[3]);
    WriteLatency(argv[2], latency);
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
    if (FFF3FP_Create(&config, &player) != FFFResult::Success) return 1;
    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoAllowNetwork | FFF3FPIoCorrectnessFirst;
    options.openDeadlineMs = 120000;
    options.readIdleTimeoutMs = 60000;
    options.seekDeadlineMs = 30000;
    const auto openStart = Clock::now();
    if (FFF3FP_OpenEx(player, argv[1], &options) != FFFResult::Success ||
        !WaitState(player, FFF3FPState::Ready, 120000)) {
        std::fprintf(stderr, "open at %.1f s RTT failed\n", latency);
        return 1;
    }
    const auto openMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - openStart).count();
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    FFF3FPSnapshot before{};
    if (!Snapshot(player, before)) return 1;
    const auto baselineFrames = before.presentedVideoFrames;
    const auto seekStart = Clock::now();
    if (FFF3FP_Seek(player, 20ll * 10'000'000ll) != FFFResult::Success ||
        !WaitPosition(player, 19ll * 10'000'000ll, 21ll * 10'000'000ll,
            static_cast<int>(latency * 1000 * 4 + 5000)) ||
        !WaitFrames(player, baselineFrames, static_cast<int>(latency * 1000 * 2 + 3000))) {
        std::fprintf(stderr, "seek at %.1f s RTT did not land and present a new frame\n", latency);
        return 1;
    }
    const auto seekMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - seekStart).count();
    FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    std::printf("PASS %.1fs RTT frame seek open=%lldms seek=%lldms\n",
        latency, static_cast<long long>(openMs), static_cast<long long>(seekMs));
    return 0;
}
