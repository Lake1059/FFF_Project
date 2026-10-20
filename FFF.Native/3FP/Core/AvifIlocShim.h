#pragma once

#include "framework.h"

#include <cstdint>
#include <string>
#include <vector>

// Repairs the one thing that stops a layered ("progressive") AVIF from opening.
//
// A progressive AVIF stores its primary image item as several AV1 spatial layers,
// and libavif lays those layers out as several extents inside mdat: the base layer
// first, each enhancement layer after it, back to back. FFmpeg's mov demuxer
// refuses the file before it reaches any decoder:
//
//     mov_read_iloc: if (extent_count > 1) {
//         avpriv_report_missing_feature(fc, "iloc: extent_count > 1");
//         return AVERROR_PATCHWELCOME;
//     }
//
// So the item never gets decoded, and the decoder is not the problem: given the
// same bytes as one extent, libdav1d decodes the top layer and reports the final
// image (measured 32.0 dB PSNR against the same source encoded without layers).
//
// Because the extents of a layered item are contiguous, "several extents" and
// "one extent spanning them" describe the same bytes. The repair is therefore a
// container edit, not a re-encode: set extent_count to 1 and widen the first
// extent's length to cover the span. Every other byte of the file is left alone.
//
// The extents that follow the first one are left in place as dead bytes. That is
// deliberate and safe: mov_read_iloc reads one extent and returns, and mov_read_default
// then positions itself from the box's *declared* size (`left = a.size - avio_tell(pb)
// + start_pos; if (left > 0) avio_skip(pb, left);`), so the leftovers are skipped as
// "garbage at atom end". That guarantee only holds for the last item in the box --
// an earlier item's leftovers would be read as the next item's header -- so a
// multi-extent item is only repaired when it is the last one listed.
//
// Every other file is handed back untouched, including every file this code does
// not fully understand. Nothing here throws, and a malformed box is a refusal,
// never a failure: the caller falls back to opening the file as it is, which
// produces FFmpeg's own error rather than one invented here.
class AvifIlocShim final {
public:
    // Reads the whole file when it is small enough to hold in memory, and repairs
    // it in place when it is a layered still image. Returns true only when `bytes`
    // holds a repaired copy; on false `bytes` is left empty and `detail` says why.
    static bool ReadAndRepair(const char* pathUtf8, const std::int64_t maxBytes,
        std::vector<std::uint8_t>& bytes, std::string& detail) noexcept;

    // The container edit itself, exposed on bytes so it can be exercised on its own.
    // On entry `bytes` holds a complete file; on a true return it holds the repaired
    // file and nothing else was touched. On false `bytes` is unmodified.
    static bool RepairInPlace(std::vector<std::uint8_t>& bytes, std::string& detail) noexcept;

private:
    struct Box {
        std::int64_t start = 0;   // first byte of the box, header included
        std::int64_t end = 0;     // one past the last byte of the box
        std::int64_t body = 0;    // first byte after the (extended) header
    };

    struct Extent {
        std::uint64_t offset = 0;
        std::uint64_t length = 0;
        std::int64_t lengthField = 0;  // byte position of this extent's length field
    };

    struct Item {
        std::uint32_t id = 0;
        std::int32_t constructionMethod = 0;
        std::int64_t extentCountField = 0;  // byte position of the extent_count field
        std::int64_t offsetField = 0;       // byte position of the FIRST extent's offset
        std::vector<Extent> extents;
    };

    static bool FindBox(const std::vector<std::uint8_t>& bytes, const std::int64_t from,
        const std::int64_t to, const char type[5], Box& output) noexcept;
    static bool IsStillImageBrand(const std::vector<std::uint8_t>& bytes, const Box& ftyp) noexcept;
    static bool ParseIloc(const std::vector<std::uint8_t>& bytes, const Box& iloc,
        std::int32_t& version, std::int32_t& offsetSize, std::int32_t& lengthSize,
        std::vector<Item>& items, std::string& detail) noexcept;
    static bool ReadUInt(const std::vector<std::uint8_t>& bytes, std::int64_t& position,
        const std::int64_t end, const std::int32_t width, std::uint64_t& value) noexcept;
    static bool WriteUInt(std::vector<std::uint8_t>& bytes, const std::int64_t position,
        const std::int32_t width, const std::uint64_t value) noexcept;
};
