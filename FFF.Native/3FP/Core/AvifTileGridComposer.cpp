#include "pch.h"
#include "3FP/Core/AvifTileGridComposer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
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

// Fills the whole canvas with the group's background colour. A zero background
// keeps the old black-fill path (and its exact limited/full-range behaviour);
// anything else goes through swscale, which writes the native components of the
// canvas pixel format instead of treating RGBA bytes as already-YUV.
bool FillCanvasBackground(AVFrame* canvas, const std::uint8_t background[4]) noexcept {
    if (canvas == nullptr || background == nullptr) return false;
    const auto allZero = background[0] == 0 && background[1] == 0 &&
        background[2] == 0 && background[3] == 0;
    ptrdiff_t stride[AV_NUM_DATA_POINTERS] = {};
    for (int plane = 0; plane < AV_NUM_DATA_POINTERS; ++plane)
        stride[plane] = canvas->linesize[plane];
    if (allZero) {
        return av_image_fill_black(canvas->data, stride,
            static_cast<AVPixelFormat>(canvas->format), canvas->color_range,
            canvas->width, canvas->height) >= 0;
    }
    auto* sws = sws_getContext(1, 1, AV_PIX_FMT_RGBA,
        canvas->width, canvas->height, static_cast<AVPixelFormat>(canvas->format),
        SWS_POINT, nullptr, nullptr, nullptr);
    if (sws == nullptr) return false;
    std::uint8_t sourcePixel[4] = {background[0], background[1], background[2], background[3]};
    std::uint8_t* sourceData[4] = {sourcePixel, nullptr, nullptr, nullptr};
    int sourceStride[4] = {4, 0, 0, 0};
    const auto scaled = sws_scale(sws, sourceData, sourceStride, 0, 1, canvas->data, canvas->linesize);
    sws_freeContext(sws);
    return scaled > 0;
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
        // grid->coded_width/height describe the coded canvas; width/height are
        // the final presentation size after the group's right/bottom crop. The
        // old code used coded_width/height, so a cropped grid showed the cropped
        // band. Draw straight into the presented box and shift tile offsets by
        // the crop origin.
        const auto cropX = std::max(0, grid->horizontal_offset);
        const auto cropY = std::max(0, grid->vertical_offset);
        width = grid->width > 0 ? grid->width : grid->coded_width - cropX;
        height = grid->height > 0 ? grid->height : grid->coded_height - cropY;
        if (width <= 0 || height <= 0) return false;
        std::memcpy(background, grid->background, 4);
        tiles.clear();
        tiles.reserve(grid->nb_tiles);
        for (unsigned tile = 0; tile < grid->nb_tiles; ++tile) {
            if (grid->offsets[tile].idx >= entry->nb_streams) return false;
            Tile placement;
            placement.streamIndex = StreamIndex(format, entry->streams[grid->offsets[tile].idx]);
            placement.left = grid->offsets[tile].horizontal - cropX;
            placement.top = grid->offsets[tile].vertical - cropY;
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

AVFrame* AvifTileGridComposer::BeginCanvas(AVFormatContext* format,
    const TileSource& firstSource, const Tile& firstTile,
    const std::int32_t width, const std::int32_t height,
    const std::uint8_t background[4]) noexcept {
    if (format == nullptr || firstSource.streamIndex != firstTile.streamIndex ||
        width <= 0 || height <= 0) return nullptr;
    AVFrame* first = DecodeTile(format, firstSource);
    if (first == nullptr || first->format == AV_PIX_FMT_NONE) {
        av_frame_free(&first);
        return nullptr;
    }
    auto* canvas = av_frame_alloc();
    if (canvas == nullptr) {
        av_frame_free(&first);
        return nullptr;
    }
    canvas->format = first->format;
    canvas->width = width;
    canvas->height = height;
    // The decoded frame, not codecpar, is authoritative: a container without a
    // pixi property reports AV_PIX_FMT_NONE, and alpha/bit-depth drift between
    // codecpar and the bitstream would otherwise size the canvas for the wrong
    // pixel layout. The black fill therefore also uses the bitstream's range.
    canvas->color_range = first->color_range;
    canvas->color_primaries = first->color_primaries;
    canvas->color_trc = first->color_trc;
    canvas->colorspace = first->colorspace;
    canvas->chroma_location = first->chroma_location;
    if (av_frame_get_buffer(canvas, 0) < 0) {
        av_frame_free(&first);
        av_frame_free(&canvas);
        return nullptr;
    }
    // Canvas pixels no tile covers take the group's declared background colour;
    // a fully covered canvas costs only the fill. A zero background keeps the
    // previous black fill (and its exact range handling).
    if (!FillCanvasBackground(canvas, background)) {
        av_frame_free(&first);
        av_frame_free(&canvas);
        return nullptr;
    }
    Blit(first, canvas, firstTile.left, firstTile.top);
    av_frame_free(&first);
    return canvas;
}

bool AvifTileGridComposer::ComposeTileOnto(AVFormatContext* format, const TileSource& source,
    const Tile& tile, AVFrame* canvas) noexcept {
    if (format == nullptr || canvas == nullptr || source.streamIndex != tile.streamIndex) return false;
    AVFrame* frame = DecodeTile(format, source);
    if (frame == nullptr) return false;
    if (frame->format != canvas->format) {
        av_frame_free(&frame);
        return false;
    }
    Blit(frame, canvas, tile.left, tile.top);
    av_frame_free(&frame);
    return true;
}

AVFrame* AvifTileGridComposer::Compose(AVFormatContext* format, const std::vector<Tile>& tiles,
    const std::vector<TileSource>& sources, const std::int32_t width, const std::int32_t height,
    const std::uint8_t background[4]) noexcept {
    if (format == nullptr || tiles.empty()) return nullptr;
    const TileSource* firstSource = nullptr;
    for (const auto& candidate : sources) {
        if (candidate.streamIndex == tiles.front().streamIndex) { firstSource = &candidate; break; }
    }
    if (firstSource == nullptr) return nullptr;
    auto* canvas = BeginCanvas(format, *firstSource, tiles.front(), width, height, background);
    if (canvas == nullptr) return nullptr;
    for (std::size_t index = 1; index < tiles.size(); ++index) {
        const TileSource* source = nullptr;
        for (const auto& candidate : sources) {
            if (candidate.streamIndex == tiles[index].streamIndex) { source = &candidate; break; }
        }
        if (source == nullptr || !ComposeTileOnto(format, *source, tiles[index], canvas)) {
            av_frame_free(&canvas);
            return nullptr;
        }
    }
    return canvas;
}
