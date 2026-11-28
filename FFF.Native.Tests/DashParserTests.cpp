#include "3FP/Stream/DashManifest.h"

#include <cstdio>
#include <string>

using namespace FFFPlayer::Stream;

int main() {
    double seconds = 0;
    if (!DashParser::ParseIsoDuration("PT1H2M3.5S", seconds) ||
        seconds < 3723.49 || seconds > 3723.51) return 1;
    if (!DashParser::ParseIsoDuration("P1D", seconds) || seconds != 86400) return 1;
    if (DashParser::ParseIsoDuration("not-a-duration", seconds)) return 1;

    const std::string xml =
        "<MPD type=\"dynamic\" mediaPresentationDuration=\"PT10S\" "
        "minimumUpdatePeriod=\"PT2S\" availabilityStartTime=\"2026-01-01T00:00:00Z\">"
        "<BaseURL>https://cdn.example.com/dash/</BaseURL>"
        "<Period id=\"p0\" start=\"PT0S\" duration=\"PT10S\">"
        "<AdaptationSet mimeType=\"video/mp4\" codecs=\"avc1\">"
        "<SegmentTemplate timescale=\"1000\" duration=\"2000\" startNumber=\"1\" "
        "media=\"chunk-$Number%05d$.m4s\" initialization=\"init.mp4\"/>"
        "<Representation id=\"v1\" bandwidth=\"800000\" width=\"1280\" height=\"720\" "
        "codecs=\"avc1.4d401f\">"
        "<SegmentTimeline><S t=\"0\" d=\"2000\" r=\"4\"/></SegmentTimeline>"
        "</Representation>"
        "</AdaptationSet>"
        "<AdaptationSet mimeType=\"audio/mp4\">"
        "<Representation id=\"a1\" bandwidth=\"128000\">"
        "<SegmentList><SegmentURL media=\"audio-1.m4s\" mediaRange=\"0-100\"/>"
        "<SegmentURL media=\"audio-2.m4s\"/></SegmentList>"
        "</Representation>"
        "</AdaptationSet>"
        "</Period>"
        "</MPD>";
    DashManifest manifest;
    std::string error;
    if (!DashParser::Parse(xml, "https://origin.example.com/master.mpd", manifest, error)) {
        std::fprintf(stderr, "DASH parse failed: %s\n", error.c_str());
        return 1;
    }
    if (!manifest.valid || !manifest.dynamic || manifest.durationSeconds != 10 ||
        manifest.minimumUpdatePeriodSeconds != 2 || manifest.baseUrl != "https://cdn.example.com/dash/" ||
        manifest.periods.size() != 1) return 1;

    const auto& period = manifest.periods[0];
    if (period.id != "p0" || period.startSeconds != 0 || period.durationSeconds != 10 ||
        period.adaptationSets.size() != 2) return 1;
    const auto& video = period.adaptationSets[0];
    if (video.mimeType != "video/mp4" || video.representations.size() != 1) return 1;
    const auto& rep = video.representations[0];
    if (rep.id != "v1" || rep.bandwidth != 800000 || rep.width != 1280 || rep.height != 720 ||
        rep.segmentTemplate.timescale != 1000 || rep.segmentTemplate.duration != 2000 ||
        rep.segmentTemplate.startNumber != 1 ||
        rep.segmentTemplate.media != "chunk-$Number%05d$.m4s" ||
        rep.timeline.size() != 1 || rep.timeline[0].duration != 2000 || rep.timeline[0].repeat != 4)
        return 1;
    const auto& audio = period.adaptationSets[1];
    if (audio.representations.size() != 1 || audio.representations[0].segmentUrls.size() != 2 ||
        audio.representations[0].segmentUrls[0].mediaRange != "0-100") return 1;

    const auto expanded = DashParser::ExpandTemplate(
        "chunk-$Number%05d$-$Time$-$RepresentationID$-$Bandwidth$.m4s", "v1", 800000, 1, 0);
    if (expanded != "chunk-00001-0-v1-800000.m4s") {
        std::fprintf(stderr, "template expansion failed: %s\n", expanded.c_str());
        return 1;
    }
    if (!DashParser::TemplateIsSupported("chunk-$Number$.m4s") ||
        DashParser::TemplateIsSupported("chunk-$Unknown$.m4s")) return 1;

    DashManifest invalid;
    if (DashParser::Parse("<MPD/>", "https://x/y.mpd", invalid, error)) return 1;

    std::printf("PASS DASH manifest parser\n");
    return 0;
}
