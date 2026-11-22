#pragma once

#include <d2d1.h>

namespace fff::render {
inline D2D1_STROKE_STYLE_PROPERTIES TimedTextStrokeStyleProperties() noexcept {
    D2D1_STROKE_STYLE_PROPERTIES properties{};
    properties.startCap = D2D1_CAP_STYLE_FLAT;
    properties.endCap = D2D1_CAP_STYLE_FLAT;
    properties.dashCap = D2D1_CAP_STYLE_FLAT;
    properties.lineJoin = D2D1_LINE_JOIN_ROUND;
    properties.miterLimit = 1.0f;
    properties.dashStyle = D2D1_DASH_STYLE_SOLID;
    return properties;
}
}
