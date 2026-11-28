#include "3FP/Api/FFF.Player.Api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

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
    return FFF3FP_OpenEx(player, url.c_str(), &options);
}

bool WaitReady(FFF3FPHandle player) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        FFF3FPSnapshot snapshot{};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        if (FFF3FP_GetSnapshot(player, &snapshot) == FFFResult::Success &&
            (snapshot.state == FFF3FPState::Ready || snapshot.state == FFF3FPState::Playing)) {
            return snapshot.videoWidth != 0;
        }
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    const std::string url = argc > 1 ? argv[1]
        : "http://127.0.0.1:18866/hls-test/playlist.m3u8";
    const int routeCount = argc > 2 ? std::max(1, std::min(10, std::atoi(argv[2]))) : 9;
    const bool gpu = argc > 3 && std::string(argv[3]) == "gpu";
    const int playSeconds = argc > 4 ? std::max(1, std::atoi(argv[4])) : 2;
    std::vector<FFF3FPHandle> players;
    for (int i = 0; i < routeCount; ++i) {
        auto* player = CreatePlayer(gpu);
        if (player == nullptr || OpenHls(player, url) != FFFResult::Success || !WaitReady(player)) {
            std::fprintf(stderr, "route %d did not become ready (gpu=%d)\n", i, gpu ? 1 : 0);
            return 1;
        }
        players.push_back(player);
    }
    if (routeCount == 9) {
        auto* tenth = CreatePlayer(gpu);
        if (OpenHls(tenth, url) != FFFResult::ResourceExhausted) {
            std::fprintf(stderr, "tenth route was not rejected\n");
            return 1;
        }
        FFF3FP_Destroy(tenth);
    }
    for (auto* player : players) FFF3FP_Play(player);
    std::this_thread::sleep_for(std::chrono::seconds(playSeconds));
    int active = 0;
    int gpuRoutes = 0;
    int width = 0;
    for (auto* player : players) {
        FFF3FPSnapshot snapshot{};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        if (FFF3FP_GetSnapshot(player, &snapshot) == FFFResult::Success) {
            if (snapshot.presentedVideoFrames > 0) ++active;
            if (snapshot.decodeMode == FFF3FPDecodeMode::Gpu) ++gpuRoutes;
            width = static_cast<int>(snapshot.videoWidth);
        }
    }
    std::printf("nine-route active=%d/%d gpu=%d/%d width=%d\n", active, routeCount,
        gpuRoutes, routeCount, width);
    if (active < routeCount) return 1;
    for (auto* player : players) FFF3FP_Close(player);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (auto* player : players) FFF3FP_Destroy(player);
    auto* after = CreatePlayer(gpu);
    if (after == nullptr || OpenHls(after, url) != FFFResult::Success || !WaitReady(after)) {
        std::fprintf(stderr, "route release did not free admission\n");
        return 1;
    }
    FFF3FP_Close(after);
    FFF3FP_Destroy(after);
    std::printf("PASS nine-route stress and release gpu=%d count=%d\n", gpu ? 1 : 0, routeCount);
    return 0;
}
