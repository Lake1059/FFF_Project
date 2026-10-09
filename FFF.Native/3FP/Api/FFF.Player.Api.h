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

enum class FFF3FPVideoScalingQuality : std::uint32_t {
    Balanced = 0,
    HighQuality = 1,
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
    ExternalDynamic = 7,
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
    FFF3FPVideoScalingQuality videoScalingQuality;
    std::uint32_t forceHdrOutput;
    // Preferred DXGI adapter index for the D3D11 device.
    // Use -1 (or any negative value) to keep the built-in policy: pick the adapter
    // that drives the monitor containing the output window. The index matches
    // IDXGIFactory1::EnumAdapters1 — the enumeration the managed side must use so
    // both sides agree on "which adapter is #1".
    // Out-of-range or failed enumeration falls back to the built-in policy.
    // NOTE: 0 is a valid adapter index, NOT "unset". A host that leaves this
    // field zeroed therefore pins playback to adapter #0 and silently overrides
    // the monitor match, so every caller must initialise it to -1 explicitly.
    std::int32_t preferredAdapterIndex = -1;
    // Presentation policy for SDR sources on an Advanced Color display.
    //   0 = Never: this policy adds no reason to leave the classic 8/10-bit sRGB
    //       swap chain, which is the pre-existing behaviour. Sources that already
    //       needed the scRGB chain before the field existed - an HDR transfer
    //       function, or wide-gamut primaries - still get it; switching to Never
    //       does not demote them.
    //   1 = Auto: additionally route a plain SDR source through the 16-bit scRGB
    //       swap chain when its bit depth exceeds 8. Without this, a 10-bit SDR
    //       source sits on a gamma R10G10B10A2 chain that DWM resamples as an
    //       ordinary SDR window, so the extra precision is rewritten away.
    // The display gate (OutputSupportsHdr) still applies in both modes: on a
    // non-Advanced Color display the SDR chain is used unchanged.
    std::uint32_t sdrScRgbMode = 0;
    // Adaptive downscale-before-upload. Software-decoded frames cross PCIe as whole
    // planes -- an 8K P010 frame is ~100 MB -- which caps playback well below the
    // source rate on a bus that cannot carry it. When the session is software decoding
    // AND the renderer is dropping a large share of frames, resampling the frame down
    // to the size actually on screen first cuts that transfer by roughly the square of
    // the ratio, for about a millisecond of CPU work.
    //
    // Off by default. It never engages for hardware decoding (those frames are already
    // GPU-resident, so there is nothing to save) or for still images.
    //   0 = Disabled (default)
    //   1 = Enabled: engage automatically once drops are sustained
    std::uint32_t adaptiveDownscaleBeforeUpload = 0;
    // Sustained drop ratio, in percent, that engages the adaptive downscale. Measured
    // over a rolling window so one hiccup cannot trigger it. A lower value engages
    // sooner, at the cost of resampling even when the pipeline was nearly keeping up.
    std::uint32_t adaptiveDownscaleDropPercent = 20;
    // Adaptive software-decoder thread count. A fixed count is wrong in both
    // directions. Measured here, the decode optimum lands around 6-12 workers
    // depending on codec and resolution (8K HEVC 6, 8K AV1 8, 4K AV1 32), and going
    // past it actively hurts: 8K HEVC at 32 threads is 2.14x slower than at 6, because
    // those frames are memory-bandwidth bound and extra workers only contend. Equally,
    // an easy source does not need the workers at all, so starting low and growing
    // only under pressure keeps the CPU free when playback is comfortable.
    //
    // Bounded to [minSoftwareDecoderThreads, maxSoftwareDecoderThreads], and never
    // applied to hardware decoding.
    //   0 = Disabled: one fixed count chosen at open time (the previous behaviour)
    //   1 = Enabled: adapt within the bounds below
    std::uint32_t adaptiveDecoderThreads = 0;
    // Ladder bounds. Clamped to [1, 64]; ffmpeg caps the usable count itself.
    std::uint32_t minSoftwareDecoderThreads = 4;
    std::uint32_t maxSoftwareDecoderThreads = 32;
    // Sustained drop ratio, in percent, meaning "decoding cannot keep up", which grows
    // the ladder; below the shrink threshold it shrinks again. Same rolling-window idea
    // as the adaptive downscale but with separate thresholds, so the two policies
    // cannot drive each other into oscillation.
    std::uint32_t decoderGrowDropPercent = 10;
    std::uint32_t decoderShrinkDropPercent = 2;
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
    // IAMF immersive audio. Appended fields, so the struct stays layout-compatible
    // for hosts built against the previous size (they pass .size and the kernel
    // only writes what fits). iamfActive is set when the AOM reference decoder is
    // rendering the track; iamfChannels is then the real output channel count
    // (e.g. 12 for 7.1.4) rather than one substream's share.
    std::uint32_t iamfActive;
    std::uint32_t iamfChannels;
    std::int32_t iamfSoundSystem;
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
    // 8 = BGRA8, 10 = RGB10A2, 16 = R16G16B16A16_FLOAT scRGB.
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
    // Time spent copying decoded planes into the shader-visible textures, in the
    // same 100 ns base as the other diagnostics. Strictly appended at the end:
    // putting a new field earlier would shift every field after it, and hosts
    // (and our own probes) read the struct by fixed offset. The upload path had no
    // metric at all before this, so upload changes could not be measured.
    std::uint64_t videoUpload100ns;
    // IAMF: channels the *content* declares, which is not the same as iamfChannels.
    // The render target is always 7.1.4 (12), and when the content is smaller libiamf
    // fills the speakers it does not have with digital silence rather than upmixing --
    // verified against its own iamfdec: a 7.1 file rendered at -s5 still produces 12
    // channels, with exactly four of them at peak -inf. So iamfChannels answers "what are
    // we rendering into" while this answers "what is actually in the file", and a UI that
    // showed only the former would label a 7.1 track as 7.1.4 even though four speakers
    // will stay silent. 0 when libiamf reports no per-element detail.
    // Appended at the end for the same offset reason as videoUpload100ns above.
    std::uint32_t iamfContentChannels;
};

struct FFF3FPAudioPeakLevels {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t channelCount;
    std::uint32_t inputChannelCount;
    float values[8];
    float inputValues[8];
};

using FFF3FPHandle = void*;
using FFF3FPBitmapSubtitleHandle = void*;
using FFF3FPAssSubtitleHandle = void*;

// Process-wide native log sink. The kernel calls the installed callback with UTF-8 log
// lines from any of its threads (decode / present / recovery); route them wherever the
// host keeps its logs. Install once at startup, before creating sessions.
// Passing nullptr detaches the sink, but detaching is not synchronous with respect to a
// line already in flight: a host whose callback is a managed delegate must keep that
// delegate reachable until the kernel's threads have stopped. Releasing it right after
// detaching can otherwise deliver a call into a torn-down runtime (CLR fatal, exit 127).
// The type is noexcept because the kernel calls it from its decode/present threads and has
// no way to handle anything coming back through it: a host whose callback is managed code
// must catch its own failures, otherwise an exception escaping here terminates the process.
using FFF3FPLogCallback = void(__cdecl*)(void* context, const char* utf8Line) noexcept;

enum class FFF3FPBitmapSubtitleFlags : std::uint32_t {
    None = 0,
    Clear = 1,
    EndOfStream = 2,
    Forced = 4,
    MoreData = 8,
    Unchanged = 16,
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
    // shadowOffsetX/Y carry the Gaussian standard deviation when this flag is set.
    SoftShadow = 32,
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
// without judging screenshots by eye. HDR/scRGB outputs are linear values and
// may exceed 1.0 (1.0 represents 80 nits).
struct FFF3FPColorTransform {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPColorMode colorMode;
    FFF3FPColorTransfer transfer;
    // Gamut switch: 0 = Rec.709, 1 = Rec.2020, 2 = P3 (DCI/Display).
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

// NVIDIA RTX Video Super Resolution (driver-level D3D11 video processor
// extension). This is a switch, NOT a default: Off unless the host asks for it.
//
// Scope and safety: VSR runs an AI model, so the presented pixels stop being a
// reproducible function of the decoded frame. Hosts that compare frames
// pixel-for-pixel must leave it Off, which is also why the renderer refuses to
// enable it while a comparison-style mode is active.
enum class FFF3FPVideoSuperResolution : std::uint32_t {
    // Default. The shader scaling path is used exactly as before.
    Off = 0,
    // Attempt VSR whenever the source satisfies the driver's conditions
    // (upscaling, source height 360p..1440p, NVIDIA adapter).
    Auto = 1,
};

// Why the last presented frame did or did not use VSR. Reported so a host can
// explain the state without guessing; 0 means "used" when active is 1.
enum class FFF3FPVideoSuperResolutionReason : std::uint32_t {
    None = 0,
    NotRequested = 1,
    NotNvidiaAdapter = 2,
    NotUpscaling = 3,
    SourceResolutionOutOfRange = 4,
    UnsupportedSourceFormat = 5,
    DriverRejected = 6,
};

struct FFF3FPVideoSuperResolutionStatus {
    std::uint32_t size;
    std::uint32_t version; // == 1
    FFF3FPVideoSuperResolution requested;
    // The adapter backing the renderer's D3D11 device is NVIDIA.
    std::uint32_t nvidiaAdapter;
    // VSR was requested and VideoProcessorBlt succeeded on the last frame.
    // IMPORTANT: this means "the driver accepted the request and the blit did
    // not fail" -- NOT a guarantee that the image was perceptibly enhanced.
    // The extension has no reliable capability query, so an accepted request is
    // the strongest signal available (NVIDIA's own status indicator exists for
    // the same reason).
    std::uint32_t active;
    // The driver rejected the request or the blit failed; VSR is latched off
    // for the lifetime of this device.
    std::uint32_t driverRejected;
    FFF3FPVideoSuperResolutionReason reason;
    std::uint32_t sourceWidth;
    std::uint32_t sourceHeight;
    std::uint32_t targetWidth;
    std::uint32_t targetHeight;
};

#ifdef FFFNATIVE_EXPORTS
#define FFF3FP_API extern "C" __declspec(dllexport)
#else
#define FFF3FP_API extern "C" __declspec(dllimport)
#endif

FFF3FP_API std::uint32_t FFF3FP_GetApiVersion() noexcept;
FFF3FP_API std::int32_t FFF3FP_GetColorExtensionStatus() noexcept;
FFF3FP_API const char* FFF3FP_GetColorExtensionStatusText(std::uint32_t state, std::uint32_t variant) noexcept;
FFF3FP_API void FFF3FP_SetColorExtensionAuthorizationPrompt(int (__cdecl* callback)(char*, std::uint32_t)) noexcept;
FFF3FP_API FFFResult FFF3FP_AuthenticateColorExtension(const char* codeUtf8) noexcept;
// Install the process-wide native log sink.
FFF3FP_API void FFF3FP_SetLogCallback(FFF3FPLogCallback callback, void* context) noexcept;
FFF3FP_API FFFResult FFF3FP_Create(const FFF3FPConfiguration* configuration,
    FFF3FPHandle* player) noexcept;
FFF3FP_API FFFResult FFF3FP_Open(FFF3FPHandle player, const char* localPathUtf8) noexcept;
FFF3FP_API FFFResult FFF3FP_DiscNavigate(FFF3FPHandle player, int command, int value, int y) noexcept;
FFF3FP_API FFFResult FFF3FP_CopySdrFrame(FFF3FPHandle player, void* pixels, std::uint32_t capacity,
    std::uint32_t* width, std::uint32_t* height, std::uint32_t discOnly) noexcept;
FFF3FP_API FFFResult FFF3FP_GetDiscStatus(FFF3FPHandle player, char* output, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
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
    float sdrPeakNits, float hdrPeakNits, float sdrPaperWhiteNits,
    std::uint32_t forceHdrOutput) noexcept;
// Present pacing (VRR / G-SYNC / FreeSync low-latency path):
// enableTearing = 1 selects Present(0, DXGI_PRESENT_ALLOW_TEARING) so a VRR display
// can scan out on its own schedule; 0 keeps the default vsync-locked Present(1, 0).
// Swap chains are created with the ALLOW_TEARING capability flag whenever the OS
// reports support, so toggling does not require chain recreation. The request is a
// preference: it is remembered across device/chain re-creation, and where the chain
// was created without the ALLOW_TEARING flag (the OS reports no support) the renderer
// simply keeps the vsync-locked path. Only enableTearing > 1 is rejected
// (InvalidArgument); a successful call does not by itself prove tearing will happen.
FFF3FP_API FFFResult FFF3FP_SetPresentConfig(FFF3FPHandle player,
    std::uint32_t enableTearing) noexcept;
FFF3FP_API FFFResult FFF3FP_SetOutputWindow(FFF3FPHandle player, void* outputWindow) noexcept;
FFF3FP_API FFFResult FFF3FP_SetInteractiveMove(FFF3FPHandle player, std::uint32_t enabled) noexcept;
// View transform for frame inspection: zoom scales the fitted video box
// (1.0 = fit, >1 = magnify), panX/panY are normalized offsets in [-1,1]
// relative to the unzoomed box.
//
// pan is positive = the picture moves right/down, so it follows the pointer: a drag to
// the right should be sent as increasing panX, and the image travels with the cursor.
// That is the convention scroll views use, and what lakeUI's PixelPictureBox does
// (_scrollX = start - dx). This sign was inverted here until it was measured; a host
// written against the old behaviour should negate its pan values.
FFF3FP_API FFFResult FFF3FP_SetViewTransform(FFF3FPHandle player,
    float zoom, float panX, float panY) noexcept;
// Cursor-anchored zoom. Multiplies the current zoom by `factor` (use 1.25 to step in and
// 0.8 to step out, matching lakeUI's PixelPictureBox) while keeping whatever is under the
// given point fixed. anchorX/anchorY are normalised to [0,1] across the client area, so a
// host can pass the wheel position directly without knowing the fit geometry.
// Appended export: hosts that never call it are unaffected.
FFF3FP_API FFFResult FFF3FP_ZoomViewAt(FFF3FPHandle player,
    float factor, float anchorX, float anchorY, float* resultingZoom) noexcept;
// enable == 0 keeps the behavior above. enable == 1 caps the fitted box
// at the source's native size, which turns zoom into an absolute screen:video pixel
// ratio (zoom 1 = pixel-exact 1:1 instead of fit-to-window, and larger windows show
// the whole frame at native size with letterboxing). Returns NotSupported while a
// disc is open, because that renderer owns its own geometry.
FFF3FP_API FFFResult FFF3FP_SetFitLimitToNative(FFF3FPHandle player,
    std::uint32_t enable) noexcept;
// Equirectangular 360-degree video projection. yaw/pitch/fovY use degrees;
// pitch is clamped by the renderer so the horizon never rolls or flips.
FFF3FP_API FFFResult FFF3FP_Set360View(FFF3FPHandle player,
    std::uint32_t enabled, float yaw, float pitch, float fovY) noexcept;
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
// Batch pixel readback. Samples a rectangular region
// of the presented frame in ONE staging copy + Map instead of one GPU round
// trip per pixel. dst receives w*h RGBA32F samples (row-major, premultiplied
// order R,G,B,A), normalized exactly like FFF3FPVideoPixelProbe fields.
// Returns the actual output bit depth via outputBitDepth.
FFF3FP_API FFFResult FFF3FP_ReadVideoPixelRegion(FFF3FPHandle player,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    float* dst, std::uint32_t dstFloatCount,
    std::uint32_t* outputBitDepth) noexcept;
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

// Image information for picture viewing. Fields the source does not carry are
// reported as "unknown" (negative, or 0xFFFFFFFF for rotation) rather than 0,
// so callers must handle unknown explicitly instead of reading a default.
struct FFF3FPImageInfo {
    std::uint32_t size;                  // sizeof(FFF3FPImageInfo)
    std::uint32_t version;               // 1
    std::int32_t  frameCount;            // total frames; <0 unknown
    std::int32_t  loopCount;             // 0 = infinite, -1 = not looping, >0 = count; <0 unknown
    std::uint32_t flags;                 // FFF3FP_IMAGE_FLAG_*
    std::uint32_t rotationQuarterTurns;  // 0..3 from EXIF orientation; 0xFFFFFFFF unknown
    std::int32_t  sourcePixelFormat;     // AVPixelFormat as reported by FFmpeg
    std::uint32_t sourceBitDepth;
    std::uint32_t iccProfileSizeBytes;   // 0 when absent
    std::int32_t  colorPrimaries;        // AVColorPrimaries; <0 unknown
    std::int32_t  colorSpace;            // AVColorSpace; <0 unknown
    std::int32_t  colorTransfer;         // AVColorTransfer; <0 unknown
    std::uint32_t reserved[4];
};

#define FFF3FP_IMAGE_FLAG_STATIC       0x1u
#define FFF3FP_IMAGE_FLAG_ANIMATED     0x2u
#define FFF3FP_IMAGE_FLAG_MULTI_FRAME  0x4u
#define FFF3FP_IMAGE_FLAG_HAS_ALPHA    0x8u
#define FFF3FP_IMAGE_FLAG_HAS_ICC      0x10u
#define FFF3FP_IMAGE_FLAG_HAS_ROTATION 0x20u
// Source primaries wider than Rec.709 (BT.2020, DCI-P3, Display P3). Displaying
// these without clipping requires the scRGB (HDR) output path; an SDR swap
// chain cannot hold colours outside Rec.709.
#define FFF3FP_IMAGE_FLAG_WIDE_GAMUT   0x40u

// Report what a still or animated image actually contains: frame count, EXIF
// rotation, pixel format / bit depth and whether an ICC profile is embedded.
// Returns NotSupported on a native build predating this entry point, so hosts
// degrade instead of failing.
FFF3FP_API FFFResult FFF3FP_GetImageInfo(FFF3FPHandle player, FFF3FPImageInfo* info) noexcept;
// Probes the subtitle streams of a container file without opening a playback
// session. The JSON output reuses the stream shape of FFF3FP_GetMediaInfo and
// adds a top-level startTimeSeconds (containers may carry a nonzero start
// time; a standalone .mks has no reference stream to normalize against).
// Success with an empty streams array means the file opened but has no
// subtitle stream.
FFF3FP_API FFFResult FFF3FP_ProbeSubtitleStreams(const char* localPathUtf8,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API FFFResult FFF3FP_GetLastError(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// Render-target diagnostics. RenderTargetInfo reports the
// current swapchain/client/destination sizes (for App-side overlay positioning
// and pixel-probe coordinate mapping).
// Returns InvalidState when no swapchain exists yet (e.g. before the first
// frame is presented); the contents of *info are then unspecified and must not
// be interpreted as a 0x0 target.
struct FFF3FPRenderTargetInfo {
    std::uint32_t size;
    std::uint32_t version; // == 2
    std::uint32_t swapWidth;
    std::uint32_t swapHeight;
    std::uint32_t clientWidth;
    std::uint32_t clientHeight;
    std::int32_t destX;
    std::int32_t destY;
    std::uint32_t destWidth;
    std::uint32_t destHeight;
    std::uint32_t outputBitDepth;
    std::uint32_t hdr;
};
FFF3FP_API FFFResult FFF3FP_GetRenderTargetInfo(FFF3FPHandle player,
    FFF3FPRenderTargetInfo* info) noexcept;
// Re-present the last cached frame on the presenter thread.
// The host calls it after a child HWND resize so flips keep issuing while
// ResizeBuffers stays on the presenter.
FFF3FP_API FFFResult FFF3FP_Redraw(FFF3FPHandle player) noexcept;
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

// --- RTX Video Super Resolution (opt-in switch; see the enum above) ---
//
// These are appended AFTER the v16 surface and deliberately do not change
// FFF3FPConfiguration or PlayerApiVersion: FFF3FP_Create rejects a version
// mismatch outright, so bumping it would break every existing host (the 3FC
// shell included) until it is rebuilt in lockstep. Hosts are expected to
// resolve these entry points dynamically (GetProcAddress) and degrade when
// they are absent, which is how a single host can drive both a pre-VSR kernel
// and this one.
//
// Takes effect from the next presented frame; safe to call during playback.
// Returns InvalidArgument for an out-of-range enum value, and InvalidState
// when the host asks to enable VSR while a comparison-style mode is active.
FFF3FP_API FFFResult FFF3FP_SetVideoSuperResolution(FFF3FPHandle player,
    FFF3FPVideoSuperResolution mode) noexcept;
FFF3FP_API FFFResult FFF3FP_GetVideoSuperResolutionStatus(FFF3FPHandle player,
    FFF3FPVideoSuperResolutionStatus* status) noexcept;

// --- Frame readback for screenshots ---
//
// Appended after the v16 surface like the VSR and rotation exports, so
// PlayerApiVersion is unchanged and an existing host keeps working. Resolve
// dynamically (GetProcAddress) and degrade when absent.
//
// Why this exists: the host's old screenshot path captured the *screen*
// (Graphics.CopyFromScreen). That fails whenever the window is occluded or
// minimised, and an HDR frame comes back as DWM's SDR-mapped 8-bit result. This
// renders off-screen instead, so it is independent of window visibility and can
// carry an HDR / wide-gamut frame at full precision.
enum class FFF3FPCopyFrameLayout : std::uint32_t {
    // Source resolution, honouring the view rotation (a rotated photo comes out
    // portrait). This is the "original picture" a user expects from a screenshot.
    Source = 0,
    // The current swap-chain size: what the window is showing right now.
    Window = 1,
};

enum class FFF3FPCopyFrameFormat : std::uint32_t {
    // 32bpp BGRA, 4 bytes per pixel. SDR code values.
    Bgra8 = 0,
    // 16-bit half-float RGBA, 8 bytes per pixel, in the linear scRGB contract
    // (1.0 = 80 nits) -- i.e. the same values the HDR swap chain receives. A host
    // that writes this to a file must map it to PQ/HLG itself; writing the raw
    // numbers as if they were sRGB will look wrong.
    Rgba16Float = 1,
};

// Two-call contract, like FFF3FP_CopyBitmapSubtitlePixels: pass pixels = null to
// learn *width/*height, then call again with a buffer of width*height*bpp bytes.
// Capacity is measured in bytes. Returns BufferTooSmall when capacity is short,
// and writes the required size into *width/*height in both cases.
FFF3FP_API FFFResult FFF3FP_CopyFrame(FFF3FPHandle player, void* pixels,
    std::uint32_t capacity, std::uint32_t* width, std::uint32_t* height,
    FFF3FPCopyFrameLayout layout, FFF3FPCopyFrameFormat format) noexcept;
// Bit depth of the most recent FFF3FP_CopyFrame result: 8 for Bgra8, 16 for
// Rgba16Float. Lets a host label the file it just wrote without re-deriving it.
FFF3FP_API FFFResult FFF3FP_GetLastCopyFrameBitDepth(FFF3FPHandle player,
    std::uint32_t* bitDepth) noexcept;

// --- View rotation (quarter turns clockwise, 0..3) ---
//
// Appended after the v16 surface, like the VSR entry points above, so
// FFF3FPConfiguration and PlayerApiVersion stay untouched and an existing host
// keeps working without a rebuild. Resolve dynamically (GetProcAddress) and
// degrade when absent.
//
// Rotation is a *view* property: it does not re-decode and does not touch the
// pixel format. The renderer rotates the sampling coordinate and swaps the fit
// box's axes, so a portrait photo fills a portrait-shaped box instead of being
// letterboxed. Values above 3 are InvalidArgument. Disc playback has its own
// geometry path and returns Success without changing anything.
FFF3FP_API FFFResult FFF3FP_SetViewRotation(FFF3FPHandle player,
    std::uint32_t quarterTurnsClockwise) noexcept;
// Reads back the current rotation; useful for hosts that cycle R / Shift+R and
// need to keep their UI in step after a device-loss reset.
FFF3FP_API FFFResult FFF3FP_GetViewRotation(FFF3FPHandle player,
    std::uint32_t* quarterTurnsClockwise) noexcept;
