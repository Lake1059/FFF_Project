#pragma once

#include "3FP/IO/HttpMediaSource.h"
#include "3FP/Stream/DashSegmentPlan.h"
#include "3FP/Stream/SegmentCache.h"

#include <cstdint>
#include <memory>
#include <string>

// P4 3FP-owned DASH segment fetch path. The planner stays pure; this source is
// the piece that turns a DashSegmentPlan into bytes, validates each media range,
// and stores the result in the shared SegmentCache. It is deliberately not tied
// to FFmpeg's dash demuxer, so a future 3FP fMP4 demuxer can consume the same
// initialization + fragment mapping.
namespace FFFPlayer::Stream {

class DashSegmentSource final {
public:
    static std::unique_ptr<DashSegmentSource> Create(DashSegmentPlan plan,
        FFFPlayer::Io::IoOptions options, std::uint64_t cacheBytes,
        std::string& error) noexcept;

    ~DashSegmentSource() = default;
    DashSegmentSource(const DashSegmentSource&) = delete;
    DashSegmentSource& operator=(const DashSegmentSource&) = delete;

    bool FetchInitialization(std::string& error) noexcept;
    bool FetchSegment(std::size_t index, std::string& error) noexcept;
    // Fetch initialization and the first `count` media segments. This is the
    // product-facing prefetch entry; failures leave already fetched entries in
    // the cache so a caller may retry only the gap.
    bool Prefetch(std::size_t count, std::string& error) noexcept;

    SegmentCache::Bytes Initialization() const noexcept { return cache_.Get(0); }
    SegmentCache::Bytes Segment(std::size_t index) const noexcept {
        return cache_.Get(index + 1);
    }
    const DashSegmentPlan& Plan() const noexcept { return plan_; }
    SegmentCache& Cache() noexcept { return cache_; }

private:
    DashSegmentSource(DashSegmentPlan plan, FFFPlayer::Io::IoOptions options,
        std::uint64_t cacheBytes) noexcept;

    bool FetchUrl(const std::string& url, const std::string& rangeText,
        std::uint64_t cacheKey, std::string& error) noexcept;

    DashSegmentPlan plan_;
    FFFPlayer::Io::IoOptions options_;
    SegmentCache cache_;
};

} // namespace FFFPlayer::Stream
