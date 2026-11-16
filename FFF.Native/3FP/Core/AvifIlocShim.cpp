#include "pch.h"
#include "3FP/Core/AvifIlocShim.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

// Brands that mean "a still image in ISOBMFF", i.e. the files whose repair is
// understood here. AVIF and the HEIF still image family all share the item /
// iloc layout this code edits.
bool IsKnownBrand(const char brand[4]) {
    static const char* const kBrands[] = {
        "avif", "avis", "heic", "heix", "heim", "heis",
        "hevc", "hevx", "hevm", "hevs", "mif1", "msf1", "miaf",
    };
    for (const auto* candidate : kBrands) {
        if (std::memcmp(brand, candidate, 4) == 0) return true;
    }
    return false;
}

std::wstring FromUtf8(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
        result.data(), length);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

}  // namespace

bool AvifIlocShim::FindBox(const std::vector<std::uint8_t>& bytes, const std::int64_t from,
    const std::int64_t to, const char type[5], Box& output) noexcept {
    if (from < 0 || to < from || static_cast<std::uint64_t>(to) > bytes.size()) return false;
    auto position = from;
    while (to - position >= 8) {
        std::uint64_t size = 0;
        std::int64_t header = 8;
        auto cursor = position;
        if (!ReadUInt(bytes, cursor, to, 4, size)) return false;
        if (size == 1) {
            header = 16;
            cursor += 4; // Skip type before the extended size.
            if (!ReadUInt(bytes, cursor, to, 8, size)) return false;
        }
        const auto start = position;
        const auto body = start + header;
        if (size == 0) size = static_cast<std::uint64_t>(to - start);
        if (size < static_cast<std::uint64_t>(header) ||
            size > static_cast<std::uint64_t>(to - start)) return false;
        const auto end = start + static_cast<std::int64_t>(size);
        if (std::memcmp(bytes.data() + start + 4, type, 4) == 0) {
            output = Box{start, end, body};
            return true;
        }
        position = end;
    }
    return false;
}

bool AvifIlocShim::ReadUInt(const std::vector<std::uint8_t>& bytes, std::int64_t& position,
    const std::int64_t end, const std::int32_t width, std::uint64_t& value) noexcept {
    if (width < 0 || width > 8 || position < 0 ||
        end < position || width > end - position ||
        static_cast<std::uint64_t>(end) > bytes.size())
        return false;
    std::uint64_t result = 0;
    for (std::int32_t index = 0; index < width; ++index)
        result = (result << 8) | bytes[static_cast<std::size_t>(position + index)];
    value = result;
    position += width;
    return true;
}

bool AvifIlocShim::WriteUInt(std::vector<std::uint8_t>& bytes, const std::int64_t position,
    const std::int32_t width, const std::uint64_t value) noexcept {
    if (width <= 0 || width > 8 || position < 0 ||
        position + width > static_cast<std::int64_t>(bytes.size()))
        return false;
    for (std::int32_t index = 0; index < width; ++index) {
        const auto shift = 8 * (width - 1 - index);
        bytes[static_cast<std::size_t>(position + index)] =
            static_cast<std::uint8_t>((value >> shift) & 0xFF);
    }
    return true;
}

bool AvifIlocShim::IsStillImageBrand(const std::vector<std::uint8_t>& bytes, const Box& ftyp) noexcept {
    // major_brand, then minor_version, then the compatible brand list.
    const auto end = ftyp.end;
    if (ftyp.body + 8 > end) return false;
    auto major = std::array<char, 4>{};
    std::memcpy(major.data(), bytes.data() + ftyp.body, 4);
    if (IsKnownBrand(major.data())) return true;
    for (auto position = ftyp.body + 8; position + 4 <= end; position += 4) {
        auto brand = std::array<char, 4>{};
        std::memcpy(brand.data(), bytes.data() + position, 4);
        if (IsKnownBrand(brand.data())) return true;
    }
    return false;
}

bool AvifIlocShim::ParsePitm(const std::vector<std::uint8_t>& bytes, const Box& meta,
    std::uint32_t& itemId, std::string& detail) noexcept {
    Box pitm{};
    if (!FindBox(bytes, meta.body + 4, meta.end, "pitm", pitm)) {
        detail = "The metadata box has no primary item box.";
        return false;
    }
    auto position = pitm.body;
    std::uint64_t raw = 0;
    if (!ReadUInt(bytes, position, pitm.end, 1, raw)) {
        detail = "The primary item box is truncated.";
        return false;
    }
    const auto version = static_cast<std::int32_t>(raw);
    if (!ReadUInt(bytes, position, pitm.end, 3, raw) ||
        !ReadUInt(bytes, position, pitm.end, version < 1 ? 2 : 4, raw)) {
        detail = "The primary item box is truncated.";
        return false;
    }
    itemId = static_cast<std::uint32_t>(raw);
    return itemId != 0;
}

bool AvifIlocShim::ParseIloc(const std::vector<std::uint8_t>& bytes, const Box& iloc,
    std::int32_t& version, std::int32_t& offsetSize, std::int32_t& lengthSize,
    std::int32_t& baseOffsetSize, std::vector<Item>& items, std::string& detail) {
    // Field order here is the order FFmpeg's mov_read_iloc reads them, not the
    // plain ISO 14496-12 one: HEIF gives every item a data_reference_index, in
    // version 0 as well, and HEIF version 1/2 add an index before each extent.
    const auto end = iloc.end;
    auto position = iloc.body;
    std::uint64_t raw = 0;
    if (!ReadUInt(bytes, position, end, 1, raw)) { detail = "The item location box is truncated."; return false; }
    version = static_cast<std::int32_t>(raw);
    if (!ReadUInt(bytes, position, end, 3, raw)) { detail = "The item location box is truncated."; return false; }
    std::uint64_t packed = 0;
    if (!ReadUInt(bytes, position, end, 1, packed)) { detail = "The item location box is truncated."; return false; }
    offsetSize = static_cast<std::int32_t>(packed >> 4);
    lengthSize = static_cast<std::int32_t>(packed & 0xF);
    if (!ReadUInt(bytes, position, end, 1, packed)) { detail = "The item location box is truncated."; return false; }
    baseOffsetSize = static_cast<std::int32_t>(packed >> 4);
    const auto indexSize = version ? static_cast<std::int32_t>(packed & 0xF) : 0;
    if (indexSize != 0) { detail = "The item location box indexes its extents; that layout is not repaired."; return false; }
    if (offsetSize == 0 || lengthSize == 0 || offsetSize > 8 || lengthSize > 8 || baseOffsetSize > 8) { detail = "The item location box uses unsupported field widths."; return false; }
    std::uint64_t itemCount = 0;
    if (!ReadUInt(bytes, position, end, version < 2 ? 2 : 4, itemCount)) { detail = "The item location box is truncated."; return false; }
    if (itemCount == 0 || itemCount > 4096) { detail = "The item location box has an unsupported item count."; return false; }
    items.clear();
    items.reserve(static_cast<std::size_t>(itemCount));
    std::uint64_t totalExtents = 0;
    for (std::uint64_t index = 0; index < itemCount; ++index) {
        Item item;
        if (!ReadUInt(bytes, position, end, version < 2 ? 2 : 4, raw)) { detail = "The item location box is truncated."; return false; }
        item.id = static_cast<std::uint32_t>(raw);
        if (version > 0) {
            if (!ReadUInt(bytes, position, end, 2, raw)) { detail = "The item location box is truncated."; return false; }
            item.constructionMethod = static_cast<std::int32_t>(raw & 0xF);
        }
        // data_reference_index.
        if (!ReadUInt(bytes, position, end, 2, raw)) { detail = "The item location box is truncated."; return false; }
        item.dataReferenceIndex = static_cast<std::uint16_t>(raw);
        std::uint64_t baseOffset = 0;
        if (!ReadUInt(bytes, position, end, baseOffsetSize, baseOffset)) { detail = "The item location box is truncated."; return false; }
        item.baseOffset = baseOffset;
        item.extentCountField = position;
        std::uint64_t extentCount = 0;
        if (!ReadUInt(bytes, position, end, 2, extentCount)) { detail = "The item location box is truncated."; return false; }
        if (extentCount > 65536 - totalExtents ||
            extentCount > static_cast<std::uint64_t>((end - position) / (offsetSize + lengthSize))) {
            detail = "The item location box exceeds the extent budget or is truncated.";
            return false;
        }
        totalExtents += extentCount;
        auto firstOffsetField = position;
        for (std::uint64_t extent = 0; extent < extentCount; ++extent) {
            Extent entry;
            if (!ReadUInt(bytes, position, end, offsetSize, entry.relativeOffset)) { detail = "The item location box is truncated."; return false; }
            entry.lengthField = position;
            if (!ReadUInt(bytes, position, end, lengthSize, entry.length)) { detail = "The item location box is truncated."; return false; }
            if (entry.relativeOffset > UINT64_MAX - baseOffset) { detail = "The item offset overflows."; return false; }
            entry.offset = entry.relativeOffset + baseOffset;
            if (extent == 0) item.offsetField = firstOffsetField;
            item.extents.push_back(entry);
        }
        items.push_back(std::move(item));
    }
    return true;
}

bool AvifIlocShim::RepairInPlace(std::vector<std::uint8_t>& bytes, std::string& detail,
    std::int32_t* mergedExtents) noexcept try {
    if (mergedExtents != nullptr) *mergedExtents = 0;
    const auto size = static_cast<std::int64_t>(bytes.size());
    Box ftyp{};
    if (!FindBox(bytes, 0, size, "ftyp", ftyp)) { detail = "The file has no file type box."; return false; }
    if (!IsStillImageBrand(bytes, ftyp)) { detail = "The file is not an AVIF or HEIF still image."; return false; }
    Box meta{};
    if (!FindBox(bytes, 0, size, "meta", meta)) { detail = "The file has no metadata box."; return false; }
    // meta is a full box: four bytes of version and flags precede its children.
    const auto metaChildren = meta.body + 4;
    if (metaChildren >= meta.end) { detail = "The metadata box is truncated."; return false; }
    Box iloc{};
    if (!FindBox(bytes, metaChildren, meta.end, "iloc", iloc)) { detail = "The file has no item location box."; return false; }

    std::uint32_t primaryItemId = 0;
    const bool hasPitm = ParsePitm(bytes, meta, primaryItemId, detail);
    std::int32_t version = 0;
    std::int32_t offsetSize = 0;
    std::int32_t lengthSize = 0;
    std::int32_t baseOffsetSize = 0;
    std::vector<Item> items;
    if (!ParseIloc(bytes, iloc, version, offsetSize, lengthSize, baseOffsetSize, items, detail))
        return false;
    if (items.empty()) { detail = "The item location box lists no items."; return false; }
    // The standard names the primary item. A legacy file without pitm keeps the
    // old "last item" fallback, but only when there is no primary box at all.
    if (!hasPitm) {
        primaryItemId = items.back().id;
        detail = "The metadata box has no primary item box; using the last item.";
    }
    Item* target = nullptr;
    for (auto& item : items) {
        if (item.id == primaryItemId) { target = &item; break; }
    }
    if (target == nullptr) { detail = "The primary item is not listed in the item location box."; return false; }
    if (target->extents.size() < 2) { detail = "The primary item is stored as a single extent."; return false; }
    if (target->constructionMethod != 0) { detail = "The item does not use plain file offsets."; return false; }
    if (target->dataReferenceIndex != 0) { detail = "The primary item's data is in an external file."; return false; }
    if (offsetSize <= 0 || lengthSize <= 0 || baseOffsetSize < 0 || baseOffsetSize > 8) {
        detail = "The item location box uses unusable field widths.";
        return false;
    }

    // Merge in item order, not file order. Extents are a logical sequence (base
    // layer, then enhancements); their file offsets may be in any order. The
    // contiguous fast path is only when each extent starts where the previous
    // one ended.
    std::uint64_t span = 0;
    bool contiguous = true;
    for (std::size_t index = 0; index < target->extents.size(); ++index) {
        const auto& extent = target->extents[index];
        if (extent.length > UINT64_MAX - span) { detail = "The item's merged length overflows."; return false; }
        if (index > 0) {
            const auto& previous = target->extents[index - 1];
            if (previous.length > UINT64_MAX - previous.offset ||
                previous.offset + previous.length != extent.offset) contiguous = false;
        }
        span += extent.length;
    }
    if (span == 0) { detail = "The item's extents are empty."; return false; }
    const auto oldSize = static_cast<std::uint64_t>(bytes.size());
    // Overlapping extents can otherwise duplicate bytes and grow the merged copy
    // past the source file itself.
    if (span > oldSize) { detail = "The item's merged length exceeds the file size."; return false; }
    std::vector<std::uint8_t> copied;
    std::uint64_t targetStart = 0;
    if (contiguous) {
        targetStart = target->extents.front().offset;
        if (targetStart > oldSize || span > oldSize - targetStart) {
            detail = "The item's extents reach outside the file.";
            return false;
        }
    } else {
        copied.reserve(static_cast<std::size_t>(span));
        for (const auto& extent : target->extents) {
            if (extent.offset > oldSize || extent.length > oldSize - extent.offset) {
                detail = "The item's extents reach outside the file.";
                return false;
            }
            copied.insert(copied.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(extent.offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(extent.offset + extent.length));
        }
    }
    const auto widthBits = 8 * lengthSize;
    if (widthBits < 64 && span >= (static_cast<std::uint64_t>(1) << widthBits)) {
        detail = "The merged extent is too long for the item location box's length field.";
        return false;
    }

    // Compute the rebuilt box size without writing it: the target loses all but
    // one extent and its base_offset becomes zero, so its fields shrink.
    const auto itemCountWidth = version < 2 ? 2 : 4;
    const auto ilocHeader = static_cast<std::int32_t>(iloc.body - iloc.start);
    std::uint64_t payloadSize = 4u + 2u + static_cast<std::uint64_t>(itemCountWidth);
    for (const auto& item : items) {
        payloadSize += itemCountWidth;
        if (version > 0) payloadSize += 2;
        payloadSize += 2;  // data_reference_index
        payloadSize += static_cast<std::uint64_t>(baseOffsetSize);
        payloadSize += 2;  // extent_count
        const auto extentCount = &item == target ? 1u : item.extents.size();
        payloadSize += static_cast<std::uint64_t>(extentCount) *
            static_cast<std::uint64_t>(offsetSize + lengthSize);
    }
    const auto newIlocSize = static_cast<std::uint64_t>(ilocHeader) + payloadSize;
    if (newIlocSize > UINT32_MAX) { detail = "The rebuilt item location box is too large."; return false; }
    const auto oldMetaSize = meta.end - meta.start;
    const auto metaHeader = static_cast<std::int32_t>(meta.body - meta.start);
    const auto childrenBefore = static_cast<std::uint64_t>(iloc.start - (meta.body + 4));
    const auto childrenAfter = static_cast<std::uint64_t>(meta.end - iloc.end);
    const auto newMetaSize = static_cast<std::uint64_t>(metaHeader) + 4u +
        childrenBefore + newIlocSize + childrenAfter;
    const auto delta = static_cast<std::int64_t>(newMetaSize) - oldMetaSize;
    const auto newSize = static_cast<std::int64_t>(size) + delta + static_cast<std::int64_t>(copied.size());
    if (newSize < 0 || newSize > static_cast<std::int64_t>(UINT32_MAX)) {
        detail = "The repaired file would exceed the item location box's size range.";
        return false;
    }

    // An absolute offset after the edited meta box moves by delta; an offset
    // before it does not. Appended data starts at the end of the old content,
    // which is itself shifted by delta.
    const auto shiftedOffset = [&](const std::uint64_t offset) noexcept {
        return offset >= static_cast<std::uint64_t>(meta.end)
            ? offset + static_cast<std::uint64_t>(delta)
            : offset;
    };
    if (contiguous) targetStart = shiftedOffset(targetStart);
    else targetStart = static_cast<std::uint64_t>(newSize) - copied.size();

    const auto writeField = [](std::vector<std::uint8_t>& output,
        const std::int32_t width, const std::uint64_t value) noexcept {
        for (std::int32_t index = 0; index < width; ++index) {
            const auto shift = 8 * (width - 1 - index);
            output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
    };
    const auto fitsField = [](const std::int32_t width, const std::uint64_t value) noexcept {
        return width >= 8 || value < (static_cast<std::uint64_t>(1) << (8 * width));
    };
    if (!fitsField(offsetSize, targetStart)) {
        detail = "The merged extent offset does not fit the item location box's offset field.";
        return false;
    }

    std::vector<std::uint8_t> newIloc;
    newIloc.reserve(static_cast<std::size_t>(newIlocSize));
    if (ilocHeader == 16) {
        writeField(newIloc, 4, 1);
        newIloc.insert(newIloc.end(), {'i', 'l', 'o', 'c'});
        writeField(newIloc, 8, newIlocSize);
    } else {
        writeField(newIloc, 4, newIlocSize);
        newIloc.insert(newIloc.end(), {'i', 'l', 'o', 'c'});
    }
    // Preserve version and flags exactly.
    newIloc.insert(newIloc.end(), bytes.begin() + static_cast<std::ptrdiff_t>(iloc.body),
        bytes.begin() + static_cast<std::ptrdiff_t>(iloc.body + 4));
    writeField(newIloc, 1, static_cast<std::uint64_t>((offsetSize << 4) | lengthSize));
    writeField(newIloc, 1, static_cast<std::uint64_t>((baseOffsetSize << 4) |
        (version ? 0 : 0)));
    writeField(newIloc, itemCountWidth, items.size());
    for (const auto& item : items) {
        writeField(newIloc, itemCountWidth, item.id);
        if (version > 0)
            writeField(newIloc, 2, static_cast<std::uint64_t>(item.constructionMethod & 0xF));
        writeField(newIloc, 2, item.dataReferenceIndex);
        // Only construction method 0 in this file carries absolute file offsets.
        // Items stored in idat (method 1) or in an external data reference keep
        // their offsets relative to that store; shifting them with the meta box
        // would point them at unrelated bytes.
        const bool fileOffsetItem = item.constructionMethod == 0 && item.dataReferenceIndex == 0;
        const auto newBase = &item == target ? 0u :
            fileOffsetItem ? shiftedOffset(item.baseOffset) : item.baseOffset;
        if (!fitsField(baseOffsetSize, newBase)) {
            detail = "An item base offset does not fit the item location box's base offset field.";
            return false;
        }
        writeField(newIloc, baseOffsetSize, newBase);
        const auto extentCount = &item == target ? 1u : item.extents.size();
        writeField(newIloc, 2, extentCount);
        if (&item == target) {
            writeField(newIloc, offsetSize, targetStart);
            writeField(newIloc, lengthSize, span);
        } else {
            for (const auto& extent : item.extents) {
                const auto absolute = fileOffsetItem ? shiftedOffset(extent.offset) : extent.offset;
                if (absolute < newBase) {
                    detail = "An item extent precedes its base offset.";
                    return false;
                }
                const auto relative = absolute - newBase;
                if (!fitsField(offsetSize, relative)) {
                    detail = "An item extent offset does not fit the item location box's offset field.";
                    return false;
                }
                writeField(newIloc, offsetSize, relative);
                writeField(newIloc, lengthSize, extent.length);
            }
        }
    }

    std::vector<std::uint8_t> newMeta;
    newMeta.reserve(static_cast<std::size_t>(newMetaSize));
    if (metaHeader == 16) {
        writeField(newMeta, 4, 1);
        newMeta.insert(newMeta.end(), {'m', 'e', 't', 'a'});
        writeField(newMeta, 8, newMetaSize);
    } else {
        writeField(newMeta, 4, newMetaSize);
        newMeta.insert(newMeta.end(), {'m', 'e', 't', 'a'});
    }
    newMeta.insert(newMeta.end(), bytes.begin() + static_cast<std::ptrdiff_t>(meta.body),
        bytes.begin() + static_cast<std::ptrdiff_t>(meta.body + 4));
    newMeta.insert(newMeta.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(meta.body + 4),
        bytes.begin() + static_cast<std::ptrdiff_t>(iloc.start));
    newMeta.insert(newMeta.end(), newIloc.begin(), newIloc.end());
    newMeta.insert(newMeta.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(iloc.end),
        bytes.begin() + static_cast<std::ptrdiff_t>(meta.end));

    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(newSize));
    output.insert(output.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(meta.start));
    output.insert(output.end(), newMeta.begin(), newMeta.end());
    output.insert(output.end(), bytes.begin() + static_cast<std::ptrdiff_t>(meta.end), bytes.end());
    output.insert(output.end(), copied.begin(), copied.end());
    // Build the success detail before replacing the caller's buffer: if the
    // allocation throws inside this noexcept boundary, bytes must stay unchanged.
    auto success = "Merged " + std::to_string(target->extents.size()) +
        " extents of primary item " + std::to_string(primaryItemId) + " into one.";
    bytes = std::move(output);
    detail = std::move(success);
    if (mergedExtents != nullptr) *mergedExtents = static_cast<std::int32_t>(target->extents.size());
    return true;
} catch (...) {
    detail.clear();
    return false;
}

bool AvifIlocShim::ReadAndRepair(const char* pathUtf8, const std::int64_t maxBytes,
    std::vector<std::uint8_t>& bytes, std::string& detail,
    std::int32_t* mergedExtents) noexcept try {
    if (mergedExtents != nullptr) *mergedExtents = 0;
    bytes.clear();
    detail.clear();
    if (pathUtf8 == nullptr || *pathUtf8 == '\0' || maxBytes <= 0) { detail = "The path is not usable."; return false; }
    const auto path = FromUtf8(pathUtf8);
    if (path.empty()) { detail = "The path is not valid UTF-8."; return false; }
    const auto file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { detail = "Could not open the file with shared access."; return false; }
    struct FileCloser { HANDLE file; ~FileCloser() { CloseHandle(file); } } closer{file};
    LARGE_INTEGER fileSize{};
    const auto sized = GetFileSizeEx(file, &fileSize);
    if (!sized || fileSize.QuadPart < 0) { detail = "Could not read the file size."; return false; }
    if (fileSize.QuadPart > maxBytes || fileSize.QuadPart > MAXDWORD) { detail = "The file is larger than the still-image repair limit."; return false; }
    // Small enough that reading it whole costs less than the demuxer's own probing:
    // still images are the only files that reach this code.
    std::vector<std::uint8_t> content(static_cast<std::size_t>(fileSize.QuadPart));
    DWORD read = 0;
    const auto ok = content.empty()
        ? TRUE
        : ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr);
    if (!ok || static_cast<std::size_t>(read) != content.size()) {
        detail = "Could not read the whole file.";
        return false;
    }
    auto reason = std::string{};
    if (!RepairInPlace(content, reason, mergedExtents)) { detail = std::move(reason); return false; }
    bytes = std::move(content);
    detail = std::move(reason);
    return true;
} catch (...) {
    bytes.clear();
    detail.clear();
    return false;
}
