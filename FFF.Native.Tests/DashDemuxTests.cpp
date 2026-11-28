extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}
#include <cstdio>
#include <string>

int main() {
    const char* url = "http://127.0.0.1:18866/dash-test/manifest.mpd";
    AVFormatContext* format = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "http,https,tcp,tls,file,crypto,data", 0);
    const int result = avformat_open_input(&format, url, nullptr, &options);
    av_dict_free(&options);
    if (result < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, error, sizeof(error));
        std::fprintf(stderr, "dash open failed: %s\n", error);
        return 1;
    }
    std::printf("format=%s streams=%d duration=%lld\n",
        format->iformat && format->iformat->name ? format->iformat->name : "?",
        format->nb_streams, static_cast<long long>(format->duration));
    AVPacket* packet = av_packet_alloc();
    int count = 0;
    while (count < 8 && av_read_frame(format, packet) >= 0) {
        ++count;
        av_packet_unref(packet);
    }
    std::printf("packets=%d\n", count);
    av_packet_free(&packet);
    avformat_close_input(&format);
    return count >= 8 ? 0 : 1;
}
