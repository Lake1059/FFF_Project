#include "Shared/Ffmpeg/SharedFileInput.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <cstdio>
#include <string>

static int OpenAndRead(const char* url) {
    std::string error;
    AVIOInterruptCB interrupt{};
    auto io = SharedFileInput::Open(url, error, 0, false, &interrupt);
    if (!io) {
        std::fprintf(stderr, "input open failed: %s\n", error.c_str());
        return 1;
    }
    AVFormatContext* format = avformat_alloc_context();
    if (format == nullptr) return 1;
    format->pb = io->Context();
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback = interrupt;
    if (avformat_open_input(&format, url, nullptr, nullptr) < 0) {
        std::fprintf(stderr, "demuxer open failed\n");
        avformat_free_context(format);
        return 1;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        std::fprintf(stderr, "stream info failed\n");
        avformat_close_input(&format);
        return 1;
    }
    std::printf("format=%s streams=%u duration=%lld\n",
        format->iformat && format->iformat->name ? format->iformat->name : "?",
        format->nb_streams, static_cast<long long>(format->duration));
    AVPacket* packet = av_packet_alloc();
    int packets = 0;
    while (packets < 8 && av_read_frame(format, packet) >= 0) {
        std::printf("packet stream=%d size=%d pts=%lld\n", packet->stream_index,
            packet->size, static_cast<long long>(packet->pts));
        av_packet_unref(packet);
        ++packets;
    }
    av_packet_free(&packet);
    if (packets == 0) {
        std::fprintf(stderr, "no packets\n");
        avformat_close_input(&format);
        return 1;
    }
    const auto target = format->duration > 0 ? format->duration / 2 : 0;
    if (target > 0 && av_seek_frame(format, 0, target, AVSEEK_FLAG_BACKWARD) < 0) {
        std::fprintf(stderr, "seek failed\n");
        avformat_close_input(&format);
        return 1;
    }
    packet = av_packet_alloc();
    packets = 0;
    while (packets < 3 && av_read_frame(format, packet) >= 0) {
        av_packet_unref(packet);
        ++packets;
    }
    av_packet_free(&packet);
    std::printf("seek packets=%d\n", packets);
    avformat_close_input(&format);
    return packets > 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: HttpDemuxTests <url>\n");
        return 2;
    }
    const int result = OpenAndRead(argv[1]);
    std::printf(result == 0 ? "PASS http demux\n" : "FAIL http demux\n");
    return result;
}
