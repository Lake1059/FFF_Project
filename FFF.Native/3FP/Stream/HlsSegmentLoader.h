#pragma once

#include "3FP/IO/HttpMediaSource.h"
#include "3FP/Stream/HlsPlaylist.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// P3 segment loader. It fetches one HLS segment through the same HTTP source
// used by byte-range media, then applies AES-128-CBC when the playlist key
// applies. It does not concatenate segments or pretend a live playlist is a
// seekable file; that belongs to the HLS demuxer layer.
namespace FFFPlayer::Stream {

struct HlsLoadedSegment final {
    std::vector<std::uint8_t> bytes;
    std::uint64_t sequence = 0;
};

class HlsSegmentLoader final {
public:
    static bool Load(const HlsSegment& segment, const FFFPlayer::Io::IoOptions& options,
        HlsLoadedSegment& output, std::string& error);
    static bool DeriveIv(const HlsKey& key, std::uint64_t sequence,
        std::array<std::uint8_t, 16>& iv, std::string& error) noexcept;
    static bool DecryptAes128(const std::vector<std::uint8_t>& key,
        const std::array<std::uint8_t, 16>& iv, std::vector<std::uint8_t>& bytes,
        std::string& error) noexcept;
};

} // namespace FFFPlayer::Stream
