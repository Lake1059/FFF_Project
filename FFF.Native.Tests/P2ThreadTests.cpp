#include "3FP/Api/FFF.Player.Api.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>

using Clock = std::chrono::steady_clock;

static bool WaitForState(FFF3FPHandle player, FFF3FPState wanted, int timeoutMs,
    FFF3FPSnapshot* out = nullptr) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        FFF3FPSnapshot snapshot{};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        if (FFF3FP_GetSnapshot(player, &snapshot) != FFFResult::Success) return false;
        if (out != nullptr) *out = snapshot;
        if (snapshot.state == wanted) return true;
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
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
    if (FFF3FP_OpenEx(player, argv[1], &options) != FFFResult::Success) {
        FFF3FP_Destroy(player);
        return 1;
    }
    if (!WaitForState(player, FFF3FPState::Ready, 30000)) {
        std::fprintf(stderr, "not ready\n");
        FFF3FP_Destroy(player);
        return 1;
    }
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    const auto pauseStart = Clock::now();
    if (FFF3FP_Pause(player) != FFFResult::Success) return 1;
    if (!WaitForState(player, FFF3FPState::Paused, 1000)) {
        std::fprintf(stderr, "pause did not land\n");
        return 1;
    }
    const auto pauseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - pauseStart).count();

    const auto seekStart = Clock::now();
    if (FFF3FP_Seek(player, 20ll * 10'000'000ll) != FFFResult::Success) return 1;
    FFF3FPSnapshot snapshot{};
    bool seekLanded = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        FFF3FPSnapshot current{};
        current.size = sizeof(current);
        current.version = 23;
        if (FFF3FP_GetSnapshot(player, &current) != FFFResult::Success) return 1;
        if (current.state == FFF3FPState::Failed) return 1;
        if (current.position100ns >= 19ll * 10'000'000ll &&
            current.position100ns <= 21ll * 10'000'000ll) {
            snapshot = current;
            seekLanded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto seekMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - seekStart).count();

    const auto closeStart = Clock::now();
    FFF3FP_Close(player);
    const auto closeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - closeStart).count();
    FFF3FP_Destroy(player);
    if (!seekLanded) {
        std::fprintf(stderr, "seek did not land position=%lld\n",
            static_cast<long long>(snapshot.position100ns));
        return 1;
    }
    if (pauseMs > 500 || seekMs > 3000 || closeMs > 700) {
        std::fprintf(stderr, "command latency too high pause=%lld seek=%lld close=%lld\n",
            static_cast<long long>(pauseMs), static_cast<long long>(seekMs),
            static_cast<long long>(closeMs));
        return 1;
    }
    std::printf("PASS P2 threaded command handling pause=%lldms seek=%lldms close=%lldms\n",
        static_cast<long long>(pauseMs), static_cast<long long>(seekMs),
        static_cast<long long>(closeMs));
    return 0;
}
