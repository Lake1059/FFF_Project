#include "3FP/Stream/HlsLiveWindow.h"
#include "3FP/Stream/HlsSegmentLoader.h"
#include "3FP/Stream/SegmentCache.h"
#include "3FP/Stream/DashManifest.h"
#include "3FP/Stream/DashAvailability.h"
#include "3FP/Stream/DashSegmentPlan.h"
#include "3FP/Stream/DashSegmentSource.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace {
std::uint64_t ReadCounter(const char* path) {
    std::ifstream input(path);
    std::uint64_t value = 0;
    input >> value;
    return value;
}
} // namespace

static FFFPlayer::Stream::HlsSegment MakeSegment(std::uint64_t sequence) {
    FFFPlayer::Stream::HlsSegment segment;
    segment.sequence = sequence;
    segment.durationSeconds = 2;
    segment.uri = "seg" + std::to_string(sequence) + ".ts";
    return segment;
}

int main(int argc, char** argv) {
    using namespace FFFPlayer::Stream;
    HlsLiveWindow window(3);
    HlsPlaylist playlist;
    playlist.valid = true;
    playlist.targetDurationSeconds = 6;
    playlist.mediaSequence = 10;
    playlist.segments = {MakeSegment(10), MakeSegment(11), MakeSegment(12)};
    if (window.Update(playlist, 1000) != 3 || window.NextSequence() != 10 ||
        !window.NextSegment() || window.NextSegment()->sequence != 10) {
        std::fprintf(stderr, "hls initial window failed\n");
        return 1;
    }
    window.Advance();
    window.Advance();
    playlist.mediaSequence = 11;
    playlist.segments = {MakeSegment(11), MakeSegment(12), MakeSegment(13)};
    if (window.Update(playlist, 2000) != 1 || window.NextSequence() != 12 ||
        window.Segments().size() != 3 || window.Segments().back().sequence != 13) {
        std::fprintf(stderr, "hls sliding window failed\n");
        return 1;
    }
    if (!window.RefreshDue(5000) || window.RefreshDue(4000)) {
        std::fprintf(stderr, "hls refresh due failed\n");
        return 1;
    }
    std::size_t hlsIndex = 0;
    if (!window.FindSegmentForTime(3.0, hlsIndex) || hlsIndex != 1 ||
        !window.FindSegmentForTime(5.5, hlsIndex) || hlsIndex != 2 ||
        window.FindSegmentForTime(6.1, hlsIndex)) {
        std::fprintf(stderr, "hls segment time index failed\n");
        return 1;
    }
    HlsPrefetchPlan plan = window.PlanPrefetch(1000, 1'000'000, 5000, 6);
    if (plan.segmentCount != 6 || plan.estimatedBytes == 0 || !plan.refreshDue) {
        std::fprintf(stderr, "hls prefetch plan failed count=%u bytes=%llu\n",
            plan.segmentCount, static_cast<unsigned long long>(plan.estimatedBytes));
        return 1;
    }
    std::vector<HlsVariant> variants;
    variants.push_back({1'000'000, 640, 360, "avc1", "low.m3u8"});
    variants.push_back({3'000'000, 1280, 720, "avc1", "mid.m3u8"});
    variants.push_back({5'000'000, 1920, 1080, "avc1", "high.m3u8"});
    if (HlsLiveWindow::SelectVariant(variants, 4'000'000, 3'000'000) != 1 ||
        HlsLiveWindow::SelectVariant(variants, 7'000'000, 3'000'000) != 2 ||
        HlsLiveWindow::SelectVariant(variants, 500'000, 0) != 0) {
        std::fprintf(stderr, "hls variant selection failed\n");
        return 1;
    }
    if (HlsLiveWindow::SelectVariantForBuffer(variants, 8'000'000, 3'000'000,
            10'000'000, 8'000'000, 3'000'000, false) != 2 ||
        HlsLiveWindow::SelectVariantForBuffer(variants, 8'000'000, 3'000'000,
            5'000'000, 8'000'000, 3'000'000, false) != 1 ||
        HlsLiveWindow::SelectVariantForBuffer(variants, 8'000'000, 3'000'000,
            2'000'000, 8'000'000, 3'000'000, false) != 0 ||
        HlsLiveWindow::SelectVariantForBuffer(variants, 8'000'000, 3'000'000,
            10'000'000, 8'000'000, 3'000'000, true) != 0) {
        std::fprintf(stderr, "hls abr buffer policy failed\n");
        return 1;
    }

    std::string error;
    // A manifest can declare an absurd mediaRange. Reject it before any
    // request, rather than downloading until the process runs out of memory.
    {
        DashSegmentPlan oversizedPlan;
        DashSegmentDescriptor descriptor;
        descriptor.number = 1;
        descriptor.url = "http://127.0.0.1:18866/slow-test.mp4";
        descriptor.mediaRange = "0-70000000";
        oversizedPlan.segments.push_back(std::move(descriptor));
        std::string sourceError;
        auto oversized = DashSegmentSource::Create(std::move(oversizedPlan),
            FFFPlayer::Io::IoOptions{}, 128ull * 1024 * 1024, sourceError);
        if (oversized == nullptr || oversized->FetchSegment(0, sourceError) ||
            sourceError.find("byte ceiling") == std::string::npos) {
            std::fprintf(stderr, "oversized DASH segment was not rejected: %s\n",
                sourceError.c_str());
            return 1;
        }
    }
    if (argc >= 3) {
        std::ifstream playlistFile(argv[2]);
        if (!playlistFile) return 1;
        std::ostringstream playlistText;
        playlistText << playlistFile.rdbuf();
        HlsPlaylist livePlaylist;
        if (!HlsParser::Parse(playlistText.str(),
            "http://127.0.0.1:18866/hls-test/playlist.m3u8", livePlaylist, error)) {
            std::fprintf(stderr, "real hls parse failed: %s\n", error.c_str());
            return 1;
        }
        HlsLiveWindow liveWindow(4);
        if (liveWindow.Update(livePlaylist, 1000) == 0 || !liveWindow.NextSegment()) {
            std::fprintf(stderr, "real hls window failed\n");
            return 1;
        }
        SegmentCache segmentCache(32ull * 1024 * 1024);
        FFFPlayer::Io::IoOptions options{};
        std::uint32_t loaded = 0;
        for (; loaded < 2; ++loaded) {
            const auto segment = liveWindow.NextSegment();
            if (!segment) break;
            HlsLoadedSegment bytes;
            std::string loadError;
            if (!HlsSegmentLoader::Load(*segment, options, bytes, loadError)) {
                std::fprintf(stderr, "hls segment load failed: %s\n", loadError.c_str());
                return 1;
            }
            if (!segmentCache.Put(segment->sequence, bytes.bytes)) {
                std::fprintf(stderr, "hls segment cache put failed\n");
                return 1;
            }
            liveWindow.Advance();
        }
        if (loaded != 2 || segmentCache.BytesUsed() < 400'000) {
            std::fprintf(stderr, "hls segment pipeline failed loaded=%u bytes=%llu\n",
                loaded, static_cast<unsigned long long>(segmentCache.BytesUsed()));
            return 1;
        }
    }

    const std::string dynamicManifest =
        "<?xml version=\"1.0\"?>"
        "<MPD type=\"dynamic\" availabilityStartTime=\"2026-11-25T00:00:00Z\""
        " timeShiftBufferDepth=\"PT10S\" minimumUpdatePeriod=\"PT2S\">"
        "<Period start=\"PT0S\"><AdaptationSet mimeType=\"video/mp4\">"
        "<Representation id=\"v\" bandwidth=\"1000000\">"
        "<SegmentTemplate timescale=\"1000\" duration=\"2000\" startNumber=\"1\""
        " media=\"seg-$Number%05d$.m4s\" initialization=\"init.m4s\"/>"
        "</Representation></AdaptationSet></Period></MPD>";
    DashManifest manifest;
    if (!DashParser::Parse(dynamicManifest, "http://example.test/live.mpd", manifest, error)) {
        std::fprintf(stderr, "dash parse failed: %s\n", error.c_str());
        return 1;
    }
    const auto ast = DashAvailability::ParseAvailabilityStart(manifest);
    if (ast < 0 || !DashAvailability::IsAvailable(manifest, 0, 0, 2, ast + 5) ||
        DashAvailability::IsAvailable(manifest, 0, 6, 2, ast + 4) ||
        DashAvailability::IsAvailable(manifest, 0, 0, 2, ast + 20) ||
        !DashAvailability::IsAvailable(manifest, 0, 8, 2, ast + 20) ||
        !DashAvailability::RefreshDue(manifest, ast + 5, ast + 8)) {
        std::fprintf(stderr, "dash availability failed ast=%lld\n",
            static_cast<long long>(ast));
        return 1;
    }
    if (manifest.periods.empty() || manifest.periods[0].adaptationSets.empty() ||
        manifest.periods[0].adaptationSets[0].representations.empty()) return 1;
    auto livePlan = DashSegmentPlanner::Build(
        manifest.periods[0].adaptationSets[0].representations[0], "http://example.test/live.mpd", 8);
    std::size_t dashIndex = 0;
    if (livePlan.segments.size() < 2 || livePlan.segments[1].startTime != 2000 ||
        livePlan.segments[1].duration != 2000 || livePlan.segments[1].timescale != 1000 ||
        !livePlan.FindSegmentForTime(3000, dashIndex) || dashIndex != 1 ||
        livePlan.FindSegmentForTime(20000, dashIndex)) {
        std::fprintf(stderr, "dash segment time index failed\n");
        return 1;
    }

    if (argc >= 2) {
        std::ifstream file(argv[1]);
        if (!file) return 1;
        std::ostringstream text;
        text << file.rdbuf();
        const std::string dashBase = argc >= 5
            ? argv[3] : "http://127.0.0.1:18866/dash-test/manifest.mpd";
        DashManifest real;
        if (!DashParser::Parse(text.str(), dashBase, real, error)) {
            std::fprintf(stderr, "real dash parse failed: %s\n", error.c_str());
            return 1;
        }
        if (real.periods.empty() || real.periods[0].adaptationSets.empty()) return 1;
        const auto& adaptation = real.periods[0].adaptationSets[0];
        if (adaptation.representations.empty()) return 1;
        auto segmentPlan = DashSegmentPlanner::Build(adaptation.representations[0],
            dashBase, 8);
        FFFPlayer::Io::IoOptions options{};
        std::string sourceError;
        auto source = DashSegmentSource::Create(std::move(segmentPlan), options,
            16ull * 1024 * 1024, sourceError);
        const auto before = argc >= 5 ? ReadCounter(argv[4]) : 0;
        if (source == nullptr || !source->Prefetch(2, sourceError)) {
            std::fprintf(stderr, "dash segment source failed: %s\n", sourceError.c_str());
            return 1;
        }
        const auto after = argc >= 5 ? ReadCounter(argv[4]) : 0;
        const auto requests = after >= before ? after - before : 0;
        const auto init = source->Initialization();
        const auto first = source->Segment(0);
        if (!init || init->empty() || !first || first->empty()) {
            std::fprintf(stderr, "dash segment source returned empty bytes\n");
            return 1;
        }
        // init + two media segments must not open one request per 64 KiB block.
        if (argc >= 5 && requests > 4) {
            std::fprintf(stderr, "DASH request amplification: requests=%llu\n",
                static_cast<unsigned long long>(requests));
            return 1;
        }
        std::printf("PASS P3P4 advanced tests hls-window hls-segments dash-availability dash-source init=%zu seg0=%zu requests=%llu\n",
            init->size(), first->size(), static_cast<unsigned long long>(requests));
        return 0;
    }
    std::printf("PASS P3P4 advanced tests hls-window dash-availability\n");
    return 0;
}
