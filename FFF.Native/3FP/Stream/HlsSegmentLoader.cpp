#include "pch.h"
#include "3FP/Stream/HlsSegmentLoader.h"

extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace FFFPlayer::Stream {
namespace {
// A segment is fetched into memory as one vector. Without a ceiling, a broken
// or malicious origin can stream forever and turn a 2 s segment into an OOM.
// 64 MiB covers 4K 10 s at ~50 Mbps and keeps nine-route worst cases bounded.
constexpr std::size_t MaxSegmentBytes = 64ull * 1024 * 1024;

bool FetchBytes(const std::string& uri, const FFFPlayer::Io::IoOptions& options,
    const bool hasRange, const std::uint64_t rangeOffset, const std::uint64_t rangeLength,
    std::vector<std::uint8_t>& bytes, std::string& error) {
    FFFPlayer::Io::Result result;
    auto source = FFFPlayer::Io::HttpMediaSource::Create(uri, options, result, error);
    if (!source || result.status != FFFPlayer::Io::Status::Ok) return false;
    if (hasRange && rangeLength > MaxSegmentBytes) {
        error = "HLS segment range exceeds the segment byte ceiling.";
        return false;
    }
    if (hasRange && source->Seek(static_cast<std::int64_t>(rangeOffset), options).status !=
        FFFPlayer::Io::Status::Ok) {
        error = "HLS segment byte range seek failed.";
        return false;
    }
    const auto size = source->Size();
    std::uint64_t expected = rangeLength;
    if (!hasRange && size > 0) expected = static_cast<std::uint64_t>(size);
    if (expected > MaxSegmentBytes) {
        error = "HLS segment size exceeds the segment byte ceiling.";
        return false;
    }
    bytes.clear();
    if (expected != 0) bytes.reserve(static_cast<std::size_t>(expected));
    std::array<std::uint8_t, 64 * 1024> buffer{};
    std::int64_t readPosition = hasRange ? static_cast<std::int64_t>(rangeOffset) : 0;
    int retries = 0;
    while (true) {
        std::uint32_t readBytes = 0;
        const auto read = source->Read(buffer.data(), static_cast<std::uint32_t>(buffer.size()),
            readBytes, options);
        if (read.status == FFFPlayer::Io::Status::Eof) break;
        if (read.status == FFFPlayer::Io::Status::Ok) {
            if (readBytes == 0) break;
            // Check before insert: the HTTP layer may hand us one final chunk
            // that crosses the declared range or the configured ceiling.
            if (readBytes > MaxSegmentBytes - bytes.size()) {
                error = "HLS segment grew beyond the segment byte ceiling.";
                return false;
            }
            bytes.insert(bytes.end(), buffer.data(), buffer.data() + readBytes);
            readPosition += static_cast<std::int64_t>(readBytes);
            if (hasRange && bytes.size() >= rangeLength) break;
            if (!hasRange && size >= 0 && bytes.size() >= static_cast<std::size_t>(size)) break;
            continue;
        }
        if (read.status == FFFPlayer::Io::Status::WouldBlock ||
            read.status == FFFPlayer::Io::Status::Retryable) {
            if (++retries > 4) {
                error = "HLS segment read did not make progress.";
                return false;
            }
            // The failed read leaves the reader request open at readPosition.
            // A Seek is what forces a fresh Range request (and a fresh ledger
            // epoch); simply calling Read again would retry the dead handle.
            const auto seek = source->Seek(readPosition, options);
            if (seek.status != FFFPlayer::Io::Status::Ok) {
                error = "HLS segment retry seek failed.";
                return false;
            }
            Sleep(static_cast<DWORD>(100 * retries));
            continue;
        }
        error = "HLS segment read failed.";
        return false;
    }
    if (hasRange) {
        if (bytes.size() < rangeLength) {
            error = "HLS segment ended before its byte range was complete.";
            return false;
        }
        bytes.resize(static_cast<std::size_t>(rangeLength));
    }
    return true;
}

bool ParseHexByte(const char c, std::uint8_t& value) {
    if (c >= '0' && c <= '9') { value = static_cast<std::uint8_t>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { value = static_cast<std::uint8_t>(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { value = static_cast<std::uint8_t>(c - 'A' + 10); return true; }
    return false;
}
} // namespace

bool HlsSegmentLoader::DeriveIv(const HlsKey& key, const std::uint64_t sequence,
    std::array<std::uint8_t, 16>& iv, std::string& error) noexcept {
    iv.fill(0);
    std::string text = key.iv;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text.erase(0, 2);
    if (text.empty()) {
        // RFC 8216: without an explicit IV the media sequence number is the
        // 128-bit IV, big-endian.
        for (std::size_t i = 0; i < 8; ++i)
            iv[15 - i] = static_cast<std::uint8_t>(sequence >> (8 * i));
        return true;
    }
    if (text.size() != 32) {
        error = "HLS AES IV must be 128 bits of hexadecimal text.";
        return false;
    }
    for (std::size_t i = 0; i < 16; ++i) {
        std::uint8_t high = 0;
        std::uint8_t low = 0;
        if (!ParseHexByte(text[i * 2], high) || !ParseHexByte(text[i * 2 + 1], low)) {
            error = "HLS AES IV contains a non-hexadecimal digit.";
            return false;
        }
        iv[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool HlsSegmentLoader::DecryptAes128(const std::vector<std::uint8_t>& key,
    const std::array<std::uint8_t, 16>& iv, std::vector<std::uint8_t>& bytes,
    std::string& error) noexcept {
    if (key.size() != 16) {
        error = "HLS AES-128 key must be 16 bytes.";
        return false;
    }
    if (bytes.size() % 16 != 0) {
        error = "HLS AES-128 ciphertext is not a multiple of 16 bytes.";
        return false;
    }
    AVAES* aes = av_aes_alloc();
    if (aes == nullptr) {
        error = "Could not allocate the HLS AES context.";
        return false;
    }
    if (av_aes_init(aes, key.data(), 128, 1) < 0) {
        av_free(aes);
        error = "Could not initialize the HLS AES key.";
        return false;
    }
    std::array<std::uint8_t, 16> chain = iv;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 16) {
        av_aes_crypt(aes, bytes.data() + offset, bytes.data() + offset, 1,
            chain.data(), 1);
    }
    av_free(aes);
    return true;
}

bool HlsSegmentLoader::Load(const HlsSegment& segment, const FFFPlayer::Io::IoOptions& options,
    HlsLoadedSegment& output, std::string& error) {
    output = {};
    output.sequence = segment.sequence;
    if (segment.uri.empty()) {
        error = "HLS segment has no resolved URI.";
        return false;
    }
    if (!FetchBytes(segment.uri, options, segment.range.hasRange, segment.range.offset,
        segment.range.length, output.bytes, error)) return false;

    if (!segment.key.valid || segment.key.method == "NONE" ||
        segment.key.method.empty()) return true;
    if (segment.key.method != "AES-128") {
        error = "Unsupported HLS encryption method: " + segment.key.method;
        return false;
    }
    if (segment.key.uri.empty()) {
        error = "HLS AES-128 key has no URI.";
        return false;
    }
    std::vector<std::uint8_t> key;
    if (!FetchBytes(segment.key.uri, options, false, 0, 0, key, error)) return false;
    if (key.size() != 16) {
        error = "HLS AES-128 key response is not 16 bytes.";
        return false;
    }
    std::array<std::uint8_t, 16> iv{};
    if (!DeriveIv(segment.key, segment.sequence, iv, error)) return false;
    return DecryptAes128(key, iv, output.bytes, error);
}

} // namespace FFFPlayer::Stream
