// CicpSupport.h — single source of truth for "which CICP values the renderer
// actually accepts".
//
// Extracted from PlayerVideoRenderer::UpdateCicpInputs in VideoRenderer.cpp so
// that the media-info reporting layer (PlayerSession.cpp / PickDeclaredColor)
// and the renderer itself share the same whitelist instead of F2 having copied
// it by hand. Behaviour is unchanged: each function returns true for exactly
// the set of AVCOL_* values the renderer's own gates accept.
//
// Source of truth inside VideoRenderer.cpp:
//  * CicpPrimariesSupported  -> the `supportedPrimaries` switch at ~line 7743.
//    Unsupported primaries are not fatal: the renderer falls back to the
//    default gamut (Rec.709 SDR / Rec.2020 HDR) and logs once.
//  * CicpTransferSupported   -> the `supportedTransfer` switch at ~line 7752.
//    Unsupported transfer functions are not fatal either: the renderer keeps
//    the stream's inferred encoding and logs once.
//  * CicpMatrixSupported     -> the renderer no longer rejects an unknown YUV
//    component transform at presentation time. ToSwsColorSpace keeps the
//    established 709/601 fallback, while the media-info layer reports the
//    matrix as ignored instead of claiming a precise ICTCP conversion.
//    UNSPECIFIED is accepted (the sws layer picks a width-based default).
//    Range (color_range) has no whitelist: IsFullRange treats anything that is
//    not explicitly JPEG as limited range, so CicpMatrixSupported does NOT
//    cover range; callers that need a range gate must keep their own.
//
// Pure header, inline/constexpr, no .cpp on purpose: adding a TU would force a
// vcxproj edit. Consumers must already have included <libavutil/pixfmt.h>
// (both VideoRenderer.cpp and PlayerSession.cpp do, via pch/extern "C"); the
// include here is defensive and guarded.
#pragma once

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace fff::render {

// Primaries the shader gamut switch (ResolveSourceGamut) and the fallback
// policy know how to name. Anything else is degraded to the default gamut.
inline constexpr bool CicpPrimariesSupported(const int value) noexcept {
    switch (value) {
    case AVCOL_PRI_RESERVED0:
    case AVCOL_PRI_UNSPECIFIED:
    case AVCOL_PRI_BT709:
    case AVCOL_PRI_BT470BG:
    case AVCOL_PRI_SMPTE170M:
    case AVCOL_PRI_SMPTE240M:
    case AVCOL_PRI_BT2020:
    case AVCOL_PRI_SMPTE431:
    case AVCOL_PRI_SMPTE432:
        return true;
    default:
        return false;
    }
}

// Transfer functions the HDR/shader path (ResolveDecodeTransfer + the
// transferFromFrame -> shader-index mapping) understands. Anything else is
// degraded to the stream-class encoding.
inline constexpr bool CicpTransferSupported(const int value) noexcept {
    switch (value) {
    case AVCOL_TRC_RESERVED0:
    case AVCOL_TRC_UNSPECIFIED:
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_GAMMA22:
    case AVCOL_TRC_GAMMA28:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_SMPTE240M:
    case AVCOL_TRC_LINEAR:
    case AVCOL_TRC_IEC61966_2_1:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
    case AVCOL_TRC_SMPTE2084:
    case AVCOL_TRC_ARIB_STD_B67:
        return true;
    default:
        return false;
    }
}

// YUV component transforms the renderer's YUV upload path (sws_scale + the
// shader YUV->RGB matrix) can apply precisely. Anything outside this list on
// a YUV frame is reported as ignored and falls back to the established 709/601
// choice in ToSwsColorSpace; on an RGB frame the value is ignored, which is
// why the reporting layer uses this as a "declared but renderable" gate
// rather than a fatal check.
inline constexpr bool CicpMatrixSupported(const int value) noexcept {
    switch (value) {
    case AVCOL_SPC_UNSPECIFIED:
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_YCGCO:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return true;
    default:
        return false;
    }
}

} // namespace fff::render
