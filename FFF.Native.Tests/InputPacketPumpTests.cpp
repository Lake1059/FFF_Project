#include "3FP/IO/InputPacketPump.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, argv[1], nullptr, nullptr) < 0 || format == nullptr) {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return 1;
    }

    FFFPlayer::Io::InputPacketPump pump;
    if (!pump.Start(format, 4ull * 1024 * 1024, [] { return 0; })) {
        avformat_close_input(&format);
        return 1;
    }
    std::uint64_t packets = 0;
    std::uint64_t bytes = 0;
    AVPacket* packet = av_packet_alloc();
    if (packet == nullptr) return 1;
    for (int spin = 0; spin < 20000; ++spin) {
        if (pump.TryPop(*packet)) {
            bytes += static_cast<std::uint64_t>(packet->size > 0 ? packet->size : 0);
            ++packets;
            av_packet_unref(packet);
            continue;
        }
        if (pump.EndOfStream()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    av_packet_free(&packet);
    const auto readPackets = pump.PacketsRead();
    const auto readBytes = pump.BytesRead();
    pump.Stop();
    avformat_close_input(&format);
    if (packets == 0 || readPackets == 0 || readBytes == 0 || packets != readPackets) {
        std::fprintf(stderr, "pump mismatch packets=%llu read=%llu bytes=%llu\n",
            static_cast<unsigned long long>(packets),
            static_cast<unsigned long long>(readPackets),
            static_cast<unsigned long long>(readBytes));
        return 1;
    }
    std::printf("PASS input packet pump packets=%llu bytes=%llu queued-bytes=%llu\n",
        static_cast<unsigned long long>(packets),
        static_cast<unsigned long long>(bytes),
        static_cast<unsigned long long>(readBytes));
    return 0;
}
