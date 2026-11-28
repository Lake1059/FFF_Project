#include "3FP/IO/PacketCache.h"

extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/error.h>
}

#include <cstdio>
#include <cstring>

using namespace FFFPlayer::Io;

static bool MakePacket(AVPacket* packet, const std::int64_t pts, const bool keyframe,
    const int payload) {
    if (av_new_packet(packet, payload) < 0) return false;
    packet->pts = pts;
    packet->dts = pts;
    packet->flags = keyframe ? AV_PKT_FLAG_KEY : 0;
    packet->stream_index = 0;
    std::memset(packet->data, static_cast<int>(pts & 0xff), payload);
    return true;
}

int main() {
    PacketCache cache(1000);
    cache.Reset(7);
    for (int i = 0; i < 10; ++i) {
        AVPacket packet{};
        const bool key = i == 0 || i == 4 || i == 8;
        if (!MakePacket(&packet, i * 100, key, 100)) return 1;
        if (!cache.Push(packet, 0, 7)) return 1;
        av_packet_unref(&packet);
    }
    if (cache.Count() != 10 || cache.Bytes() != 1000 || cache.KeyframeCount() != 3) {
        std::fprintf(stderr, "cache accounting failed: %u %llu %u\n", cache.Count(),
            static_cast<unsigned long long>(cache.Bytes()), cache.KeyframeCount());
        return 1;
    }
    std::int64_t keyframe = -1;
    if (!cache.FindKeyframeBefore(0, 350, keyframe) || keyframe != 0) return 1;
    if (!cache.FindKeyframeBefore(0, 450, keyframe) || keyframe != 400) return 1;
    if (!cache.FindKeyframeBefore(0, 900, keyframe) || keyframe != 800) return 1;
    auto packets = cache.CopyFromKeyframe(0, 350, 16);
    if (packets.size() != 4) return 1;
    packets = cache.CopyFromKeyframe(0, 650, 3);
    if (packets.size() != 3) return 1;

    AVPacket extra{};
    if (!MakePacket(&extra, 1000, false, 100)) return 1;
    if (!cache.Push(extra, 0, 7)) return 1;
    av_packet_unref(&extra);
    if (cache.Count() != 10 || cache.Bytes() != 1000) return 1;
    if (cache.FindKeyframeBefore(0, 350, keyframe)) {
        std::fprintf(stderr, "unexpected keyframe at %lld after eviction\n",
            static_cast<long long>(keyframe));
        return 1;
    }
    if (!cache.FindKeyframeBefore(0, 450, keyframe) || keyframe != 400) return 1;

    cache.Reset(8);
    if (cache.Count() != 0 || cache.Generation() != 8) return 1;
    AVPacket stale{};
    if (!MakePacket(&stale, 0, true, 100)) return 1;
    if (cache.Push(stale, 0, 7)) return 1; // old generation must be rejected
    av_packet_unref(&stale);

    PacketCache tiny(64);
    AVPacket tooLarge{};
    if (!MakePacket(&tooLarge, 0, true, 100)) return 1;
    if (tiny.Push(tooLarge, 0, 8)) return 1;
    av_packet_unref(&tooLarge);

    std::printf("PASS PacketCache\n");
    return 0;
}
