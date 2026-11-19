#include "pch.h"
#include "3FP/Core/AvifItemProbe.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {

struct Box {
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t body = 0;
};

bool ReadUInt(const std::uint8_t* data, std::int64_t& position, const std::int64_t end,
    const std::int32_t width, std::uint64_t& value) noexcept {
    if (data == nullptr || width < 0 || width > 8 || position < 0 ||
        position + width > end) return false;
    std::uint64_t result = 0;
    for (std::int32_t index = 0; index < width; ++index)
        result = (result << 8) | data[position + index];
    value = result;
    position += width;
    return true;
}

bool FindBox(const std::uint8_t* data, const std::int64_t from, const std::int64_t to,
    const char type[5], Box& output) noexcept {
    auto position = from;
    while (position + 8 <= to) {
        std::uint64_t size = 0;
        std::int64_t header = 8;
        auto cursor = position;
        if (!ReadUInt(data, cursor, to, 4, size)) return false;
        if (size == 1) {
            header = 16;
            if (!ReadUInt(data, cursor, to, 8, size)) return false;
        }
        const auto start = position;
        const auto body = start + header;
        if (size == 0) size = static_cast<std::uint64_t>(to - start);
        if (size < static_cast<std::uint64_t>(header)) return false;
        const auto end = start + static_cast<std::int64_t>(size);
        if (end > to) return false;
        if (std::memcmp(data + start + 4, type, 4) == 0) {
            output = Box{start, end, body};
            return true;
        }
        position = end;
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

struct Property {
    std::string type;
    std::int32_t a1lxCount = 0;
    bool hasA1lx = false;
    bool lselPresent = false;
    std::int32_t layerId = -1;
    std::int32_t opIndex = -1;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

bool ParsePitm(const std::uint8_t* data, const Box& meta, std::uint32_t& itemId,
    std::string& detail) noexcept {
    Box pitm{};
    if (!FindBox(data, meta.body + 4, meta.end, "pitm", pitm)) {
        detail = "The metadata box has no primary item box.";
        return false;
    }
    auto position = pitm.body;
    std::uint64_t raw = 0;
    if (!ReadUInt(data, position, pitm.end, 1, raw)) {
        detail = "The primary item box is truncated.";
        return false;
    }
    const auto version = static_cast<std::int32_t>(raw);
    if (version > 1) {
        detail = "The primary item box uses an unsupported version.";
        return false;
    }
    if (!ReadUInt(data, position, pitm.end, 3, raw)) {
        detail = "The primary item box is truncated.";
        return false;
    }
    if (!ReadUInt(data, position, pitm.end, version == 0 ? 2 : 4, raw)) {
        detail = "The primary item box is truncated.";
        return false;
    }
    itemId = static_cast<std::uint32_t>(raw);
    return itemId != 0;
}

bool ParseProperty(const std::uint8_t* data, const Box& box, Property& property,
    std::string& detail) noexcept {
    if (box.body > box.end) {
        detail = "An item property box is truncated.";
        return false;
    }
    property.type.assign(reinterpret_cast<const char*>(data + box.start + 4), 4);
    if (property.type == "ispe") {
        auto position = box.body;
        std::uint64_t raw = 0;
        std::uint64_t height = 0;
        if (!ReadUInt(data, position, box.end, 1, raw) ||
            !ReadUInt(data, position, box.end, 3, raw) ||
            !ReadUInt(data, position, box.end, 4, raw) ||
            !ReadUInt(data, position, box.end, 4, height)) {
            detail = "The image spatial extents property is truncated.";
            return false;
        }
        property.width = static_cast<std::int32_t>(raw);
        property.height = static_cast<std::int32_t>(height);
        return true;
    }
    if (property.type == "a1lx") {
        auto position = box.body;
        std::uint64_t raw = 0;
        if (!ReadUInt(data, position, box.end, 1, raw)) {
            detail = "The layered image indexing property is truncated.";
            return false;
        }
        const auto large = (raw & 1u) != 0;
        property.hasA1lx = true;
        property.a1lxCount = 0;
        for (int index = 0; index < 3; ++index) {
            if (!ReadUInt(data, position, box.end, large ? 4 : 2, raw)) {
                // A short a1lx still identifies a layered item; the count just
                // stays unknown. Do not turn a usable file into a refusal.
                return true;
            }
            if (raw != 0) ++property.a1lxCount;
        }
        return true;
    }
    if (property.type == "lsel") {
        auto position = box.body;
        std::uint64_t raw = 0;
        if (!ReadUInt(data, position, box.end, 2, raw)) {
            detail = "The layer selector property is truncated.";
            return false;
        }
        property.lselPresent = true;
        property.layerId = static_cast<std::int32_t>(raw);
        return true;
    }
    if (property.type == "a1op") {
        auto position = box.body;
        std::uint64_t raw = 0;
        if (!ReadUInt(data, position, box.end, 1, raw)) {
            detail = "The operating point selector property is truncated.";
            return false;
        }
        property.opIndex = static_cast<std::int32_t>(raw);
        return true;
    }
    // Any other property is irrelevant to layered still-image detection.
    return true;
}

bool ParseIpco(const std::uint8_t* data, const Box& iprp, std::vector<Property>& properties,
    std::string& detail) noexcept {
    Box ipco{};
    if (!FindBox(data, iprp.body, iprp.end, "ipco", ipco)) {
        detail = "The item property container is missing.";
        return false;
    }
    auto position = ipco.body;
    while (position + 8 <= ipco.end) {
        std::uint64_t size = 0;
        auto cursor = position;
        if (!ReadUInt(data, cursor, ipco.end, 4, size)) {
            detail = "The item property container is truncated.";
            return false;
        }
        std::int64_t header = 8;
        if (size == 1) {
            header = 16;
            if (!ReadUInt(data, cursor, ipco.end, 8, size)) {
                detail = "The item property container is truncated.";
                return false;
            }
        }
        if (size == 0) size = static_cast<std::uint64_t>(ipco.end - position);
        if (size < static_cast<std::uint64_t>(header) ||
            position + static_cast<std::int64_t>(size) > ipco.end) {
            detail = "The item property container has an invalid box size.";
            return false;
        }
        Box propertyBox{position, position + static_cast<std::int64_t>(size),
            position + header};
        Property property;
        if (!ParseProperty(data, propertyBox, property, detail)) return false;
        properties.push_back(std::move(property));
        position = propertyBox.end;
    }
    return !properties.empty();
}

bool ParseIpma(const std::uint8_t* data, const Box& iprp, const std::uint32_t primaryItemId,
    std::vector<std::uint16_t>& associations, std::string& detail) noexcept;

bool ParseIloc(const std::uint8_t* data, const Box& meta, const std::uint32_t primaryItemId,
    AvifItemInfo& info, std::string& detail) noexcept {
    Box iloc{};
    if (!FindBox(data, meta.body + 4, meta.end, "iloc", iloc)) {
        // An item without iloc is not necessarily broken; the probe simply has
        // no extent information to offer, so the caller must keep the repair
        // path.
        detail = "The metadata box has no item location box.";
        return false;
    }
    auto position = iloc.body;
    std::uint64_t raw = 0;
    std::uint64_t versionValue = 0;
    std::uint64_t flags = 0;
    std::uint64_t sizes = 0;
    if (!ReadUInt(data, position, iloc.end, 1, versionValue) ||
        !ReadUInt(data, position, iloc.end, 3, flags)) {
        detail = "The item location box is truncated.";
        return false;
    }
    (void)flags;
    const auto version = static_cast<std::int32_t>(versionValue);
    if (version > 2) {
        detail = "The item location box uses an unsupported version.";
        return false;
    }
    if (!ReadUInt(data, position, iloc.end, 2, sizes)) {
        detail = "The item location box is truncated.";
        return false;
    }
    const auto offsetSize = static_cast<std::int32_t>((sizes >> 12) & 0x0F);
    const auto lengthSize = static_cast<std::int32_t>((sizes >> 8) & 0x0F);
    const auto baseOffsetSize = static_cast<std::int32_t>((sizes >> 4) & 0x0F);
    const auto indexSize = version >= 1 ? static_cast<std::int32_t>(sizes & 0x0F) : 0;
    if (offsetSize > 8 || lengthSize > 8 || baseOffsetSize > 8 || indexSize > 8) {
        detail = "The item location box uses an invalid field size.";
        return false;
    }
    std::uint64_t itemCount = 0;
    if (!ReadUInt(data, position, iloc.end, version == 2 ? 4 : 2, itemCount) ||
        itemCount > 4096) {
        detail = "The item location box has an invalid item count.";
        return false;
    }
    bool foundPrimary = false;
    for (std::uint64_t item = 0; item < itemCount; ++item) {
        std::uint64_t itemId = 0;
        if (!ReadUInt(data, position, iloc.end, version == 2 ? 4 : 2, itemId)) {
            detail = "The item location box is truncated.";
            return false;
        }
        std::uint64_t constructionMethod = 0;
        if (version >= 1 && !ReadUInt(data, position, iloc.end, 2, constructionMethod)) {
            detail = "The item location box is truncated.";
            return false;
        }
        constructionMethod &= 0x0F;
        std::uint64_t dataReferenceIndex = 0;
        if (!ReadUInt(data, position, iloc.end, 2, dataReferenceIndex)) {
            detail = "The item location box is truncated.";
            return false;
        }
        std::uint64_t baseOffset = 0;
        if (baseOffsetSize != 0 && !ReadUInt(data, position, iloc.end, baseOffsetSize, baseOffset)) {
            detail = "The item location box is truncated.";
            return false;
        }
        std::uint64_t extentCount = 0;
        if (!ReadUInt(data, position, iloc.end, 2, extentCount) || extentCount > 4096) {
            detail = "The item location box has an invalid extent count.";
            return false;
        }
        const bool primary = itemId == primaryItemId;
        if (primary) {
            foundPrimary = true;
            info.extentCount = static_cast<std::int32_t>(extentCount);
            info.constructionMethod = static_cast<std::int32_t>(constructionMethod);
            info.dataReferenceIndex = static_cast<std::int32_t>(dataReferenceIndex);
        }
        for (std::uint64_t extent = 0; extent < extentCount; ++extent) {
            if (version >= 1 && indexSize != 0 &&
                !ReadUInt(data, position, iloc.end, indexSize, raw)) {
                detail = "The item location box is truncated.";
                return false;
            }
            if (offsetSize != 0 && !ReadUInt(data, position, iloc.end, offsetSize, raw)) {
                detail = "The item location box is truncated.";
                return false;
            }
            if (lengthSize != 0 && !ReadUInt(data, position, iloc.end, lengthSize, raw)) {
                detail = "The item location box is truncated.";
                return false;
            }
        }
    }
    if (!foundPrimary) {
        detail = "The item location box has no entry for the primary item.";
        return false;
    }
    info.hasIloc = true;
    return true;
}

bool InspectMeta(const std::uint8_t* data, const Box& meta, AvifItemInfo& info,
    std::string& detail) noexcept {
    if (meta.body + 4 >= meta.end) {
        detail = "The metadata box is truncated.";
        return false;
    }
    std::uint32_t primaryItemId = 0;
    if (!ParsePitm(data, meta, primaryItemId, detail)) return false;
    Box iprp{};
    if (!FindBox(data, meta.body + 4, meta.end, "iprp", iprp)) {
        detail = "The metadata box has no item properties.";
        return false;
    }
    std::vector<Property> properties;
    if (!ParseIpco(data, iprp, properties, detail)) return false;
    std::vector<std::uint16_t> associations;
    if (!ParseIpma(data, iprp, primaryItemId, associations, detail)) return false;

    info.valid = true;
    info.hasPitm = true;
    info.primaryItemId = primaryItemId;
    for (const auto indexValue : associations) {
        const auto index = static_cast<std::size_t>(indexValue);
        if (index == 0 || index > properties.size()) continue;
        const auto& property = properties[index - 1];
        if (property.type == "ispe") {
            if (property.width > 0 && property.height > 0) {
                info.ispeWidth = property.width;
                info.ispeHeight = property.height;
            }
        } else if (property.type == "a1lx" && property.hasA1lx) {
            info.hasA1lx = true;
            info.layerCount = property.a1lxCount + 1;
        } else if (property.type == "lsel" && property.lselPresent) {
            info.lselPresent = true;
            info.layerId = property.layerId;
        } else if (property.type == "a1op" && property.opIndex >= 0) {
            info.opIndex = property.opIndex;
        }
    }
    // iloc is optional for the probe's primary purpose. A malformed iloc
    // leaves hasIloc=false so the caller keeps the repair path; it must not
    // turn a usable a1lx/lsel item into a refusal.
    std::string ilocDetail;
    if (!ParseIloc(data, meta, primaryItemId, info, ilocDetail)) {
        // Fields filled before the parser hit a truncated/unsupported extent are
        // not a usable location. The caller only tests hasIloc, but leaving the
        // stale count behind would make a malformed box look single-extent if a
        // future caller forgets the flag.
        info.hasIloc = false;
        info.extentCount = -1;
        info.constructionMethod = -1;
        info.dataReferenceIndex = -1;
    }
    return true;
}

bool FindMetaInTail(const std::uint8_t* data, const std::int64_t size, Box& meta) noexcept {
    // A meta box after a large mdat has no ftyp in front of it in the tail
    // window. Find the box type and validate the size field before accepting it.
    for (std::int64_t type = 4; type + 4 <= size; ++type) {
        if (std::memcmp(data + type, "meta", 4) != 0) continue;
        const auto start = type - 4;
        std::uint64_t boxSize = 0;
        auto cursor = start;
        if (!ReadUInt(data, cursor, size, 4, boxSize)) continue;
        std::int64_t header = 8;
        if (boxSize == 1) {
            header = 16;
            cursor = type + 4;
            if (!ReadUInt(data, cursor, size, 8, boxSize)) continue;
        }
        if (boxSize == 0) boxSize = static_cast<std::uint64_t>(size - start);
        if (boxSize < static_cast<std::uint64_t>(header) ||
            start + static_cast<std::int64_t>(boxSize) > size) continue;
        meta = Box{start, start + static_cast<std::int64_t>(boxSize), start + header};
        return true;
    }
    return false;
}

bool ParseIpma(const std::uint8_t* data, const Box& iprp, const std::uint32_t primaryItemId,
    std::vector<std::uint16_t>& associations, std::string& detail) noexcept {
    bool foundEntry = false;
    auto position = iprp.body;
    while (position + 8 <= iprp.end) {
        Box ipma{};
        if (!FindBox(data, position, iprp.end, "ipma", ipma)) break;
        position = ipma.end;
        auto cursor = ipma.body;
        std::uint64_t raw = 0;
        if (!ReadUInt(data, cursor, ipma.end, 1, raw)) {
            detail = "The item property association box is truncated.";
            return false;
        }
        const auto version = static_cast<std::int32_t>(raw);
        if (!ReadUInt(data, cursor, ipma.end, 3, raw)) {
            detail = "The item property association box is truncated.";
            return false;
        }
        const auto flags = static_cast<std::uint32_t>(raw);
        if (!ReadUInt(data, cursor, ipma.end, 4, raw)) {
            detail = "The item property association box is truncated.";
            return false;
        }
        const auto entryCount = static_cast<std::uint32_t>(raw);
        if (entryCount > 4096) {
            detail = "The item property association box has too many entries.";
            return false;
        }
        for (std::uint32_t entry = 0; entry < entryCount; ++entry) {
            if (!ReadUInt(data, cursor, ipma.end, version < 1 ? 2 : 4, raw)) {
                detail = "The item property association box is truncated.";
                return false;
            }
            const auto itemId = static_cast<std::uint32_t>(raw);
            if (!ReadUInt(data, cursor, ipma.end, 1, raw)) {
                detail = "The item property association box is truncated.";
                return false;
            }
            const auto associationCount = static_cast<std::uint32_t>(raw);
            if (associationCount > 64) {
                detail = "The item property association box has too many associations.";
                return false;
            }
            std::vector<std::uint16_t> itemAssociations;
            itemAssociations.reserve(associationCount);
            for (std::uint32_t association = 0; association < associationCount; ++association) {
                if (!ReadUInt(data, cursor, ipma.end, (flags & 1u) ? 2 : 1, raw)) {
                    detail = "The item property association box is truncated.";
                    return false;
                }
                const auto propertyIndex = static_cast<std::uint16_t>(
                    (flags & 1u) ? (raw & 0x7FFFu) : (raw & 0x7Fu));
                if (propertyIndex != 0) itemAssociations.push_back(propertyIndex);
            }
            if (itemId == primaryItemId && !foundEntry) {
                associations = std::move(itemAssociations);
                foundEntry = true;
            }
        }
        if (foundEntry) return true;
    }
    detail = foundEntry
        ? detail
        : "The item property association box has no entry for the primary item.";
    return foundEntry;
}

}  // namespace

bool AvifItemProbe::InspectBytes(const std::uint8_t* data, const std::size_t size,
    AvifItemInfo& info, std::string& detail) noexcept {
    info = AvifItemInfo{};
    detail.clear();
    if (data == nullptr || size < 16) {
        detail = "The file is too small to contain an image metadata box.";
        return false;
    }
    const auto end = static_cast<std::int64_t>(size);
    Box ftyp{};
    if (!FindBox(data, 0, end, "ftyp", ftyp)) {
        detail = "The file has no file type box.";
        return false;
    }
    Box meta{};
    if (!FindBox(data, ftyp.end, end, "meta", meta)) {
        detail = "The file has no metadata box.";
        return false;
    }
    return InspectMeta(data, meta, info, detail);
}

bool AvifItemProbe::InspectTailBytes(const std::uint8_t* data, const std::size_t size,
    AvifItemInfo& info, std::string& detail) noexcept {
    info = AvifItemInfo{};
    detail.clear();
    if (data == nullptr || size < 16) {
        detail = "The tail window is too small to contain an image metadata box.";
        return false;
    }
    Box meta{};
    if (!FindMetaInTail(data, static_cast<std::int64_t>(size), meta)) {
        detail = "The tail window has no metadata box.";
        return false;
    }
    return InspectMeta(data, meta, info, detail);
}

bool AvifItemProbe::Read(const char* pathUtf8, const std::int64_t maxBytes,
    AvifItemInfo& info, std::string& detail) noexcept {
    info = AvifItemInfo{};
    detail.clear();
    if (pathUtf8 == nullptr || *pathUtf8 == '\0' || maxBytes <= 0) {
        detail = "The probe path or byte limit is not usable.";
        return false;
    }
    const auto path = FromUtf8(pathUtf8);
    if (path.empty()) {
        detail = "The probe path is not valid UTF-8.";
        return false;
    }
    const auto file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        detail = "Could not open the file for property probing.";
        return false;
    }
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(file);
        detail = "Could not read the file size for property probing.";
        return false;
    }
    const auto readBytes = std::min<std::int64_t>(fileSize.QuadPart, maxBytes);
    if (readBytes > 64 * 1024 * 1024) {
        CloseHandle(file);
        detail = "The property probe byte limit is too large.";
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(readBytes));
    DWORD read = 0;
    const auto ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != bytes.size()) {
        detail = "Could not read the property-probe prefix.";
        return false;
    }
    const bool inspected = InspectBytes(bytes.data(), bytes.size(), info, detail);
    // The meta box may follow a large mdat. The prefix read above cannot see it,
    // and the old fallback was the full-file iloc repair. Try a bounded tail
    // window first so a normal single-extent file still avoids that read.
    bool tailInspected = false;
    const auto tailLimit = std::min<std::int64_t>(maxBytes, 4 * 1024 * 1024);
    if (!inspected && fileSize.QuadPart > readBytes && tailLimit > 0) {
        const auto tailBytes = std::min<std::int64_t>(fileSize.QuadPart, tailLimit);
        const auto tailFile = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (tailFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER offset{};
            offset.QuadPart = fileSize.QuadPart - tailBytes;
            std::vector<std::uint8_t> tail(static_cast<std::size_t>(tailBytes));
            DWORD tailRead = 0;
            const auto positioned = SetFilePointerEx(tailFile, offset, nullptr, FILE_BEGIN);
            const auto tailOk = positioned != FALSE &&
                ReadFile(tailFile, tail.data(), static_cast<DWORD>(tail.size()), &tailRead, nullptr);
            CloseHandle(tailFile);
            if (tailOk && static_cast<std::size_t>(tailRead) == tail.size()) {
                std::string tailDetail;
                tailInspected = InspectTailBytes(tail.data(), tail.size(), info, tailDetail);
            }
        }
    }
    const auto result = inspected || tailInspected;
    return result;
}
