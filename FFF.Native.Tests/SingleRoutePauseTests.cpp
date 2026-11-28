#include "3FP/Api/FFF.Player.Api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <windows.h>
#include <psapi.h>

using Clock = std::chrono::steady_clock;

namespace {
FFF3FPHandle CreatePlayer(const bool gpu) {
    FFF3FPConfiguration config{};
    config.size = sizeof(config);
    config.version = 20;
    config.decodeMode = gpu ? FFF3FPDecodeMode::Gpu : FFF3FPDecodeMode::Cpu;
    config.colorMode = FFF3FPColorMode::MapToSdr;
    config.sdrPeakNits = 100.0f;
    config.hdrPeakNits = 1000.0f;
    config.sdrPaperWhiteNits = 100.0f;
    config.preferredAdapterIndex = -1;
    FFF3FPHandle player = nullptr;
    return FFF3FP_Create(&config, &player) == FFFResult::Success ? player : nullptr;
}

FFFResult OpenHls(FFF3FPHandle player, const std::string& url) {
    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoAllowNetwork | FFF3FPIoCorrectnessFirst;
    options.openDeadlineMs = 30000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 10000;
    return FFF3FP_OpenEx(player, url.c_str(), &options);
}

FFF3FPSnapshot Snapshot(FFF3FPHandle player) {
    FFF3FPSnapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    FFF3FP_GetSnapshot(player, &snapshot);
    return snapshot;
}

bool WaitState(FFF3FPHandle player, FFF3FPState wanted, const int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        const auto snapshot = Snapshot(player);
        if (snapshot.state == wanted) return true;
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void PrintMemory(const char* label, FFF3FPHandle player) {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    const auto snapshot = Snapshot(player);
    std::printf("MEM label=%s private=%.1fMiB ws=%.1fMiB handles=%lu state=%u "
        "decode=%u queued=%u presented=%llu dropped=%llu\n",
        label, static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0),
        static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0),
        static_cast<unsigned long>(handles),
        static_cast<unsigned>(snapshot.state), static_cast<unsigned>(snapshot.decodeMode),
        snapshot.queuedVideoFrames,
        static_cast<unsigned long long>(snapshot.presentedVideoFrames),
        static_cast<unsigned long long>(snapshot.droppedVideoFrames));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string url = argv[1];
    const bool gpu = argc > 2 && std::string(argv[2]) == "gpu";
    const int playSeconds = argc > 3 ? (std::max)(1, std::atoi(argv[3])) : 3;
    const int pauseSeconds = argc > 4 ? (std::max)(1, std::atoi(argv[4])) : 2;
    auto* player = CreatePlayer(gpu);
    if (player == nullptr) return 1;
    const auto openStart = Clock::now();
    if (OpenHls(player, url) != FFFResult::Success || !WaitState(player, FFF3FPState::Ready, 30000)) {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    const auto openMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - openStart).count();
    std::printf("OPEN ok=%lldms\n", static_cast<long long>(openMs));
    PrintMemory("ready", player);
    if (FFF3FP_Play(player) != FFFResult::Success || !WaitState(player, FFF3FPState::Playing, 2000)) {
        std::fprintf(stderr, "play failed\n");
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(playSeconds));
    PrintMemory("playing", player);

    const auto pauseStart = Clock::now();
    if (FFF3FP_Pause(player) != FFFResult::Success || !WaitState(player, FFF3FPState::Paused, 2000)) {
        std::fprintf(stderr, "pause failed\n");
        return 1;
    }
    const auto pauseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - pauseStart).count();
    PrintMemory("paused", player);
    std::this_thread::sleep_for(std::chrono::seconds(pauseSeconds));
    PrintMemory("paused_hold", player);

    const auto resumeStart = Clock::now();
    if (FFF3FP_Play(player) != FFFResult::Success || !WaitState(player, FFF3FPState::Playing, 3000)) {
        std::fprintf(stderr, "resume failed\n");
        return 1;
    }
    const auto resumeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - resumeStart).count();
    PrintMemory("resumed", player);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    PrintMemory("resumed_hold", player);
    std::printf("RESULT openMs=%lld pauseMs=%lld resumeMs=%lld\n",
        static_cast<long long>(openMs), static_cast<long long>(pauseMs),
        static_cast<long long>(resumeMs));
    FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    return 0;
}
