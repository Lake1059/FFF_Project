#include "pch.h"
#include "3FP/Hdr/HdrToneCurve.h"

#include <algorithm>
#include <cmath>

namespace {

/// Node count = knee endpoint + intermediate anchors + (1,1) endpoint.
constexpr std::uint32_t kMaxNodes = kHdrMaxBezierAnchors + 2;

struct NodeSet {
    std::uint32_t count = 0;                 ///< number of intervals + 1
    float x[kMaxNodes] = {};
    float y[kMaxNodes] = {};
};

/// Build the monotone node set for a window, or return false if the anchors
/// cannot describe a valid non-decreasing curve.
///
/// The spec encodes the anchors as y values only; their x positions are not
/// transmitted. Even spacing between the knee and (1,1) is the natural reading
/// of "intermediate anchor parameters" and is what this implementation uses.
bool BuildNodes(const HdrProcessingWindow& window, NodeSet& nodes) noexcept {
    const auto anchorCount = std::min<std::uint32_t>(
        window.bezierAnchorCount, kHdrMaxBezierAnchors);
    if (!window.toneMappingPresent || anchorCount == 0) return false;

    const float kneeX = window.kneePointX;
    const float kneeY = window.kneePointY;
    if (!std::isfinite(kneeX) || !std::isfinite(kneeY)) return false;
    if (kneeX < 0.0f || kneeX >= 1.0f) return false;
    if (kneeY < 0.0f || kneeY > 1.0f) return false;
    // The knee must not sit above the identity line.
    if (kneeY > kneeX + 1.0e-4f) return false;

    // knee_x == 0 encodes "no linear segment": the anchors span the full range
    // and the curve runs from (0,0) to (1,1). Real vectors use this form.
    nodes.count = anchorCount + 2;
    nodes.x[0] = kneeX;
    nodes.y[0] = kneeY;
    const float span = 1.0f - kneeX;
    for (std::uint32_t index = 0; index < anchorCount; ++index) {
        const auto value = window.bezierAnchors[index];
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
        nodes.x[index + 1] = kneeX + span *
            static_cast<float>(index + 1) / static_cast<float>(anchorCount + 1);
        nodes.y[index + 1] = value;
    }
    nodes.x[anchorCount + 1] = 1.0f;
    nodes.y[anchorCount + 1] = 1.0f;

    // Non-decreasing in both coordinates is required for a valid mapping.
    for (std::uint32_t index = 1; index < nodes.count; ++index) {
        if (nodes.x[index] <= nodes.x[index - 1]) return false;
        if (nodes.y[index] < nodes.y[index - 1]) return false;
    }
    return true;
}

/// Monotone cubic Hermite (PCHIP, Fritsch-Carlson) evaluation.
///
/// Slopes are harmonic means of the adjacent secants, so they cannot change
/// sign: the interpolant is monotone wherever the data is, which is exactly the
/// property a tone curve needs.
float EvaluateNodes(const NodeSet& nodes, const float x) noexcept {
    const auto last = nodes.count - 1;
    if (x <= nodes.x[0]) {
        // Linear part before the knee: slope through (0,0) and the knee.
        if (nodes.x[0] <= 0.0f) return nodes.y[0];
        return nodes.y[0] * (x / nodes.x[0]);
    }
    if (x >= nodes.x[last]) return nodes.y[last];

    std::uint32_t segment = 0;
    while (segment + 1 < last && x > nodes.x[segment + 1]) ++segment;

    const float h = nodes.x[segment + 1] - nodes.x[segment];
    if (h <= 0.0f) return nodes.y[segment + 1];

    // Secant slopes, with the PCHIP harmonic-mean interior slopes.
    const auto secant = [&nodes](const std::uint32_t index) noexcept {
        return (nodes.y[index + 1] - nodes.y[index]) /
            (nodes.x[index + 1] - nodes.x[index]);
    };

    const auto slopeAt = [&](const std::uint32_t index) noexcept {
        if (index == 0) return secant(0);
        if (index == last) return secant(last - 1);
        const float previous = secant(index - 1);
        const float next = secant(index);
        if (previous == 0.0f || next == 0.0f) return 0.0f;
        if ((previous < 0.0f) != (next < 0.0f)) return 0.0f;
        const float hPrevious = nodes.x[index] - nodes.x[index - 1];
        const float hNext = nodes.x[index + 1] - nodes.x[index];
        const float w1 = 2.0f * hNext + hPrevious;
        const float w2 = hNext + 2.0f * hPrevious;
        return (w1 + w2) / (w1 / previous + w2 / next);
    };

    const float d0 = slopeAt(segment);
    const float d1 = slopeAt(segment + 1);
    const float t = (x - nodes.x[segment]) / h;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return h00 * nodes.y[segment] + h10 * h * d0 +
        h01 * nodes.y[segment + 1] + h11 * h * d1;
}

}  // namespace

namespace HdrToneCurveBuild {

void BuildIdentity(HdrToneCurve& curve) noexcept {
    curve.identity = true;
    curve.kneePointX = 0.0f;
    curve.kneePointY = 0.0f;
    const auto last = static_cast<float>(kHdrToneCurveLutSize - 1);
    for (std::uint32_t index = 0; index < kHdrToneCurveLutSize; ++index)
        curve.lut[index] = static_cast<float>(index) / last;
}

float EvaluateCurve(const HdrProcessingWindow& window, const float x) noexcept {
    NodeSet nodes{};
    if (!BuildNodes(window, nodes)) return std::clamp(x, 0.0f, 1.0f);
    if (!std::isfinite(x)) return 0.0f;
    return std::clamp(EvaluateNodes(nodes, std::clamp(x, 0.0f, 1.0f)), 0.0f, 1.0f);
}

bool BuildFromWindow(const HdrProcessingWindow& window, HdrToneCurve& curve) noexcept {
    BuildIdentity(curve);
    NodeSet nodes{};
    if (!BuildNodes(window, nodes)) return false;

    const auto last = static_cast<float>(kHdrToneCurveLutSize - 1);
    auto previous = 0.0f;
    for (std::uint32_t index = 0; index < kHdrToneCurveLutSize; ++index) {
        const auto x = static_cast<float>(index) / last;
        const auto y = std::clamp(EvaluateNodes(nodes, x), 0.0f, 1.0f);
        // Enforce monotonicity numerically as well: accumulated rounding must
        // never be allowed to introduce a local inversion into the table.
        if (index > 0 && y < previous) {
            curve.lut[index] = previous;
        } else {
            curve.lut[index] = y;
            previous = y;
        }
    }
    curve.identity = false;
    curve.kneePointX = window.kneePointX;
    curve.kneePointY = window.kneePointY;
    return true;
}

}  // namespace HdrToneCurveBuild
