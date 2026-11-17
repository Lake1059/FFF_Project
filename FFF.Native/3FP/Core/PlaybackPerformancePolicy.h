#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

// Session-local resource policy. Profile 0 preserves the existing adaptive pool;
// 1 doubles its headroom, 2 halves it and disables automatic pool growth.
// These bounds admit work; they do not allocate buffers up front.
//
// UNIT: every integer returned below is 100 ns (one hundred nanoseconds), the same
// base as PlayerSession's *100ns fields and the WASAPI hns* time base.
// The suffix is part of the contract, not decoration: a 2026-11-17 review read
// 1'500'000 as "1.5 s" (microseconds) when it is 150 ms, a 10x misread.
struct PlaybackPerformancePolicy final {
    std::uint32_t video = 0;
    std::uint32_t audio = 0;

    std::size_t VideoBudget(std::size_t balanced) const noexcept {
        return video == 1 ? balanced * 2 : video == 2 ? balanced / 2 : balanced;
    }
    std::size_t VideoFrames(std::size_t balanced) const noexcept {
        return (std::max)(std::size_t{2}, VideoBudget(balanced));
    }
    constexpr std::int64_t VideoLookAhead100ns() const noexcept {
        return video == 1 ? 3'000'000 : video == 2 ? 750'000 : 1'500'000;
    }
    constexpr std::int64_t AudioInitial100ns() const noexcept {
        return audio == 1 ? 1'200'000 : audio == 2 ? 300'000 : 600'000;
    }
    constexpr std::int64_t AudioMaximum100ns() const noexcept {
        return audio == 1 ? 4'800'000 : audio == 2 ? 300'000 : 2'400'000;
    }
    constexpr std::int64_t AudioMoveFloor100ns() const noexcept {
        return audio == 1 ? 2'400'000 : audio == 2 ? 300'000 : 1'200'000;
    }

    // Deprecated spellings: same values, no unit in the name. Kept for one release
    // so out-of-tree callers keep compiling; migrate to the *100ns forms.
    [[deprecated("use VideoLookAhead100ns(); the value is 100 ns, not microseconds")]]
    std::int64_t VideoLookAhead() const noexcept { return VideoLookAhead100ns(); }
    [[deprecated("use AudioInitial100ns(); the value is 100 ns, not microseconds")]]
    std::int64_t AudioInitial() const noexcept { return AudioInitial100ns(); }
    [[deprecated("use AudioMaximum100ns(); the value is 100 ns, not microseconds")]]
    std::int64_t AudioMaximum() const noexcept { return AudioMaximum100ns(); }
    [[deprecated("use AudioMoveFloor100ns(); the value is 100 ns, not microseconds")]]
    std::int64_t AudioMoveFloor() const noexcept { return AudioMoveFloor100ns(); }
};

// Pin the numbers the review is written against. If any branch is retuned, this
// trips at compile time and the docs/scheme that quote them must move with it.
static_assert(PlaybackPerformancePolicy{0u, 0u}.VideoLookAhead100ns() == 1'500'000,
    "profile 0 video look-ahead must stay 1'500'000 x 100 ns == 150 ms");
static_assert(PlaybackPerformancePolicy{0u, 0u}.AudioInitial100ns() == 600'000,
    "profile 0 audio initial target must stay 600'000 x 100 ns == 60 ms");
static_assert(PlaybackPerformancePolicy{0u, 0u}.AudioMaximum100ns() == 2'400'000,
    "profile 0 audio ceiling must stay 2'400'000 x 100 ns == 240 ms");
static_assert(PlaybackPerformancePolicy{0u, 0u}.AudioMoveFloor100ns() == 1'200'000,
    "profile 0 audio move floor must stay 1'200'000 x 100 ns == 120 ms");
static_assert(PlaybackPerformancePolicy{1u, 1u}.VideoLookAhead100ns() == 3'000'000,
    "profile 1 video look-ahead must stay 3'000'000 x 100 ns == 300 ms");
static_assert(PlaybackPerformancePolicy{2u, 2u}.AudioMaximum100ns() == 300'000,
    "profile 2 audio ceiling must stay 300'000 x 100 ns == 30 ms");
