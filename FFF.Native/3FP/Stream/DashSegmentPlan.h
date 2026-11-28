#pragma once

#include "3FP/Stream/DashManifest.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// P4 segment planner: turns one Representation's SegmentTemplate/List into
// concrete URLs and timeline coordinates. It never fetches; the source layer
// decides how many segments to ask for and how to cache them.
namespace FFFPlayer::Stream {

struct DashSegmentDescriptor final {
    std::uint64_t number = 0;
    std::int64_t startTime = 0;
    std::uint64_t duration = 0;
    std::uint64_t timescale = 0;
    std::string url;
    std::string mediaRange;
};

struct DashSegmentPlan final {
    std::string initializationUrl;
    std::vector<DashSegmentDescriptor> segments;

    // P2-2: resolve a presentation timestamp (in the representation timescale)
    // to a segment without fetching or scanning media. This is the index the
    // source layer uses for a seek that lands on a non-zero segment.
    bool FindSegmentForTime(std::int64_t time, std::size_t& index) const noexcept {
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const auto& segment = segments[i];
            if (segment.duration == 0) continue;
            if (time >= segment.startTime &&
                time < segment.startTime + static_cast<std::int64_t>(segment.duration)) {
                index = i;
                return true;
            }
        }
        return false;
    }
};

class DashSegmentPlanner final {
public:
    static DashSegmentPlan Build(const DashRepresentation& representation,
        const std::string_view manifestBaseUrl, const std::uint32_t maxSegments = 64) {
        DashSegmentPlan plan;
        if (maxSegments == 0) return plan;
        std::string base(manifestBaseUrl);
        if (!representation.baseUrl.empty())
            base = DashParser::ResolveUri(manifestBaseUrl, representation.baseUrl);
        const auto& templ = representation.segmentTemplate;
        if (!templ.initialization.empty()) {
            plan.initializationUrl = DashParser::ResolveUri(base,
                DashParser::ExpandTemplate(templ.initialization, representation.id,
                    representation.bandwidth, templ.startNumber, 0));
        }
        if (!representation.segmentUrls.empty()) {
            for (std::size_t i = 0; i < representation.segmentUrls.size() && i < maxSegments; ++i) {
                DashSegmentDescriptor descriptor;
                descriptor.number = templ.startNumber + i;
                descriptor.timescale = templ.timescale;
                descriptor.url = DashParser::ResolveUri(base,
                    representation.segmentUrls[i].media);
                descriptor.mediaRange = representation.segmentUrls[i].mediaRange;
                plan.segments.push_back(std::move(descriptor));
            }
            return plan;
        }
        if (templ.media.empty()) return plan;
        if (!representation.timeline.empty()) {
            std::int64_t time = 0;
            std::uint64_t number = templ.startNumber;
            for (const auto& entry : representation.timeline) {
                if (entry.time >= 0) time = entry.time;
                const auto repeats = std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(entry.repeat) + 1,
                    maxSegments - plan.segments.size());
                for (std::uint64_t i = 0; i < repeats; ++i) {
                    DashSegmentDescriptor descriptor;
                    descriptor.number = number++;
                    descriptor.startTime = time;
                    descriptor.duration = entry.duration;
                    descriptor.timescale = templ.timescale;
                    descriptor.url = DashParser::ResolveUri(base,
                        DashParser::ExpandTemplate(templ.media, representation.id,
                            representation.bandwidth, descriptor.number, descriptor.startTime));
                    plan.segments.push_back(std::move(descriptor));
                    time += static_cast<std::int64_t>(entry.duration);
                    if (plan.segments.size() >= maxSegments) return plan;
                }
            }
            return plan;
        }
        if (templ.duration != 0) {
            for (std::uint32_t i = 0; i < maxSegments; ++i) {
                DashSegmentDescriptor descriptor;
                descriptor.number = templ.startNumber + i;
                descriptor.startTime = static_cast<std::int64_t>(i) *
                    static_cast<std::int64_t>(templ.duration);
                descriptor.duration = templ.duration;
                descriptor.timescale = templ.timescale;
                descriptor.url = DashParser::ResolveUri(base,
                    DashParser::ExpandTemplate(templ.media, representation.id,
                        representation.bandwidth, descriptor.number, descriptor.startTime));
                plan.segments.push_back(std::move(descriptor));
            }
        }
        return plan;
    }
};

} // namespace FFFPlayer::Stream
