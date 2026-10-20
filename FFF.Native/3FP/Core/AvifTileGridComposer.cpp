#include "pch.h"
#include "3FP/Core/AvifTileGridComposer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cstring>

namespace {

// Resolves a stream pointer back to its index, which is what both packet reading
// and the decoder are addressed by.
std::int32_t StreamIndex(const AVFormatContext* format, const AVStream* stream) noexcept {
    if (format == nullptr || stream == nullptr) return -1;
    for (unsigned index = 0; index < format->nb_streams; ++index)
        if (format->streams[index] == stream) return static_cast<std::int32_t>(index);
    return -1;
}

}  // namespace

bool AvifTileGridComposer::Describe(AVFormatContext* format, std::int32_t& width,
    std::int32_t& height, std::vector<Tile>& tiles, std::uint8_t background[4]) noexcept {
    if (format == nullptr || format->nb_stream_groups == 0) return false;
    for (unsigned group = 0; group < format->nb_stream_groups; ++group) {
        const auto* entry = format->stream_groups[group];
        if (entry == nullptr || entry->type != AV_STREAM_GROUP_PARAMS_TILE_GRID) continue;
        const auto* grid = entry->params.tile_grid;
        if (grid == nullptr || grid->nb_tiles == 0 || grid->coded_width <= 0 || grid->coded_height <= 0)
            return false;
        if (grid->nb_tiles > entry->nb_streams) return false;
        width = grid->coded_width;
        height = grid->coded_height;
        std::memcpy(background, grid->background, 4);
        tiles.clear();
        tiles.reserve(grid->nb_tiles);
        for (unsigned tile = 0; tile < grid->nb_tiles; ++tile) {
            if (grid->offsets[tile].idx >= entry->nb_streams) return false;
            Tile placement;
            placement.streamIndex = StreamIndex(format, entry->streams[grid->offsets[tile].idx]);
            placement.left = grid->offsets[tile].horizontal;
            placement.top = grid->offsets[tile].vertical;
            if (placement.streamIndex < 0) return false;
            tiles.push_back(placement);
        }
        // Every tile has to land on the same canvas in the same pixel format,
        // otherwise there is nothing to copy onto: check before any decoding.
        const auto pixelFormat = format->streams[tiles.front().streamIndex]->codecpar->format;
        for (const auto& tile : tiles) {
            if (format->streams[tile.streamIndex]->codecpar->format != pixelFormat) return false;
        }
        return true;
    }
    return false;
}

void AvifTileGridComposer::ReleaseSources(std::vector<TileSource>& sources) noexcept {
    for (auto& source : sources) {
        for (auto* packet : source.packets) av_packet_free(&packet);
        source.packets.clear();
    }
    sources.clear();
}

AVFrame* AvifTileGridComposer::DecodeTile(AVFormatContext* format, const TileSource& source) noexcept {
    if (format == nullptr || source.streamIndex < 0 ||
        source.streamIndex >= static_cast<std::int32_t>(format->nb_streams))
        return nullptr;
    const auto* stream = format->streams[source.streamIndex];
    const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) return nullptr;
    auto* context = avcodec_alloc_context3(codec);
    if (context == nullptr) return nullptr;
    AVFrame* frame = nullptr;
    if (avcodec_parameters_to_context(context, stream->codecpar) >= 0 &&
        avcodec_open2(context, codec, nullptr) >= 0) {
        frame = av_frame_alloc();
        if (frame != nullptr) {
            bool decoded = false;
            for (auto* packet : source.packets) {
                if (avcodec_send_packet(context, packet) < 0) continue;
                if (avcodec_receive_frame(context, frame) >= 0) { decoded = true; break; }
            }
            // A tile can also come out of the decoder without a packet of its own.
            if (!decoded) {
                avcodec_send_packet(context, nullptr);
                decoded = avcodec_receive_frame(context, frame) >= 0;
            }
            if (!decoded) { av_frame_free(&frame); }
        }
    }
    avcodec_free_context(&context);
    return frame;
}

void AvifTileGridComposer::Blit(const AVFrame* source, AVFrame* destination,
    const std::int32_t left, const std::int32_t top) noexcept {
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(source->format));
    if (descriptor == nullptr) return;
    for (int plane = 0; plane < 4; ++plane) {
        if (source->data[plane] == nullptr || destination->data[plane] == nullptr) continue;
        const auto horizontalShift = plane == 1 || plane == 2 ? descriptor->log2_chroma_w : 0;
        const auto verticalShift = plane == 1 || plane == 2 ? descriptor->log2_chroma_h : 0;
        const auto step = av_image_get_linesize(static_cast<AVPixelFormat>(source->format), 1, plane);
        if (step <= 0) continue;
        const auto sourceWidth = (source->width + (1 << horizontalShift) - 1) >> horizontalShift;
        const auto sourceHeight = (source->height + (1 << verticalShift) - 1) >> verticalShift;
        const auto canvasWidth = (destination->width + (1 << horizontalShift) - 1) >> horizontalShift;
        const auto canvasHeight = (destination->height + (1 << verticalShift) - 1) >> verticalShift;
        auto targetX = left >> horizontalShift;
        auto targetY = top >> verticalShift;
        // A tile is allowed to hang off the canvas: keep the part that lands.
        const auto sourceX = targetX < 0 ? -targetX : 0;
        const auto sourceY = targetY < 0 ? -targetY : 0;
        targetX = std::max<std::int32_t>(targetX, 0);
        targetY = std::max<std::int32_t>(targetY, 0);
        const auto columns = std::min<std::int32_t>(sourceWidth - sourceX, canvasWidth - targetX);
        const auto rows = std::min<std::int32_t>(sourceHeight - sourceY, canvasHeight - targetY);
        if (columns <= 0 || rows <= 0) continue;
        const auto byteWidth = static_cast<std::size_t>(columns) * static_cast<std::size_t>(step);
        for (std::int32_t row = 0; row < rows; ++row) {
            const auto from = source->data[plane] +
                static_cast<std::size_t>(sourceY + row) * static_cast<std::size_t>(source->linesize[plane]) +
                static_cast<std::size_t>(sourceX) * static_cast<std::size_t>(step);
            auto* to = destination->data[plane] +
                static_cast<std::size_t>(targetY + row) * static_cast<std::size_t>(destination->linesize[plane]) +
                static_cast<std::size_t>(targetX) * static_cast<std::size_t>(step);
            std::memcpy(to, from, byteWidth);
        }
    }
}

AVFrame* AvifTileGridComposer::Compose(AVFormatContext* format, const std::vector<Tile>& tiles,
    const std::vector<TileSource>& sources, const std::int32_t width, const std::int32_t height,
    const std::uint8_t background[4]) noexcept {
    if (format == nullptr || tiles.empty() || width <= 0 || height <= 0) return nullptr;
    std::vector<AVFrame*> decoded;
    decoded.reserve(tiles.size());
    AVFrame* canvas = nullptr;
    for (const auto& tile : tiles) {
        AVFrame* frame = nullptr;
        for (const auto& source : sources) {
            if (source.streamIndex != tile.streamIndex) continue;
            frame = DecodeTile(format, source);
            break;
        }
        if (frame == nullptr) {
            for (auto* done : decoded) av_frame_free(&done);
            return nullptr;
        }
        decoded.push_back(frame);
    }
    canvas = av_frame_alloc();
    if (canvas == nullptr) {
        for (auto* done : decoded) av_frame_free(&done);
        return nullptr;
    }
    canvas->format = decoded.front()->format;
    canvas->width = width;
    canvas->height = height;
    canvas->color_range = decoded.front()->color_range;
    canvas->color_primaries = decoded.front()->color_primaries;
    canvas->color_trc = decoded.front()->color_trc;
    canvas->colorspace = decoded.front()->colorspace;
    canvas->chroma_location = decoded.front()->chroma_location;
    if (av_frame_get_buffer(canvas, 0) < 0) {
        av_frame_free(&canvas);
        for (auto* done : decoded) av_frame_free(&done);
        return nullptr;
    }
    // The canvas pixels no tile covers take the group's background colour; the
    // usual case is a fully covered canvas, where this costs nothing.
    ptrdiff_t blackStride[AV_NUM_DATA_POINTERS] = {};
    for (int plane = 0; plane < AV_NUM_DATA_POINTERS; ++plane)
        blackStride[plane] = canvas->linesize[plane];
    av_image_fill_black(canvas->data, blackStride,
        static_cast<AVPixelFormat>(canvas->format), canvas->color_range, width, height);
    (void)background;
    for (std::size_t index = 0; index < tiles.size(); ++index)
        Blit(decoded[index], canvas, tiles[index].left, tiles[index].top);
    for (auto* done : decoded) av_frame_free(&done);
    return canvas;
}
