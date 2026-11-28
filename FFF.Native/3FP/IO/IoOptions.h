#pragma once

#include "IoStatus.h"

#include <cstdint>

namespace FFFPlayer::Io {

enum class IoMode : std::uint8_t {
    CorrectnessFirst = 0,
    Realtime = 1,
};

struct IoOptions final {
    IoMode mode = IoMode::CorrectnessFirst;
    std::uint32_t latencyClass = 0; // 0=L0 .. 4=L4
    std::uint32_t readIdleTimeoutMs = 30000;
    std::uint32_t openDeadlineMs = 60000;
    std::uint32_t seekDeadlineMs = 30000;
    std::uint64_t maxBytesInFlight = 64ull * 1024ull * 1024ull;
    bool allowReconnect = true;
    bool allowDegrade = false;
    bool allowRealtimeUdpFallback = false;
};

} // namespace FFFPlayer::Io
