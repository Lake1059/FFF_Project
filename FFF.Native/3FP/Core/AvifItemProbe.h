#pragma once

#include "framework.h"

#include <cstdint>
#include <string>

struct AVIOInterruptCB;

// Read-only probe for the item properties a layered AVIF actually declares.
//
// The previous layered-still signal was the iloc extent count. That is a storage
// layout, not a format signal: libavif writes one extent per layer, but a
// conformant file may store several layers in one extent. The standard signal
// lives in the primary item's property associations:
//
//   pitm              primary item id
//   iprp/ipco/ipma    item -> property index associations
//   a1lx              AV1LayeredImageIndexingProperty (layer sizes 1..3)
//   lsel              LayerSelectorProperty (0..3 or 0xFFFF)
//   a1op              OperatingPointSelectorProperty (op_index)
//   ispe              final image size
//
// This probe never edits the file and never fails the open. A malformed or
// truncated meta box is a refusal: the caller falls back to ordinary still-image
// handling and FFmpeg reports its own error if the file cannot be opened.
struct AvifItemInfo {
    bool valid = false;
    bool hasPitm = false;
    std::uint32_t primaryItemId = 0;
    // Number of layers when a1lx declares them. 0 means "not declared".
    // a1lx records sizes for layers 1..3; the first layer is implicit, so a
    // non-zero count of recorded sizes maps to count + 1 layers.
    std::int32_t layerCount = 0;
    bool hasA1lx = false;
    bool lselPresent = false;
    // -1 means the property is absent; 0xFFFF means all layers.
    std::int32_t layerId = -1;
    std::int32_t opIndex = -1;
    std::int32_t ispeWidth = 0;
    std::int32_t ispeHeight = 0;
    // iloc information for the primary item. The probe uses this to decide
    // whether AvifIlocShim has any work: one plain file-offset extent needs no
    // merge, while a multi-extent item (or any item stored in idat / an external
    // data reference) must still go through the repair path.
    bool hasIloc = false;
    std::int32_t extentCount = -1;
    std::int32_t constructionMethod = -1;
    std::int32_t dataReferenceIndex = -1;
};

class AvifItemProbe final {
public:
    // Reads only the beginning of the file (the meta box is normally in the
    // first few KiB) and fills `info`. Returns false with `detail` when the
    // primary item or its property associations cannot be read. `info.valid`
    // is still false on failure. Prefix reads are capped at 16 MiB; the optional
    // tail window is capped at 4 MiB. deadlineTick is an absolute GetTickCount64
    // deadline (0 disables it); interrupt is checked during open, reads and seek.
    static bool Read(const char* pathUtf8, std::int64_t maxBytes,
        AvifItemInfo& info, std::string& detail, ULONGLONG deadlineTick = 0,
        const AVIOInterruptCB* interrupt = nullptr) noexcept;

    // Same parsing on an in-memory prefix. Exposed for tests and for callers
    // that already hold the bytes.
    static bool InspectBytes(const std::uint8_t* data, std::size_t size,
        AvifItemInfo& info, std::string& detail) noexcept;

    // Parses a tail window that may start inside mdat. Used when the meta box
    // follows a large mdat and is not reachable from the prefix.
    static bool InspectTailBytes(const std::uint8_t* data, std::size_t size,
        AvifItemInfo& info, std::string& detail) noexcept;
};
