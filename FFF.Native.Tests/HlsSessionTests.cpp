#include "3FP/Api/FFF.Player.Api.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
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
    FFF3FPIoOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.flags = FFF3FPIoAllowNetwork | FFF3FPIoCorrectnessFirst;
    options.openDeadlineMs = 30000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 30000;
    const std::string url = argc > 1 ? argv[1] : "http://127.0.0.1:18866/hls-test/playlist.m3u8";
    const bool live = argc > 2 && std::string(argv[2]) == "LIVE";
    const auto opened = FFF3FP_OpenEx(player, url.c_str(), &options);
    std::printf("open result=%d\n", static_cast<int>(opened));
    std::fflush(stdout);
    if (opened != FFFResult::Success) {
        char error[512]{};
        std::uint32_t required = 0;
        FFF3FP_GetLastError(player, error, sizeof(error), &required);
        char info[1024]{};
        FFF3FP_GetMediaInfo(player, info, sizeof(info), &required);
        std::fprintf(stderr, "open failed result=%d error=%s info=%s\n",
            static_cast<int>(opened), error, info);
        FFF3FP_Destroy(player);
        return 1;
    }
    FFF3FPSnapshot snapshot{};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    FFFResult snapResult = FFFResult::InvalidArgument;
    for (int attempt = 0; attempt < 120; ++attempt) {
        snapshot = {};
        snapshot.size = sizeof(snapshot);
        snapshot.version = 23;
        snapResult = FFF3FP_GetSnapshot(player, &snapshot);
        if (snapResult == FFFResult::Success &&
            (snapshot.state == FFF3FPState::Ready || snapshot.state == FFF3FPState::Playing ||
             snapshot.state == FFF3FPState::Failed || snapshot.state == FFF3FPState::Ended)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::printf("snapshot result=%d\n", static_cast<int>(snapResult));
    std::fflush(stdout);
    if (snapResult != FFFResult::Success) return 1;
    std::printf("state=%u duration=%lld us\n", static_cast<unsigned>(snapshot.state),
        static_cast<long long>(snapshot.duration100ns / 10));
    if (!live && (snapshot.duration100ns < 5'000'000 || snapshot.videoWidth == 0)) {
        std::fprintf(stderr, "unexpected snapshot width=%u duration=%lld\n",
            snapshot.videoWidth, static_cast<long long>(snapshot.duration100ns));
        return 1;
    }
    if (FFF3FP_Play(player) != FFFResult::Success) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    FFF3FP_GetSnapshot(player, &snapshot);
    std::printf("playing state=%u position=%lld us frames=%llu\n",
        static_cast<unsigned>(snapshot.state), static_cast<long long>(snapshot.position100ns / 10),
        static_cast<unsigned long long>(snapshot.presentedVideoFrames));
    if (!live && snapshot.position100ns <= 0) return 1;
    if (live) {
        FFF3FP_Close(player);
        FFF3FP_Destroy(player);
        std::printf("PASS %s live playback\n", argc > 2 ? argv[2] : "HLS");
        return 0;
    }
    if (FFF3FP_Seek(player, 3'000'0000ll) != FFFResult::Success) return 1;
    // A segmented source may have to open and range-read a new segment after
    // the seek. Give the virtual demuxer enough time for that network round trip;
    // this is a correctness wait, not the seek-command latency assertion.
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    FFF3FP_GetSnapshot(player, &snapshot);
    std::printf("seek state=%u position=%lld us\n", static_cast<unsigned>(snapshot.state),
        static_cast<long long>(snapshot.position100ns / 10));
    if (snapshot.position100ns < 2'500'0000ll) return 1;
    FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    std::printf("PASS %s PlayerSession playback\n", argc > 2 ? argv[2] : "HLS");
    return 0;
}
