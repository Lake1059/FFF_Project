#pragma once

// ST 2094-40 (HDR10+) / ST 2094-30 (HDR Vivid) dynamic metadata snapshot.
//
// Why this is a separate, fixed-size, trivially-copyable type
// ----------------------------------------------------------
// HdrFrameState is passed by value in several places (HdrProcessor::State()
// returns a copy under the mutex, VideoRenderer::Render holds one for the whole
// frame). Anything reachable from it therefore has to be cheap to copy and
// must not allocate: no std::vector, no std::string. The window/percentile
// arrays are fixed at the spec maxima, so one struct covers every conforming
// stream with no heap traffic on the playback path.
//
// Reading maxscl correctly (this is the trap)
// -------------------------------------------
// libavutil/hdr_dynamic_metadata.h documents maxscl as "the maximum of the
// color components of *linearized* RGB values", range 0..1. It is therefore
// already in the light domain with 1.0 = the PQ peak (10000 cd/m^2), and the
// conversion is the plain ratio  nits = maxscl * 10000. It must NOT be fed
// through a second ST 2084 inverse EOTF: maxscl = 0.0334 is 334 nits, whereas
// an inverse EOTF on the same number yields 0.02 nits (a whole clip whose peak
// is black).
//
// Field-name trap in the percentile array
// ---------------------------------------
// AVHDRPlusPercentile swaps the obvious reading: `percentage` (uint8_t) is the
// percentage (0..100) and `percentile` (AVRational) is the *linearized maxRGB
// value* (0..1). Taking them by name silently mixes a 0..100 quantity with a
// 0..1 one and neither range check fires.

#include <cstdint>

/// Spec maxima. ST 2094-40 allows num_windows in [1,3], and caps both the
/// distribution and the bezier anchor arrays at 15 entries.
inline constexpr std::uint32_t kHdrMaxProcessingWindows = 3;
inline constexpr std::uint32_t kHdrMaxDistributionPoints = 15;
inline constexpr std::uint32_t kHdrMaxBezierAnchors = 15;

enum class HdrMetadataKind : std::uint32_t {
    None = 0,
    Hdr10Plus = 1,   // ST 2094-40
    HdrVivid = 2,    // ST 2094-30 (curve interpretation differs; see HdrToneCurve)
};

/// One entry of distribution_maxrgb. See the field-name trap above.
struct HdrDistributionPoint {
    std::uint8_t percentage = 0;      ///< 0..100 (av's `percentage`)
    float linearizedMaxRgb = 0.0f;    ///< 0..1   (av's `percentile`)
    float nits = 0.0f;                ///< derived: linearizedMaxRgb * 10000
};

/// One ST 2094-40 processing window (params[i]).
struct HdrProcessingWindow {
    bool valid = false;
    float upperLeftX = 0.0f;
    float upperLeftY = 0.0f;
    float lowerRightX = 1.0f;
    float lowerRightY = 1.0f;
    /// Linearized RGB maxima, 0..1. nits = value * 10000.
    float maxScl[3] = { 0.0f, 0.0f, 0.0f };
    float averageMaxRgb = 0.0f;
    float fractionBrightPixels = 0.0f;
    std::uint32_t distributionCount = 0;
    HdrDistributionPoint distribution[kHdrMaxDistributionPoints] = {};
    /// tone_mapping_flag: when 0 the knee/bezier fields carry no function.
    bool toneMappingPresent = false;
    float kneePointX = 0.0f;
    float kneePointY = 0.0f;
    std::uint32_t bezierAnchorCount = 0;
    float bezierAnchors[kHdrMaxBezierAnchors] = {};
    /// Derived at parse time so the render path never recomputes them.
    float signalPeakNits = 0.0f;         ///< max(maxScl) * 10000
    float distributionPeakNits = 0.0f;   ///< highest percentile entry * 10000
};

/// Why a field was dropped. Recorded so the snapshot can report *why* a stream
/// fell back instead of silently behaving like plain HDR10.
enum class HdrMetadataDegrade : std::uint32_t {
    None = 0,
    WindowCountInvalid = 1,
    NonMonotonicDistribution = 2,
    AnchorsOutOfRange = 3,
    ToneMappingAbsent = 4,
    TooFewAnchors = 5,
    NonFiniteValues = 6,
};

/// True when the degrade reason makes the tone-mapping function unusable, so the
/// renderer must stay on the previous (static HDR10) path. Non-blocking reasons
/// still allow the curve to be applied.
inline bool IsBlockingDegrade(const HdrMetadataDegrade reason) noexcept {
    switch (reason) {
    case HdrMetadataDegrade::ToneMappingAbsent:
    case HdrMetadataDegrade::TooFewAnchors:
    case HdrMetadataDegrade::NonFiniteValues:
    case HdrMetadataDegrade::WindowCountInvalid:
        return true;
    default:
        return false;
    }
}

struct HdrDynamicMetadata {
    HdrMetadataKind kind = HdrMetadataKind::None;
    /// Validated window count, 0..kHdrMaxProcessingWindows.
    std::uint32_t windowCount = 0;
    HdrProcessingWindow windows[kHdrMaxProcessingWindows] = {};
    /// targeted_system_display_maximum_luminance, already in cd/m^2 -- AV's side data
    /// delivers it descaled (measured: the FATE sample reads 400.000000 for a 400-nit
    /// target). The "units of 0.0001 cd/m^2" wording in libavutil's header describes the
    /// coded bitstream syntax element, not this struct, so multiplying by 10000 here would
    /// push every value into ValidPeak's clamp and silently saturate it.
    float targetedDisplayNits = 0.0f;
    bool hasTargetedDisplay = false;
    HdrMetadataDegrade degrade = HdrMetadataDegrade::None;
    /// Bumped every time a frame carried fresh metadata (diagnostics + tests).
    std::uint64_t serial = 0;
    /// How many consecutive frames have reused this snapshot instead of a new one.
    std::uint64_t heldFrames = 0;

    bool HasToneMappingFunction() const noexcept {
        return windowCount > 0 && windows[0].valid && windows[0].toneMappingPresent;
    }
};
