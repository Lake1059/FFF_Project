#include "3FP/Api/FFF.Player.Api.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

static bool WaitState(FFF3FPHandle player, FFF3FPState wanted, int timeoutMs) {
    for (int i = 0; i < timeoutMs / 10; ++i) {
        FFF3FPSnapshot snapshot{};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        if (FFF3FP_GetSnapshot(player, &snapshot) != FFFResult::Success) return false;
        if (snapshot.state == wanted) return true;
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static FFF3FPHandle MakePlayer() {
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
    if (FFF3FP_Create(&config, &player) != FFFResult::Success) return nullptr;
    return player;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    SetEnvironmentVariableW(L"FFF_IO_ROUTE_CAPACITY_MB", L"256");
    FFF3FPHandle players[5]{};
    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoAllowNetwork | FFF3FPIoCorrectnessFirst;
    options.maxBytesInFlight = 64ull * 1024 * 1024;
    for (int i = 0; i < 4; ++i) {
        players[i] = MakePlayer();
        if (players[i] == nullptr) return 1;
        if (FFF3FP_OpenEx(players[i], argv[1], &options) != FFFResult::Success) return 1;
        if (!WaitState(players[i], FFF3FPState::Ready, 10000)) return 1;
    }
    players[4] = MakePlayer();
    if (players[4] == nullptr) return 1;
    const auto fifth = FFF3FP_OpenEx(players[4], argv[1], &options);
    if (fifth != FFFResult::ResourceExhausted) {
        std::fprintf(stderr, "dynamic budget fifth=%d\n", static_cast<int>(fifth));
        return 1;
    }
    FFF3FP_Close(players[0]);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    FFF3FP_Destroy(players[0]);
    players[0] = MakePlayer();
    if (players[0] == nullptr ||
        FFF3FP_OpenEx(players[0], argv[1], &options) != FFFResult::Success) return 1;
    if (!WaitState(players[0], FFF3FPState::Ready, 10000)) return 1;
    for (auto* player : players) {
        if (player != nullptr) {
            FFF3FP_Close(player);
            FFF3FP_Destroy(player);
        }
    }
    std::printf("PASS dynamic route budget capacity=256MB routes=4 fifth=%d\n",
        static_cast<int>(fifth));
    return 0;
}
