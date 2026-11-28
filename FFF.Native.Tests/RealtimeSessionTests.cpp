#include "3FP/Api/FFF.Player.Api.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

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
    if (argc >= 4 && std::strcmp(argv[3], "udp") == 0)
        options.flags |= FFF3FPIoAllowRtspUdpFallback;
    options.openDeadlineMs = 30000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 30000;
    const auto opened = FFF3FP_OpenEx(player, argv[1], &options);
    if (opened != FFFResult::Success) {
        char error[512]{};
        std::uint32_t required = 0;
        FFF3FP_GetLastError(player, error, sizeof(error), &required);
        std::fprintf(stderr, "open failed result=%d error=%s\n", static_cast<int>(opened), error);
        FFF3FP_Destroy(player);
        return 1;
    }

    FFF3FPSnapshot snapshot{};
    for (int attempt = 0; attempt < 160; ++attempt) {
        snapshot = {};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        FFF3FP_GetSnapshot(player, &snapshot);
        if (snapshot.state == FFF3FPState::Ready || snapshot.state == FFF3FPState::Playing ||
            snapshot.state == FFF3FPState::Failed || snapshot.state == FFF3FPState::Ended) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::printf("state=%u size=%ux%u duration=%lld us\n",
        static_cast<unsigned>(snapshot.state), snapshot.videoWidth, snapshot.videoHeight,
        static_cast<long long>(snapshot.duration100ns / 10));
    if (snapshot.state == FFF3FPState::Failed || snapshot.videoWidth == 0) {
        char error[512]{};
        std::uint32_t required = 0;
        FFF3FP_GetLastError(player, error, sizeof(error), &required);
        std::fprintf(stderr, "realtime source failed: %s\n", error);
        return 1;
    }
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    for (int attempt = 0; attempt < 80; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        snapshot = {};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        FFF3FP_GetSnapshot(player, &snapshot);
        if (snapshot.presentedVideoFrames > 0) break;
    }
    std::printf("playing state=%u frames=%llu position=%lld us\n",
        static_cast<unsigned>(snapshot.state),
        static_cast<unsigned long long>(snapshot.presentedVideoFrames),
        static_cast<long long>(snapshot.position100ns / 10));
    if (snapshot.presentedVideoFrames == 0) return 1;
    FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    std::printf("PASS realtime source playback\n");
    return 0;
}
