#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

// Deadline arithmetic for I/O 2.0. Every function is pure and saturating:
// malformed inputs (RTT=0, goodput=0, huge byte counts) must produce a finite,
// usable deadline rather than wrapping to "immediate timeout".
namespace FFFPlayer::Io {

inline constexpr std::uint32_t kMinuteMs = 60u * 1000u;
inline constexpr std::uint32_t kCorrectnessOpenDeadlineMs = 180u * 1000u;
inline constexpr std::uint32_t kCorrectnessSeekDeadlineMs = 60u * 1000u;

constexpr std::uint64_t SaturatingMultiply(std::uint64_t value, std::uint64_t factor) noexcept {
    if (factor == 0) return 0;
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    return value > max / factor ? max : value * factor;
}

constexpr std::uint64_t CeilingDivide(std::uint64_t value, std::uint64_t divisor) noexcept {
    return divisor == 0 ? value : value / divisor + (value % divisor != 0 ? 1u : 0u);
}

constexpr std::uint32_t ClampMs(std::uint64_t value, std::uint32_t minimum,
    std::uint32_t maximum) noexcept {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return static_cast<std::uint32_t>(value);
}

// 2 s .. 30 s; a slow but valid read is never mistaken for EOF.
constexpr std::uint32_t ReadIdleTimeoutMs(std::uint32_t rttMs,
    std::uint32_t chunkTimeMs) noexcept {
    const auto raw = SaturatingMultiply(3, rttMs) + SaturatingMultiply(2, chunkTimeMs);
    return ClampMs(std::max<std::uint64_t>(2000, raw), 2000, 30000);
}

// 5 s .. 60 s. `probeBytes / goodput` is the part the probe itself needs.
constexpr std::uint32_t OpenDeadlineMs(std::uint32_t rttMs, std::uint64_t probeBytes,
    std::uint64_t goodputBps) noexcept {
    const auto transferMs = goodputBps == 0
        ? (std::numeric_limits<std::uint64_t>::max)()
        : CeilingDivide(SaturatingMultiply(probeBytes, 1000), goodputBps);
    const auto raw = SaturatingMultiply(4, rttMs) + transferMs;
    return ClampMs(std::max<std::uint64_t>(5000, raw), 5000, 60000);
}

// 3 s .. 30 s. The 2*GOP term covers the demuxer re-preroll, not the seek itself.
constexpr std::uint32_t SeekDeadlineMs(std::uint32_t rttMs, std::uint64_t gopBytes,
    std::uint64_t goodputBps) noexcept {
    const auto transferMs = goodputBps == 0
        ? (std::numeric_limits<std::uint64_t>::max)()
        : CeilingDivide(SaturatingMultiply(SaturatingMultiply(gopBytes, 2), 1000), goodputBps);
    const auto raw = SaturatingMultiply(4, rttMs) + transferMs;
    return ClampMs(std::max<std::uint64_t>(3000, raw), 3000, 30000);
}

// 30 s .. 120 s. This is the watchdog that only produces Stalled, never Failed.
constexpr std::uint32_t StallTimeoutMs(std::uint32_t rttMs) noexcept {
    const auto raw = std::max<std::uint64_t>(30000, SaturatingMultiply(4, rttMs));
    return ClampMs(raw, 30000, 120000);
}

// Waiting-network threshold. It is a UI/backoff signal, not a terminal state.
constexpr std::uint64_t HardStallMs(std::uint32_t rttMs) noexcept {
    return std::max<std::uint64_t>(5 * kMinuteMs, SaturatingMultiply(10, rttMs));
}

// Correctness-first mode deliberately allows longer opens/seeks. It still has a
// finite UI deadline so a dead source cannot freeze the command thread forever.
constexpr std::uint32_t OperationDeadlineMs(bool correctnessFirst,
    std::uint32_t computedMs, std::uint32_t correctnessMs) noexcept {
    return correctnessFirst ? std::max(computedMs, correctnessMs) : computedMs;
}

constexpr std::uint32_t RetryBackoffMs(std::uint32_t attempt) noexcept {
    const auto exponent = std::min<std::uint32_t>(attempt, 7);
    const auto raw = SaturatingMultiply(250, 1ull << exponent);
    return ClampMs(raw, 250, 30000);
}

constexpr std::uint32_t LatencyClass(std::uint32_t rttMs) noexcept {
    if (rttMs < 50) return 0;       // L0
    if (rttMs <= 300) return 1;     // L1
    if (rttMs <= 2000) return 2;    // L2
    if (rttMs <= 30000) return 3;   // L3
    return 4;                       // L4
}

constexpr std::uint32_t ContinuousReadWindowMs(std::uint32_t rttMs) noexcept {
    return ClampMs(std::max<std::uint64_t>(10000, SaturatingMultiply(2, rttMs)),
        10000, (std::numeric_limits<std::uint32_t>::max)());
}

} // namespace FFFPlayer::Io
