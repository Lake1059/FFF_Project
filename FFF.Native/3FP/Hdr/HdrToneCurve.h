#pragma once

// ST 2094-40 tone-mapping function reconstruction.
//
// What the spec gives us
// ----------------------
// For each processing window ST 2094-40 encodes a curve as:
//   * a linear segment from (0,0) to (knee_point_x, knee_point_y), and
//   * a curved segment from the knee to (1,1), described by
//     num_bezier_curve_anchors intermediate anchor values.
//
// The spec does NOT mandate how the receiver interpolates the anchors, so the
// method is a receiver choice. This file implements one and documents why.
//
// Why monotone cubic Hermite (PCHIP)
// ----------------------------------
// A tone-mapping function must be monotonically non-decreasing: any overshoot
// inverts local contrast and reads as banding or ringing. An ordinary cubic
// spline through the same anchors can overshoot; PCHIP cannot, by construction
// (its slopes are harmonic means that vanish at sign changes). It also needs no
// iteration and no linear solve, so it is cheap and easy to test.
//
// Why the curve is evaluated into a LUT on the CPU
// ------------------------------------------------
// Evaluating PCHIP per pixel would mean a segment search plus a branch per
// pixel. Baking the window curves into a small 1-D table once per frame keeps
// the shader to a single interpolation, and -- more importantly -- makes the
// reconstruction verifiable by unit tests without a GPU.

#include "3FP/Hdr/HdrDynamicMetadata.h"

#include <cstdint>

/// Table resolution. SDR presentation resolves to 8/10-bit output, for which
/// 256 entries leave the quantisation step far below one output code.
inline constexpr std::uint32_t kHdrToneCurveLutSize = 256;

/// One reconstructed window curve.
struct HdrToneCurve {
    /// Identity mapping: y = x. Used whenever no usable metadata curve exists,
    /// so callers always have a valid table to bind.
    bool identity = true;
    /// Reconstructed curve sampled uniformly over x in [0,1].
    /// lut[i] corresponds to x = i / (kHdrToneCurveLutSize - 1).
    float lut[kHdrToneCurveLutSize] = {};
    float kneePointX = 0.0f;
    float kneePointY = 0.0f;
};

namespace HdrToneCurveBuild {

/// Fill `curve` with the identity mapping.
void BuildIdentity(HdrToneCurve& curve) noexcept;

/// Reconstruct the tone-mapping curve of one ST 2094-40 window.
///
/// Returns false when the window carries no usable curve (tone_mapping_flag
/// clear, no anchors, malformed knee/anchors); in that case `curve` is left as
/// the identity and the caller must stay on its previous mapping path.
///
/// The anchors are assumed to be validated and monotone as produced by
/// HdrMetadataParse; this function re-checks monotonicity and fails rather than
/// emitting a non-monotone table.
bool BuildFromWindow(const HdrProcessingWindow& window, HdrToneCurve& curve) noexcept;

/// Evaluate the monotone Hermite interpolant at an arbitrary x in [0,1].
/// Exposed so unit tests can check monotonicity and endpoint exactness at
/// points between LUT samples.
float EvaluateCurve(const HdrProcessingWindow& window, float x) noexcept;

}  // namespace HdrToneCurveBuild
