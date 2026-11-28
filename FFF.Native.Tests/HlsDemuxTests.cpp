#include "Shared/Ffmpeg/HttpMediaReader.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <cstdio>
#include <cstdint>
#include <string>

namespace {

struct Adapter final {
    HttpMediaReader* reader = nullptr;
    std::int64_t position = 0;
};

int ReadPacket(void* opaque, std::uint8_t* buffer, int bufferSize) {
    auto* adapter = static_cast<Adapter*>(opaque);
    const int read = adapter->reader->Read(buffer, bufferSize, 30000, {}, adapter->position);
    if (read > 0) adapter->position += read;
    return read;
}

std::int64_t SeekPacket(void* opaque, std::int64_t offset, int whence) {
    auto* adapter = static_cast<Adapter*>(opaque);
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) return adapter->reader->SizeKnown() ? adapter->reader->Size() : AVERROR(ENOSYS);
    std::int64_t target = offset;
    if (whence == SEEK_CUR) target += adapter->position;
    else if (whence == SEEK_END) {
        if (!adapter->reader->SizeKnown()) return AVERROR(ENOSYS);
        target += adapter->reader->Size();
    } else if (whence != SEEK_SET) return AVERROR(EINVAL);
    std::string error;
    if (!adapter->reader->Seek(target, 30000, {}, error)) {
        std::fprintf(stderr, "seek failed: %s\n", error.c_str());
        return AVERROR(EIO);
    }
    adapter->position = target;
    return target;
}

std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    return buffer;
}

} // namespace

int main() {
    const std::string url = "http://127.0.0.1:18866/hls-test/playlist.m3u8";
    std::string error;
    auto reader = HttpMediaReader::Open(url, 0, nullptr, error);
    if (reader == nullptr) {
        std::fprintf(stderr, "HTTP open failed: %s\n", error.c_str());
        return 1;
    }
    Adapter adapter{reader.get(), 0};
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(32768));
    auto* pb = avio_alloc_context(buffer, 32768, 0, &adapter, ReadPacket, nullptr, SeekPacket);
    if (pb == nullptr) return 1;
    auto* format = avformat_alloc_context();
    format->pb = pb;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file,crypto,data,http,https,tcp,tls", 0);
    av_dict_set(&options, "protocol_blacklist", "udp,rtp,rtsp,srt,rist,ftp,ssh", 0);
    const int opened = avformat_open_input(&format, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) {
        std::fprintf(stderr, "HLS open failed: %s\n", FfmpegError(opened).c_str());
        avformat_close_input(&format);
        avio_context_free(&pb);
        return 1;
    }
    const std::string_view name = format->iformat != nullptr && format->iformat->name != nullptr
        ? format->iformat->name : "";
    std::printf("format=%s streams=%d duration=%lld\n", std::string(name).c_str(),
        format->nb_streams, static_cast<long long>(format->duration));
    if (name.find("hls") == std::string_view::npos) {
        std::fprintf(stderr, "not the hls demuxer\n");
        return 1;
    }
    int packetCount = 0;
    AVPacket* packet = av_packet_alloc();
    while (packetCount < 12 && av_read_frame(format, packet) >= 0) {
        ++packetCount;
        av_packet_unref(packet);
    }
    if (packetCount < 12) return 1;
    const int seeked = av_seek_frame(format, -1, 3 * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
    if (seeked < 0) {
        std::fprintf(stderr, "HLS seek failed: %s\n", FfmpegError(seeked).c_str());
        return 1;
    }
    int afterSeek = 0;
    while (afterSeek < 4 && av_read_frame(format, packet) >= 0) {
        ++afterSeek;
        av_packet_unref(packet);
    }
    if (afterSeek < 4) return 1;
    av_packet_free(&packet);
    avformat_close_input(&format);
    avio_context_free(&pb);
    std::printf("PASS HLS demux packets=%d after-seek=%d\n", packetCount, afterSeek);
    return 0;
}
