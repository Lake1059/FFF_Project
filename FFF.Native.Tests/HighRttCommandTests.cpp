#include "3FP/Api/FFF.Player.Api.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

using Clock = std::chrono::steady_clock;

namespace {
std::mutex g_eventMutex;
std::string g_lastEvent;
std::atomic<bool> g_seekUnavailable{false};

void EventThunk(void*, FFF3FPEvent type, const char* detail) {
    std::lock_guard lock(g_eventMutex);
    g_lastEvent = detail == nullptr ? std::string() : detail;
    if (type == FFF3FPEvent::OperationCompleted && g_lastEvent.find("seek-unavailable") != std::string::npos)
        g_seekUnavailable.store(true);
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

void WriteLatency(const char* path, const char* value) {
    std::ofstream out(path, std::ios::trunc);
    out << value;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const char* latencyFile = argv[2];
    WriteLatency(latencyFile, "30");
    FFF3FPConfiguration config{};
    config.size = sizeof(config);
    config.version = 20;
    config.decodeMode = FFF3FPDecodeMode::Cpu;
    config.colorMode = FFF3FPColorMode::MapToSdr;
    config.sdrPeakNits = 100.0f;
    config.hdrPeakNits = 1000.0f;
    config.sdrPaperWhiteNits = 100.0f;
    config.preferredAdapterIndex = -1;
    config.eventCallback = EventThunk;
    FFF3FPHandle player = nullptr;
    if (FFF3FP_Create(&config, &player) != FFFResult::Success || player == nullptr) return 1;

    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoAllowNetwork | FFF3FPIoCorrectnessFirst;
    options.openDeadlineMs = 180000;
    options.readIdleTimeoutMs = 60000;
    options.seekDeadlineMs = 30000;
    const auto openStart = Clock::now();
    if (FFF3FP_OpenEx(player, argv[1], &options) != FFFResult::Success ||
        !WaitState(player, FFF3FPState::Ready, 180000)) {
        std::fprintf(stderr, "open at 30 s RTT failed\n");
        return 1;
    }
    const auto openMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - openStart).count();
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto seekStart = Clock::now();
    if (FFF3FP_Seek(player, 20ll * 10'000'000ll) != FFFResult::Success) {
        std::fprintf(stderr, "seek request refused before queue\n");
        return 1;
    }
    bool rejected = false;
    for (int i = 0; i < 100; ++i) {
        if (g_seekUnavailable.load()) { rejected = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto seekMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - seekStart).count();
    if (!rejected) {
        std::fprintf(stderr, "30 s RTT seek did not reject promptly (%.0f ms)\n",
            static_cast<double>(seekMs));
        return 1;
    }
    const auto pauseStart = Clock::now();
    if (FFF3FP_Pause(player) != FFFResult::Success || !WaitState(player, FFF3FPState::Paused, 1000)) {
        std::fprintf(stderr, "pause blocked while seek was rejected\n");
        return 1;
    }
    const auto pauseMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - pauseStart).count();
    const auto closeStart = Clock::now();
    FFF3FP_Close(player);
    const auto closeMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - closeStart).count();
    FFF3FP_Destroy(player);
    if (seekMs > 1000 || pauseMs > 500 || closeMs > 700) {
        std::fprintf(stderr, "high-RTT command latency too high seek=%lld pause=%lld close=%lld\n",
            static_cast<long long>(seekMs), static_cast<long long>(pauseMs),
            static_cast<long long>(closeMs));
        return 1;
    }
    std::printf("PASS 30s RTT seek-unavailable open=%lldms seek=%lldms pause=%lldms close=%lldms\n",
        static_cast<long long>(openMs), static_cast<long long>(seekMs),
        static_cast<long long>(pauseMs), static_cast<long long>(closeMs));
    return 0;
}
