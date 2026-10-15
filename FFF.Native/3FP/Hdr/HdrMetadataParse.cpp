#include "pch.h"
#include "3FP/Hdr/HdrMetadataParse.h"

extern "C" {
#include <libavutil/hdr_dynamic_metadata.h>
#include <libavutil/hdr_dynamic_vivid_metadata.h>
#include <libavutil/rational.h>
}

#include <cmath>

namespace {

/// av_q2d on a rational that may be unset/invalid. Unset AVRationals are
/// {0,0} or {1,0}; both would divide by zero, so guard before converting.
bool RationalToDouble(const AVRational value, double& out) noexcept {
    if (value.den == 0) return false;
    const auto converted = av_q2d(value);
    if (!std::isfinite(converted)) return false;
    out = converted;
    return true;
}

/// A value that must land in [0,1] to be usable as a normalised signal.
bool RationalUnit(const AVRational value, float& out) noexcept {
    double converted = 0.0;
    if (!RationalToDouble(value, converted)) return false;
    if (converted < 0.0 || converted > 1.0) return false;
    out = static_cast<float>(converted);
    return true;
}

/// Same, but tolerates the tiny overshoot that fixed-point encoders produce
/// (e.g. 1.0000001) by clamping instead of rejecting.
bool RationalUnitTolerant(const AVRational value, float& out) noexcept {
    double converted = 0.0;
    if (!RationalToDouble(value, converted)) return false;
    if (converted < -1.0e-4 || converted > 1.0 + 1.0e-4) return false;
    out = static_cast<float>(std::clamp(converted, 0.0, 1.0));
    return true;
}

}  // namespace

namespace HdrMetadataParse {

float LinearizedToNits(const float linearized) noexcept {
    if (!std::isfinite(linearized)) return 0.0f;
    if (linearized <= 0.0f) return 0.0f;
    // maxscl/documentation: linearized RGB in [0,1] where 1.0 is the PQ peak.
    return std::clamp(linearized, 0.0f, 1.0f) * 10000.0f;
}

float TargetedDisplayToNits(const double rationalValue) noexcept {
    if (!std::isfinite(rationalValue) || rationalValue <= 0.0) return 0.0f;
    // The ST 2094-40 *bitstream* carries this in units of 0.0001 cd/m^2, but
    // libavutil normalises it while parsing the SEI: for the FATE HDR10+ vector
    // ffprobe reports `targeted_system_display_maximum_luminance=400/1`, i.e.
    // 400 cd/m^2 already. Scaling by 10000 a second time yields 4,000,000, which
    // the clamp below would silently saturate to 10000 -- a plausible-looking
    // wrong answer, which is why this is verified against ffprobe rather than
    // against the header text alone.
    return std::clamp(static_cast<float>(rationalValue), 1.0f, 10000.0f);
}

HdrDynamicMetadata FromHdr10Plus(const AVDynamicHDRPlus& plus) noexcept {
    HdrDynamicMetadata result{};
    result.kind = HdrMetadataKind::Hdr10Plus;

    // ---- window count -----------------------------------------------------
    // Spec: 1..3. Anything else is non-conforming; keep the sample usable by
    // treating window 0 as a full-frame window, but record why.
    std::uint32_t count = plus.num_windows;
    if (count < 1 || count > kHdrMaxProcessingWindows) {
        result.degrade = HdrMetadataDegrade::WindowCountInvalid;
        count = 1;
    }
    result.windowCount = count;

    // ---- targeted system display luminance --------------------------------
    if (const auto targeted = av_q2d(plus.targeted_system_display_maximum_luminance);
        std::isfinite(targeted) && targeted > 0.0) {
        result.targetedDisplayNits = TargetedDisplayToNits(targeted);
        result.hasTargetedDisplay = result.targetedDisplayNits > 0.0f;
    }

    // ---- per-window parameters -------------------------------------------
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& source = plus.params[index];
        auto& window = result.windows[index];
        window.valid = true;

        // Corners are normalised to the picture. Missing/invalid corners fall
        // back to the full-frame default rather than failing the whole window.
        const bool windowIsFullFrame = result.degrade == HdrMetadataDegrade::WindowCountInvalid;
        if (windowIsFullFrame) {
            window.upperLeftX = 0.0f; window.upperLeftY = 0.0f;
            window.lowerRightX = 1.0f; window.lowerRightY = 1.0f;
        } else {
            if (!RationalUnitTolerant(source.window_upper_left_corner_x, window.upperLeftX) ||
                !RationalUnitTolerant(source.window_upper_left_corner_y, window.upperLeftY) ||
                !RationalUnitTolerant(source.window_lower_right_corner_x, window.lowerRightX) ||
                !RationalUnitTolerant(source.window_lower_right_corner_y, window.lowerRightY)) {
                window.upperLeftX = 0.0f; window.upperLeftY = 0.0f;
                window.lowerRightX = 1.0f; window.lowerRightY = 1.0f;
            }
            // A degenerate/empty window carries no pixels; normalise it away.
            if (window.lowerRightX <= window.upperLeftX) { window.upperLeftX = 0.0f; window.lowerRightX = 1.0f; }
            if (window.lowerRightY <= window.upperLeftY) { window.upperLeftY = 0.0f; window.lowerRightY = 1.0f; }
        }

        // maxscl: linearized RGB, so nits = value * 10000 (see header notes).
        float maxSclPeak = 0.0f;
        for (std::uint32_t channel = 0; channel < 3; ++channel) {
            float value = 0.0f;
            if (!RationalUnitTolerant(source.maxscl[channel], value)) {
                value = 0.0f;
                if (result.degrade == HdrMetadataDegrade::None)
                    result.degrade = HdrMetadataDegrade::NonFiniteValues;
            }
            window.maxScl[channel] = value;
            maxSclPeak = std::max(maxSclPeak, value);
        }
        window.signalPeakNits = LinearizedToNits(maxSclPeak);

        float average = 0.0f;
        if (RationalUnitTolerant(source.average_maxrgb, average))
            window.averageMaxRgb = average;
        float fraction = 0.0f;
        if (RationalUnitTolerant(source.fraction_bright_pixels, fraction))
            window.fractionBrightPixels = fraction;

        // ---- distribution_maxrgb ------------------------------------------
        // Field names are swapped relative to their meaning: `percentage` is
        // the percentage (0..100) and `percentile` is the linearized maxRGB.
        //
        // The spec does not require the maxRGB column to be monotonic in the
        // percentage column: it is a sampled description of the scene, and real
        // vectors (e.g. the FATE HDR10+ clip) carry a non-monotonic shape. Only
        // the encoding ranges are validated here.
        auto distributionCount = std::min<std::uint32_t>(
            source.num_distribution_maxrgb_percentiles, kHdrMaxDistributionPoints);
        for (std::uint32_t point = 0; point < distributionCount; ++point) {
            const auto& entry = source.distribution_maxrgb[point];
            float maxRgb = 0.0f;
            if (!RationalUnitTolerant(entry.percentile, maxRgb)) {
                distributionCount = point;
                if (result.degrade == HdrMetadataDegrade::None)
                    result.degrade = HdrMetadataDegrade::NonFiniteValues;
                break;
            }
            auto& target = window.distribution[point];
            target.percentage = entry.percentage;
            target.linearizedMaxRgb = maxRgb;
            target.nits = LinearizedToNits(maxRgb);
        }
        window.distributionCount = distributionCount;
        // The peak is the largest sampled value, not the last one, because the
        // column is not ordered by magnitude.
        for (std::uint32_t point = 0; point < distributionCount; ++point)
            window.distributionPeakNits = std::max(
                window.distributionPeakNits, window.distribution[point].nits);

        // ---- tone mapping function ----------------------------------------
        window.toneMappingPresent = source.tone_mapping_flag != 0;
        if (!window.toneMappingPresent) {
            if (result.degrade == HdrMetadataDegrade::None)
                result.degrade = HdrMetadataDegrade::ToneMappingAbsent;
            continue;
        }

        float kneeX = 0.0f;
        float kneeY = 0.0f;
        // The header text describes knee_point_x as excluding 0, but real
        // streams -- including the FFmpeg FATE HDR10+ vector -- encode (0,0).
        // That is a meaningful encoding: a zero-length linear segment, i.e. the
        // curve spans the whole range from (0,0) to (1,1). Rejecting it would
        // throw away usable metadata, so 0 is accepted here.
        if (!RationalUnitTolerant(source.knee_point_x, kneeX) ||
            !RationalUnitTolerant(source.knee_point_y, kneeY)) {
            window.toneMappingPresent = false;
            if (result.degrade == HdrMetadataDegrade::None)
                result.degrade = HdrMetadataDegrade::NonFiniteValues;
            continue;
        }
        // The knee must not sit above the identity line; a knee above it would
        // brighten shadows, which no mastering intent encodes.
        if (kneeY > kneeX + 1.0e-4f) {
            window.toneMappingPresent = false;
            if (result.degrade == HdrMetadataDegrade::None)
                result.degrade = HdrMetadataDegrade::AnchorsOutOfRange;
            continue;
        }
        // A knee at x == 0 means there is no linear segment: the curve spans the
        // whole range from (0,0) to (1,1) and the anchors are distributed over
        // the full width. That is exactly what the FFmpeg FATE HDR10+ vector
        // encodes, so it is accepted and carried through as kneeX == 0.
        window.kneePointX = kneeX;
        window.kneePointY = kneeY;

        auto anchorCount = std::min<std::uint32_t>(
            source.num_bezier_curve_anchors, kHdrMaxBezierAnchors);
        float previousAnchor = kneeY;
        bool anchorsValid = true;
        for (std::uint32_t anchor = 0; anchor < anchorCount; ++anchor) {
            float value = 0.0f;
            if (!RationalUnitTolerant(source.bezier_curve_anchors[anchor], value) ||
                value < previousAnchor) {
                anchorsValid = false;
                break;
            }
            previousAnchor = value;
            window.bezierAnchors[anchor] = value;
        }
        if (!anchorsValid) {
            window.toneMappingPresent = false;
            if (result.degrade == HdrMetadataDegrade::None)
                result.degrade = HdrMetadataDegrade::AnchorsOutOfRange;
            continue;
        }
        window.bezierAnchorCount = anchorCount;
        if (anchorCount == 0) {
            // tone_mapping_flag was set but no intermediate anchors exist, so
            // there is no curve to rebuild.
            window.toneMappingPresent = false;
            if (result.degrade == HdrMetadataDegrade::None)
                result.degrade = HdrMetadataDegrade::TooFewAnchors;
            continue;
        }
    }

    return result;
}

HdrDynamicMetadata FromVivid(const AVDynamicHDRVivid& vivid) noexcept {
    HdrDynamicMetadata result{};
    result.kind = HdrMetadataKind::HdrVivid;

    std::uint32_t count = vivid.num_windows;
    if (count < 1 || count > kHdrMaxProcessingWindows) {
        result.degrade = HdrMetadataDegrade::WindowCountInvalid;
        count = 1;
    }
    result.windowCount = count;

    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& source = vivid.params[index];
        auto& window = result.windows[index];
        window.valid = true;
        window.upperLeftX = 0.0f; window.upperLeftY = 0.0f;
        window.lowerRightX = 1.0f; window.lowerRightY = 1.0f;

        // Vivid measures content brightness as normalised maxRGB values.
        float maximumMaxRgb = 0.0f;
        if (RationalUnitTolerant(source.maximum_maxrgb, maximumMaxRgb))
            window.maxScl[0] = window.maxScl[1] = window.maxScl[2] = maximumMaxRgb;
        float average = 0.0f;
        if (RationalUnitTolerant(source.average_maxrgb, average))
            window.averageMaxRgb = average;
        window.signalPeakNits = LinearizedToNits(maximumMaxRgb);
        // HDR Vivid's tone mapping is a *different* curve family: a parametric
        // base curve (base_param_m_p / m_m / m_a / m_b / m_n) plus an optional
        // three-spline, not ST 2094-40's knee + bezier anchors. The CUVA 005.1
        // specification that defines how those parameters form the curve is not
        // publicly available (the CUVA publisher's download is gated), so
        // the formula cannot be implemented from a citable source.
        //
        // Rather than invent a curve, the window is deliberately left without a
        // tone-mapping function: Vivid keeps the previous peak-only behaviour
        // and never claims the dynamic path. `window.toneMappingPresent` stays
        // false and HdrMetadataDegrade::ToneMappingAbsent is reported, which is
        // what makes the snapshot honest about it.
        window.toneMappingPresent = false;
        if (source.tone_mapping_mode_flag != 0) {
            if (source.tone_mapping_param_num > 0) {
                // Like HDR10+, the Vivid bitstream stores this normalised but
                // libavutil hands it over as a rational; take it at face value.
                const auto& mapping = source.tm_params[0];
                if (const auto targeted =
                        av_q2d(mapping.targeted_system_display_maximum_luminance);
                    std::isfinite(targeted) && targeted > 0.0) {
                    result.targetedDisplayNits = std::clamp(
                        static_cast<float>(targeted), 1.0f, 10000.0f);
                    result.hasTargetedDisplay = true;
                }
            }
        }
    }

    if (result.degrade == HdrMetadataDegrade::None)
        result.degrade = HdrMetadataDegrade::ToneMappingAbsent;
    return result;
}

}  // namespace HdrMetadataParse
