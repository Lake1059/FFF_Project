#include "3FP/IO/HttpMediaSource.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace FFFPlayer::Io;

int main() {
    const std::string url = "http://127.0.0.1:18865/test.mp4";
    IoOptions options;
    options.openDeadlineMs = 15000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 30000;
    Result result;
    std::string error;
    auto source = HttpMediaSource::Create(url, options, result, error);
    if (!source || result.status != Status::Ok) {
        std::fprintf(stderr, "HTTP source create failed: %s\n", error.c_str());
        return 1;
    }
    if (source->Size() != 16121) {
        std::fprintf(stderr, "size=%lld\n", static_cast<long long>(source->Size()));
        return 1;
    }
    if (!HasFeature(source->Capabilities().features, SourceFeature::RangeRequests) ||
        !source->Capabilities().ByteExact()) return 1;
    std::uint8_t buffer[4096]{};
    std::uint32_t read = 0;
    const auto first = source->Read(buffer, sizeof(buffer), read, options);
    if (first.status != Status::Ok || read != 4096) {
        std::fprintf(stderr, "first read status=%u error=%u read=%u\n",
            static_cast<unsigned>(first.status), static_cast<unsigned>(first.error), read);
        return 1;
    }
    const auto seek = source->Seek(1000, options);
    if (seek.status != Status::Ok) {
        std::fprintf(stderr, "seek status=%u error=%u\n",
            static_cast<unsigned>(seek.status), static_cast<unsigned>(seek.error));
        return 1;
    }
    const auto second = source->Read(buffer, sizeof(buffer), read, options);
    if (second.status != Status::Ok || read != 4096) {
        std::fprintf(stderr, "second read status=%u error=%u read=%u\n",
            static_cast<unsigned>(second.status), static_cast<unsigned>(second.error), read);
        return 1;
    }
    if (source->Seek(16121, options).status != Status::Ok) {
        std::fprintf(stderr, "end seek failed\n");
        return 1;
    }
    const auto eof = source->Read(buffer, sizeof(buffer), read, options);
    if (eof.status != Status::Eof) {
        std::fprintf(stderr, "eof status=%u error=%u read=%u\n",
            static_cast<unsigned>(eof.status), static_cast<unsigned>(eof.error), read);
        return 1;
    }
    source->Close();
    std::printf("PASS HttpMediaSource adapter\n");
    return 0;
}
