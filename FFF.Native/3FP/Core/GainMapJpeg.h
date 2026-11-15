// GainMapJpeg.h — JPEG / WebP family gain-map container probe.
//
// Scope: LOCATE the gain-map sub-stream inside a JPEG Ultra-HDR / Apple JPEG /
// Adobe-gcontainer JPEG / WebP file and pull the container metadata. Pixel
// decoding and gain-map APPLICATION are intentionally NOT here — the main
// implementer feeds `gainBytes` to the existing decoder and runs the single
// shared apply math.
//
// Families handled:
//   * ISO / Ultra HDR JPEG  — MPF (APP2) multi-picture, both TIFF endians
//                             (MM\0* big-endian / II*\0 little-endian),
//                             ISO 21496-1 binary metadata (APP2 urn segment),
//                             XMP hdr-gain-map ("hdrgm") legacy metadata.
//   * Adobe / Google gcontainer — XMP Container:Directory with an
//                             Item:Semantic="GainMap" entry.
//   * Apple JPEG            — v1 (headroom from Exif AppleMakerNote tag 33/48)
//                             and v2/v3 (headroom from XMP HDRGainMap namespace).
//   * WebP gain map          — RIFF chunk walk; carve VP8/VP8L frame, report
//                             honestly when no gain-map metadata chunk exists.
//
// This file only READS the container. It never touches GainMapProbe.*.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fp3 {

// Result of probing a JPEG/WebP file for an embedded gain map.
struct JpegGainMapHit {
    bool found = false;

    // One of: "jpeg_uhdr" | "jpeg_apple_v1" | "jpeg_apple_v2" | "jpeg_apple_v3"
    //         | "jpeg_adobe_gcontainer" | "webp_gainmap"
    std::string containerKind;

    // "iso" | "apple" | "vendor128"
    std::string family;

    // "xmp" | "exif" | "app2" | "gcontainer"
    std::string metaSource;

    // hdr-gain-map metadata (ISO family). Values are in the log-boost domain the
    // shared apply math expects; channels are R,G,B.
    float offsetSdr[3] = {0.f, 0.f, 0.f};
    float offsetHdr[3] = {0.f, 0.f, 0.f};
    float logBoost[6]  = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f}; // [minR,maxR,minG,maxG,minB,maxB]

    // Apple family: headroom in log2 units (8x = 3.0). 0 when not Apple.
    float headroom = 0.f;

    // Gain gamma (single-channel fallback when no per-channel triple).
    float gamma = 1.f;

    // Carved bytes. baseBytes is the primary JPEG/WebP frame (when the container
    // splits them); gainBytes is the gain-map code stream (JPEG or WebP/VP8L).
    std::vector<uint8_t> baseBytes;
    std::vector<uint8_t> gainBytes;

    // "jpeg" | "webp"
    std::string gainCodec;

    // Human-readable reason: when !found, why; when found, any caveat.
    std::string reason;
};

// Probe `path` for an embedded gain-map container. Never throws; on any
// structural problem fills `out.reason` and leaves `out.found = false`.
bool ProbeJpegGainMap(const std::wstring& path, JpegGainMapHit& out);

} // namespace fp3
