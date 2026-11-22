#include "3FP/Core/PlaybackTimeMath.h"
#include <cstdio>

using FFFPlaybackTime::SaturatingAddInt64;
using FFFPlaybackTime::SaturatingSubInt64;

constexpr auto Min = (std::numeric_limits<std::int64_t>::min)();
constexpr auto Max = (std::numeric_limits<std::int64_t>::max)();

static_assert(SaturatingAddInt64(12, 30) == 42);
static_assert(SaturatingAddInt64(Max, 1) == Max);
static_assert(SaturatingAddInt64(Min, -1) == Min);
static_assert(SaturatingAddInt64(Max, Min) == -1);
static_assert(SaturatingSubInt64(42, 30) == 12);
static_assert(SaturatingSubInt64(Min, 1) == Min);
static_assert(SaturatingSubInt64(Max, -1) == Max);
static_assert(SaturatingSubInt64(Min, Min) == 0);
static_assert(SaturatingSubInt64(0, Min) == Max);

int main() {
    std::puts("PASS playback timestamp arithmetic boundaries");
}
