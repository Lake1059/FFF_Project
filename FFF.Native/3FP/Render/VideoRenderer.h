#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Hdr/HdrProcessor.h"
#include "3FP/Hdr/HdrToneCurve.h"

#include <array>
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
struct ID3D11Texture3D;
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
    FFFResult SetScalingMode(std::uint32_t mode) noexcept;
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
    FFFResult PanView(float deltaX, float deltaY) noexcept;
    FFFResult ImageZoom(float& zoom, float& minimum, float& maximum) const noexcept;
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
    // Presentation pacing (PlayerApi exports FFF3FP_GetPresentPacingStatus). This layer only
    // produces a ceiling: the window's flip rate is bounded by min(declared panel rate,
    // per-media ceiling), and that ceiling is executed by the presenter's own schedule plus
    // the vblank clamp that Present(1,0) already applies on a fixed-refresh panel. Nothing
    // here picks which refresh slot a flip lands on any more. What killed the slot-aiming
    // system was two measurements on this machine's 144 Hz panel (2026-10-16, docs/03 §A9/A12):
    // on a quiet box the aiming arm and the fully-yielded variable-refresh arm both read 100.0%
    // of on-screen holds inside the rounding band with zero out-of-band samples -- aiming
    // bought no measurable difference at 60 fps on 144 Hz -- and the 4K60 run put 100% of 4214
    // submit-error samples inside one refresh period while 40% of its flips never reached the
    // display, so accuracy was not the bottleneck. Whether removing the aiming degrades the
    // 1/2-alternation case (120 fps content, 1.2 refreshes per frame) is measured, not
    // asserted: see the 12e_cfr_120 before/after arm in docs/03. The pacing readings below are
    // what is left of the grid, kept as instrumentation.
    // The source frame period, in 100 ns, handed over by the session for each frame it
    // releases. It no longer feeds the ceiling (the setters below do, per media); it stays
    // because the on-screen hold band is expected from it and the discard bucketing reads it
    // per frame. Zero means "unknown", which makes the hold interval unscoreable.
    void SetContentFramePeriod100ns(std::int64_t period100ns) noexcept;
    // 每条媒体一条上限，会话在 open 时按片源帧率设进来。逐帧周期不能当上限用：可变帧率
    // 片源每帧报一个数，上限会跟着逐帧抖，所以这里要的是一个媒体一个值。
    // 0 = 该媒体没有可用的帧率上界，只剩面板那一半。
    void SetMediaFrameRateCeilingHz(double ceilingHz) noexcept;
    // Presentation pace floor (adaptive pacing, driven from the session's late
    // branch). Zero is "off"; any positive value caps the bound from below -- the
    // presenter then holds every flip one floor-period out, which is what lets a
    // falling-behind pipeline show every picture at a slower cadence instead of
    // discarding pictures. Written on the session worker thread, read on the
    // presenter thread.
    void SetPresentPaceFloorHz(double paceHz) noexcept;
    // 只把这个上限往上抬、永不往下压。会话在看到更短的逐帧间距时调它：容器那两个字段在
    // MPEG-TS 上会**偏低**（没有索引，按流头采样），初值钉死就会掐掉后来的快段。
    void RaiseMediaFrameRateCeilingHz(double ceilingHz) noexcept;
    double MediaFrameRateCeilingHz() const noexcept;
    // The bound currently in force, in Hz: min(declared panel rate, ceiling) with the ceiling
    // left out when it is 0. Derived on demand from the same two inputs the presenter
    // schedules against, so the reading cannot drift out of step with the rule.
    double FrameRateBoundHz() const noexcept;
    // The display side of that min(): the panel's *declared* rate, always. The calibrated
    // value is deliberately not used -- see the body for the two ways it goes wrong. The
    // session needs this on its own, not folded into the bound, because "content is faster
    // than the panel" is a *reason* for discarding a frame and it has to be told apart from
    // "the pipeline fell behind the media clock".
    double DisplayCadenceHz() const noexcept;
    struct PresentPacingStatus {
        float declaredRefreshHz = 0.0f;
        // Panel period recovered from the swap chain's own frame statistics
        // (IDXGISwapChain::GetFrameStatistics: SyncRefreshCount against SyncQPCTime). Pure
        // readout since 2026-10-18: it is no longer allowed to feed the ceiling, because with
        // variable refresh on it measures the content cadence, not the panel.
        float measuredRefreshHz = 0.0f;
        float frameRateBoundHz = 0.0f;
        // 上限在岗 = 1：有输出窗口、取得到声明面板率、且没走无同步/移动循环那两条路。
        // 名字留着是为了 ABI 与宿主可读，语义已从"翻帧被钉在网格上"换成这句话。
        std::uint32_t gridLocked = 0;
        // 上限不在岗时说明是哪一个原因：
        //   0 = 在岗
        //   1 = 无同步（VRR/撕裂）路，让位给显示自己排
        //   2 = 上限不可用：没有输出窗口，或声明刷新率取不到
        //   3 = 交互式移动循环这一帧自己掌节奏
        std::uint32_t pacingBypass = 0;
        // 上限合规误差：这次提交 −（上次提交 + 上限周期），100 ns 单位、带符号，负值＝翻得
        // 比上限允许的最快速度还快。字段名里的 slot 是旧的（ABI），读的已经不是对槽位的偏差。
        // 读数按惯例除以面板周期换算成"几个刷新周期"，探针与 HOLD 行都是这么打的。
        std::int64_t lastSlotError100ns = 0;
        // Displayed outcome, counted off PresentRefreshCount: how many flips were held for
        // either rounding (floor or ceiling) of the refresh count the ceiling asked for.
        std::uint64_t holdSampled = 0;
        std::uint64_t holdWithinRounding = 0;
        std::uint64_t holdWorstDeviationRefreshes = 0;
        // Flips handed to Present() whose image never reached the monitor -- a later flip
        // replaced them in the queue before a vblank sampled the chain. This is the number
        // that makes the hold accounting interpretable: PresentRefreshCount only advances
        // when an image actually goes on screen, so without it a legitimate span over two or
        // three flips reads as a 10-refresh hitch.
        std::uint64_t neverCompositedPresents = 0;
    };
    PresentPacingStatus QueryPresentPacing() const noexcept;
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
    // frameIndex is the session's published FFF3FPSnapshot::frameIndex for this frame;
    // it is the key injected ST 2094-40 entries are matched on. Callers that render a
    // frame the session never indexes (cover art, still images, warm-up) pass
    // HdrProcessor::kUnknownFrameIndex and keep the pts-based behaviour.
    // `imageMode` marks a still image (not an animated one, which plays as video).
    // Images are judged differently from video: upscaling a picture is expected to
    // preserve the source pixel grid so it reads as a crisp enlargement, whereas
    // video upscaling wants a smooth reconstruction kernel. Passing it here keeps
    // the two policies in one place instead of guessing from the frame.
    //
    // `auxiliaryFrame` / `auxiliaryComposition` carry the optional second video layer.
    // When the caller requests composition (enabled != 0) its coded components are
    // composed over the base layer before any colour mapping runs, and the kind-3
    // table then sees the combined signal. When the request is present but no frame
    // could be paired, the table is withheld for that frame rather than sampled on
    // samples it was not baked for; callers must pass the request either way.
    FFFResult Render(const AVFrame* frame, std::int64_t frameIndex,
        bool limitToNativeSize = false,
        bool coverArt = false, bool prepareOnly = false, bool imageMode = false,
        const AVFrame* auxiliaryFrame = nullptr,
        const FFF3FPAuxiliaryComposition* auxiliaryComposition = nullptr) noexcept;
    FFFResult Redraw() noexcept;
    /// Establish the presentation chain for a freshly bound output window and
    /// present exactly one frame. Redraw() is a no-op while no decoded frame is
    /// cached -- and a session that was opened headless (host rebuilding its
    /// HWND, or a brand-new session binding its first window) has none, because
    /// the headless Render path deliberately keeps no GPU cache. Without this
    /// entry the window is stranded: nothing creates the swap chain until a
    /// frame is rendered, and in a paused/ready session no frame ever is.
    /// Safe from the session command queue: it takes the same locks the
    /// presenter thread takes, in the same order.
    FFFResult PresentAfterBind() noexcept;
    // Re-pull the HDR classification and the ST 2094-40 curve from the processor and
    // refresh the shader inputs for the frame already sitting in the cached surface, so
    // installing or clearing a metadata table shows up in the picture without waiting for
    // a decoded frame that may never come (paused / end-of-stream). Only ever called on
    // the present path: `cachedVideoSettings_` belongs to the device lock, not to the
    // thread that received the API call.
    void RefreshHdrShaderInputs() noexcept;
    // The decode transfer function follows the pixels, never the metadata; this is the
    // single place that decides it, for Render() and for the refresh above.
    std::uint32_t ResolveDecodeTransfer(const HdrFrameState& hdrState) const noexcept;
    FFFResult CreateD3D11HardwareDeviceContext(AVBufferRef** context) noexcept;
    bool OwnsD3D11Texture(ID3D11Texture2D* texture) const noexcept;
    /// Zero-cost capability probe for D3D11VA hardware decoding.
    ///
    /// Ask the driver instead of consulting a table: the answer describes this GPU
    /// and this driver build, so it stays correct on machines where a static
    /// "codec X at resolution Y is fine" rule would be wrong. It is a capability
    /// question only -- can this stream be decoded at all -- never a throughput
    /// question, which has no standard API and is left alone.
    enum class D3D11DecodeSupport : std::uint8_t {
        /// The probe could not run (no device yet, or no ID3D11VideoDevice).
        /// The caller must keep its existing fallback ordering: this is not a
        /// verdict, and treating it as "unsupported" would disable hardware
        /// decoding whenever the renderer has not warmed up yet.
        Unknown = 0,
        /// The driver exposes at least one decoder configuration for the stream.
        Supported = 1,
        /// The driver has no configuration for it; software decoding is the only route.
        Unsupported = 2,
    };
    /// `codecId` is an AVCodecID, `pixelFormat` an AVPixelFormat and `profile` an
    /// FFmpeg profile. All three are passed as integers so this header stays free
    /// of FFmpeg types.
    D3D11DecodeSupport QueryD3D11DecodeSupport(std::int32_t codecId, std::int32_t profile,
        std::int32_t pixelFormat, std::int32_t width, std::int32_t height) noexcept;
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
    // Stream declaration counterpart of HdrState(): never changes per frame.
    HdrFrameState StreamHdrState() const noexcept;
    bool IsHdrSourceStream() const noexcept;
    std::uint64_t PresentedVideoFrames() const noexcept;
    std::uint64_t CoalescedVideoFrames() const noexcept;
    std::uint64_t SwapChainPresents() const noexcept;
    std::uint64_t SubmittedVideoGeneration() const noexcept;
    std::uint64_t PresentedVideoGeneration() const noexcept;
    bool HasPendingVideoPresentation() const noexcept;
    bool HasOutputWindow() const noexcept;
    /// Whether a decoded frame is currently sitting in the cached surface.
    /// Unsynchronized on purpose: the only caller is the session command queue
    /// deciding between Redraw() and PresentAfterBind() at bind time, and a
    /// stale answer there costs at most one redundant present.
    bool HasCachedVideo() const noexcept { return hasCachedVideo_; }
    std::uint64_t PresentWait100ns() const noexcept;
    std::uint64_t DeviceLockWait100ns() const noexcept;
    std::uint64_t SoftwareConvert100ns() const noexcept;
    std::uint64_t Upload100ns() const noexcept;
    std::uint64_t UploadMapWait100ns() const noexcept;
    std::uint64_t UploadCopy100ns() const noexcept;
    std::uint64_t DownscaleResample100ns() const noexcept;
    std::uint64_t ScalePassCount() const noexcept;
    std::uint64_t UploadCount() const noexcept;
    std::uint64_t UploadCopyBytes() const noexcept;
    std::uint32_t UploadPath() const noexcept;
    std::uint32_t OutputBitDepth() const noexcept;
    FFF3FPVideoScalingMode ActualVideoScalingMode() const noexcept;
    // Presentation diagnostics for the ST 2094 paths; see FFF3FPHdrSignalDiagnostics.
    FFF3FPHdrSignalDiagnostics HdrSignalDiagnostics() const noexcept;
    /// RTX Video Super Resolution switch and diagnostics. See the API header
    /// for why `active` means "the driver accepted the request", not "the
    /// image is provably sharper".
    FFFResult SetVideoSuperResolution(FFF3FPVideoSuperResolution mode) noexcept;
    FFF3FPVideoSuperResolution RequestedVideoSuperResolution() const noexcept;
    bool VideoSuperResolutionActive() const noexcept;
    void FillVideoSuperResolutionStatus(FFF3FPVideoSuperResolutionStatus& status) const noexcept;
    /// NVIDIA automatic HDR (RTX Video HDR) switch and diagnostics. Same
    /// ownership split as VSR: the renderer owns the device-facing decision.
    /// When TrueHDR is engaged it takes precedence over VSR for the frame:
    /// its video-processor pass writes PQ/BT.2020 samples, which VSR's
    /// same-format YUV intermediate contract cannot represent.
    FFFResult SetVideoAutoHdr(FFF3FPVideoAutoHdr mode) noexcept;
    FFF3FPVideoAutoHdr RequestedVideoAutoHdr() const noexcept;
    bool VideoAutoHdrActive() const noexcept;
    void FillVideoAutoHdrStatus(FFF3FPVideoAutoHdrStatus& status) const noexcept;
    std::string FallbackReason() const;
    std::string LastError() const;
    // ST 2094-40 injection. The HdrProcessor owns the
    // table; the renderer forwards so PlayerSession has a single entry point.
    FFFResult SetInjectedHdrMetadata(const FFF3FPHdrDynamicMetadataEntry* entries,
        std::uint32_t count) noexcept;
    void ClearInjectedHdrMetadata() noexcept;
    // Generic dynamic-metadata carrier (FFF3FP_SetDynamicMetadata): same table
    // and picture path as the ST 2094-40 injection, kind-tagged packets.
    FFFResult SetDynamicMetadata(const FFF3FPDynamicMetadataPacket* packets,
        std::uint32_t count) noexcept;
    void ClearDynamicMetadata(std::uint32_t kind) noexcept;
    std::uint32_t HdrMetadataSource() const noexcept;

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
        // LRU bookkeeping. Danmaku strings churn (a string is on screen for
        // seconds and rarely repeats), so only entries tagged danmaku take
        // part in the bounded quota; subtitle/lyrics entries are precious and
        // never evicted by it.
        bool danmaku = false;
        std::uint64_t lastUsed = 0;
    };
    // Integer atlas rect used by the eviction free list. The float fields of
    // TimedTextSprite carry the same geometry in pixel units.
    struct TimedTextAtlasRect {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
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
        bool danmaku = false;
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
    // Resamples the decoded frame down to the display size into an internal buffer,
    // returning false when the frame should be uploaded untouched. On true, the
    // caller uploads AdaptivePlanes()/AdaptiveLines() at the requested size.
    bool PrepareAdaptiveDownscale(const AVFrame* frame, std::uint32_t width,
        std::uint32_t height, std::uint32_t outputWidth, std::uint32_t outputHeight) noexcept;
    std::uint8_t* const* AdaptivePlanes() const noexcept { return adaptivePlanes_; }
    const int* AdaptiveLines() const noexcept { return adaptiveLines_; }
    FFFResult PrepareScaledVideo(std::uint32_t outputWidth, std::uint32_t outputHeight,
        ID3D11ShaderResourceView** views, bool rgbSource = false) noexcept;
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
    /// Upscale the decoded planes through a video processor with RTX VSR
    /// enabled, redirecting effectiveSourceViews_ at the result. Returns Success
    /// with videoSuperResolutionActive_ == false when VSR simply does not apply.
    /// `targetWidth`/`targetHeight` are the destination rect in *viewport* axes,
    /// computed in DrawCachedVideo; they are converted to the picture's own axes
    /// because a quarter-turn rotation swaps them.
    FFFResult ApplySuperResolution(std::uint32_t targetWidth,
        std::uint32_t targetHeight) noexcept;
    // --- NVIDIA automatic HDR (RTX Video HDR / TrueHDR) ---
    /// Convert the decoded SDR planes through a video processor with TrueHDR
    /// enabled, writing PQ / BT.2020 samples into an R10G10B10A2_UNORM surface
    /// and redirecting effectiveSourceViews_[0] at it. Unlike VSR this does
    /// NOT require an upscale or a 360p..1440p source: the extension is
    /// orthogonal to scaling (mpv forces the VP at 1:1 for exactly this
    /// reason). The caller must present the frame through the HDR output
    /// path, which ApplyTrueHdr itself checks.
    FFFResult ApplyTrueHdr(std::uint32_t targetWidth,
        std::uint32_t targetHeight) noexcept;
    /// Build plane SRVs over an NV12/P010 texture, matching EnsurePipeline's
    /// view formats exactly (a mismatch silently shifts colours).
    bool CreateYuvPlaneViews(ID3D11Texture2D* texture, std::uint32_t format,
        ID3D11ShaderResourceView** views) noexcept;
    void ReleaseSuperResolutionResources() noexcept;
    void ReleaseTrueHdrResources() noexcept;
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
    // Pacing bookkeeping, once per flip, on the presenter thread: mirrors the declared panel
    // rate into the readouts, re-polls it when the calibrated value drifts from it (see the
    // body -- the output window belongs to the host, so this is the only hook available), and
    // publishes whether the ceiling is in force and why not. Caller must hold
    // timedTextMutex_, because presentationFrameRate_ is written under it at every other site.
    void PublishPacingStatus() noexcept;
    // Feeds one swap-chain frame-statistics sample into the panel-period readout, scores the
    // last flip against the ceiling's own cadence, and settles the on-screen hold band. Plain
    // integers rather than the DXGI struct so this header stays free of the dxgi includes.
    // presentCount and generation are carried for the interval accounting: a flip whose
    // image never reached the monitor leaves PresentRefreshCount where it was, and the only
    // way to tell that apart from frame-statistics lag is PresentCount advancing anyway,
    // while generation says whether the flip even had a new picture to hand over.
    void UpdatePresentGrid(std::int64_t syncRefresh, std::int64_t syncQpc,
        std::int64_t landedRefresh, std::int64_t presentCount, std::uint64_t generation,
        bool sequenceDisjoint) noexcept;
    FFFResult DrawTimedText(TimedTextLayerSlot slot) noexcept;
    void TimedTextThread() noexcept;
    void StopTimedTextThread() noexcept;
    void CompositeTimedText(ID3D11RenderTargetView* target, TimedTextLayerSlot slot) noexcept;
    void ReleaseTimedTextSlotResources(TimedTextLayerSlot slot) noexcept;
    void ReleaseTimedTextResources(bool resetRenderedState = true) noexcept;
    bool OutputSupportsHdr() noexcept;
    // Drop the "only push when it moved" cache. Call whenever the swap chain object
    // or its buffers change: the metadata a display currently holds belongs to the OLD
    // chain, so without this the new chain never receives a single SetHDRMetaData call
    // while isHdrSource and GetRenderTargetInfo().hdr both keep reporting HDR.
    void InvalidateHdrMetadataCache() noexcept;
    void SetHdrMetadata() noexcept;
    // Rebuild the b2 constant buffer + curve LUT for this frame's ST 2094
    // metadata. Returns false to leave the shader on its original path.
    bool BuildDynamicToneSettings(const HdrDynamicMetadata& metadata,
        DynamicShaderSettings& settings,
        std::array<float, kDynamicCurveLutWidth * kHdrMaxProcessingWindows>& curves) noexcept;
    // Fill the kind-3 (caller-supplied colour transform) rows of `settings` and
    // upload the 3-D LUT when the table changed. Returns true when a transform
    // is in force for this frame; the b2 rows stay zero otherwise. Call with the
    // device lock held (it uploads on the immediate context).
    bool BuildColorTransformSettings(const HdrFrameState& hdrState,
        DynamicShaderSettings& settings) noexcept;
    // Upload the optional auxiliary layer's three planes and fill the b3 row.
    // Fails (leaving `settings` zeroed) when the frame is not a 10-bit planar
    // 4:2:0 picture or a plane is not CPU-addressable; the caller then treats
    // the request as unpaired. Call with the device lock held.
    FFFResult PrepareElComposition(const AVFrame* frame,
        const FFF3FPAuxiliaryComposition& composition,
        ElCompositionSettings& settings) noexcept;
    void ReleaseElCompositionResources() noexcept;
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
    std::uint32_t cachedOriginalInputLayout_ = 0;
    float cachedOriginalSampleScale_ = 1;
    ID3D11PixelShader* coverBackdropPixelShader_;
    ID3D11PixelShader* timedTextPixelShader_;
    ID3D11PixelShader* scalePixelShader_;
    ID3D11SamplerState* sampler_;
    ID3D11SamplerState* pointSampler_;
    ID3D11SamplerState* panoramaSampler_;
    ID3D11Buffer* constants_;
    ID3D11Buffer* scaleConstants_;
    // ST 2094 dynamic tone mapping (HDR10+ / HDR Vivid). Both are optional:
    // when the buffer or the LUT cannot be created, dynamicCurveBuilt_ stays false
    // and playback falls back to the original static mapping. The LUT is a
    // 256x3 R32_FLOAT texture holding one reconstructed curve per window.
    ID3D11Buffer* dynamicConstants_ = nullptr;
    ID3D11Texture2D* dynamicCurveTexture_ = nullptr;
    ID3D11ShaderResourceView* dynamicCurveView_ = nullptr;
    bool dynamicCurveBuilt_ = false;
    // Caller-supplied colour transform (kind 3): one 33^3 R32G32B32_FLOAT 3-D
    // lookup table, bound at t4 and sampled trilinearly through the same clamped
    // linear sampler as the curve LUT. Optional like the ST 2094 resources: when
    // it cannot be created, transformLutBuilt_ stays false, the kind-3 rows of
    // b2 stay zero and the shader takes its original path.
    ID3D11Texture3D* transformLutTexture_ = nullptr;
    ID3D11ShaderResourceView* transformLutView_ = nullptr;
    bool transformLutBuilt_ = false;
    // The payload whose table is currently resident in transformLutTexture_.
    // Comparing the shared_ptr keeps a static table at one upload per install
    // instead of 421 KB per frame; cleared on device teardown so a recreated
    // texture always receives a fresh upload.
    std::shared_ptr<const FFF3FPColorTransformPayload> uploadedTransform_;
    // Mirrors DynamicShaderSettings::transformEnabled of the last refresh, so the
    // SDR layout specialisation (which fixes InputLayout/ColorMode/Transfer/Gamut
    // at compile time) can be skipped while a transform is in force.
    bool cachedTransformEnabled_ = false;
    // Optional auxiliary-layer composition resources. Three R16_UINT planes bound
    // at t5..t7 (the code values themselves, never normalised, so the integer
    // composition in the shader is exact) and the parameter row at b3. Created on
    // demand; `cachedElComposeEnabled_` says they hold this frame's layer.
    struct ElPlaneTexture {
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* view = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    ElPlaneTexture elPlanes_[3];
    ID3D11Buffer* elCompositionBuffer_ = nullptr;
    bool cachedElComposeEnabled_ = false;
    // Set when the caller asked for composition but no auxiliary frame was paired:
    // the kind-3 table must not run on the uncomposed picture, and a metadata
    // refresh must not silently re-enable it for the cached frame.
    bool cachedElTransformBlocked_ = false;
    // Raised when the injected ST 2094 table is installed or cleared, consumed on the
    // present path so the refresh happens under the device lock instead of racing the
    // presentation pump.
    std::atomic<bool> hdrInputsDirty_{ false };
    // Presentation diagnostics (see FFF3FPHdrSignalDiagnostics). Counters accumulate for
    // the life of the renderer; the signaled block is whatever was last handed to DXGI.
    // Each field is atomic on its own, so a poll can catch a mixed read across a
    // block change; there is no way to read the block atomically.
    std::atomic<std::uint64_t> dynamicCurveFramesApplied_{ 0 };
    std::atomic<std::uint64_t> dynamicCurveFramesRefused_{ 0 };
    std::atomic<std::uint64_t> hdrMetaDataCalls_{ 0 };
    std::atomic<std::uint64_t> hdrMetaDataSuppressed_{ 0 };
    std::atomic<std::uint32_t> signaledMaxMastering_{ 0 };
    std::atomic<std::uint32_t> signaledMinMastering_{ 0 };
    std::atomic<std::uint32_t> signaledMaxCll_{ 0 };
    std::atomic<std::uint32_t> signaledMaxFall_{ 0 };
    // Frame-derived halves of the transfer decision, kept so a refresh reproduces exactly
    // what Render() computed: metadata arriving later must not change how samples decode.
    bool lastFrameDeclaresTrc_ = false;
    std::uint32_t lastTransferFromFrame_ = 0u;
    // Last HDR10 metadata block handed to the swap chain. With ST 2094 metadata
    // the content-light fields vary per scene, and re-submitting every frame
    // makes the display tone mapping hunt, so submissions are gated on a real
    // change.
    DXGI_HDR_METADATA_HDR10 lastHdrMetadata_ = {};
    bool hdrMetadataValid_ = false;
    ID3D11Texture2D* sourceTextures_[3];
    ID3D11ShaderResourceView* sourceViews_[3];
    // Upload-capacity state, read by WantsStagingUpload. Both are atomic because the
    // device (and its reset of the refusal) can be created off the session worker
    // thread that runs Render / EnsurePipeline.
    std::atomic<std::uint64_t> dedicatedVideoMemoryBytes_{ 0 };
    std::atomic<bool> uploadRingUnavailable_{ false };
    ID3D11Texture2D* uploadStaging_[2][3]{};
    ID3D11Texture2D* uploadTextures_[3]{};
    ID3D11ShaderResourceView* uploadViews_[3]{};
    unsigned uploadSlot_ = 0;
    // "Does this frame go through the STAGING ring?" -- the size table, the capacity
    // gate and this device's refusal, in that order. EnsurePipeline and Render must
    // read the SAME answer: DEFAULT textures without a ring would make the upload's
    // Map(WRITE_DISCARD) fail, which is why this is one predicate and not two tests.
    bool WantsStagingUpload(std::uint32_t width, std::uint32_t height) const noexcept;
    PlaneScaleChain planeScaleChains_[3];
    std::uint64_t scaledVideoGeneration_;
    std::uint32_t scaledOutputWidth_;
    std::uint32_t scaledOutputHeight_;
    // Part of the scaled-video cache key: VSR changes the effective source
    // without advancing the decode generation, so the cached views would
    // otherwise be reused across a VSR state change. TrueHDR changes both the
    // effective source and its layout (single RGB plane), so it gets its own
    // key member for the same reason.
    bool scaledVideoSuperResolution_;
    bool scaledVideoTrueHdr_;
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
    // ---- Presentation pacing ceiling (see FrameRateBoundHz) ----
    // Written on the session/API thread, read on the presenter thread.
    std::atomic<std::int64_t> contentPeriod100ns_{ 0 };
    // 一条媒体一个帧率上界，会话在 open 时写；0 = 这条媒体没有可用的上界。
    // Atomic because the session worker writes it while the presenter schedules against it.
    std::atomic<double> mediaCeilingHz_{ 0.0 };
    // Adaptive presentation pace floor (0 = off). See SetPresentPaceFloorHz: the
    // presenter schedules against FrameRateBoundHz, which takes the min with this.
    std::atomic<double> paceFloorHz_{ 0.0 };
    // Max targetFrameRate over visible timed-text layers (0 = none visible). The
    // media ceiling must not swallow this half: a 24 fps source with a 60 Hz danmaku
    // layer still owes the overlay 60 flips/s even though the pictures change 24
    // times a second. FrameRateBoundHz raises against the declared panel rate, so
    // this request can never raise the bound above what the panel scans out. Written
    // on the API thread, read on the presenter thread.
    std::atomic<float> timedTextLayerTargetHz_{ 0.0f };
    // Set by every chain creation, consumed by the presenter thread: the frame-statistics
    // sequence restarts with the chain, so a delta taken across the swap is not a period.
    std::atomic<bool> gridResetRequested_{ true };
    // Presenter-thread-only state -- TimedTextThread is the sole caller of PresentTimedText,
    // and PresentCurrentFrame runs inside it -- so none of this needs a lock against itself.
    // The atomics at the bottom of this block are mirrored after each flip so the API thread
    // can read them without taking presentMutex_, which the presenter holds across a blocking
    // Present.
    double gridPeriodQpc_ = 0.0;             // QPC ticks per refresh; 0 = not calibrated
    // The instant this thread actually handed the flip to Present, recorded immediately
    // before the call, plus the previous flip's instant: their difference is the real submit
    // spacing, and that is what gets scored against the ceiling's period. DXGI's SyncQPCTime
    // is when the scheduler sampled, not when we submitted, so neither number uses it.
    std::int64_t lastSubmitQpc_ = 0;
    std::int64_t previousSubmitQpc_ = 0;
    std::int64_t pacingPollQpc_ = 0;         // last declared-rate re-poll; 1 s gate
    // Displayed-outcome accounting. Keeping the flip rate at the ceiling is only half the
    // job; whether the panel then held the frame for the number of refreshes the content
    // period asks for is the half the eye actually sees, and it is read from
    // PresentRefreshCount -- the vblank on which the chain's last image reached the monitor.
    // Two consecutive reads of it differ by the refreshes the earlier flip was on screen for.
    std::int64_t lastLandedRefresh_ = 0;
    // 一条翻帧*该*占几个刷新周期（内容周期 ÷ 声明面板周期，144 Hz 上的 60 fps 就是 2.4）。
    // 它的 floor 和 ceiling 框住眼睛能接受为"匀"的全部停留，判带就打在这个带上，而不是
    // "等于某个瞄点"：要把某个 PresentRefreshCount 差值归给某次翻帧，得先对齐两条计数器
    // 序列，而两种对齐都试过、都停在 30-47% 而面板平均正好是 2.4。取整带给出同一句判断，
    // 却不依赖那个对齐是对的。
    // 停在"等待被显示器采样"里的那串翻帧，各自该占几个刷新周期的累计值。一次上屏结算一个
    // 停留样本并清零——这样被后来者取代、从未上屏的那些翻帧会把自己应得的那份计入带内，
    // 而不是让下一次停留凭空多出好几格。
    double pendingHoldRatio_ = 0.0;
    // 结算一个停留样本所用的区间账，三个数各自独立数同一段区间：
    //   intervalFlips_          —— 这段区间里有几次翻帧；
    //   intervalAdvances_       —— 这些翻帧一共带进来几张新画面（生成号差值之和）；
    //   intervalNeverComposited_—— 其中几次翻帧的 PresentRefreshCount 没有前进。
    // 这三条就是它们的答案（2026-10-16 用 FPT_HOLD_TRACE 逐样本量的，不是推的）：
    //   adv == flips 在每趟 ~500 个样本里成立（只有重启后那一个例外）⇒ "按新画面数开带"
    //   与"按翻帧数开带"操作数相同，那个改法因此没做；
    //   nc=1 的区间实测 meanHeld 4.61~4.80 对预测 2×ratio=4.80 ⇒ 累计口径是对的。
    std::uint32_t intervalFlips_ = 0;
    std::uint32_t intervalAdvances_ = 0;
    std::uint32_t intervalNeverComposited_ = 0;
    // 这段区间里出现过"内容周期未知的翻帧"（contentPeriod100ns_ <= 0，会话还没给出这一帧
    // 该占几格）。这样的区间结算不出"停留落在预期的 ⌊r⌋/⌈r⌉ 内"这句话，判带要跳过它。
    bool intervalUnscoreable_ = false;
    std::int64_t lastPresentCount_ = 0;
    std::uint64_t lastFlipGeneration_ = 0;
    std::atomic<std::uint64_t> neverCompositedPresents_{ 0 };
    std::atomic<std::uint64_t> holdSampled_{ 0 };
    std::atomic<std::uint64_t> holdWithinRounding_{ 0 };
    std::atomic<std::uint64_t> holdWorstDeviation_{ 0 };
    std::int64_t lastSyncRefresh_ = 0;       // previous frame-statistics sample pair
    std::int64_t lastSyncQpc_ = 0;
    // Readouts mirrored after each flip. measuredRefreshHz_ is a pure measurement: with
    // variable refresh on it equals the content cadence, so it must not become a bound input.
    std::atomic<double> measuredRefreshHz_{ 0.0 };
    std::atomic<double> declaredRefreshHz_{ 0.0 };
    std::atomic<std::uint32_t> gridLocked_{ 0 };     // 1 = the ceiling is in force
    std::atomic<std::uint32_t> pacingBypass_{ 2 };   // starts "no ceiling available"
    std::atomic<std::int64_t> lastSlotError100ns_{ 0 };   // ceiling-cadence error; name is ABI
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
    // Atomic because the status report may read it from another thread while the
    // device is being (re)created.
    std::atomic<bool> nvidiaAdapter_{ false };
    // VSR was requested AND the blit succeeded on the last presented frame.
    std::atomic<bool> videoSuperResolutionActive_{ false };
    // The driver rejected the request (or the blit failed). Latched off so later
    // frames do not repeat a failing call; cleared by a fresh Auto request in
    // SetVideoSuperResolution, which re-probes in case VSR was just enabled in
    // the NVIDIA control panel.
    std::atomic<bool> videoSuperResolutionRejected_{ false };
    // Why the last frame did not use VSR, for the status report.
    std::atomic<FFF3FPVideoSuperResolutionReason> videoSuperResolutionReason_{
        FFF3FPVideoSuperResolutionReason::NotRequested };
    // Last reason written to the process log, so a steady state (e.g. VSR Off)
    // logs its transition once instead of once per frame. Sentinelled with a
    // value outside the enum.
    std::atomic<std::uint32_t> videoSuperResolutionLoggedReason_{ 0xFFFFFFFFu };
    // 临时诊断：VSR 步进账。只在 deviceMutex_ 保护的渲染路径上累加，所以是普通量；
    // 每 300 帧经内核日志槽报一次，重建计数器用来判几何是否逐帧抖动。
    std::int64_t vsrBuildTicks_{ 0 };
    std::int64_t vsrBltTicks_{ 0 };
    std::uint64_t vsrFrames_{ 0 };
    std::uint64_t vsrRebuilds_{ 0 };
    // 临时诊断：TrueHDR 步进账，与 VSR 同款但走 FPT_HDR_TRACE。两路抢同一条
    // 视频处理器时，混在一份日志里分不清是谁在重建。
    std::int64_t trueHdrBuildTicks_{ 0 };
    std::int64_t trueHdrBltTicks_{ 0 };
    std::uint64_t trueHdrFrames_{ 0 };
    std::uint64_t trueHdrRebuilds_{ 0 };
    // Geometry this frame *would* have used, in the *picture's* axes (see
    // ApplySuperResolution). Published before the geometry gates so the host can
    // explain a skip (0x0 said nothing about why NotUpscaling fired). Atomic
    // because the status getter reads them on the host's thread. These are a
    // diagnostic only: they must never be the cache key, or the comparison
    // becomes a value against itself.
    std::atomic<std::uint32_t> videoSuperResolutionSourceWidth_{ 0 };
    std::atomic<std::uint32_t> videoSuperResolutionSourceHeight_{ 0 };
    std::atomic<std::uint32_t> videoSuperResolutionTargetWidth_{ 0 };
    std::atomic<std::uint32_t> videoSuperResolutionTargetHeight_{ 0 };
    // Geometry the cached pass was *built* with. Written only inside the rebuild
    // block, so it stays put while a frame skips or publishes new diagnostics.
    std::uint32_t superResolutionPassSourceWidth_{ 0 };
    std::uint32_t superResolutionPassSourceHeight_{ 0 };
    std::uint32_t superResolutionPassTargetWidth_{ 0 };
    std::uint32_t superResolutionPassTargetHeight_{ 0 };
    // The upscaled NV12/P010 surface and its per-plane views. Owned here, never
    // swapped into sourceViews_ (that array is EnsurePipeline's long-lived
    // pipeline state and mutating it would corrupt the next frame).
    ID3D11Texture2D* superResolutionTexture_{ nullptr };
    ID3D11ShaderResourceView* superResolutionViews_[3]{};
    // DXGI_FORMAT of the cached super-resolution surface (0 when absent).
    std::uint32_t superResolutionSourceFormat_{ 0 };
    // The video processor, both views and the two QIs that drive the upscaled
    // pass. Cached across frames on purpose: building them every frame measures
    // 60fps -> 1-4fps because the driver re-negotiates its AI scaler on each
    // fresh processor, so only the blit stays in the per-frame path.
    // `superResolutionInputTexture_` is held with its own reference so that a
    // destroyed and reallocated decoded texture cannot alias the cached input
    // view by address.
    ID3D11VideoDevice* superResolutionVideoDevice_{ nullptr };
    ID3D11VideoContext* superResolutionVideoContext_{ nullptr };
    ID3D11VideoProcessorEnumerator* superResolutionEnumerator_{ nullptr };
    ID3D11VideoProcessor* superResolutionProcessor_{ nullptr };
    ID3D11VideoProcessorInputView* superResolutionInputView_{ nullptr };
    ID3D11VideoProcessorOutputView* superResolutionOutputView_{ nullptr };
    ID3D11Texture2D* superResolutionInputTexture_{ nullptr };
    // --- NVIDIA automatic HDR (RTX Video HDR / TrueHDR, opt-in; default Off) ---
    // Host-visible switch. Off keeps the renderer byte-identical to the
    // pre-TrueHDR behaviour, same invariant as VSR.
    std::atomic<FFF3FPVideoAutoHdr> requestedVideoAutoHdr_{ FFF3FPVideoAutoHdr::Off };
    // Driver capability answer for the current Auto request, probed once per
    // request (the probe needs a live video processor, so it runs on the
    // render path -- latched so a negative answer costs one call, not one
    // per frame).
    std::atomic<bool> videoTrueHdrSupported_{ false };
    std::atomic<bool> videoTrueHdrProbed_{ false };
    // TrueHDR was requested, the probe read back 1, and the blit succeeded on
    // the last presented frame with the PQ/BT.2020 output contract in force.
    std::atomic<bool> videoTrueHdrActive_{ false };
    // The driver refused (probe, Set or blit). Latched until a fresh Auto
    // request, mirroring the VSR discipline.
    std::atomic<bool> videoTrueHdrRejected_{ false };
    // Why the last frame did or did not use TrueHDR, for the status report.
    std::atomic<FFF3FPVideoAutoHdrReason> videoTrueHdrReason_{
        FFF3FPVideoAutoHdrReason::NotRequested };
    // Last reason written to the process log (dedup, sentinel outside enum).
    std::atomic<std::uint32_t> videoTrueHdrLoggedReason_{ 0xFFFFFFFFu };
    // Geometry of the cached TrueHDR surface in the picture's axes. Atomic
    // because the status getter reads them on the host's thread.
    std::atomic<std::uint32_t> videoTrueHdrSourceWidth_{ 0 };
    std::atomic<std::uint32_t> videoTrueHdrSourceHeight_{ 0 };
    std::atomic<std::uint32_t> videoTrueHdrOutputWidth_{ 0 };
    std::atomic<std::uint32_t> videoTrueHdrOutputHeight_{ 0 };
    // The R10G10B10A2_UNORM surface the TrueHDR pass writes and its single
    // SRV. Like the VSR surface it is owned here and never swapped into
    // sourceViews_.
    ID3D11Texture2D* trueHdrTexture_{ nullptr };
    ID3D11ShaderResourceView* trueHdrView_{ nullptr };
    // The TrueHDR video-processor pass, cached across frames with the same
    // discipline as the super-resolution one: rebuilding it every frame costs
    // ~20x the frame budget because the driver re-warms its enhancement on each
    // fresh processor. `trueHdrPpeRequested_` is part of the pass identity --
    // the cached processor carries the super-resolution extension only if the
    // host asked for it when the processor was built.
    ID3D11VideoDevice* trueHdrVideoDevice_{ nullptr };
    ID3D11VideoContext* trueHdrVideoContext_{ nullptr };
    ID3D11VideoProcessorEnumerator* trueHdrEnumerator_{ nullptr };
    ID3D11VideoProcessor* trueHdrProcessor_{ nullptr };
    ID3D11VideoProcessorInputView* trueHdrInputView_{ nullptr };
    ID3D11VideoProcessorOutputView* trueHdrOutputView_{ nullptr };
    ID3D11Texture2D* trueHdrInputTexture_{ nullptr };
    bool trueHdrPpeRequested_{ false };
    // Decode transfer of the frame currently held, untouched by the TrueHDR
    // re-tag. The ApplyTrueHdr SDR-source gate and every draw's baseline must
    // read THIS, not cachedVideoSettings_.transfer: DrawWithShader retags that
    // field to 1 (PQ) whenever a frame's pixels came out of the TrueHDR pass,
    // and on a repeated draw of the SAME frame (pause, redraw, pixel readback)
    // no new frame prepare runs to restore it. Reading the retagged field
    // there made a TrueHDR frame self-block every subsequent draw of itself
    // (skip: source is not SDR) and leaked transfer=1 into the shader constants
    // of those skipped draws, which then decoded the decoded SDR picture as PQ.
    std::atomic<std::uint32_t> decodedTransfer_{ 0 };
    // effectiveSourceViews_ is rebuilt from sourceViews_ on every frame and then
    // optionally redirected at superResolutionViews_. Keeping the two apart is
    // what makes VSR switching safe mid-stream.
    ID3D11ShaderResourceView* effectiveSourceViews_[3]{};
    FFF3FPVideoScalingQuality scalingQuality_;
    std::uint32_t scalingMode_ = 0;
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
    // Split of upload100ns_ (see the snapshot fields of the same name): without
    // these an upload regression could not be attributed to waiting on the GPU,
    // to moving bytes, or to a CPU resample done before the upload.
    std::atomic<std::uint64_t> uploadMapWait100ns_;
    std::atomic<std::uint64_t> uploadCopy100ns_;
    std::atomic<std::uint64_t> downscaleResample100ns_;
    std::atomic<std::uint64_t> scalePassCount_;
    // Operand the bandwidth claim needs: uploadCopy100ns_ without a byte count is
    // a duration that cannot be turned into GB/s, and without an upload count it is
    // divided by presented frames even when dropped ones were uploaded too.
    std::atomic<std::uint64_t> uploadCount_;
    std::atomic<std::uint64_t> uploadCopyBytes_;
    std::atomic<std::uint32_t> uploadPath_;
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
    // Rects freed by danmaku LRU eviction, reused before the shelf cursor
    // advances. Without this the atlas only ever grows, then full-clears and
    // re-rasterises every slot a few frames later.
    std::vector<TimedTextAtlasRect> timedTextAtlasFreeRects_;
    std::uint32_t danmakuSpriteCount_ = 0;
    std::uint64_t timedTextSpriteClock_ = 0;
    std::uint64_t danmakuSpriteEvictions_ = 0;
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
