#pragma once

#include "3FR/Api/FFF.Native.Api.h"

#include <cstdint>

enum class FFF3FPDecodeMode : std::uint32_t {
    Unspecified = 0,
    Cpu = 1,
    Gpu = 2,
    D3D11 = Gpu,
};

enum class FFF3FPColorMode : std::uint32_t {
    MapToSdr = 0,
    RawHdrAsSdr = 1,
    MapToHdr = 2,
};

enum class FFF3FPVideoScalingMode : std::uint32_t {
    Shader = 0,
    D3D11VideoProcessor = 1,
};

enum class FFF3FPColorTransfer : std::uint32_t {
    SdrBt709 = 0,
    Pq = 1,
    Hlg = 2,
};

enum class FFF3FPHdrFormat : std::uint32_t {
    Sdr = 0,
    Hdr10 = 1,
    Hdr10Plus = 2,
    Hlg = 3,
    DolbyVision = 4,
    HdrVivid = 5,
};

enum class FFF3FPHdrCompatibility : std::uint32_t {
    None = 0,
    Hdr10 = 1u << 0,
    Hlg = 1u << 1,
    DolbyVision = 1u << 2,
    HdrVivid = 1u << 3,
};

enum class FFF3FPHdrProcessingPath : std::uint32_t {
    None = 0,
    StaticHdr10 = 1,
    Hdr10PlusDynamic = 2,
    HlgDisplayMapped = 3,
    DolbyVisionHdr10Fallback = 4,
    DolbyVisionFelFallback = 5,
    HdrVividDynamic = 6,
};

enum class FFF3FPDolbyVisionEnhancementLayer : std::uint32_t {
    None = 0,
    Mel = 1,
    Fel = 2,
    Unknown = 3,
};

enum class FFF3FPState : std::uint32_t {
    Idle = 0,
    Opening = 1,
    Ready = 2,
    Playing = 3,
    Paused = 4,
    Ended = 5,
    Failed = 6,
    Closed = 7,
};

enum class FFF3FPEvent : std::uint32_t {
    StateChanged = 1,
    OpenCompleted = 2,
    OperationCompleted = 3,
    PlaybackEnded = 4,
    Error = 5,
    ColorModeChanged = 6,
    DeviceChanged = 7,
};

using FFF3FPEventCallback = void(__cdecl*)(void* context, FFF3FPEvent eventType,
    const char* detailJsonUtf8);

struct FFF3FPConfiguration {
    std::uint32_t size;
    std::uint32_t version;
    void* outputWindow;
    FFF3FPDecodeMode decodeMode;
    FFF3FPColorMode colorMode;
    float sdrPeakNits;
    float hdrPeakNits;
    float sdrPaperWhiteNits;
    const char* audioEndpointIdUtf8;
    FFF3FPEventCallback eventCallback;
    void* eventCallbackContext;
};

struct FFF3FPSnapshot {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPState state;
    FFF3FPDecodeMode decodeMode;
    FFF3FPColorMode requestedColorMode;
    FFF3FPColorMode actualColorMode;
    std::int64_t position100ns;
    std::int64_t duration100ns;
    std::int64_t frameIndex;
    std::int64_t framePts;
    std::int32_t frameTimeBaseNumerator;
    std::int32_t frameTimeBaseDenominator;
    std::int32_t selectedVideoStream;
    std::int32_t selectedAudioStream;
    std::uint32_t videoWidth;
    std::uint32_t videoHeight;
    std::uint32_t isHdrSource;
    std::uint32_t isExternalAudio;
    std::int64_t externalAudioOffset100ns;
    std::uint64_t decodedVideoFrames;
    std::uint64_t presentedVideoFrames;
    std::uint64_t droppedVideoFrames;
    std::uint32_t queuedVideoFrames;
    std::uint32_t sourcePeakNits;
    // Audio diagnostics use the same 100 ns time base as position100ns. They
    // report renderer state only; the media clock remains the owner of video
    // presentation timing.
    std::uint64_t decodedAudioFrames;
    std::int64_t audioPosition100ns;
    std::int64_t bufferedAudio100ns;
    std::uint64_t audioUnderruns;
    std::uint64_t audioTimestampJitterFrames;
    std::uint64_t audioDiscontinuities;
    std::uint64_t audioInsertedSilenceFrames;
    std::uint64_t audioDroppedOverlapFrames;
    // API v5 diagnostics. `presentedVideoFrames` counts decoded frames accepted
    // by the renderer (including headless/clip-mode sessions); `swapChainPresents`
    // is the count of actual successful DXGI presents.
    std::uint64_t coalescedVideoFrames;
    std::uint64_t audioRejectedFrames;
    std::uint64_t swapChainPresents;
    std::uint64_t presentWait100ns;
    std::uint64_t deviceLockWait100ns;
    std::uint64_t hardwareTransfer100ns;
    std::uint64_t softwareConvert100ns;
    // Rolling packet-rate estimates for the currently selected streams.
    // These are media-time rates, not the container's static bit_rate field.
    std::uint64_t videoBitRate;
    std::uint64_t audioBitRate;
    // Actual swap-chain precision after renderer/device capability fallback.
    // 8 = BGRA8, 10 = RGB10A2.
    std::uint32_t videoOutputBitDepth;
    // The path used for the most recently rendered frame. The renderer selects
    // this automatically from the decoded surface and output requirements.
    FFF3FPVideoScalingMode videoScalingMode;
    // Advances only after a real demuxer seek succeeds and the new media
    // position has been published. Overlay producers use it to discard old state.
    std::uint64_t timelineGeneration;
    // API v7 HDR diagnostics. Dynamic metadata is copied into renderer-owned
    // state before the decoded AVFrame is released, so these values always
    // describe the cached/presented frame rather than the next decoded frame.
    FFF3FPHdrFormat hdrFormat;
    std::uint32_t compatibleHdrFormats;
    FFF3FPHdrProcessingPath hdrProcessingPath;
    std::uint32_t dolbyVisionProfile;
    std::uint32_t dolbyVisionLevel;
    std::uint32_t hasDolbyVisionRpu;
    std::uint32_t hasDolbyVisionEnhancementLayer;
    FFF3FPDolbyVisionEnhancementLayer dolbyVisionEnhancementLayer;
    std::uint32_t dynamicHdrMetadataActive;
    std::uint32_t hdrFallbackActive;
    std::uint32_t displayMinLuminanceMilliNits;
    std::uint32_t displayPeakNits;
    std::uint32_t displayFullFramePeakNits;
    std::uint32_t effectiveTargetPeakNits;
};

struct FFF3FPAudioPeakLevels {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t channelCount;
    std::uint32_t reserved;
    float values[8];
};

using FFF3FPHandle = void*;
using FFF3FPBitmapSubtitleHandle = void*;
using FFF3FPAssSubtitleHandle = void*;

enum class FFF3FPBitmapSubtitleFlags : std::uint32_t {
    None = 0,
    Clear = 1,
    EndOfStream = 2,
    Forced = 4,
    MoreData = 8,
};

struct FFF3FPBitmapSubtitleFrame {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPBitmapSubtitleFlags flags;
    std::uint32_t reserved;
    std::int64_t start100ns;
    std::int64_t end100ns;
    std::int32_t canvasWidth;
    std::int32_t canvasHeight;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
    std::int32_t stride;
    std::uint32_t pixelBytes;
    std::int64_t sequence;
};

enum class FFF3FPTimedTextCommandType : std::uint32_t {
    Text = 1,
    Bitmap = 2,
};

enum class FFF3FPTimedTextFlags : std::uint32_t {
    None = 0,
    Bold = 1,
    Italic = 2,
    Underline = 4,
    Strikeout = 8,
    HdrHighlightBitmap = 16,
};

enum class FFF3FPTimedTextAlignment : std::uint32_t {
    Near = 0,
    Center = 1,
    Far = 2,
};

struct FFF3FPTimedTextCommand {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPTimedTextCommandType type;
    FFF3FPTimedTextFlags flags;
    float x;
    float y;
    float width;
    float height;
    std::uint32_t foregroundArgb;
    std::uint32_t outlineArgb;
    float fontSize;
    float outlineWidth;
    FFF3FPTimedTextAlignment horizontalAlignment;
    FFF3FPTimedTextAlignment verticalAlignment;
    const char* textUtf8;
    const char* fontFamilyUtf8;
    const void* bitmapBgra;
    std::uint32_t bitmapWidth;
    std::uint32_t bitmapHeight;
    std::uint32_t bitmapStride;
    std::uint32_t bitmapBytes;
    std::uint64_t contentId;
    // Optional version-1 tail. outlineWidth is the final visible distance
    // outside the filled glyph; the renderer uses a 2x centered geometry pen.
    std::uint32_t shadowArgb;
    float shadowOffsetX;
    float shadowOffsetY;
    std::uint32_t reserved;
};

struct FFF3FPTimedTextRasterizationProbe {
    std::uint32_t size;
    std::uint32_t version;
    float outlineWidth;
    float shadowOffsetX;
    float shadowOffsetY;
    float geometryStrokeWidth;
    float effectLeft;
    float effectTop;
    float effectRight;
    float effectBottom;
    float shadowAngleDegrees;
    std::uint32_t naturalSymmetricRendering;
    std::uint32_t grayscaleAntialiasing;
    std::uint32_t pixelSnappingDisabled;
    std::uint32_t outlineIsExternal;
};

// DirectWrite metrics used by the managed subtitle layout. The input font size
// is in the same pixel/DIP unit as FFF3FPTimedTextCommand::fontSize.
struct FFF3FPTimedTextMeasurement {
    std::uint32_t size;
    std::uint32_t version;
    float layoutHeight;
    float visibleTop;
    float visibleBottom;
};

struct FFF3FPTimedTextLayer {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t canvasWidth;
    std::uint32_t canvasHeight;
    std::uint32_t commandCount;
    // 0 = subtitle, 1 = danmaku, 2 = player information, 3 = lyrics. Kept in
    // the original reserved field so the version-1 ABI remains stable while
    // the producers remain independent.
    std::uint32_t layerSlot;
    std::uint64_t sequence;
    const FFF3FPTimedTextCommand* commands;
    // Optional version-1 tail. Older callers may pass the legacy size and are
    // treated as 60 Hz; current callers publish the layer's independent pace.
    float targetFrameRate;
    std::uint32_t reserved2;
    // Optional lyrics presentation tail. Values use logical percentages so
    // managed layout and the native cover renderer share one configuration.
    float coverBackdropBlurRadius;
    std::uint32_t coverBackdropBlurPasses;
    std::uint32_t coverBackdropDownsampleFactor;
    std::uint32_t coverBackdropTintArgb;
    float coverRegionWidthPercentage;
    float lyricsRegionWidthPercentage;
    // Legacy horizontal padding now represents the left inset. The optional
    // tail below adds an independent right inset without changing old fields.
    float coverHorizontalPaddingPercentage;
    float coverVerticalPaddingPercentage;
    float coverRightPaddingPercentage;
};

struct FFF3FPTimedTextStatus {
    std::uint32_t size;
    std::uint32_t version;
    std::uint64_t submittedSequence;
    std::uint64_t renderedSequence;
    std::uint32_t commandCount;
    std::uint32_t canvasWidth;
    std::uint32_t canvasHeight;
    std::uint32_t reserved;
    std::uint64_t visiblePixelCount;
    std::uint64_t spriteCacheHits;
    std::uint64_t spriteCacheMisses;
    // D3D11 exposes only logical buffer 0 for flip-model chains. Its physical
    // identity rotates, so this count must advance once per final presentation.
    std::uint64_t backBufferAcquisitionCount;
    std::uint64_t compositePixelShaderInvocations;
};

// Numeric probe for the production color transform.  This is deliberately
// independent of a swap chain so automated tests can verify luminance anchors
// without judging screenshots by eye.
struct FFF3FPColorTransform {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPColorMode colorMode;
    FFF3FPColorTransfer transfer;
    std::uint32_t source2020;
    std::uint32_t reserved;
    float inputRed;
    float inputGreen;
    float inputBlue;
    float sdrPeakNits;
    float sourcePeakNits;
    float paperWhiteNits;
    float outputRed;
    float outputGreen;
    float outputBlue;
};

// Deterministic ABI-v1 probe for HDR classification and luminance policy.
// Dolby residual: 0=unknown/not present, 1=MEL, 2=FEL.
struct FFF3FPHdrProcessingProbe {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPColorTransfer transfer;
    std::uint32_t dolbyVisionProfile;
    std::uint32_t dolbyVisionLevel;
    std::uint32_t dolbyVisionCompatibilityId;
    std::uint32_t dolbyVisionRpu;
    std::uint32_t dolbyVisionEnhancementLayer;
    std::uint32_t dolbyVisionResidual;
    std::uint32_t hdr10PlusMetadata;
    std::uint32_t hdrVividMetadata;
    float displayPeakNits;
    float displayFullFramePeakNits;
    float targetPeakOverrideNits;
    FFF3FPHdrFormat outputFormat;
    std::uint32_t outputCompatibility;
    FFF3FPHdrProcessingPath outputProcessingPath;
    FFF3FPDolbyVisionEnhancementLayer outputEnhancementLayer;
    std::uint32_t outputDynamicMetadata;
    std::uint32_t outputFallback;
    std::uint32_t outputTargetPeakNits;
};

// Reads one pixel from the current video-only back buffer before desktop color
// management. Intended for renderer regression tests and diagnostics.
struct FFF3FPVideoPixelProbe {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t x;
    std::uint32_t y;
    float red;
    float green;
    float blue;
    float alpha;
    FFF3FPVideoScalingMode scalingMode;
    std::uint32_t outputBitDepth;
    FFF3FPColorMode colorMode;
    std::uint32_t reserved;
};

#ifdef FFFNATIVE_EXPORTS
#define FFF3FP_API extern "C" __declspec(dllexport)
#else
#define FFF3FP_API extern "C" __declspec(dllimport)
#endif

FFF3FP_API std::uint32_t FFF3FP_GetApiVersion() noexcept;
FFF3FP_API FFFResult FFF3FP_Create(const FFF3FPConfiguration* configuration,
    FFF3FPHandle* player) noexcept;
FFF3FP_API FFFResult FFF3FP_Open(FFF3FPHandle player, const char* localPathUtf8) noexcept;
FFF3FP_API FFFResult FFF3FP_Play(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Pause(FFF3FPHandle player) noexcept;
// Synchronously stops the current audio renderer and discards already-submitted
// endpoint buffers. Intended for media/session replacement, not normal pause.
FFF3FP_API FFFResult FFF3FP_DiscardAudioOutput(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Stop(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Close(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Seek(FFF3FPHandle player, std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekKeyframe(FFF3FPHandle player, std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekFrame(FFF3FPHandle player, std::int64_t frameIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_StepFrame(FFF3FPHandle player, std::int32_t direction) noexcept;
FFF3FP_API FFFResult FFF3FP_StepKeyframe(FFF3FPHandle player, std::int32_t direction) noexcept;
FFF3FP_API FFFResult FFF3FP_SelectVideoStream(FFF3FPHandle player,
    std::int32_t streamIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_SelectAudioStream(FFF3FPHandle player,
    std::int32_t streamIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_LoadExternalAudio(FFF3FPHandle player,
    const char* localPathUtf8, std::int32_t streamIndex, std::int64_t offset100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_ClearExternalAudio(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_SetExternalAudioOffset(FFF3FPHandle player,
    std::int64_t offset100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SetColorMode(FFF3FPHandle player, FFF3FPColorMode mode,
    float sdrPeakNits, float hdrPeakNits, float sdrPaperWhiteNits) noexcept;
FFF3FP_API FFFResult FFF3FP_SetOutputWindow(FFF3FPHandle player, void* outputWindow) noexcept;
FFF3FP_API FFFResult FFF3FP_SetAudioEndpoint(FFF3FPHandle player,
    const char* endpointIdUtf8) noexcept;
// Recreates only the WASAPI renderer.  The media session and its selected
// streams stay intact; playback resumes at the current media position.
FFF3FP_API FFFResult FFF3FP_SetAudioExclusiveMode(FFF3FPHandle player,
    std::uint32_t exclusive) noexcept;
FFF3FP_API FFFResult FFF3FP_SetVolume(FFF3FPHandle player, float volume, std::uint32_t muted) noexcept;
FFF3FP_API FFFResult FFF3FP_SetTimedTextLayer(FFF3FPHandle player,
    const FFF3FPTimedTextLayer* layer) noexcept;
FFF3FP_API FFFResult FFF3FP_GetSnapshot(FFF3FPHandle player, FFF3FPSnapshot* snapshot) noexcept;
FFF3FP_API FFFResult FFF3FP_ReadVideoPixel(FFF3FPHandle player,
    FFF3FPVideoPixelProbe* probe) noexcept;
FFF3FP_API FFFResult FFF3FP_GetAudioPeakLevels(FFF3FPHandle player,
    FFF3FPAudioPeakLevels* levels) noexcept;
FFF3FP_API FFFResult FFF3FP_GetTimedTextStatus(FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_GetDanmakuStatus(FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_GetLyricsStatus(FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_EvaluateColorTransform(FFF3FPColorTransform* transform) noexcept;
FFF3FP_API FFFResult FFF3FP_EvaluateHdrProcessing(
    FFF3FPHdrProcessingProbe* probe) noexcept;
FFF3FP_API FFFResult FFF3FP_EvaluateTimedTextRasterization(
    FFF3FPTimedTextRasterizationProbe* probe) noexcept;
FFF3FP_API FFFResult FFF3FP_MeasureTimedText(const char* textUtf8,
    const char* fontFamilyUtf8, float fontSize, FFF3FPTimedTextFlags flags,
    float maxWidth, float outlineWidth, float shadowOffsetX, float shadowOffsetY,
    std::uint32_t shadowEnabled, FFF3FPTimedTextMeasurement* measurement) noexcept;
// Returns the natural single-line DirectWrite width used by text commands.
FFF3FP_API FFFResult FFF3FP_MeasureTimedTextWidth(const char* textUtf8,
    const char* fontFamilyUtf8, float fontSize, FFF3FPTimedTextFlags flags,
    float* width) noexcept;
FFF3FP_API FFFResult FFF3FP_GetMediaInfo(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API FFFResult FFF3FP_GetLastError(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_Destroy(FFF3FPHandle player) noexcept;

FFF3FP_API FFFResult FFF3FP_OpenBitmapSubtitle(const char* localPathUtf8,
    std::int32_t streamIndex, FFF3FPBitmapSubtitleHandle* decoder) noexcept;
FFF3FP_API FFFResult FFF3FP_ReadBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder,
    FFF3FPBitmapSubtitleFrame* frame) noexcept;
FFF3FP_API FFFResult FFF3FP_CopyBitmapSubtitlePixels(FFF3FPBitmapSubtitleHandle decoder,
    void* output, std::uint32_t outputSize) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder,
    std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_GetBitmapSubtitleLastError(FFF3FPBitmapSubtitleHandle decoder,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_DestroyBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder) noexcept;

// Renders ASS/SSA directly from libass image masks. Font directories are
// separated by LF; every TTF/OTF/TTC is loaded into this renderer's library.
FFF3FP_API FFFResult FFF3FP_OpenAssSubtitle(const char* localPathUtf8,
    const char* fontDirectoriesUtf8, std::int32_t streamIndex,
    FFF3FPAssSubtitleHandle* renderer) noexcept;
FFF3FP_API FFFResult FFF3FP_RenderAssSubtitle(FFF3FPAssSubtitleHandle renderer,
    std::int64_t position100ns, std::int32_t canvasWidth, std::int32_t canvasHeight,
    FFF3FPBitmapSubtitleFrame* frame) noexcept;
FFF3FP_API FFFResult FFF3FP_CopyAssSubtitlePixels(FFF3FPAssSubtitleHandle renderer,
    void* output, std::uint32_t outputSize) noexcept;
FFF3FP_API FFFResult FFF3FP_GetAssSubtitleLastError(FFF3FPAssSubtitleHandle renderer,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_DestroyAssSubtitle(FFF3FPAssSubtitleHandle renderer) noexcept;
