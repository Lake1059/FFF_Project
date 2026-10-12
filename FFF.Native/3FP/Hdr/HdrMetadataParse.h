#pragma once

// Pure conversion from FFmpeg's ST 2094-40 / ST 2094-30 side-data structs into
// the fixed-size HdrDynamicMetadata snapshot.
//
// Everything here is a free function with no state, no allocation and no
// throwing, so it can be unit-tested without a GPU, a window, or a decoder.
// That matters because parsing is the part of this feature most likely to be
// subtly wrong, and the only way to prove it right is to compare against an
// independent implementation (ffprobe / av_dynamic_hdr_plus_from_t35).

#include "3FP/Hdr/HdrDynamicMetadata.h"

struct AVDynamicHDRPlus;
struct AVDynamicHDRVivid;

namespace HdrMetadataParse {

/// ST 2094-40. Never throws. On malformed input returns a snapshot with
/// kind = Hdr10Plus, degrade set, and windowCount possibly 0.
HdrDynamicMetadata FromHdr10Plus(const AVDynamicHDRPlus& plus) noexcept;

/// ST 2094-30. Curve interpretation differs from HDR10+, so only the shared
/// window/percentile shape is filled here; the curve itself is built elsewhere.
HdrDynamicMetadata FromVivid(const AVDynamicHDRVivid& vivid) noexcept;

/// maxscl and the distribution values are *linearized* RGB with 1.0 = the PQ
/// peak, so this is a plain ratio -- deliberately not an ST 2084 inverse EOTF.
/// See the header comment in HdrDynamicMetadata.h.
float LinearizedToNits(float linearized) noexcept;

/// targeted_system_display_maximum_luminance, already normalised to cd/m^2 by
/// libavutil (the 0.0001-unit scaling belongs to the bitstream, not to this
/// struct), clamped to a sane range.
float TargetedDisplayToNits(double rationalValue) noexcept;

}  // namespace HdrMetadataParse
