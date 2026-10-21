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
    auto position = from;
    while (position + 8 <= to) {
        std::uint64_t size = 0;
        std::int64_t header = 8;
        auto cursor = position;
        if (!ReadUInt(bytes, cursor, to, 4, size)) return false;
        if (size == 1) {
            header = 16;
            if (!ReadUInt(bytes, cursor, to, 8, size)) return false;
        }
        const auto start = position;
        const auto body = start + header;
        if (size == 0) size = static_cast<std::uint64_t>(to - start);
        if (size < static_cast<std::uint64_t>(header)) return false;
        const auto end = start + static_cast<std::int64_t>(size);
        if (end > to) return false;
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
        position + width > end || end > static_cast<std::int64_t>(bytes.size()))
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

bool AvifIlocShim::ParseIloc(const std::vector<std::uint8_t>& bytes, const Box& iloc,
    std::int32_t& version, std::int32_t& offsetSize, std::int32_t& lengthSize,
    std::vector<Item>& items, std::string& detail) noexcept {
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
    const auto baseOffsetSize = static_cast<std::int32_t>(packed >> 4);
    const auto indexSize = version ? static_cast<std::int32_t>(packed & 0xF) : 0;
    if (indexSize != 0) { detail = "The item location box indexes its extents; that layout is not repaired."; return false; }
    if (offsetSize > 8 || lengthSize > 8 || baseOffsetSize > 8) { detail = "The item location box uses unsupported field widths."; return false; }
    std::uint64_t itemCount = 0;
    if (!ReadUInt(bytes, position, end, version < 2 ? 2 : 4, itemCount)) { detail = "The item location box is truncated."; return false; }
    if (itemCount == 0 || itemCount > 4096) { detail = "The item location box has an unsupported item count."; return false; }
    items.clear();
    items.reserve(static_cast<std::size_t>(itemCount));
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
        std::uint64_t baseOffset = 0;
        if (!ReadUInt(bytes, position, end, baseOffsetSize, baseOffset)) { detail = "The item location box is truncated."; return false; }
        item.extentCountField = position;
        std::uint64_t extentCount = 0;
        if (!ReadUInt(bytes, position, end, 2, extentCount)) { detail = "The item location box is truncated."; return false; }
        auto firstOffsetField = position;
        for (std::uint64_t extent = 0; extent < extentCount; ++extent) {
            Extent entry;
            if (!ReadUInt(bytes, position, end, offsetSize, entry.offset)) { detail = "The item location box is truncated."; return false; }
            entry.lengthField = position;
            if (!ReadUInt(bytes, position, end, lengthSize, entry.length)) { detail = "The item location box is truncated."; return false; }
            entry.offset += baseOffset;
            if (extent == 0) item.offsetField = firstOffsetField;
            item.extents.push_back(entry);
        }
        items.push_back(std::move(item));
    }
    return true;
}

bool AvifIlocShim::RepairInPlace(std::vector<std::uint8_t>& bytes, std::string& detail) noexcept {
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

    std::int32_t version = 0;
    std::int32_t offsetSize = 0;
    std::int32_t lengthSize = 0;
    std::vector<Item> items;
    if (!ParseIloc(bytes, iloc, version, offsetSize, lengthSize, items, detail)) return false;
    if (items.empty()) { detail = "The item location box lists no items."; return false; }

    // Only the last item can be repaired: an earlier item's leftover extents would
    // be read as the next item's header, because the demuxer positions itself from
    // the box size rather than from what it consumed.
    auto& target = items.back();
    if (target.extents.size() < 2) { detail = "The primary item is stored as a single extent."; return false; }
    if (target.constructionMethod != 0) { detail = "The item does not use plain file offsets."; return false; }

    auto ordered = target.extents;
    std::sort(ordered.begin(), ordered.end(), [](const Extent& left, const Extent& right) {
        return left.offset < right.offset;
    });
    std::uint64_t span = 0;
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (index > 0 && ordered[index - 1].offset + ordered[index - 1].length != ordered[index].offset) {
            detail = "The item's extents are not contiguous; they cannot be merged.";
            return false;
        }
        span += ordered[index].length;
    }
    const auto start = static_cast<std::int64_t>(ordered.front().offset);
    if (start < 0 || start + static_cast<std::int64_t>(span) > size) {
        detail = "The item's extents reach outside the file.";
        return false;
    }
    const auto widthBits = 8 * lengthSize;
    if (widthBits < 64 && span >= (static_cast<std::uint64_t>(1) << widthBits)) {
        detail = "The merged extent is too long for the item location box's length field.";
        return false;
    }
    // Write the merged extent into the slot the demuxer reads, and leave the rest
    // of the box untouched: the extents that follow become dead bytes that the
    // box walker skips.
    if (!WriteUInt(bytes, target.extentCountField, 2, 1) ||
        !WriteUInt(bytes, target.offsetField, offsetSize, ordered.front().offset) ||
        !WriteUInt(bytes, target.extents.front().lengthField, lengthSize, span)) {
        detail = "The item location box could not be rewritten.";
        return false;
    }
    detail = "Merged " + std::to_string(ordered.size()) + " contiguous extents into one.";
    return true;
}

bool AvifIlocShim::ReadAndRepair(const char* pathUtf8, const std::int64_t maxBytes,
    std::vector<std::uint8_t>& bytes, std::string& detail) noexcept {
    bytes.clear();
    detail.clear();
    if (pathUtf8 == nullptr || *pathUtf8 == '\0' || maxBytes <= 0) { detail = "The path is not usable."; return false; }
    const auto path = FromUtf8(pathUtf8);
    if (path.empty()) { detail = "The path is not valid UTF-8."; return false; }
    const auto file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { detail = "Could not open the file with shared access."; return false; }
    LARGE_INTEGER fileSize{};
    const auto sized = GetFileSizeEx(file, &fileSize);
    if (!sized || fileSize.QuadPart < 0) { CloseHandle(file); detail = "Could not read the file size."; return false; }
    if (fileSize.QuadPart > maxBytes) { CloseHandle(file); detail = "The file is larger than the still-image repair limit."; return false; }
    // Small enough that reading it whole costs less than the demuxer's own probing:
    // still images are the only files that reach this code.
    std::vector<std::uint8_t> content(static_cast<std::size_t>(fileSize.QuadPart));
    DWORD read = 0;
    const auto ok = content.empty()
        ? TRUE
        : ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || static_cast<std::size_t>(read) != content.size()) {
        detail = "Could not read the whole file.";
        return false;
    }
    auto reason = std::string{};
    if (!RepairInPlace(content, reason)) { detail = std::move(reason); return false; }
    bytes = std::move(content);
    detail = std::move(reason);
    return true;
}
