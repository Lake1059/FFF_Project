#include "3FP/Stream/HlsSegmentLoader.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>

using namespace FFFPlayer::Stream;

namespace {
std::uint64_t ReadCounter(const char* path) {
    std::ifstream input(path);
    std::uint64_t value = 0;
    input >> value;
    return value;
}
} // namespace

int main(int argc, char** argv) {
    if (argc >= 3) {
        HlsSegment segment;
        segment.uri = argv[1];
        segment.sequence = 100;
        FFFPlayer::Io::IoOptions options;
        options.openDeadlineMs = 15000;
        options.readIdleTimeoutMs = 30000;
        options.seekDeadlineMs = 30000;
        const auto before = ReadCounter(argv[2]);
        HlsLoadedSegment loaded;
        std::string error;
        if (!HlsSegmentLoader::Load(segment, options, loaded, error)) {
            std::fprintf(stderr, "large segment load failed: %s\n", error.c_str());
            return 1;
        }
        const auto after = ReadCounter(argv[2]);
        const auto requests = after >= before ? after - before : 0;
        if (loaded.bytes.size() < 1024 * 1024) {
            std::fprintf(stderr, "large segment too small: %zu\n", loaded.bytes.size());
            return 1;
        }
        // One Range response must serve the whole segment. Before this guard
        // the loader opened a fresh request every 64 KiB (48 requests for a
        // 3 MB segment). Allow two for the optional initial range seek.
        if (requests > 2) {
            std::fprintf(stderr, "segment request amplification: requests=%llu\n",
                static_cast<unsigned long long>(requests));
            return 1;
        }
        std::printf("PASS HLS large segment bytes=%zu requests=%llu\n",
            loaded.bytes.size(), static_cast<unsigned long long>(requests));
        return 0;
    }

    HlsKey key;
    key.valid = true;
    key.method = "AES-128";
    key.iv = "0x00000000000000000000000000000000";
    std::array<std::uint8_t, 16> iv{};
    std::string error;
    if (!HlsSegmentLoader::DeriveIv(key, 0, iv, error)) return 1;
    if (iv[0] != 0 || iv[15] != 0) return 1;

    key.iv.clear();
    if (!HlsSegmentLoader::DeriveIv(key, 0x0102030405060708ull, iv, error)) return 1;
    if (iv[8] != 1 || iv[9] != 2 || iv[15] != 8) return 1;

    // FIPS-197 AES-128 ECB vector; CBC with a zero IV has the same first block.
    std::vector<std::uint8_t> aesKey(16);
    for (std::uint32_t i = 0; i < 16; ++i) aesKey[i] = static_cast<std::uint8_t>(i);
    std::vector<std::uint8_t> cipher = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
    };
    std::array<std::uint8_t, 16> zeroIv{};
    if (!HlsSegmentLoader::DecryptAes128(aesKey, zeroIv, cipher, error)) return 1;
    const std::uint8_t expected[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    if (std::memcmp(cipher.data(), expected, sizeof(expected)) != 0) {
        std::fprintf(stderr, "AES decrypt mismatch\n");
        return 1;
    }

    HlsSegment segment;
    segment.uri = "http://127.0.0.1:18865/test.mp4";
    segment.sequence = 100;
    FFFPlayer::Io::IoOptions options;
    options.openDeadlineMs = 15000;
    options.readIdleTimeoutMs = 30000;
    options.seekDeadlineMs = 30000;
    HlsLoadedSegment loaded;
    if (!HlsSegmentLoader::Load(segment, options, loaded, error)) {
        std::fprintf(stderr, "segment load failed: %s\n", error.c_str());
        return 1;
    }
    if (loaded.bytes.size() != 16121 || loaded.sequence != 100) {
        std::fprintf(stderr, "segment size=%zu\n", loaded.bytes.size());
        return 1;
    }

    segment.range.hasRange = true;
    segment.range.offset = 1000;
    segment.range.length = 4096;
    HlsLoadedSegment ranged;
    if (!HlsSegmentLoader::Load(segment, options, ranged, error) ||
        ranged.bytes.size() != 4096) return 1;

    segment.range.offset = 0;
    segment.range.length = 65ull * 1024 * 1024;
    HlsLoadedSegment oversized;
    error.clear();
    if (HlsSegmentLoader::Load(segment, options, oversized, error) ||
        error.find("byte ceiling") == std::string::npos) {
        std::fprintf(stderr, "oversized segment was not rejected: %s\n", error.c_str());
        return 1;
    }

    std::printf("PASS HLS segment loader\n");
    return 0;
}
