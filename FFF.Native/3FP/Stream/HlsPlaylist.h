#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Minimal, strict HLS playlist parser for I/O 2.0. It parses master and media
// playlists without touching the network; segment fetching and AES decryption
// stay in the source layer. The parser is deliberately pure so it can be
// tested with literal playlists and reused by DASH/HLS shared segment code.
namespace FFFPlayer::Stream {

struct HlsKey final {
    std::string method;
    std::string uri;
    std::string iv;
    bool valid = false;
};

struct HlsByteRange final {
    std::uint64_t length = 0;
    std::uint64_t offset = 0;
    bool hasRange = false;
    bool offsetExplicit = false;
};

struct HlsSegment final {
    double durationSeconds = 0;
    std::string uri;
    std::uint64_t sequence = 0;
    HlsByteRange range;
    HlsKey key;
    bool discontinuity = false;
};

struct HlsVariant final {
    std::uint64_t bandwidth = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string codecs;
    std::string uri;
};

struct HlsPlaylist final {
    bool valid = false;
    bool master = false;
    bool endList = false;
    bool event = false;
    std::uint32_t targetDurationSeconds = 0;
    std::uint64_t mediaSequence = 0;
    std::string playlistType;
    std::string mapUri;
    std::vector<HlsVariant> variants;
    std::vector<HlsSegment> segments;
};

class HlsParser final {
public:
    // `baseUri` is the playlist URL, used to resolve relative segment/variant
    // URIs. A base ending in '/' is treated as a directory; otherwise the last
    // path component is stripped like a normal URL.
    static bool Parse(std::string_view text, std::string_view baseUri,
        HlsPlaylist& playlist, std::string& error);
    static std::string ResolveUri(std::string_view baseUri, std::string_view uri);
};

} // namespace FFFPlayer::Stream
