#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Hdr/HdrProcessor.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct AVFrame;
struct AVCodecParameters;
struct AVBufferRef;
struct SwsContext;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11VideoDevice;
struct ID3D11VideoContext;
struct ID3D11VideoProcessorEnumerator;
struct ID3D11VideoProcessor;
struct ID3D11BlendState;
struct ID3D11Query;
struct IDXGISwapChain4;
struct ID2D1Factory1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;
struct ID2D1Effect;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextLayout;
struct IDWriteRenderingParams;

struct TimedTextRenderCommand {
    FFF3FPTimedTextCommandType type = FFF3FPTimedTextCommandType::Text;
    FFF3FPTimedTextFlags flags = FFF3FPTimedTextFlags::None;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    std::uint32_t foregroundArgb = 0;
    std::uint32_t outlineArgb = 0;
    float fontSize = 0;
    float outlineWidth = 0;
    std::uint32_t shadowArgb = 0;
    float shadowOffsetX = 0;
    float shadowOffsetY = 0;
    FFF3FPTimedTextAlignment horizontalAlignment = FFF3FPTimedTextAlignment::Near;
    FFF3FPTimedTextAlignment verticalAlignment = FFF3FPTimedTextAlignment::Near;
    struct TextContent {
        std::uint64_t identity = 0;
        std::wstring text;
        std::wstring fontFamily;
    };
    std::shared_ptr<const TextContent> content;
    std::vector<std::uint8_t> bitmap;
    std::uint32_t bitmapWidth = 0;
    std::uint32_t bitmapHeight = 0;
    std::uint32_t bitmapStride = 0;
    std::uint64_t contentId = 0;
};

struct TimedTextRenderLayer {
    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    std::uint64_t sequence = 0;
    float targetFrameRate = 60.0f;
    float coverBackdropBlurRadius = 30.0f;
    std::uint32_t coverBackdropBlurPasses = 3;
    std::uint32_t coverBackdropDownsampleFactor = 4;
    std::uint32_t coverBackdropTintArgb = 0x78000000u;
    float coverRegionWidthPercentage = 50.0f;
    float lyricsRegionWidthPercentage = 50.0f;
    float coverLeftPaddingPercentage = 7.5f;
    float coverRightPaddingPercentage = 0.0f;
    float coverVerticalPaddingPercentage = 7.5f;
    std::vector<TimedTextRenderCommand> commands;
};

enum class TimedTextLayerSlot : std::uint32_t {
    Subtitle = 0,
    Danmaku = 1,
    PlayerInformation = 2,
    Lyrics = 3,
    Disc = 4,
};

FFFResult EvaluateVideoColorTransform(FFF3FPColorTransform& transform) noexcept;
FFFResult EvaluateTimedTextRasterization(FFF3FPTimedTextRasterizationProbe& probe) noexcept;
FFFResult MeasureTimedText(const char* textUtf8, const char* fontFamilyUtf8,
    float fontSize, FFF3FPTimedTextFlags flags, float maxWidth, float outlineWidth,
    float shadowOffsetX, float shadowOffsetY, bool shadowEnabled,
    FFF3FPTimedTextMeasurement& measurement) noexcept;
FFFResult MeasureTimedTextWidth(const char* textUtf8, const char* fontFamilyUtf8,
    float fontSize, FFF3FPTimedTextFlags flags, float& width) noexcept;

class PlayerVideoRenderer final {
public:
    explicit PlayerVideoRenderer(std::function<void()> recoveryCallback = {}) noexcept;
    ~PlayerVideoRenderer();

    FFFResult SetWindow(HWND window) noexcept;
    // Choose which DXGI adapter creates the D3D11 device.
    // index = -1 (default) keeps the built-in policy: the adapter driving the monitor
    // that contains the output window. index >= 0 is used as the argument of
    // IDXGIFactory1::EnumAdapters1. Takes effect on the next device creation
    // (EnsureDevice), including device-loss recovery.
    // Out-of-range / non-enumerable indices silently fall back to the built-in policy.
    FFFResult SetPreferredAdapterIndex(std::int32_t index) noexcept;
    void SetDiscAspect(double aspect) noexcept { discAspect_.store(static_cast<float>(aspect)); }
    void SetInteractiveMove(bool enabled) noexcept;
    FFFResult SetScalingQuality(FFF3FPVideoScalingQuality quality) noexcept;
    // Adaptive downscale-before-upload: opt-in, software-decode only, and engaged
    // only while frames are being dropped. See the API field for the rationale.
    void ConfigureAdaptiveDownscale(bool enabled, std::uint32_t dropPercent) noexcept;
    void ObserveFrameForAdaptiveDownscale(bool dropped) noexcept;
    // Presentation policy for SDR sources, see FFF3FPConfiguration::sdrScRgbMode.
    //   0 = Never, 1 = Auto (route SDR sources the SDR chain cannot carry through
    //   the 16-bit scRGB chain when the display runs Advanced Color).
    FFFResult SetSdrScRgbMode(std::uint32_t mode) noexcept;
    FFFResult SetViewTransform(float zoom, float panX, float panY) noexcept;
    // Creates the D3D11 device ahead of the first frame. Measured at 195-245 ms on this
    // machine, and inside the first Open it is the whole of the delay a user sees before
    // the first picture appears. Doing it while the host is still setting up moves that
    // cost off the critical path. Safe to call at any time; a no-op once the device
    // exists, and failure is not fatal because Open still creates it on demand.
    FFFResult WarmDevice() noexcept;
    // Multiplies the zoom by `factor` while keeping the viewport point
    // (anchorX, anchorY) -- both normalised to [0,1] over the client area -- under the
    // cursor. Hosts that implement wheel zoom themselves have to redo the fit and pan
    // arithmetic and usually drift; this keeps the anchor exact.
    FFFResult ZoomViewAt(float factor, float anchorX, float anchorY,
        float* resultingZoom = nullptr) noexcept;
    // Current zoom and pan, so a host that zooms through ZoomViewAt can keep its cached
    // values in step without a second query API.
    void ViewTransform(float& zoom, float& panX, float& panY) const noexcept;
    /// View rotation in quarter turns clockwise (0..3), applied on top of the
    /// view transform. Kept as its own setter rather than folded into
    /// SetViewTransform so existing callers keep their signature.
    FFFResult SetViewRotation(std::uint32_t quarterTurnsClockwise) noexcept;
    std::uint32_t ViewRotation() const noexcept;
    // Cap the fit box at the source's native size (PlayerApi exports
    // FFF3FP_SetFitLimitToNative). While enabled, zoom is the screen:video pixel
    // ratio, so zoom == 1 renders the source 1:1 instead of fitted to the window.
    FFFResult SetFitLimitToNative(bool enable) noexcept;
    // Presentation policy toggle (PlayerApi exports FFF3FP_SetPresentConfig).
    FFFResult SetPresentConfig(bool enableTearing) noexcept;
    // Render-target diagnostics for the managed API surface
    // (PlayerApi exports FFF3FP_GetRenderTargetInfo). Reports the current
    // swap-chain / client / video-destination sizes so the App can position
    // overlays and map pixel-probe coordinates without guessing.
    struct RenderTargetInfo {
        std::uint32_t swapWidth = 0;
        std::uint32_t swapHeight = 0;
        std::uint32_t clientWidth = 0;
        std::uint32_t clientHeight = 0;
        // Signed origin: panning a magnified picture pushes the box's left/top edge
        // outside the back buffer, so negative values are legitimate.
        std::int32_t destX = 0;
        std::int32_t destY = 0;
        std::uint32_t destWidth = 0;
        std::uint32_t destHeight = 0;
        std::uint32_t outputBitDepth = 0;
        bool hdr = false;
    };
    FFFResult GetRenderTargetInfo(RenderTargetInfo& info) noexcept;
    FFFResult Set360View(bool enabled, float yaw, float pitch, float fovY) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits, bool forceHdrOutput = false) noexcept;
    FFFResult ForceSdrOutputForSdrSource() noexcept;
    void ConfigureHdrStream(const AVCodecParameters* parameters) noexcept;
    // True when the source wants the scRGB presentation path: HDR transfer or
    // widened primaries always do; a plain SDR source does so under the Auto
    // SDR policy (sdrScRgbMode_ == 1) when its bit depth exceeds 8.
    bool WantsScRgbPresentationPath(std::uint32_t bitDepth) const noexcept;
    // Paper white actually used for the picture: the Windows SDR content
    // brightness when an SDR source is presented on the scRGB chain (so it lands
    // on the luminance DWM would have given the classic SDR chain), otherwise the
    // configured value.
    float EffectivePaperWhiteNits() const noexcept;
    // `imageMode` marks a still image (not an animated one, which plays as video).
    // Images are judged differently from video: upscaling a picture is expected to
    // preserve the source pixel grid so it reads as a crisp enlargement, whereas
    // video upscaling wants a smooth reconstruction kernel. Passing it here keeps
    // the two policies in one place instead of guessing from the frame.
    FFFResult Render(const AVFrame* frame, bool limitToNativeSize = false,
        bool coverArt = false, bool prepareOnly = false, bool imageMode = false) noexcept;
    FFFResult Redraw() noexcept;
    FFFResult CreateD3D11HardwareDeviceContext(AVBufferRef** context) noexcept;
    FFFResult PresentTimedText() noexcept;
    FFFResult ReadPixel(FFF3FPVideoPixelProbe& probe) noexcept;
    // Batch pixel readback (single GPU staging copy).
    FFFResult ReadPixelRegion(std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height, float* dst,
        std::uint32_t dstFloatCount, std::uint32_t* outputBitDepth) noexcept;
    FFFResult CopySdrFrame(void* pixels, std::uint32_t capacity, std::uint32_t& width,
        std::uint32_t& height, bool discOnly) noexcept;
    /// Frame readback for screenshots. Unlike CopySdrFrame (which is pinned to the
    /// swap-chain size and to BGRA8) this renders off-screen, so it can produce the
    /// source resolution and can carry an HDR / wide-gamut frame in 16-bit scRGB.
    ///
    /// `layout` 0 = source resolution (aspect preserved, rotation applied),
    ///          1 = the current swap-chain size (what the window shows).
    /// `format` 0 = BGRA8 (SDR), 1 = RGBA16F half floats (linear scRGB, HDR).
    ///
    /// Rendering goes to a private texture, never to the live back buffer: taking a
    /// screenshot must not disturb the frame on screen.
    FFFResult CopyFrame(void* pixels, std::uint32_t capacity, std::uint32_t& width,
        std::uint32_t& height, std::uint32_t layout, std::uint32_t format) noexcept;
    /// Bit depth of the frame produced by the most recent CopyFrame (8 or 16).
    std::uint32_t FinalReadbackBitDepth() const noexcept { return finalReadbackFormat_; }
    FFFResult SetTimedTextLayer(TimedTextRenderLayer layer, TimedTextLayerSlot slot) noexcept;
    FFFResult GetTimedTextStatus(FFF3FPTimedTextStatus& status, TimedTextLayerSlot slot) noexcept;
    bool DeviceRecoveryRequested() const noexcept;
    bool RequestRecoveryIfDeviceLost() noexcept;
    FFFResult RecreateDeviceResources() noexcept;
    void ResetMedia() noexcept;
    void Close() noexcept;

    FFF3FPColorMode ActualColorMode() const noexcept;
    /// True when the current stream's primaries are wider than Rec.709
    /// (BT.2020, DCI-P3, Display P3). Such sources need the scRGB output path
    /// even when their transfer function is SDR, otherwise the extra colours
    /// are clipped away.
    bool IsWideGamutSource() const noexcept;
    float SourcePeakNits() const noexcept;
    HdrFrameState HdrState() const noexcept;
    std::uint64_t PresentedVideoFrames() const noexcept;
    std::uint64_t CoalescedVideoFrames() const noexcept;
    std::uint64_t SwapChainPresents() const noexcept;
    std::uint64_t SubmittedVideoGeneration() const noexcept;
    std::uint64_t PresentedVideoGeneration() const noexcept;
    bool HasPendingVideoPresentation() const noexcept;
    bool HasOutputWindow() const noexcept;
    std::uint64_t PresentWait100ns() const noexcept;
    std::uint64_t DeviceLockWait100ns() const noexcept;
    std::uint64_t SoftwareConvert100ns() const noexcept;
    std::uint64_t Upload100ns() const noexcept;
    std::uint32_t OutputBitDepth() const noexcept;
    FFF3FPVideoScalingMode ActualVideoScalingMode() const noexcept;
    /// RTX Video Super Resolution switch and diagnostics. See the API header
    /// for why `active` means "the driver accepted the request", not "the
    /// image is provably sharper".
    FFFResult SetVideoSuperResolution(FFF3FPVideoSuperResolution mode) noexcept;
    FFF3FPVideoSuperResolution RequestedVideoSuperResolution() const noexcept;
    bool VideoSuperResolutionActive() const noexcept;
    void FillVideoSuperResolutionStatus(FFF3FPVideoSuperResolutionStatus& status) const noexcept;
    std::string FallbackReason() const;
    std::string LastError() const;

private:
    enum class CoverBackdropRenderResult {
        Complete,
        Deferred,
        Failed,
    };
    struct TimedTextSprite {
        float atlasX = 0;
        float atlasY = 0;
        float offsetX = 0;
        float offsetY = 0;
        float width = 0;
        float height = 0;
    };
    struct TimedTextSpriteInstance {
        float destination[4]{};
        float uv[4]{};
    };
    struct PendingTimedTextSprite {
        std::size_t commandIndex = 0;
        std::shared_ptr<IDWriteTextLayout> layout;
        std::uint64_t key = 0;
        TimedTextSprite sprite{};
        float outline = 0;
        float shadowX = 0;
        float shadowY = 0;
    };
    struct ScalePassResource {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t axis = 0;
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* target = nullptr;
        ID3D11ShaderResourceView* view = nullptr;
    };
    struct PlaneScaleChain {
        std::uint32_t sourceWidth = 0;
        std::uint32_t sourceHeight = 0;
        std::uint32_t targetWidth = 0;
        std::uint32_t targetHeight = 0;
        std::uint32_t format = 0;
        std::vector<ScalePassResource> passes;
    };

    FFFResult EnsureDevice() noexcept;
    std::uint32_t PreferredOutputBitDepth(std::uint32_t sourceBitDepth, bool hdr) noexcept;
    FFFResult EnsureSwapChain(std::uint32_t width, std::uint32_t height,
        std::uint32_t sourceBitDepth) noexcept;
    // sourceBitDepth is the depth of the frame the caller is about to render, not the
    // member: a scRGB rejection has to pick the classic-chain rung for *that* frame, and
    // the member (sourceBitDepth_) is still 0 until EnsurePipeline sees the first one.
    // Reading the member here silently demotes a 10-bit SDR source to BGRA8 on the very
    // first chain and forces a second reconfigure one frame later.
    FFFResult CreateSwapChain(std::uint32_t width, std::uint32_t height,
        bool hdr, std::uint32_t outputBits, std::uint32_t sourceBitDepth) noexcept;
    FFFResult ReconfigureSwapChain(bool hdr, std::uint32_t outputBits,
        std::uint32_t sourceBitDepth) noexcept;
    FFFResult EnsurePipeline(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
        std::uint32_t inputLayout, std::uint32_t bitDepth,
        std::uint32_t chromaWidthShift, std::uint32_t chromaHeightShift,
        bool externalSource = false) noexcept;
    FFFResult EnsureVideoProcessor(ID3D11Texture2D* inputTexture,
        ID3D11Texture2D* outputTexture, std::uint32_t inputColorSpace,
        std::uint32_t outputColorSpace) noexcept;
    FFFResult EnsureVideoProcessorInputSurface(std::uint32_t format) noexcept;
    FFFResult RenderVideoProcessorInput() noexcept;
    FFFResult DrawWithShader(ID3D11RenderTargetView* target, float x, float y,
        float width, float height, std::uint32_t effect = 0,
        ID3D11ShaderResourceView* const* sourceViews = nullptr) noexcept;
    bool UploadExtensionEnhancement(const AVFrame* frame) noexcept;
    bool ReconstructExtensionEnhancement(std::uint32_t width, std::uint32_t height,
        std::uint32_t layout, float sampleScale) noexcept;
    void ReleaseExtensionEnhancement() noexcept;
    void ReleaseExtensionReconstruction() noexcept;
    // Resamples the decoded frame down to the display size into an internal buffer,
    // returning false when the frame should be uploaded untouched. On true, the
    // caller uploads AdaptivePlanes()/AdaptiveLines() at the requested size.
    bool PrepareAdaptiveDownscale(const AVFrame* frame, std::uint32_t width,
        std::uint32_t height, std::uint32_t outputWidth, std::uint32_t outputHeight) noexcept;
    std::uint8_t* const* AdaptivePlanes() const noexcept { return adaptivePlanes_; }
    const int* AdaptiveLines() const noexcept { return adaptiveLines_; }
    FFFResult PrepareScaledVideo(std::uint32_t outputWidth, std::uint32_t outputHeight,
        ID3D11ShaderResourceView** views) noexcept;
    // Picks the scaling kernel from the horizontal/vertical ratios, honouring the
    // configured quality and whether the source is a still image. Upscaling and
    // downscaling deliberately get different kernels; see the definition.
    std::uint32_t SelectScaleFilter(float scaleX, float scaleY) const noexcept;
    FFFResult EnsurePlaneScaleChain(std::size_t plane, std::uint32_t sourceWidth,
        std::uint32_t sourceHeight, std::uint32_t targetWidth,
        std::uint32_t targetHeight, std::uint32_t format) noexcept;
    FFFResult ExecuteScalePass(ID3D11ShaderResourceView* source,
        std::uint32_t sourceWidth, std::uint32_t sourceHeight,
        const ScalePassResource& pass, std::uint32_t filter) noexcept;
    void ReleaseScaleResources() noexcept;
    FFFResult DrawWithVideoProcessor(ID3D11Texture2D* inputTexture,
        ID3D11Texture2D* outputTexture, const RECT& destination,
        std::uint32_t inputColorSpace, std::uint32_t outputColorSpace) noexcept;
    bool CanUseDirectVideoProcessor() const noexcept;
    // --- RTX Video Super Resolution (P2) ---
    /// Source/adapter preconditions for the VP upscale step, independent of any
    /// colour-mode decision. Deliberately does NOT test transfer function or
    /// output bit depth: VSR is a spatial operation and works on HDR sources too.
    bool CanUseVideoProcessorForUpscale() const noexcept;
    /// CanUseVideoProcessorForUpscale() plus the vendor gate, the "actually
    /// upscaling" requirement and NVIDIA's documented 360p..1440p input range.
    bool ShouldEnableNvidiaSuperResolution(std::uint32_t targetWidth,
        std::uint32_t targetHeight) const noexcept;
    /// Upscale the decoded planes through a video processor with RTX VSR
    /// enabled, filling effectiveSourceViews_ with the result. Returns Success
    /// with videoSuperResolutionActive_ == false when VSR simply does not apply.
    FFFResult ApplySuperResolution(std::uint32_t targetWidth,
        std::uint32_t targetHeight) noexcept;
    /// Build plane SRVs over an NV12/P010 texture, matching EnsurePipeline's
    /// view formats exactly (a mismatch silently shifts colours).
    bool CreateYuvPlaneViews(ID3D11Texture2D* texture, std::uint32_t format,
        ID3D11ShaderResourceView** views) noexcept;
    void ReleaseSuperResolutionResources() noexcept;
    void ReleaseVideoProcessor() noexcept;
    void ReleaseVideoProcessorInputSurface() noexcept;
    FFFResult AcquireBackBufferTarget(ID3D11Texture2D** buffer,
        ID3D11RenderTargetView** target) noexcept;
    /// Cached off-screen surface used by CopyFrame. Kept separate from the swap chain
    /// so a screenshot never touches the buffer being displayed. Recreated only when
    /// the requested size/format changes.
    FFFResult AcquireOffscreenTarget(std::uint32_t width, std::uint32_t height,
        std::uint32_t format, ID3D11Texture2D** texture,
        ID3D11RenderTargetView** target) noexcept;
    void ReleaseOffscreenTarget() noexcept;
    /// Copies `source` into `pixels` as packed BGRA8 or RGBA16F half floats.
    FFFResult ReadbackTexture(ID3D11Texture2D* source, void* pixels,
        std::uint32_t capacity, std::uint32_t width, std::uint32_t height,
        std::uint32_t format) noexcept;
    struct CachedVideoSettings {
        // gamut: 0 = Rec.709, 1 = Rec.2020, 2 = P3 (DCI/Display).
        std::uint32_t colorMode = 0, transfer = 0, gamut = 0, reserved = 0;
        float sdrPeak = 100, hdrPeak = 100, paperWhite = 203, targetPeak = 1000;
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
    FFFResult EnsureTimedTextResources(TimedTextLayerSlot slot) noexcept;
    FFFResult EnsureD2DContext() noexcept;
    FFFResult EnsureTimedTextAtlas(std::uint32_t size) noexcept;
    FFFResult EnsureTimedTextInstanceCapacity(std::size_t count) noexcept;
    FFFResult EnsureCoverBackdropResources() noexcept;
    FFFResult DrawCoverBackdrop(ID3D11RenderTargetView* target) noexcept;
    FFFResult RenderCoverBackdropCache() noexcept;
    CoverBackdropRenderResult TryRenderCoverBackdropCache() noexcept;
    void RequestCoverBackdropRender(bool force = false) noexcept;
    void CoverBackdropThread() noexcept;
    void StopCoverBackdropThread() noexcept;
    void ReleaseCoverBackdropResources() noexcept;
    /// Renders the cached frame into `target`, laying it out for an output surface of
    /// `outputWidth` x `outputHeight`. The size is a parameter rather than always
    /// swapWidth_/swapHeight_ so the same code can also paint an off-screen surface at
    /// the source resolution (screenshot readback) without a second render path.
    FFFResult DrawCachedVideo(ID3D11RenderTargetView* target,
        std::uint32_t outputWidth, std::uint32_t outputHeight) noexcept;
    FFFResult DrawCachedVideo(ID3D11RenderTargetView* target) noexcept {
        return DrawCachedVideo(target, swapWidth_, swapHeight_);
    }
    FFFResult PresentCurrentFrame(IDXGISwapChain4* swapChain,
        std::uint64_t renderedVideoGeneration) noexcept;
    FFFResult DrawTimedText(TimedTextLayerSlot slot) noexcept;
    void TimedTextThread() noexcept;
    void StopTimedTextThread() noexcept;
    void CompositeTimedText(ID3D11RenderTargetView* target, TimedTextLayerSlot slot) noexcept;
    void ReleaseTimedTextSlotResources(TimedTextLayerSlot slot) noexcept;
    void ReleaseTimedTextResources(bool resetRenderedState = true) noexcept;
    bool OutputSupportsHdr() noexcept;
    void SetHdrMetadata() noexcept;
    void ClearSurface() noexcept;
    void ReleaseDeviceObjects() noexcept;
    void RequestDeviceRecovery(long result, const char* operation) noexcept;
    bool RequestRecoveryIfDeviceLostLocked() noexcept;
    void SetError(std::string message) noexcept;

    HWND window_;
    // -1 = auto (adapter driving the window's monitor). See
    // SetPreferredAdapterIndex. Atomic because it is written by
    // SetPreferredAdapterIndex() (which any thread may call) and read by
    // EnsureDevice() on the render path. The device is created lazily and
    // re-created on device loss, so the preference must survive both.
    std::atomic<std::int32_t> preferredAdapterIndex_{ -1 };
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    IDXGISwapChain4* swapChain_;
    ID3D11VertexShader* vertexShader_;
    ID3D11PixelShader* pixelShader_;
    ID3D11PixelShader* sdrPixelShaders_[3]{};
    ID3D11PixelShader* extensionShader_ = nullptr;
    ID3D11Buffer* extensionConstants_ = nullptr;
    ID3D11Texture2D* extensionEnhancementTextures_[3]{};
    ID3D11ShaderResourceView* extensionEnhancementViews_[3]{};
    std::uint32_t extensionEnhancementWidth_ = 0, extensionEnhancementHeight_ = 0;
    bool extensionEnhancementSemiplanar_ = false;
    ID3D11ComputeShader* extensionEnhancementShader_ = nullptr;
    ID3D11Buffer* extensionEnhancementConstants_ = nullptr;
    ID3D11Texture2D* extensionReconstructedTextures_[3]{};
    ID3D11ShaderResourceView* extensionReconstructedViews_[3]{};
    ID3D11UnorderedAccessView* extensionReconstructedOutputs_[3]{};
    std::uint32_t extensionReconstructedWidth_ = 0, extensionReconstructedHeight_ = 0;
    bool extensionReconstructed_ = false;
    std::uint32_t cachedOriginalInputLayout_ = 0;
    float cachedOriginalSampleScale_ = 1;
    bool extensionAttempted_ = false;
    bool extensionEligible_ = false;
    ID3D11PixelShader* coverBackdropPixelShader_;
    ID3D11PixelShader* timedTextPixelShader_;
    ID3D11PixelShader* scalePixelShader_;
    ID3D11SamplerState* sampler_;
    ID3D11SamplerState* pointSampler_;
    ID3D11SamplerState* panoramaSampler_;
    ID3D11Buffer* constants_;
    ID3D11Buffer* scaleConstants_;
    ID3D11Texture2D* sourceTextures_[3];
    ID3D11ShaderResourceView* sourceViews_[3];
    PlaneScaleChain planeScaleChains_[3];
    std::uint64_t scaledVideoGeneration_;
    std::uint32_t scaledOutputWidth_;
    std::uint32_t scaledOutputHeight_;
    // Part of the scaled-video cache key: VSR changes the effective source
    // without advancing the decode generation, so the cached views would
    // otherwise be reused across a VSR state change.
    bool scaledVideoSuperResolution_;
    ID3D11ShaderResourceView* scaledSourceViews_[3];
    // Screenshot readback surface (see AcquireOffscreenTarget). Owned by the
    // renderer; deliberately independent of swapChain_.
    ID3D11Texture2D* offscreenTexture_ = nullptr;
    ID3D11RenderTargetView* offscreenTarget_ = nullptr;
    std::uint32_t offscreenWidth_ = 0;
    std::uint32_t offscreenHeight_ = 0;
    std::uint32_t offscreenFormat_ = 0;
    // Bit depth of the most recent CopyFrame result (8 or 16), so the host can label
    // the image it just received without inferring it from the request.
    std::uint32_t finalReadbackFormat_ = 0;
    ID3D11VideoDevice* videoDevice_;
    ID3D11VideoContext* videoContext_;
    ID3D11VideoProcessorEnumerator* videoProcessorEnumerator_;
    ID3D11VideoProcessor* videoProcessor_;
    ID3D11Texture2D* videoProcessorRenderTexture_;
    ID3D11RenderTargetView* videoProcessorRenderTarget_;
    ID3D11Texture2D* coverBackdropTexture_;
    ID3D11ShaderResourceView* coverBackdropView_;
    ID3D11Texture2D* coverBackdropSourceTexture_;
    ID3D11RenderTargetView* coverBackdropSourceTarget_;
    ID3D11Texture2D* timedTextTextures_[5];
    ID3D11RenderTargetView* timedTextTargets_[5];
    ID3D11ShaderResourceView* timedTextViews_[5];
    ID3D11Query* timedTextPipelineQueries_[5];
    ID3D11BlendState* timedTextBlend_;
    ID3D11Texture2D* timedTextAtlasTexture_;
    ID3D11ShaderResourceView* timedTextAtlasView_;
    bool timedTextResourcesHdr_;
    bool timedTextAtlasHdr_;
    ID3D11VertexShader* timedTextSpriteVertexShader_;
    ID3D11PixelShader* timedTextSpritePixelShader_;
    ID3D11Buffer* timedTextSpriteInstanceBuffer_;
    ID3D11ShaderResourceView* timedTextSpriteInstanceView_;
    ID2D1Factory1* d2dFactory_;
    ID2D1Device* d2dDevice_;
    ID2D1DeviceContext* d2dContext_;
    ID2D1Bitmap1* d2dCoverBackdropSource_;
    ID2D1Bitmap1* d2dCoverBackdropTarget_;
    ID2D1Effect* coverBackdropBlurEffect_;
    ID2D1Bitmap1* d2dTargets_[5];
    ID2D1Bitmap1* d2dAtlasTarget_;
    ID2D1Bitmap1* d2dTimedTextShadowTarget_;
    ID2D1Effect* timedTextShadowBlurEffect_;
    IDWriteFactory* writeFactory_;
    IDWriteRenderingParams* timedTextRenderingParams_;
    SwsContext* scaler_;
    std::uint32_t swapWidth_;
    std::uint32_t swapHeight_;
    bool swapHdr_;
    // Capability of the *current* swap chain: it was created with
    // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (so ResizeBuffers must repeat that flag, and
    // Present may pass DXGI_PRESENT_ALLOW_TEARING). Written on the chain-creation path.
    bool swapAllowTearing_;
    // Host's pacing preference (FFF3FP_SetPresentConfig). Kept apart from the capability
    // above for two reasons: clobbering it would make ResizeBuffers pass a flag the chain
    // was not created with (DXGI_ERROR_INVALID_CALL), and the capability is rewritten by
    // every chain creation, so a merged variable would lose the request on re-creation.
    // Atomic: written on the session worker thread, read on the presenter thread.
    std::atomic<bool> tearingRequested_{ false };
    std::atomic<std::uint32_t> swapOutputBits_;
    // Last drawn video destination rect (diagnostics),
    // recorded by DrawCachedVideo after each successful shader draw.
    // Origin is signed: panning a magnified picture pushes the box's left/top edge
    // outside the back buffer (negative values are legitimate).
    std::atomic<std::int32_t> lastDestX_{ 0 };
    std::atomic<std::int32_t> lastDestY_{ 0 };
    std::atomic<std::uint32_t> lastDestWidth_{ 0 };
    std::atomic<std::uint32_t> lastDestHeight_{ 0 };
    std::uint32_t sourceWidth_;
    std::uint32_t sourceHeight_;
    std::uint32_t sourceInputLayout_;
    std::uint32_t sourceBitDepth_;
    std::uint32_t sourceChromaWidthShift_;
    std::uint32_t sourceChromaHeightShift_;
    bool sourceExternal_;
    bool sourceLimitedToNativeSize_;
    bool sourceCoverArt_;
    // Still-image source. Selects the image scaling policy: pixel-block enlargement
    // for upscaling, and a smoothing kernel for downscaling. Animated images are not
    // affected -- they play as video and keep the video policy.
    bool sourceImageMode_ = false;
    std::uint32_t coverBackdropWidth_;
    std::uint32_t coverBackdropHeight_;
    std::uint64_t coverBackdropVideoGeneration_;
    std::uint64_t coverBackdropAppliedBlurSettingsGeneration_;
    std::uint32_t videoProcessorInputFormat_;
    std::uint32_t videoProcessorOutputFormat_;
    std::uint32_t videoProcessorInputColorSpace_;
    std::uint32_t videoProcessorOutputColorSpace_;
    std::uint32_t videoProcessorInputWidth_;
    std::uint32_t videoProcessorInputHeight_;
    std::uint32_t videoProcessorOutputWidth_;
    std::uint32_t videoProcessorOutputHeight_;
    bool videoProcessorConfigurationFailed_;
    int sourceColorSpace_;
    int sourceChromaLocation_;
    bool sourceFullRange_;
    bool sourceInterlaced_;
    // Primaries wider than Rec.709 (BT.2020, DCI-P3, Display P3). Such sources
    // cannot be represented by an SDR swap chain, so they are allowed onto the
    // scRGB output path even though their transfer function is SDR.
    std::atomic<bool> sourceWideGamut_;
    std::atomic<FFF3FPVideoScalingMode> actualVideoScalingMode_;
    // --- RTX Video Super Resolution (opt-in; default Off) ---
    // The host-visible switch. Off keeps the renderer byte-identical to the
    // pre-VSR behaviour, which is what the "video mode does not change"
    // invariant requires.
    std::atomic<FFF3FPVideoSuperResolution> requestedVideoSuperResolution_{
        FFF3FPVideoSuperResolution::Off };
    // Adapter of the current D3D11 device is NVIDIA (probed once per device).
    bool nvidiaAdapter_{ false };
    // VSR was requested AND the blit succeeded on the last presented frame.
    std::atomic<bool> videoSuperResolutionActive_{ false };
    // The driver rejected the request (or the blit failed). Latched for the
    // lifetime of the device so later frames do not repeat a failing call.
    bool videoSuperResolutionRejected_{ false };
    // Why the last frame did not use VSR, for the status report.
    std::atomic<FFF3FPVideoSuperResolutionReason> videoSuperResolutionReason_{
        FFF3FPVideoSuperResolutionReason::NotRequested };
    std::uint32_t videoSuperResolutionSourceWidth_{ 0 };
    std::uint32_t videoSuperResolutionSourceHeight_{ 0 };
    std::uint32_t videoSuperResolutionTargetWidth_{ 0 };
    std::uint32_t videoSuperResolutionTargetHeight_{ 0 };
    // The upscaled NV12/P010 surface and its per-plane views. Owned here, never
    // swapped into sourceViews_ (that array is EnsurePipeline's long-lived
    // pipeline state and mutating it would corrupt the next frame).
    ID3D11Texture2D* superResolutionTexture_{ nullptr };
    ID3D11ShaderResourceView* superResolutionViews_[3]{};
    // DXGI_FORMAT of the cached super-resolution surface (0 when absent).
    std::uint32_t superResolutionSourceFormat_{ 0 };
    // effectiveSourceViews_ is rebuilt from sourceViews_ on every frame and then
    // optionally redirected at superResolutionViews_. Keeping the two apart is
    // what makes VSR switching safe mid-stream.
    ID3D11ShaderResourceView* effectiveSourceViews_[3]{};
    FFF3FPVideoScalingQuality scalingQuality_;
    // Adaptive downscale-before-upload state. The policy samples a rolling window of
    // frame outcomes rather than reacting to any single drop.
    std::atomic<bool> adaptiveDownscaleEnabled_{false};
    std::atomic<std::uint32_t> adaptiveDownscaleDropPercent_{20};
    std::atomic<bool> adaptiveDownscaleActive_{false};
    std::uint64_t adaptiveWindowFrames_ = 0;
    std::uint64_t adaptiveWindowDrops_ = 0;
    // Separate sws context: the main scaler_ is configured for the colour conversion
    // of full-size frames and must not be retargeted by this policy.
    SwsContext* adaptiveScaler_ = nullptr;
    std::uint32_t adaptiveSourceWidth_ = 0;
    std::uint32_t adaptiveSourceHeight_ = 0;
    std::uint32_t adaptiveTargetWidth_ = 0;
    std::uint32_t adaptiveTargetHeight_ = 0;
    int adaptiveSourceFormat_ = -1;
    std::vector<std::uint8_t> adaptiveBuffer_;
    // Plane pointers into adaptiveBuffer_, filled by PrepareAdaptiveDownscale.
    std::uint8_t* adaptivePlanes_[4]{};
    int adaptiveLines_[4]{};
    FFF3FPColorMode requestedMode_;
    FFF3FPColorMode actualMode_;
    float sdrPeakNits_;
    float hdrPeakNits_;
    float paperWhiteNits_;
    // Windows "SDR content brightness" (AdvancedColorInfo::SdrWhiteLevelInNits),
    // 0 when unreported. See EffectivePaperWhiteNits().
    float sdrWhiteLevelNits_;
    // View transform (zoom + pan) applied when composing the video into the
    // swap chain. Normalized pan in [-1,1] relative to the unzoomed video box.
    std::atomic<float> viewZoomBits_;
    std::atomic<float> viewPanXBits_;
    std::atomic<float> viewPanYBits_;
    // Quarter turns clockwise (0..3). Applied when sampling, so the destination
    // rect only needs its aspect swapped, not a new geometry path.
    std::atomic<std::uint32_t> viewRotation_{ 0 };
    // Opt-in native-size cap for the fit box; see SetFitLimitToNative().
    std::atomic<bool> fitLimitToNative_{ false };
    std::atomic<std::uint32_t> projection360Enabled_;
    std::atomic<bool> view360RedrawPending_;
    std::atomic<float> view360YawBits_;
    std::atomic<float> view360PitchBits_;
    std::atomic<float> view360FovYBits_;
    float sourcePeakNits_;
    HdrProcessor hdrProcessor_;
    std::vector<std::uint8_t> convertedRgb_;
    mutable std::mutex deviceMutex_;
    mutable std::mutex presentMutex_;
    mutable std::mutex timedTextMutex_;
    mutable std::mutex coverBackdropThreadMutex_;
    std::condition_variable timedTextCondition_;
    std::condition_variable coverBackdropCondition_;
    std::thread timedTextThread_;
    std::thread coverBackdropThread_;
    bool timedTextThreadStop_;
    bool timedTextThreadRunning_;
    bool coverBackdropThreadStop_;
    bool coverBackdropRequestPending_;
    std::uint64_t coverBackdropRequestGeneration_;
    std::uint64_t presentationGeneration_;
    float presentationFrameRate_;
    // The producer publishes an immutable layer and renderers retain a shared
    // snapshot. This keeps the timed-text mutex short without copying every
    // command and string again on the video/present thread.
    // Subtitle, danmaku, lyrics and player information have independent producers and
    // render surfaces. Player information is always the topmost GPU layer.
    // Composite order is video -> danmaku -> subtitle -> lyrics -> disc -> information.
    std::shared_ptr<const TimedTextRenderLayer> timedTextLayers_[5];
    std::uint64_t timedTextRenderedSequences_[5];
    std::uint32_t timedTextRenderedCommandCounts_[5];
    bool timedTextRenderedHdrHighlights_[5];
    std::uint32_t timedTextWidths_[5];
    std::uint32_t timedTextHeights_[5];
    // Counts successful final swap-chain presents that included each visible
    // layer. A texture redraw is not a presentation and must not advance this.
    std::uint32_t timedTextPresentCounts_[5];
    std::atomic<std::uint64_t> backBufferAcquisitionCount_;
    bool timedTextPipelineQueryInFlight_[5];
    std::uint64_t timedTextCompositePixelInvocations_[5];
    std::atomic<float> discAspect_{0};
    CachedVideoSettings cachedVideoSettings_;
    bool hasCachedVideo_;
    std::atomic<std::uint64_t> videoGeneration_;
    std::atomic<std::uint64_t> presentedVideoGeneration_;
    std::atomic<std::uint64_t> countedVideoGeneration_;
    std::atomic<std::uint64_t> presentedVideoFrames_;
    std::atomic<std::uint64_t> coalescedVideoFrames_;
    std::atomic<std::uint64_t> swapChainPresents_;
    std::atomic<std::uint64_t> presentWait100ns_;
    std::atomic<std::uint64_t> deviceLockWait100ns_;
    std::atomic<std::uint64_t> softwareConvert100ns_;
    // Time spent copying decoded planes into the shader-visible textures. Split out
    // from softwareConvert100ns_ because that one only covers the CPU scaler: without
    // this the upload path had no metric at all, so upload changes could not be
    // measured or regressed.
    std::atomic<std::uint64_t> upload100ns_;
    std::atomic<std::uint32_t> playbackWorkPending_;
    std::atomic<bool> interactiveMove_;
    std::atomic<bool> lyricsLayoutEnabled_;
    std::atomic<std::uint32_t> coverBackdropBlurRadiusBits_;
    std::atomic<std::uint32_t> coverBackdropBlurPasses_;
    std::atomic<std::uint32_t> coverBackdropDownsampleFactor_;
    std::atomic<std::uint32_t> coverBackdropTintArgb_;
    std::atomic<std::uint32_t> coverRegionWidthPercentageBits_;
    std::atomic<std::uint32_t> lyricsRegionWidthPercentageBits_;
    std::atomic<std::uint32_t> coverLeftPaddingPercentageBits_;
    std::atomic<std::uint32_t> coverRightPaddingPercentageBits_;
    std::atomic<std::uint32_t> coverVerticalPaddingPercentageBits_;
    std::atomic<std::uint64_t> coverBackdropBlurSettingsGeneration_;
    std::atomic<bool> deviceRecoveryRequested_;
    std::function<void()> recoveryCallback_;
    HMONITOR hdrMonitor_;
    bool hdrSupportValid_;
    bool hdrSupported_;
    bool forceHdrOutput_;
    // 0 = Never, 1 = Auto; see FFF3FPConfiguration::sdrScRgbMode. Plain member:
    // it is written once during session construction and read on the render path
    // under the same lock as the colour mode.
    std::uint32_t sdrScRgbMode_;
    std::chrono::steady_clock::time_point hdrSupportCheckedAt_;
    bool hdrSwapChainRejected_;
    // Bounded caches are keyed by the immutable command content contract.  The
    // UI only changes coordinates for scrolling danmaku, so rebuilding a text
    // layout and two brushes at 60 Hz is unnecessary.
    std::unordered_map<std::uint64_t, IDWriteTextLayout*> timedTextLayouts_;
    std::deque<std::uint64_t> timedTextLayoutOrder_;
    std::unordered_map<std::uint32_t, ID2D1SolidColorBrush*> timedTextBrushes_;
    std::unordered_map<std::uint64_t, TimedTextSprite> timedTextSprites_;
    std::vector<PendingTimedTextSprite> timedTextPendingSprites_;
    std::vector<TimedTextSpriteInstance> timedTextSpriteInstances_;
    std::uint32_t timedTextAtlasX_;
    std::uint32_t timedTextAtlasY_;
    std::uint32_t timedTextAtlasRowHeight_;
    std::uint32_t timedTextAtlasSize_;
    std::uint32_t timedTextSpriteInstanceCapacity_;
    std::uint64_t timedTextSpriteCacheHits_;
    std::uint64_t timedTextSpriteCacheMisses_;
    mutable std::mutex errorMutex_;
    // fallbackReason_ is written under deviceMutex_ on the render path and read
    // through the public FallbackReason() from any thread, so it needs its own
    // leaf mutex (same pattern as errorMutex_/lastError_) - a plain std::string
    // assignment while the managed side copies it is a use-after-free window.
    mutable std::mutex fallbackMutex_;
    std::string fallbackReason_;
    std::string lastError_;
};
