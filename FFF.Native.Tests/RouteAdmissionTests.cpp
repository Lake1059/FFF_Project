#include "3FP/Api/FFF.Player.Api.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
FFF3FPHandle CreatePlayer() {
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
    return FFF3FP_Create(&config, &player) == FFFResult::Success ? player : nullptr;
}

FFFResult OpenLocal(FFF3FPHandle player) {
    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoCorrectnessFirst;
    options.openDeadlineMs = 15000;
    return FFF3FP_OpenEx(player, "C:\\PLAN\\3FCompare\\test\\assets\\video-synthetic\\test.mp4", &options);
}
} // namespace

int main() {
    std::vector<FFF3FPHandle> players;
    for (int i = 0; i < 9; ++i) {
        auto* player = CreatePlayer();
        if (player == nullptr || OpenLocal(player) != FFFResult::Success) {
            std::fprintf(stderr, "route %d open failed\n", i);
            return 1;
        }
        players.push_back(player);
    }
    auto* tenth = CreatePlayer();
    const auto rejected = OpenLocal(tenth);
    std::printf("tenth=%d\n", static_cast<int>(rejected));
    if (rejected != FFFResult::ResourceExhausted) return 1;
    FFF3FP_Destroy(tenth);
    for (auto* player : players) FFF3FP_Close(player);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    for (auto* player : players) FFF3FP_Destroy(player);
    auto* afterRelease = CreatePlayer();
    const auto reopened = OpenLocal(afterRelease);
    std::printf("after-release=%d\n", static_cast<int>(reopened));
    if (reopened != FFFResult::Success) return 1;
    FFF3FP_Close(afterRelease);
    FFF3FP_Destroy(afterRelease);
    std::printf("PASS nine-route admission and release\n");
    return 0;
}
