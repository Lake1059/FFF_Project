#include "3FP/IO/HttpMediaSource.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace FFFPlayer::Io;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: HttpExtremeTests <base-url> <mode>\n");
        return 2;
    }
    const std::string base = argv[1];
    const std::string url = std::strcmp(argv[2], "truncated") == 0
        ? base + "/__truncate/4096/test.mp4"
        : (std::strcmp(argv[2], "disconnected") == 0
            ? base + "/__disconnect/8192/test.mp4"
            : base + "/__" + argv[2] + "/test.mp4");
    IoOptions options;
    options.openDeadlineMs = 15000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 30000;
    Result result;
    std::string error;

    if (std::strcmp(argv[2], "no_range") == 0) {
        auto source = HttpMediaSource::Create(url, options, result, error);
        if (!source || result.status != Status::Ok || source->Size() != 16121) {
            std::fprintf(stderr, "no-range open failed: %s\n", error.c_str());
            return 1;
        }
        const auto capabilities = source->Capabilities();
        if (capabilities.RandomAccess() ||
            HasFeature(capabilities.features, SourceFeature::RangeRequests)) {
            std::fprintf(stderr, "no-range source advertised random access\n");
            return 1;
        }
        std::uint8_t buffer[4096]{};
        std::uint32_t read = 0;
        if (source->Read(buffer, sizeof(buffer), read, options).status != Status::Ok ||
            read != sizeof(buffer)) {
            std::fprintf(stderr, "no-range first read failed\n");
            return 1;
        }
        const auto seek = source->Seek(1000, options);
        if (seek.status != Status::Fatal || seek.error != Error::RangeUnsupported) {
            std::fprintf(stderr, "no-range seek was not rejected: status=%u error=%u\n",
                static_cast<unsigned>(seek.status), static_cast<unsigned>(seek.error));
            return 1;
        }
        std::printf("PASS no-range source flags random=0 range=0 seek=RangeUnsupported\n");
        return 0;
    }

    if (std::strcmp(argv[2], "disconnected") == 0) {
        auto source = HttpMediaSource::Create(url, options, result, error);
        if (!source || result.status != Status::Ok || source->Size() != 16121) {
            std::fprintf(stderr, "disconnect open failed: %s\n", error.c_str());
            return 1;
        }
        std::uint8_t buffer[4096]{};
        std::uint32_t read = 0;
        std::uint32_t total = 0;
        Status last = Status::Ok;
        for (;;) {
            const auto next = source->Read(buffer, sizeof(buffer), read, options);
            last = next.status;
            if (next.status != Status::Ok) break;
            if (read == 0) break;
            total += read;
            if (total > 16121) break;
        }
        if (last != Status::Eof || total != 16121) {
            std::fprintf(stderr,
                "disconnect resume failed status=%u total=%u\n",
                static_cast<unsigned>(last), total);
            return 1;
        }
        std::printf("PASS transient disconnect resumed total=%u bytes\n", total);
        return 0;
    }

    if (std::strcmp(argv[2], "truncated") == 0) {
        auto source = HttpMediaSource::Create(url, options, result, error);
        if (!source || result.status != Status::Ok) {
            std::fprintf(stderr, "truncated open failed: %s\n", error.c_str());
            return 1;
        }
        std::uint8_t buffer[4096]{};
        std::uint32_t read = 0;
        std::uint32_t total = 0;
        Status last = Status::Ok;
        for (;;) {
            const auto next = source->Read(buffer, sizeof(buffer), read, options);
            last = next.status;
            if (next.status != Status::Ok) break;
            if (read == 0) break;
            total += read;
        }
        if (last == Status::Eof) {
            std::fprintf(stderr, "truncated response was accepted as EOF after %u bytes\n", total);
            return 1;
        }
        if (last != Status::Fatal) {
            std::fprintf(stderr, "truncated response status=%u after %u bytes\n",
                static_cast<unsigned>(last), total);
            return 1;
        }
        std::printf("PASS truncated response rejected after=%u bytes\n", total);
        return 0;
    }

    if (std::strcmp(argv[2], "wrong_range") == 0) {
        auto source = HttpMediaSource::Create(url, options, result, error);
        if (source || result.status == Status::Ok) {
            std::fprintf(stderr, "wrong Content-Range was accepted\n");
            return 1;
        }
        std::printf("PASS wrong Content-Range rejected: %s\n", error.c_str());
        return 0;
    }

    std::fprintf(stderr, "unknown mode: %s\n", argv[2]);
    return 2;
}
