#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Api/FFF.Player.DecodeAdapter.h"
#include <cstdio>
int main() {
    std::printf("api=%u\n", FFF3FP_GetApiVersion());
    std::fflush(stdout);
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
    auto result = FFF3FP_Create(&config, &player);
    std::printf("create=%d player=%p\n", static_cast<int>(result), static_cast<void*>(player));
    std::fflush(stdout);
    int exitCode = result == FFFResult::Success && player != nullptr ? 0 : 1;
    if (exitCode == 0) {
        FFF3FPDecodeAdapterInfo info{};
        info.size = sizeof(info);
        info.version = FFF3FP_DECODE_ADAPTER_VERSION;
        const auto read = FFF3FP_GetDecodeAdapterInfo(player, &info);
        std::printf("cpuAdapter=%d size=%u version=%u hardware=%u index=%d\n",
            static_cast<int>(read), info.size, info.version, info.hardware, info.adapterIndex);
        if (read != FFFResult::Success || info.hardware != 0 || info.adapterIndex != -1) {
            std::printf("FAIL CPU decode adapter must report hardware=0 index=-1\n");
            exitCode = 1;
        }
        FFF3FPDecodeAdapterInfo badVersion{};
        badVersion.size = sizeof(badVersion);
        badVersion.version = FFF3FP_DECODE_ADAPTER_VERSION + 1;
        const auto badRead = FFF3FP_GetDecodeAdapterInfo(player, &badVersion);
        std::printf("badVersionAdapter=%d\n", static_cast<int>(badRead));
        if (badRead != FFFResult::InvalidArgument) exitCode = 1;
    }
    if (player) FFF3FP_Destroy(player);
    return exitCode;
}
