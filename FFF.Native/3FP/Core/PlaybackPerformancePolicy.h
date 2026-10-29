#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// Session-local resource policy. Profile 0 preserves the existing adaptive pool;
// 1 doubles its headroom, 2 halves it and disables automatic pool growth.
// These bounds admit work; they do not allocate buffers up front.
struct PlaybackPerformancePolicy final {
    std::uint32_t video = 0;
    std::uint32_t audio = 0;

    std::size_t VideoBudget(std::size_t balanced) const noexcept {
        return video == 1 ? balanced * 2 : video == 2 ? balanced / 2 : balanced;
    }
    std::size_t VideoFrames(std::size_t balanced) const noexcept {
        return (std::max)(std::size_t{2}, VideoBudget(balanced));
    }
    std::int64_t VideoLookAhead() const noexcept {
        return video == 1 ? 3'000'000 : video == 2 ? 750'000 : 1'500'000;
    }
    std::int64_t AudioInitial() const noexcept {
        return audio == 1 ? 1'200'000 : audio == 2 ? 300'000 : 600'000;
    }
    std::int64_t AudioMaximum() const noexcept {
        return audio == 1 ? 4'800'000 : audio == 2 ? 300'000 : 2'400'000;
    }
    std::int64_t AudioMoveFloor() const noexcept {
        return audio == 1 ? 2'400'000 : audio == 2 ? 300'000 : 1'200'000;
    }
};
