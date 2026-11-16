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
// The repair rebuilds the `iloc` box around the item named by `pitm`, not around
// whichever item happens to be listed last. If the primary item is not last, the
// old in-place trick would leave its surplus extents where the next item's
// header is read. A rebuilt box has no surplus bytes at all, so the primary item
// may sit anywhere; extents that are not contiguous in file order are copied to
// a new tail extent rather than assumed to be adjacent.
//
// Every other item's fields are preserved. A file whose meta box follows mdat
// only needs appended data; a meta box before mdat shifts all following offsets
// by the box-size delta, and this code adjusts them.
//
// Every file this code does not fully understand is handed back untouched: a
// malformed box is a refusal, never a failure.
class AvifIlocShim final {
public:
    // Reads the whole file when it is small enough to hold in memory, and repairs
    // it in place when it is a layered still image. Returns true only when `bytes`
    // holds a repaired copy; on false `bytes` is left empty and `detail` says why.
    // `mergedExtents` (optional) receives how many extents the primary item was
    // stored as when the repair succeeded -- the signal a caller needs to treat
    // the file as layered, since a merged item is by definition multi-extent.
    static bool ReadAndRepair(const char* pathUtf8, const std::int64_t maxBytes,
        std::vector<std::uint8_t>& bytes, std::string& detail,
        std::int32_t* mergedExtents = nullptr) noexcept;

    // The container edit itself, exposed on bytes so it can be exercised on its own.
    // On entry `bytes` holds a complete file; on a true return it holds the repaired
    // file and nothing else was touched. On false `bytes` is unmodified.
    // `mergedExtents` (optional) receives the repaired item's extent count.
    static bool RepairInPlace(std::vector<std::uint8_t>& bytes, std::string& detail,
        std::int32_t* mergedExtents = nullptr) noexcept;

private:
    struct Box {
        std::int64_t start = 0;   // first byte of the box, header included
        std::int64_t end = 0;     // one past the last byte of the box
        std::int64_t body = 0;    // first byte after the (extended) header
    };

    struct Extent {
        std::uint64_t offset = 0;       // absolute file offset (base_offset applied)
        std::uint64_t relativeOffset = 0;  // value stored in the box
        std::uint64_t length = 0;
        std::int64_t lengthField = 0;  // byte position of this extent's length field
    };

    struct Item {
        std::uint32_t id = 0;
        std::int32_t constructionMethod = 0;
        std::uint16_t dataReferenceIndex = 0;
        std::uint64_t baseOffset = 0;
        std::int64_t extentCountField = 0;  // byte position of the extent_count field
        std::int64_t offsetField = 0;       // byte position of the FIRST extent's offset
        std::vector<Extent> extents;
    };

    static bool FindBox(const std::vector<std::uint8_t>& bytes, const std::int64_t from,
        const std::int64_t to, const char type[5], Box& output) noexcept;
    static bool IsStillImageBrand(const std::vector<std::uint8_t>& bytes, const Box& ftyp) noexcept;
    static bool ParsePitm(const std::vector<std::uint8_t>& bytes, const Box& meta,
        std::uint32_t& itemId, std::string& detail) noexcept;
    static bool ParseIloc(const std::vector<std::uint8_t>& bytes, const Box& iloc,
        std::int32_t& version, std::int32_t& offsetSize, std::int32_t& lengthSize,
        std::int32_t& baseOffsetSize, std::vector<Item>& items, std::string& detail);
    static bool ReadUInt(const std::vector<std::uint8_t>& bytes, std::int64_t& position,
        const std::int64_t end, const std::int32_t width, std::uint64_t& value) noexcept;
    static bool WriteUInt(std::vector<std::uint8_t>& bytes, const std::int64_t position,
        const std::int32_t width, const std::uint64_t value) noexcept;
};
