#include "3FP/Stream/HlsPlaylist.h"

#include <cstdio>
#include <string>

using namespace FFFPlayer::Stream;

int main() {
    const std::string media =
        "#EXTM3U\n"
        "#EXT-X-VERSION:7\n"
        "#EXT-X-TARGETDURATION:4\n"
        "#EXT-X-MEDIA-SEQUENCE:100\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\",IV=0x0102\n"
        "#EXTINF:4.0,\n"
        "seg1.ts\n"
        "#EXT-X-BYTERANGE:1000@0\n"
        "#EXTINF:4.0,\n"
        "seg2.ts\n"
        "#EXT-X-BYTERANGE:500\n"
        "#EXTINF:2.0,\n"
        "seg2.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:3.5,\n"
        "../seg3.ts\n"
        "#EXT-X-ENDLIST\n";
    HlsPlaylist playlist;
    std::string error;
    if (!HlsParser::Parse(media, "https://cdn.example.com/live/path/index.m3u8", playlist, error)) {
        std::fprintf(stderr, "media parse failed: %s\n", error.c_str());
        return 1;
    }
    if (playlist.master || !playlist.endList || playlist.targetDurationSeconds != 4 ||
        playlist.mediaSequence != 100 || playlist.playlistType != "VOD" ||
        playlist.mapUri != "https://cdn.example.com/live/path/init.mp4" ||
        playlist.segments.size() != 4) {
        std::fprintf(stderr, "media metadata mismatch master=%d end=%d target=%u seq=%llu type=%s map=%s n=%zu\n",
            playlist.master ? 1 : 0, playlist.endList ? 1 : 0, playlist.targetDurationSeconds,
            static_cast<unsigned long long>(playlist.mediaSequence), playlist.playlistType.c_str(),
            playlist.mapUri.c_str(), playlist.segments.size());
        return 1;
    }
    const auto& first = playlist.segments[0];
    if (first.sequence != 100 || first.uri != "https://cdn.example.com/live/path/seg1.ts" ||
        first.range.hasRange || !first.key.valid || first.key.uri != "https://cdn.example.com/live/path/key.bin" ||
        first.key.iv != "0x0102") return 1;
    const auto& second = playlist.segments[1];
    if (second.sequence != 101 || !second.range.hasRange || second.range.offset != 0 ||
        second.range.length != 1000) return 1;
    const auto& third = playlist.segments[2];
    if (third.sequence != 102 || !third.range.hasRange || third.range.offset != 1000 ||
        third.range.length != 500 || third.durationSeconds != 2.0) return 1;
    const auto& fourth = playlist.segments[3];
    if (fourth.sequence != 103 || !fourth.discontinuity || fourth.range.hasRange ||
        fourth.uri != "https://cdn.example.com/live/seg3.ts") return 1;

    const std::string master =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=1280x720,CODECS=\"avc1.4d401f,mp4a.40.2\"\n"
        "low/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2400000,RESOLUTION=1920x1080,CODECS=\"avc1.640028,mp4a.40.2\"\n"
        "high/index.m3u8\n";
    HlsPlaylist masterPlaylist;
    if (!HlsParser::Parse(master, "https://cdn.example.com/master.m3u8", masterPlaylist, error)) {
        std::fprintf(stderr, "master parse failed: %s\n", error.c_str());
        return 1;
    }
    if (!masterPlaylist.master || masterPlaylist.variants.size() != 2 ||
        masterPlaylist.variants[0].bandwidth != 800000 ||
        masterPlaylist.variants[0].width != 1280 || masterPlaylist.variants[0].height != 720 ||
        masterPlaylist.variants[0].codecs != "avc1.4d401f,mp4a.40.2" ||
        masterPlaylist.variants[0].uri != "https://cdn.example.com/low/index.m3u8" ||
        masterPlaylist.variants[1].width != 1920) return 1;

    HlsPlaylist invalid;
    if (HlsParser::Parse("#EXT-X-TARGETDURATION:4\n", "https://x/y.m3u8", invalid, error)) return 1;

    std::printf("PASS HLS playlist parser\n");
    return 0;
}
