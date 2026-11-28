#include "3FP/Stream/DashSegmentPlan.h"

#include <cstdio>
#include <string>

using namespace FFFPlayer::Stream;

int main() {
    DashRepresentation rep;
    rep.id = "v1";
    rep.bandwidth = 800000;
    rep.segmentTemplate.timescale = 1000;
    rep.segmentTemplate.duration = 2000;
    rep.segmentTemplate.startNumber = 1;
    rep.segmentTemplate.media = "chunk-$Number%05d$.m4s";
    rep.segmentTemplate.initialization = "init.mp4";
    rep.timeline.push_back(DashTimelineEntry{0, 2000, 2});
    const auto plan = DashSegmentPlanner::Build(rep, "https://cdn.example.com/dash/");
    if (plan.initializationUrl != "https://cdn.example.com/dash/init.mp4" ||
        plan.segments.size() != 3) {
        std::fprintf(stderr, "template plan failed init=%s n=%zu\n",
            plan.initializationUrl.c_str(), plan.segments.size());
        return 1;
    }
    if (plan.segments[0].number != 1 || plan.segments[0].startTime != 0 ||
        plan.segments[0].url != "https://cdn.example.com/dash/chunk-00001.m4s" ||
        plan.segments[2].number != 3 || plan.segments[2].startTime != 4000) return 1;

    DashRepresentation list;
    list.id = "a1";
    list.segmentTemplate.startNumber = 5;
    list.segmentUrls.push_back(DashSegmentUrl{"audio-1.m4s", "0-100", ""});
    list.segmentUrls.push_back(DashSegmentUrl{"audio-2.m4s", "", ""});
    const auto listPlan = DashSegmentPlanner::Build(list, "https://cdn.example.com/dash/");
    if (listPlan.segments.size() != 2 || listPlan.segments[0].number != 5 ||
        listPlan.segments[0].url != "https://cdn.example.com/dash/audio-1.m4s" ||
        listPlan.segments[1].number != 6) return 1;

    const auto capped = DashSegmentPlanner::Build(rep, "https://cdn.example.com/dash/", 2);
    if (capped.segments.size() != 2) return 1;
    std::printf("PASS DASH segment planner\n");
    return 0;
}
