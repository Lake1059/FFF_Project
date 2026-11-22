#pragma once

#include "3FP/Hdr/HdrToneCurve.h"

#include <cstdint>

// CPU mirrors of the shared playback HLSL constant buffers.
struct VideoShaderSettings {
    // gamut: 0 = Rec.709, 1 = Rec.2020, 2 = P3 (DCI/Display).
    std::uint32_t colorMode = 0, transfer = 0, gamut = 0, reserved = 0;
    // sdrPeak is the SDR reference white (BT.2408 = 203 nits), not the panel peak.
    float sdrPeak = 203, hdrPeak = 100, paperWhite = 203, targetPeak = 1000;
    float sourceWidth = 0, sourceHeight = 0, outputWidth = 0, outputHeight = 0;
    std::uint32_t inputLayout = 0;
    float sampleScale = 1, yOffset = 0, yScale = 1;
    float cOffset = 0.5f, cScale = 1, kr = 0.2126f, kb = 0.0722f;
    // imageModeX drives the still-image pixel-block enlargement in SampleVideo;
    // it reuses what used to be a padding slot, so the constant-buffer layout
    // stays the same size.
    float chromaOffsetX = 0, chromaOffsetY = 0, imageModeX = 0, padding2 = 0;
    std::uint32_t projection360 = 0;
    float viewYaw = 0, viewPitch = 0, viewFovY = 90;
    // viewRotation is quarter turns clockwise (0..3) and reuses a former
    // padding slot, so the constant-buffer layout the HLSL cbuffer expects
    // is unchanged in size.
    float viewAspect = 1, viewRotation = 0, padding4 = 0, padding5 = 0;
};
static_assert(sizeof(VideoShaderSettings) == 128);

// Curve table width. Tied to the reconstruction's own LUT size: the shader
// samples the table with normalised coordinates, so a mismatch would rescale
// the tone curve silently instead of failing.
inline constexpr std::uint32_t kDynamicCurveLutWidth = kHdrToneCurveLutSize;
// The pixel shader divides a window's row number by a literal 3.0 to centre it in
// the LUT's rows, so the two have to stay equal.
static_assert(kHdrMaxProcessingWindows == 3,
    "SampleCurve's row centring hardcodes the window count");

// Half-width of the cross-window transition band, in normalised frame units.
// Only consulted for multi-window ST 2094 streams.
inline constexpr float kDynamicWindowBlend = 0.05f;

// CPU mirror of the HLSL `DynamicSettings` cbuffer (register b2). Bound only
// when a stream carries usable ST 2094 metadata; DynamicEnabled == 0 otherwise,
// in which case the pixel shader takes its original mapping path.
//
// Layout must match the HLSL declaration exactly:
//   row 0: enabled, windowCount, flags, windowBlend   (4 x 4 = 16)
//   row 1: sourcePeak, targetPeak, pad0.xy            (16)
//   row 2: windowMin0.xy, windowMax0.xy               (16)
//   row 3: windowMin1.xy, windowMax1.xy               (16)
//   row 4: windowMin2.xy, windowMax2.xy               (16)
//   row 5: transformEnabled, transformDomain, lutBound, pad (16)
//   row 6: outputMatrixRows[0][0..2], pad             (16)
//   row 7: outputMatrixRows[1][0..2], pad             (16)
//   row 8: outputMatrixRows[2][0..2], pad             (16)
struct DynamicShaderSettings {
    std::uint32_t enabled = 0, windowCount = 1, flags = 0;
    float windowBlend = 0.0f;
    float sourcePeak = 0.0f, targetPeak = 0.0f;
    float pad0[2] = {};
    float windowMin0[2] = {}, windowMax0[2] = {};
    float windowMin1[2] = {}, windowMax1[2] = {};
    float windowMin2[2] = {}, windowMax2[2] = {};
    // Caller-supplied colour transform (FFF3FP_SetDynamicMetadata kind 3). Rows
    // 5..8 are appended, so the ST 2094 rows above keep their offsets and the
    // two features cannot shift each other's fields.
    std::uint32_t transformEnabled = 0;  // 0 = no transform this frame
    std::uint32_t transformDomain = 0;   // payload lutDomain: 0 = coded components, 1 = coded RGB
    std::uint32_t transformLutBound = 0; // 1 = the 3-D LUT is bound at t4
    std::uint32_t transformPad0 = 0;
    // Row-major output matrix (out = M * rgb) applied to the linear-light signal
    // after transfer decoding, before gamut conversion. All-zero = identity, so
    // the uploader normalises an all-zero payload to the identity matrix.
    // Three padded register rows, deliberately NOT a flat float[9]: the
    // shader reads each float4 row's .xyz, so a compact array would put m3
    // into TransformMatrix0.w -- a component nothing reads -- and shift rows
    // 1 and 2 by one float each. sizeof is 144 either way, so only
    // tools/check_shader_cbuffer_layout.py can tell the two apart.
    float outputMatrixRows[3][4] = {};
};
static_assert(sizeof(DynamicShaderSettings) == 144,
    "DynamicShaderSettings must stay in lockstep with the HLSL DynamicSettings cbuffer");

// Optional auxiliary-layer composition, bound as its own constant buffer (b3) so
// the b0 and b2 layouts stay bit-identical to the single-stream path. With
// `enabled == 0` the shader takes its original branches untouched.
struct ElCompositionSettings {
    std::uint32_t enabled = 0;        // 1 = the auxiliary planes at t5..t7 are composed
    std::uint32_t residualCenter = 0; // neutral code of the auxiliary component
    std::int32_t residualShift = 0;   // right shift applied to (auxiliary - centre)
    std::uint32_t maxValue = 0;       // (1 << base bit depth) - 1
};
static_assert(sizeof(ElCompositionSettings) == 16,
    "ElCompositionSettings must stay in lockstep with the HLSL ElComposition cbuffer");

