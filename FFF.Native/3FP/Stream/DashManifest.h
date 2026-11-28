#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// P4 DASH manifest model and a deliberately small, namespace-agnostic parser.
// It covers the shapes 3FP needs for byte/segment sources: SegmentTemplate,
// SegmentList, SegmentBase presence, SegmentTimeline, static/dynamic MPD,
// Period/AdaptationSet/Representation and BaseURL. Full XML schema validation
// is out of scope; malformed or unsupported manifests fail closed.
namespace FFFPlayer::Stream {

struct DashSegmentTemplate final {
    std::uint64_t timescale = 0;
    std::uint64_t duration = 0;
    std::uint64_t startNumber = 1;
    std::uint64_t presentationTimeOffset = 0;
    std::string media;
    std::string initialization;
    std::string index;
    std::string bitstreamSwitching;
};

struct DashTimelineEntry final {
    std::int64_t time = -1;
    std::uint64_t duration = 0;
    std::uint32_t repeat = 0;
};

struct DashSegmentUrl final {
    std::string media;
    std::string mediaRange;
    std::string indexRange;
};

struct DashRepresentation final {
    std::string id;
    std::uint64_t bandwidth = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string codecs;
    std::string mimeType;
    std::string baseUrl;
    DashSegmentTemplate segmentTemplate;
    std::vector<DashTimelineEntry> timeline;
    std::vector<DashSegmentUrl> segmentUrls;
};

struct DashAdaptationSet final {
    std::string mimeType;
    std::string codecs;
    DashSegmentTemplate segmentTemplate;
    std::vector<DashTimelineEntry> timeline;
    std::vector<DashRepresentation> representations;
};

struct DashPeriod final {
    std::string id;
    double startSeconds = 0;
    double durationSeconds = 0;
    std::vector<DashAdaptationSet> adaptationSets;
};

struct DashManifest final {
    bool valid = false;
    bool dynamic = false;
    double durationSeconds = 0;
    double minimumUpdatePeriodSeconds = 0;
    double timeShiftBufferDepthSeconds = 0;
    double suggestedPresentationDelaySeconds = 0;
    std::string availabilityStartTime;
    std::string baseUrl;
    std::vector<DashPeriod> periods;
};

class DashParser final {
public:
    static bool Parse(std::string_view xml, std::string_view manifestUrl,
        DashManifest& manifest, std::string& error);
    // Replaces the DASH template tokens used by 3FP. Unknown tokens are left
    // intact; callers can reject them with TemplateIsSupported().
    static std::string ExpandTemplate(std::string_view templateText,
        std::string_view representationId, std::uint64_t bandwidth,
        std::uint64_t number, std::int64_t time);
    static bool TemplateIsSupported(std::string_view templateText);
    static bool ParseIsoDuration(std::string_view text, double& seconds);
    static std::string ResolveUri(std::string_view baseUrl, std::string_view uri);
};

} // namespace FFFPlayer::Stream
