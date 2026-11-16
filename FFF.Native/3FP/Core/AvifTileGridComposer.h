#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

#include <cstdint>
#include <vector>

// Assembles a tiled still image (grid AVIF/HEIC) into the one picture it is.
//
// A large still image is often stored as a grid: several independent AV1/HEVC
// items, one per tile, plus a tile grid group that says what size the assembled
// canvas is and where each tile goes. FFmpeg exposes exactly that shape -- one
// stream per tile, no stream marked default, and the canvas as a stream group --
// so a player that picks "the first stream" shows the top-left tile and calls it
// the picture. Nothing in the container carries a pre-assembled image; the
// canvas only exists once the tiles are placed.
//
// Placement is taken from the group, not guessed: `offsets` gives each tile's
// top-left corner and which of the group's streams to use, and tiles may overlap,
// in which case a later tile wins (spec order).
class AvifTileGridComposer final {
public:
    struct Tile {
        std::int32_t streamIndex = -1;  // index in AVFormatContext::streams
        std::int32_t left = 0;
        std::int32_t top = 0;
    };

    // The compressed tiles, read once at open. A still image has no timeline, so
    // the demuxer cannot be rewound to decode it a second time; keeping the
    // packets is what lets the canvas be rebuilt after a device change.
    struct TileSource {
        std::int32_t streamIndex = -1;
        std::vector<AVPacket*> packets;
    };

    // Fills `tiles` (in draw order) and the presented canvas size when the file
    // is a tiled still image. The group's `width`/`height` are the final
    // presentation box; the group's crop offset is subtracted from every tile
    // offset, so the canvas never carries the cropped-away right/bottom band.
    // Returns false for anything else, including tile grids whose tiles do not
    // share one pixel format.
    static bool Describe(AVFormatContext* format, std::int32_t& width, std::int32_t& height,
        std::vector<Tile>& tiles, std::uint8_t background[4]) noexcept;

    // Decodes the first tile, allocates the canvas in that decoded frame's pixel
    // format (not the container's codecpar format), fills uncovered pixels with
    // the group's RGBA background, and copies the first tile onto it. Returns
    // nullptr if the first tile cannot be decoded.
    static AVFrame* BeginCanvas(AVFormatContext* format, const TileSource& firstSource,
        const Tile& firstTile, std::int32_t width, std::int32_t height,
        const std::uint8_t background[4]) noexcept;

    // Decodes one tile and copies it onto `canvas`. Returns false when the tile
    // cannot be decoded or its decoded pixel format differs from the canvas.
    static bool ComposeTileOnto(AVFormatContext* format, const TileSource& source,
        const Tile& tile, AVFrame* canvas) noexcept;

    // Decodes one frame per tile and copies them onto a canvas of `width` x
    // `height`. Returns a frame the caller frees, or nullptr on any failure.
    static AVFrame* Compose(AVFormatContext* format, const std::vector<Tile>& tiles,
        const std::vector<TileSource>& sources, const std::int32_t width,
        const std::int32_t height, const std::uint8_t background[4]) noexcept;

    static void ReleaseSources(std::vector<TileSource>& sources) noexcept;

private:
    static AVFrame* DecodeTile(AVFormatContext* format, const TileSource& source) noexcept;
    static void Blit(const AVFrame* source, AVFrame* destination,
        std::int32_t left, std::int32_t top) noexcept;
};
