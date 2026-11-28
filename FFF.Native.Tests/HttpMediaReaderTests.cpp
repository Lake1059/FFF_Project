#include "Shared/Ffmpeg/HttpMediaReader.h"

extern "C" {
#include <libavutil/error.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: HttpMediaReaderTests <url> <local-file>\n");
        return 2;
    }
    const std::string url = argv[1];
    const std::string localPath = argv[2];
    std::ifstream local(localPath, std::ios::binary);
    std::vector<unsigned char> expected((std::istreambuf_iterator<char>(local)),
        std::istreambuf_iterator<char>());
    if (expected.empty()) {
        std::fprintf(stderr, "cannot read local file\n");
        return 2;
    }
    std::string error;
    AVIOInterruptCB interrupt{};
    auto reader = HttpMediaReader::Open(url, 0, &interrupt, error);
    if (!reader) {
        std::fprintf(stderr, "open failed: %s\n", error.c_str());
        return 1;
    }
    if (!reader->SizeKnown() || reader->Size() != static_cast<std::int64_t>(expected.size())) {
        std::fprintf(stderr, "size mismatch: known=%d size=%lld expected=%zu\n",
            reader->SizeKnown() ? 1 : 0,
            static_cast<long long>(reader->Size()), expected.size());
        return 1;
    }
    std::vector<unsigned char> buffer(32768);
    int read = reader->Read(buffer.data(), static_cast<int>(buffer.size()), 30000,
        interrupt, 0);
    if (read != static_cast<int>(buffer.size()) ||
        std::memcmp(buffer.data(), expected.data(), buffer.size()) != 0) {
        std::fprintf(stderr, "first read mismatch: %d\n", read);
        return 1;
    }
    if (!reader->Seek(0, 30000, interrupt, error)) {
        std::fprintf(stderr, "seek to start failed: %s\n", error.c_str());
        return 1;
    }
    // The 32 KiB AVIO buffer performs many sequential reads inside one Range
    // response; a per-read strict Content-Range check would truncate here.
    std::vector<unsigned char> sequential;
    while (sequential.size() < expected.size()) {
        const int chunk = static_cast<int>((std::min)(
            static_cast<std::size_t>(32768), expected.size() - sequential.size()));
        read = reader->Read(buffer.data(), chunk, 30000, interrupt,
            static_cast<std::int64_t>(sequential.size()));
        if (read == AVERROR_EOF) break;
        if (read < 0) {
            std::fprintf(stderr, "sequential read failed at %zu: %d\n", sequential.size(), read);
            return 1;
        }
        sequential.insert(sequential.end(), buffer.begin(), buffer.begin() + read);
    }
    if (sequential.size() != expected.size() ||
        std::memcmp(sequential.data(), expected.data(), expected.size()) != 0) {
        std::fprintf(stderr, "sequential mismatch: got=%zu expected=%zu\n",
            sequential.size(), expected.size());
        return 1;
    }
    if (!reader->Seek(1000, 30000, interrupt, error)) {
        std::fprintf(stderr, "seek failed: %s\n", error.c_str());
        return 1;
    }
    read = reader->Read(buffer.data(), static_cast<int>(buffer.size()), 30000,
        interrupt, 1000);
    if (read != static_cast<int>(buffer.size()) ||
        std::memcmp(buffer.data(), expected.data() + 1000, buffer.size()) != 0) {
        std::fprintf(stderr, "range read mismatch: %d\n", read);
        return 1;
    }
    if (!reader->Seek(static_cast<std::int64_t>(expected.size()), 30000,
        interrupt, error)) {
        std::fprintf(stderr, "end seek failed: %s\n", error.c_str());
        return 1;
    }
    read = reader->Read(buffer.data(), static_cast<int>(buffer.size()), 30000,
        interrupt, static_cast<std::int64_t>(expected.size()));
    if (read != AVERROR_EOF) {
        std::fprintf(stderr, "end read should be EOF: %d\n", read);
        return 1;
    }
    std::mt19937 random(0x3f3f3f3f);
    std::uniform_int_distribution<std::int64_t> offsets(
        0, static_cast<std::int64_t>(expected.size()) - static_cast<std::int64_t>(buffer.size()));
    for (int i = 0; i < 20; ++i) {
        const auto offset = offsets(random);
        if (!reader->Seek(offset, 30000, interrupt, error)) {
            std::fprintf(stderr, "random seek %d failed: %s\n", i, error.c_str());
            return 1;
        }
        read = reader->Read(buffer.data(), static_cast<int>(buffer.size()), 30000,
            interrupt, offset);
        if (read != static_cast<int>(buffer.size()) ||
            std::memcmp(buffer.data(), expected.data() + offset, buffer.size()) != 0) {
            std::fprintf(stderr, "random read %d mismatch at %lld: %d\n", i,
                static_cast<long long>(offset), read);
            return 1;
        }
    }
    std::printf("PASS http range source size=%zu random-seeks=20\n", expected.size());
    return 0;
}
