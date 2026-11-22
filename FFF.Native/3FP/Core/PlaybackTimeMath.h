#pragma once

#include <cstdint>
#include <limits>

namespace FFFPlaybackTime {

constexpr std::int64_t SaturatingAddInt64(std::int64_t left, std::int64_t right) noexcept {
    constexpr auto min = (std::numeric_limits<std::int64_t>::min)();
    constexpr auto max = (std::numeric_limits<std::int64_t>::max)();
    if (right > 0 && left > max - right) return max;
    if (right < 0 && left < min - right) return min;
    return left + right;
}

constexpr std::int64_t SaturatingSubInt64(std::int64_t left, std::int64_t right) noexcept {
    constexpr auto min = (std::numeric_limits<std::int64_t>::min)();
    constexpr auto max = (std::numeric_limits<std::int64_t>::max)();
    if (right > 0 && left < min + right) return min;
    if (right < 0 && left > max + right) return max;
    return left - right;
}

}
