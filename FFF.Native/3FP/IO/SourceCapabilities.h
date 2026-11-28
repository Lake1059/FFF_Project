#pragma once

#include <cstdint>

// Static capabilities a source declares before the first read. The player must
// never infer "random access" from a URL scheme alone; it asks the source and
// records the answer in the session's diagnostics.
namespace FFFPlayer::Io {

enum class SourceDomain : std::uint8_t {
    ByteRandomAccess = 0, // local, UNC, HTTP Range, WebDAV, memory
    Segmented = 1,        // HLS, DASH
    RealtimePackets = 2,  // RTSP/RTP, RTMP, live HLS
    Disc = 3,             // Blu-ray navigation VM
    Unknown = 4,
};

enum class SourceFeature : std::uint32_t {
    None = 0,
    ByteExact = 1u << 0,          // bytes at offsets are stable and exact
    RandomAccess = 1u << 1,       // Seek can jump without reopening
    RangeRequests = 1u << 2,      // HTTP 206 with Content-Range
    StableEtag = 1u << 3,         // ETag/Last-Modified usable for validation
    ExclusiveWriter = 1u << 4,    // growing file / tail read
    SegmentTimeline = 1u << 5,    // segment sequence numbers or DASH timeline
    Live = 1u << 6,
    LowLatency = 1u << 7,
    MultipleVariants = 1u << 8,   // ABR switching is possible
    Reconnectable = 1u << 9,
};

constexpr SourceFeature operator|(SourceFeature a, SourceFeature b) noexcept {
    return static_cast<SourceFeature>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool HasFeature(SourceFeature value, SourceFeature flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct SourceCapabilities final {
    SourceDomain domain = SourceDomain::Unknown;
    SourceFeature features = SourceFeature::None;
    std::int64_t sizeBytes = -1;       // -1 unknown; valid only for byte sources
    std::uint32_t segmentDurationMs = 0;
    std::uint32_t maxSegmentBytes = 0;

    constexpr bool ByteExact() const noexcept {
        return domain == SourceDomain::ByteRandomAccess &&
            HasFeature(features, SourceFeature::ByteExact);
    }

    constexpr bool RandomAccess() const noexcept {
        return HasFeature(features, SourceFeature::RandomAccess);
    }

    constexpr bool IsLive() const noexcept {
        return HasFeature(features, SourceFeature::Live);
    }

    constexpr bool IsSegmented() const noexcept {
        return domain == SourceDomain::Segmented;
    }

    constexpr bool IsRealtime() const noexcept {
        return domain == SourceDomain::RealtimePackets || IsLive();
    }
};

} // namespace FFFPlayer::Io
