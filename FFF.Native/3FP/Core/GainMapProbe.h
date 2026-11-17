#pragma once

#include "framework.h"

#include <cstdint>
#include <string>
#include <vector>

// Private (vendor) HDR GainMap probe + apply pipeline.
//
// This is a self-contained, read-only pipeline that recognizes the private
// gainmap carriers found on phone HEIC/AVIF files:
//   - ISO 21496-1 `tmap` derived item (single item or `grid` derived)
//   - Apple `auxC` URN urn:com:apple:photo:2020:aux:hdrgainmap
//   - Unregistered vendor `auxC` URN (gated on Apple HDRGainMap XMP)
// and applies the gain to the SDR base frame to produce a linear HDR frame.
//
// Fail-closed contract:
//   - If the container is recognized as gainmap-shaped but cannot be decoded or
//     its metadata is not credible, verdict = fail_closed_malformed and reason
//     is human-readable. We never silently treat such a file as SDR and claim
//     support.
//   - If the container has no gainmap, verdict = not_gainmap.
//   - If the gainmap was decoded but apply was skipped (e.g. --base-only or
//     unsupported 3-channel correction), verdict = decoded_not_applied.
//   - Only when the math actually ran on real pixels is verdict = applied.

struct GainMapItemInfo {
    int id = 0;
    std::string type;   // hvc1 / av01 / tmap / grid / Exif / ...
    std::string role;   // base | gainmap | unknown
};

struct GainMapProbeResult {
    // container
    std::string majorBrand;
    std::vector<std::string> compatibleBrands;
    bool isStill = false;

    // gainmap carrier
    bool present = false;
    std::string form;   // tmap | tmap_grid | auxc_apple | auxc_vendor | auxc_grid | none
    std::string auxCUrn;
    bool gridPresent = false;
    int gridRows = 0;
    int gridCols = 0;
    int gridFlags = 0;
    bool gridTailPresent = false;
    int gridTailW = 0;
    int gridTailH = 0;
    std::vector<GainMapItemInfo> items;

    // gainmap decode
    bool decodeOk = false;
    std::string decodeCodec;   // hevc | av1
    int decodeW = 0;
    int decodeH = 0;
    int decodeChannels = 0;    // 1 or 3 (corrected from actual pixels)
    int decodeBitDepth = 8;
    std::string decodeReason;   // readable failure reason when decodeOk=false

    // metadata
    std::string metaFamily = "none";    // iso | apple | vendor128 | none
    std::string metaSource = "default"; // tmap | makenote | xmp | default
    float offsetSdr[3] = {0.f, 0.f, 0.f};
    float offsetHdr[3] = {0.f, 0.f, 0.f};
    float logBoostMin[3] = {0.f, 0.f, 0.f};
    float logBoostMax[3] = {0.f, 0.f, 0.f};
    float gamma = 1.f;

    // Headroom units (plan §2.3). All *Log2 fields are in log2 stops
    // (linear multiplier = 2^x). There is intentionally no bare `headroom`:
    // consumers must see the unit. headroomLinear is display-only.
    double baseHdrHeadroomLog2 = 0.0;  // hB: base-side headroom (log2 stops)
    double altHdrHeadroomLog2 = 0.0;   // hA: alternate/HDR-side headroom (log2 stops)
    double headroomLinear = 0.0;       // display-only linear multiplier = 2^max(hB,hA)
    double targetHeadroomLog2 = 0.0;   // player target (log2 stops), default 4.9
    double applyWeight = 0.0;          // [0,1] interpolation weight actually applied

    // apply
    bool applyOk = false;
    std::string applyMode;     // half | rgb_f32 | jpeg | none
    double statsP50 = 0.0;
    double statsMaxLinear = 0.0;
    double statsOver1Pct = 0.0;
    std::string reason;
    std::string verdict;       // applied | decoded_not_applied | fail_closed_unsupported |
                               // fail_closed_malformed | not_gainmap
    // ISO 21496-1 reader behaviour: number of bytes after the canonical tmap
    // v0 layout that a compliant reader MUST ignore (writer padding /
    // appended extensions). 0 when the tmap payload ended exactly at consumed.
    // Promoted to a first-class JSON field (sibling of `verdict`); the
    // `trailingBytesIgnored=N` note is retained for back-compat readers.
    int trailingBytesIgnored = 0;
    std::vector<std::string> notes;

    // pixel buffers (linear RGBA float, outW x outH). Populated on successful
    // apply or base-only decode so the CLI can --dump them.
    std::vector<float> appliedRgba;
    std::vector<float> baseRgba;
    int outW = 0;
    int outH = 0;
};

class GainMapProbe final {
public:
    // Player target headroom in log2 stops. 4.9 ≈ HDR10 reference viewing
    // condition (≈1000-nit display vs ≈120-nit SDR: 2^4.9 ≈ 29.5×). Used to
    // interpolate applyWeight between base (hB) and alternate (hA) headroom.
    static constexpr double kDefaultTargetHeadroomLog2 = 4.9;

    // Probe `pathUtf8`. When `baseOnly` is true, decode the SDR base but skip
    // the gain apply (used for A/B dumps). `targetHeadroomLog2` selects the
    // player target headroom (log2 stops); defaults to kDefaultTargetHeadroomLog2.
    // The result carries verdict.
    static GainMapProbeResult Probe(const std::string& pathUtf8, bool baseOnly,
                                    double targetHeadroomLog2 = kDefaultTargetHeadroomLog2);

    // Behavioral self-test (locked-in regression assertions for F1's fixes).
    // Exercises internal helpers that cannot be reached via Probe() without
    // real compressed media (tmap trailing tolerance, PQ/HLG base gate,
    // linearize-then-assign baseRgba contract). Prints one line per check and
    // returns the number of FAILed assertions (0 == all passed). The CLI
    // `--selftest` driver additionally runs the end-to-end JXL container check.
    static int RunSelftest();
};
