#pragma once

#include "3FP/Stream/HlsPlaylist.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

// P3 live window: 3FP-owned sliding playlist state and prefetch policy. FFmpeg's
// hls demuxer remains the transport for now, but the same window rules drive
// refresh scheduling and variant selection when 3FP owns the segment source.
namespace FFFPlayer::Stream {

struct HlsPrefetchPlan final {
    std::uint32_t segmentCount = 0;
    std::uint64_t estimatedBytes = 0;
    bool refreshDue = false;
};

class HlsLiveWindow final {
public:
    explicit HlsLiveWindow(const std::uint32_t retainedSegments = 6) noexcept
        : retainedSegments_(retainedSegments == 0 ? 1 : retainedSegments) {}

    // Merge a freshly fetched media playlist. Returns the number of newly
    // appended segments. A sequence reset (or a gap larger than the retained
    // window) resets the window instead of pretending the missing segments exist.
    std::uint32_t Update(const HlsPlaylist& playlist, std::uint64_t nowMs) noexcept;
    bool RefreshDue(std::uint64_t nowMs) const noexcept;
    std::optional<HlsSegment> NextSegment() const noexcept;
    void Advance() noexcept;
    void Reset() noexcept;

    const std::vector<HlsSegment>& Segments() const noexcept { return segments_; }
    std::uint64_t NextSequence() const noexcept { return nextSequence_; }
    bool EndList() const noexcept { return endList_; }
    std::uint32_t RetainedSegments() const noexcept { return retainedSegments_; }

    HlsPrefetchPlan PlanPrefetch(std::uint32_t rttMs, std::uint64_t goodputBps,
        std::uint64_t nowMs, std::uint32_t targetDurationSeconds) const noexcept;

    // P2-2 segment index: resolve a time offset relative to the retained window
    // start to a segment without fetching media. Live timeline anchoring stays
    // with the caller; this only removes the linear media scan.
    bool FindSegmentForTime(double seconds, std::size_t& index) const noexcept;

    // BANDWIDTH-aware variant choice with a 1.3x upward hysteresis. Returns
    // -1 for an empty master playlist.
    static int SelectVariant(const std::vector<HlsVariant>& variants,
        std::uint64_t goodputBps, std::uint64_t currentBandwidth) noexcept;

    // P2-2 ABR policy: goodput/1.3 decides the upward rung, cache water levels
    // gate the switch, and a segment timeout forces one rung down. The caller
    // still owns the segment-boundary rule; this function only returns the index.
    static int SelectVariantForBuffer(const std::vector<HlsVariant>& variants,
        std::uint64_t goodputBps, std::uint64_t currentBandwidth,
        std::uint64_t cacheBytes, std::uint64_t resumeBytes,
        std::uint64_t lowBytes, bool segmentTimeout) noexcept;

private:
    std::vector<HlsSegment> segments_;
    std::uint32_t retainedSegments_ = 6;
    std::uint64_t nextSequence_ = 0;
    std::uint64_t lastRefreshMs_ = 0;
    std::uint32_t targetDurationSeconds_ = 0;
    bool endList_ = false;
};

} // namespace FFFPlayer::Stream
