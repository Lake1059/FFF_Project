#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "Deadline.h"

// Dynamic I/O budget arithmetic. Header-only so a test can compile it without
// the Windows runtime, and so PlayerSession can call it without a new DLL dependency.
//
// All byte counts are bytes, all rates are bits/second, all times are
// milliseconds. Every multiplication saturates; a zero/unknown measurement
// falls back to the conservative floor instead of producing a zero-length window.
namespace FFFPlayer::Io {

inline constexpr std::uint64_t MiB = 1024ull * 1024ull;
inline constexpr std::uint64_t GiB = 1024ull * MiB;

inline constexpr std::uint64_t kAvioMinBytes = 64ull * 1024ull;
inline constexpr std::uint64_t kAvioMaxBytes = 1ull * MiB;
inline constexpr std::uint64_t kByteWindowMinBytes = 4ull * MiB;
inline constexpr std::uint64_t kByteWindowMaxBytes = 64ull * MiB;
inline constexpr std::uint64_t kGlobalByteWindowCeilingBytes = 192ull * MiB;
inline constexpr std::uint64_t kGlobalByteWindowCeilingLargeBytes = 384ull * MiB;
inline constexpr std::uint64_t kPacketForwardMaxBytes = 128ull * MiB;
inline constexpr std::uint64_t kPacketBackMaxBytes = 64ull * MiB;
inline constexpr std::uint64_t kSegmentCacheMaxBytes = 512ull * MiB;
inline constexpr std::uint64_t kPrefetchMinBytes = 64ull * MiB;
inline constexpr std::uint64_t kPrefetchMaxBytes = 1ull * GiB;
inline constexpr std::uint64_t kPrefetchNineWayCapBytes = 256ull * MiB;

constexpr std::uint64_t BitsToBytesForMs(std::uint64_t bitsPerSecond,
    std::uint64_t milliseconds) noexcept {
    return SaturatingMultiply(bitsPerSecond, milliseconds) / 8000ull;
}

constexpr std::uint64_t EffectiveGoodputBps(std::uint64_t measuredBps,
    std::uint64_t tcpReceiveWindowBytes, std::uint32_t rttMs) noexcept {
    if (rttMs == 0 || tcpReceiveWindowBytes == 0) return measuredBps;
    const auto tcpLimitedBps = SaturatingMultiply(tcpReceiveWindowBytes, 1000) / rttMs;
    return (std::min)(measuredBps, tcpLimitedBps);
}

constexpr std::uint64_t AvioBufferBytes(std::uint64_t bitrateBps) noexcept {
    const auto raw = BitsToBytesForMs(bitrateBps, 5);
    return std::clamp<std::uint64_t>(raw, kAvioMinBytes, kAvioMaxBytes);
}

constexpr std::uint64_t BdpBytes(std::uint64_t goodputBps, std::uint32_t rttMs) noexcept {
    return BitsToBytesForMs(goodputBps, rttMs);
}

constexpr std::uint64_t ByteWindowTargetBytes(std::uint64_t goodputBps,
    std::uint32_t rttMs, std::uint64_t perSessionCeiling = kByteWindowMaxBytes) noexcept {
    const auto bdp = BdpBytes(goodputBps, rttMs);
    const auto raw = bdp + 2 * MiB;
    const auto ceiling = (std::min)(perSessionCeiling, kByteWindowMaxBytes);
    const auto floor = (std::min)(kByteWindowMinBytes, ceiling);
    if (raw < floor) return floor;
    if (raw > ceiling) return ceiling;
    return raw;
}

constexpr std::uint32_t SegmentPrefetchCount(std::uint32_t rttMs,
    std::uint32_t segmentDurationMs, std::uint32_t cap = 4) noexcept {
    if (segmentDurationMs == 0) return 1;
    const auto segments = CeilingDivide(rttMs, segmentDurationMs) + 1;
    if (segments < 1) return 1;
    if (segments > cap) return cap;
    return static_cast<std::uint32_t>(segments);
}

constexpr std::uint32_t FrameQueueDepth(std::uint32_t decodeJitterMs,
    std::uint32_t presentJitterMs, std::uint32_t frameIntervalMs,
    std::uint32_t cap = 6) noexcept {
    if (frameIntervalMs == 0) return 2;
    const auto raw = CeilingDivide(
        static_cast<std::uint64_t>(decodeJitterMs) + presentJitterMs, frameIntervalMs) + 2;
    const auto bounded = std::clamp<std::uint64_t>(raw, 2, cap);
    return static_cast<std::uint32_t>(bounded);
}

constexpr std::uint64_t PrefetchBytes(std::uint64_t availablePhysicalBytes,
    bool nineWay = false) noexcept {
    const auto raw = availablePhysicalBytes / 10;
    const auto cap = nineWay ? kPrefetchNineWayCapBytes : kPrefetchMaxBytes;
    if (raw < kPrefetchMinBytes) return kPrefetchMinBytes;
    if (raw > cap) return cap;
    return raw;
}

constexpr std::uint64_t PacketForwardBytes(std::uint64_t bitrateBps,
    std::uint32_t rttMs, std::uint32_t decodeJitterMs) noexcept {
    const auto secondsMs = ClampMs(
        SaturatingMultiply(rttMs, 1) / 2 + decodeJitterMs, 1000, 10000);
    const auto raw = BitsToBytesForMs(bitrateBps, secondsMs);
    return (std::min)(raw, kPacketForwardMaxBytes);
}

// Backward packet caching is only worthwhile when one GOP fits in the cap and
// the source is not a very high-bitrate stream. Otherwise it is disabled.
constexpr std::uint64_t PacketBackBytes(std::uint64_t bitrateBps,
    std::uint64_t gopBytes) noexcept {
    if (bitrateBps > 50ull * 1000ull * 1000ull) return 0;
    if (gopBytes > kPacketBackMaxBytes) return 0;
    return gopBytes;
}

constexpr std::uint64_t ResumeWatermarkBytes(std::uint64_t bitrateBps,
    std::uint32_t rttMs, std::uint64_t windowCeilingBytes) noexcept {
    const auto oneSecond = BitsToBytesForMs(bitrateBps, 1000);
    const auto bdp = BitsToBytesForMs(bitrateBps, rttMs);
    const auto boundedBdp = (std::min)(bdp, windowCeilingBytes);
    return (std::max)({ oneSecond, 8 * MiB, boundedBdp });
}

constexpr std::uint64_t ContinuousPlaybackMarginBps(std::uint64_t bitrateBps) noexcept {
    const auto tenPercent = bitrateBps / 10;
    return (std::max)(tenPercent, 5ull * 1000ull * 1000ull);
}

constexpr bool CanPromiseContinuousPlayback(std::uint64_t averageGoodputBps,
    std::uint64_t bitrateBps, std::uint32_t rttMs, std::uint64_t windowCeilingBytes) noexcept {
    if (bitrateBps == 0) return true;
    if (averageGoodputBps < bitrateBps + ContinuousPlaybackMarginBps(bitrateBps)) return false;
    if (BitsToBytesForMs(bitrateBps, 1000) > windowCeilingBytes) return false;
    return BitsToBytesForMs(bitrateBps, rttMs) <= windowCeilingBytes;
}

constexpr std::uint64_t SegmentCacheTargetBytes(std::uint32_t rttMs,
    std::uint32_t segmentDurationMs, std::uint64_t bitrateBps,
    std::uint64_t maxBytes = kSegmentCacheMaxBytes) noexcept {
    const auto segments = SegmentPrefetchCount(rttMs, segmentDurationMs);
    const auto bytesPerSegment = BitsToBytesForMs(bitrateBps, segmentDurationMs);
    const auto raw = SaturatingMultiply(segments, bytesPerSegment);
    return (std::min)(raw, maxBytes);
}

struct BudgetInputs final {
    std::uint64_t bitrateBps = 0;
    std::uint64_t measuredGoodputBps = 0;
    std::uint64_t tcpReceiveWindowBytes = 0;
    std::uint32_t rttMs = 0;
    std::uint32_t chunkTimeMs = 0;
    std::uint32_t decodeJitterMs = 0;
    std::uint32_t presentJitterMs = 0;
    std::uint32_t frameIntervalMs = 0;
    std::uint32_t segmentDurationMs = 0;
    std::uint64_t gopBytes = 0;
    std::uint64_t availablePhysicalBytes = 0;
    std::uint64_t globalByteWindowCeilingBytes = kGlobalByteWindowCeilingBytes;
    std::uint32_t sessionCount = 1;
    std::uint32_t frameDepthCap = 6;
    bool nineWay = false;
};

struct SessionBudget final {
    std::uint64_t byteWindowTargetBytes = kByteWindowMinBytes;
    std::uint64_t byteWindowFloorBytes = kByteWindowMinBytes;
    std::uint64_t byteWindowCeilingBytes = kByteWindowMaxBytes;
    std::uint64_t avioBufferBytes = kAvioMinBytes;
    std::uint64_t packetForwardBytes = 0;
    std::uint64_t packetBackBytes = 0;
    std::uint64_t segmentCacheBytes = 0;
    std::uint64_t prefetchBytes = kPrefetchMinBytes;
    std::uint64_t resumeWatermarkBytes = 8 * MiB;
    std::uint32_t frameQueueDepth = 2;
    std::uint32_t readIdleTimeoutMs = 0;
    std::uint32_t openDeadlineMs = 0;
    std::uint32_t seekDeadlineMs = 0;
    std::uint32_t stallTimeoutMs = 0;
    std::uint64_t hardStallMs = 0;
    bool canPromiseContinuousPlayback = false;
};

constexpr SessionBudget ComputeSessionBudget(const BudgetInputs& in) noexcept {
    SessionBudget out{};
    const auto effectiveGoodput = EffectiveGoodputBps(
        in.measuredGoodputBps, in.tcpReceiveWindowBytes, in.rttMs);
    const auto perSessionGlobal = in.sessionCount == 0
        ? in.globalByteWindowCeilingBytes
        : in.globalByteWindowCeilingBytes / in.sessionCount;
    out.byteWindowCeilingBytes = (std::min)(kByteWindowMaxBytes, perSessionGlobal);
    out.byteWindowFloorBytes = (std::min)(kByteWindowMinBytes, out.byteWindowCeilingBytes);
    out.byteWindowTargetBytes = ByteWindowTargetBytes(
        effectiveGoodput, in.rttMs, out.byteWindowCeilingBytes);
    out.avioBufferBytes = AvioBufferBytes(in.bitrateBps);
    out.packetForwardBytes = PacketForwardBytes(
        in.bitrateBps, in.rttMs, in.decodeJitterMs);
    out.packetBackBytes = PacketBackBytes(in.bitrateBps, in.gopBytes);
    out.segmentCacheBytes = SegmentCacheTargetBytes(
        in.rttMs, in.segmentDurationMs, in.bitrateBps);
    out.prefetchBytes = PrefetchBytes(in.availablePhysicalBytes, in.nineWay);
    out.resumeWatermarkBytes = ResumeWatermarkBytes(
        in.bitrateBps, in.rttMs, out.byteWindowCeilingBytes);
    out.frameQueueDepth = FrameQueueDepth(
        in.decodeJitterMs, in.presentJitterMs, in.frameIntervalMs, in.frameDepthCap);
    out.readIdleTimeoutMs = ReadIdleTimeoutMs(in.rttMs, in.chunkTimeMs);
    out.openDeadlineMs = OpenDeadlineMs(in.rttMs, 128 * 1024, effectiveGoodput);
    out.seekDeadlineMs = SeekDeadlineMs(in.rttMs, in.gopBytes, effectiveGoodput);
    out.stallTimeoutMs = StallTimeoutMs(in.rttMs);
    out.hardStallMs = HardStallMs(in.rttMs);
    out.canPromiseContinuousPlayback = CanPromiseContinuousPlayback(
        effectiveGoodput, in.bitrateBps, in.rttMs, out.byteWindowCeilingBytes);
    return out;
}

// ---------------------------------------------------------------------------
// P0 VBR admission and cache sizing.
//
// `level` is a decoder conformance ceiling, not a network measurement. A VBR
// stream routinely has peak segments above its mean, and a 30 s RTT needs
// bytes that no `MaxCPB` describes. Therefore:
//   * admission uses the largest declared/measured peak with 10% measurement
//     headroom;
//   * steady state uses measured p95 with 10% and measured average with 35%
//     headroom;
//   * burst uses the 2 s / GOP / segment worst window with 12% safety;
//   * a declared level is only a validation signal (`levelViolation`), never
//     the cache target;
//   * HLS/DASH also compute the minimum parallel segment fetches; if the route
//     ceiling cannot hold that many segments, `routeBudgetInsufficient` is
//     explicit instead of silently under-buffering.
// ---------------------------------------------------------------------------
inline constexpr std::uint64_t kBitrateSafetyPercent = 112;
inline constexpr std::uint64_t kManifestMismatchPercent = 130;
inline constexpr std::uint64_t kDefaultLookAheadMs = 6000;
inline constexpr std::uint64_t kVbrRouteFloorBytes = 64ull * MiB;
inline constexpr std::uint64_t kVbrRouteCeilingBytes = 512ull * MiB;
inline constexpr std::uint32_t kMaxRequiredParallel = 64;

struct VbrInputs final {
    std::uint64_t declaredAverageBps = 0;
    std::uint64_t declaredPeakBps = 0;
    std::uint64_t measuredAverageBps = 0;
    // Conservative measured peak (the pump's one-second peak is used when a
    // segment-level p95 is unavailable).
    std::uint64_t measuredP95Bps = 0;
    std::uint64_t goodputBps = 0;
    std::uint64_t levelMaxBps = 0;
    std::uint32_t rttMs = 0;
    std::uint32_t segmentDurationMs = 0;
    std::uint32_t gopDurationMs = 0;
    std::uint32_t lookAheadMs = static_cast<std::uint32_t>(kDefaultLookAheadMs);
    bool segmented = false;
    std::uint64_t routeCeilingBytes = kVbrRouteCeilingBytes;
};

struct VbrBudget final {
    std::uint64_t admissionPeakBps = 0;
    std::uint64_t steadyPeakBps = 0;
    std::uint64_t burstWindowMs = 2000;
    std::uint64_t burstBytes = 0;
    std::uint64_t cacheBytes = 0;
    std::uint64_t packetForwardBytes = 0;
    std::uint64_t packetCacheBytes = 0;
    std::uint64_t routeBytes = kVbrRouteFloorBytes;
    std::uint32_t requiredParallel = 1;
    bool manifestMismatch = false;
    bool levelViolation = false;
    bool routeBudgetInsufficient = false;
};

constexpr VbrBudget ComputeVbrBudget(const VbrInputs& in) noexcept {
    VbrBudget out{};
    const auto measuredP95 = (std::max)(in.measuredP95Bps, in.measuredAverageBps);
    const auto measuredP95WithHeadroom = SaturatingMultiply(measuredP95, 110) / 100;
    const auto measuredAverageWithHeadroom = SaturatingMultiply(in.measuredAverageBps, 135) / 100;
    out.admissionPeakBps = (std::max)(in.declaredPeakBps, measuredP95WithHeadroom);
    if (out.admissionPeakBps == 0) out.admissionPeakBps = in.declaredAverageBps;
    out.steadyPeakBps = (std::max)(measuredP95WithHeadroom, measuredAverageWithHeadroom);
    if (out.steadyPeakBps == 0) out.steadyPeakBps = out.admissionPeakBps;

    out.burstWindowMs = (std::max)({ 2000ull,
        static_cast<std::uint64_t>(in.gopDurationMs),
        static_cast<std::uint64_t>(in.segmentDurationMs) });
    out.burstBytes = BitsToBytesForMs(out.admissionPeakBps, out.burstWindowMs);

    if (in.segmented) {
        out.requiredParallel = 1;
        if (in.rttMs != 0 && in.segmentDurationMs != 0 &&
            in.goodputBps > out.steadyPeakBps) {
            const auto segmentsPerRtt = CeilingDivide(
                static_cast<std::uint64_t>(in.rttMs), in.segmentDurationMs);
            const auto denominator = in.goodputBps - out.steadyPeakBps;
            const auto ratio = CeilingDivide(
                SaturatingMultiply(in.goodputBps, 1000), denominator * 1000);
            const auto required = SaturatingMultiply(segmentsPerRtt, ratio);
            out.requiredParallel = static_cast<std::uint32_t>((std::min)(
                required, static_cast<std::uint64_t>(kMaxRequiredParallel)));
        }
    }

    const auto lookAheadMs = (std::max)(static_cast<std::uint64_t>(in.lookAheadMs),
        static_cast<std::uint64_t>(kDefaultLookAheadMs));
    std::uint64_t coverageMs = lookAheadMs;
    if (in.segmented && in.segmentDurationMs != 0) {
        coverageMs = (std::min)(
            SaturatingMultiply(
                static_cast<std::uint64_t>(in.segmentDurationMs),
                static_cast<std::uint64_t>(out.requiredParallel) + 2),
            180000ull);
    } else if (in.rttMs != 0) {
        // Continuous HTTP pays the RTT once per request, not once per second.
        // Keep a bounded startup/seek reserve instead of multiplying the whole
        // playback rate by a 30 s link latency.
        coverageMs = (std::max)(lookAheadMs,
            static_cast<std::uint64_t>((std::min)(in.rttMs, 10000u)) + 2000);
    }
    const auto steadyBytes = BitsToBytesForMs(out.steadyPeakBps, coverageMs);
    const auto cacheWithSafety = SaturatingMultiply(
        (std::max)(steadyBytes, out.burstBytes), kBitrateSafetyPercent) / 100;
    out.cacheBytes = (std::max)(cacheWithSafety, 16ull * MiB);
    out.packetForwardBytes = std::clamp(
        cacheWithSafety, 4ull * MiB, kPacketForwardMaxBytes);
    out.packetCacheBytes = std::clamp(
        (std::max)(out.burstBytes, BitsToBytesForMs(out.steadyPeakBps, 2000)),
        2ull * MiB, 64ull * MiB);
    out.routeBytes = std::clamp(out.cacheBytes, kVbrRouteFloorBytes, in.routeCeilingBytes);
    out.routeBudgetInsufficient = out.cacheBytes > in.routeCeilingBytes;

    out.manifestMismatch =
        (in.declaredPeakBps != 0 &&
            measuredP95 > SaturatingMultiply(in.declaredPeakBps, kManifestMismatchPercent) / 100) ||
        (in.declaredAverageBps != 0 &&
            in.measuredAverageBps > SaturatingMultiply(
                in.declaredAverageBps, kManifestMismatchPercent) / 100);
    out.levelViolation = in.levelMaxBps != 0 && out.admissionPeakBps > in.levelMaxBps;
    return out;
}


} // namespace FFFPlayer::Io
