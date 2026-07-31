#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Hdr/HdrProcessor.h"

#include <cstdint>
#include <atomic>
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
    std::vector<TimedTextRenderCommand> commands;
};

enum class TimedTextLayerSlot : std::uint32_t {
    Subtitle = 0,
    Danmaku = 1,
    PlayerInformation = 2,
};

FFFResult EvaluateVideoColorTransform(FFF3FPColorTransform& transform) noexcept;
FFFResult EvaluateTimedTextRasterization(FFF3FPTimedTextRasterizationProbe& probe) noexcept;

class PlayerVideoRenderer final {
public:
    explicit PlayerVideoRenderer(std::function<void()> recoveryCallback = {}) noexcept;
    ~PlayerVideoRenderer();

    FFFResult SetWindow(HWND window) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits) noexcept;
    FFFResult ForceSdrOutputForSdrSource() noexcept;
    void ConfigureHdrStream(const AVCodecParameters* parameters) noexcept;
    FFFResult Render(const AVFrame* frame) noexcept;
    FFFResult Redraw() noexcept;
    FFFResult CreateD3D11HardwareDeviceContext(AVBufferRef** context) noexcept;
    FFFResult PresentTimedText() noexcept;
    FFFResult ReadPixel(FFF3FPVideoPixelProbe& probe) noexcept;
    FFFResult SetTimedTextLayer(TimedTextRenderLayer layer, TimedTextLayerSlot slot) noexcept;
    FFFResult GetTimedTextStatus(FFF3FPTimedTextStatus& status, TimedTextLayerSlot slot) noexcept;
    bool DeviceRecoveryRequested() const noexcept;
    bool RequestRecoveryIfDeviceLost() noexcept;
    FFFResult RecreateDeviceResources() noexcept;
    void ResetMedia() noexcept;
    void Close() noexcept;

    FFF3FPColorMode ActualColorMode() const noexcept;
    float SourcePeakNits() const noexcept;
    HdrFrameState HdrState() const noexcept;
    std::uint64_t PresentedVideoFrames() const noexcept;
    std::uint64_t CoalescedVideoFrames() const noexcept;
    std::uint64_t SwapChainPresents() const noexcept;
    std::uint64_t PresentWait100ns() const noexcept;
    std::uint64_t DeviceLockWait100ns() const noexcept;
    std::uint64_t SoftwareConvert100ns() const noexcept;
    std::uint32_t OutputBitDepth() const noexcept;
    FFF3FPVideoScalingMode ActualVideoScalingMode() const noexcept;
    std::string FallbackReason() const;
    std::string LastError() const;

private:
    struct TimedTextSprite {
        float atlasX = 0;
        float atlasY = 0;
        float padding = 0;
        float width = 0;
        float height = 0;
    };
    struct TimedTextSpriteInstance {
        float destination[4]{};
        float uv[4]{};
    };

    FFFResult EnsureDevice() noexcept;
    std::uint32_t PreferredOutputBitDepth(std::uint32_t sourceBitDepth, bool hdr) noexcept;
    FFFResult EnsureSwapChain(std::uint32_t width, std::uint32_t height,
        std::uint32_t sourceBitDepth) noexcept;
    FFFResult CreateSwapChain(std::uint32_t width, std::uint32_t height,
        bool hdr, std::uint32_t outputBits) noexcept;
    FFFResult ReconfigureSwapChain(bool hdr, std::uint32_t outputBits) noexcept;
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
        float width, float height) noexcept;
    FFFResult DrawWithVideoProcessor(ID3D11Texture2D* inputTexture,
        ID3D11Texture2D* outputTexture, const RECT& destination,
        std::uint32_t inputColorSpace, std::uint32_t outputColorSpace) noexcept;
    bool CanUseDirectVideoProcessor() const noexcept;
    void ReleaseVideoProcessor() noexcept;
    void ReleaseVideoProcessorInputSurface() noexcept;
    FFFResult AcquireBackBufferTarget(ID3D11Texture2D** buffer,
        ID3D11RenderTargetView** target) noexcept;
    struct CachedVideoSettings {
        std::uint32_t colorMode = 0, transfer = 0, source2020 = 0, reserved = 0;
        float sdrPeak = 100, hdrPeak = 100, paperWhite = 203, targetPeak = 1000;
        float sourceWidth = 0, sourceHeight = 0, outputWidth = 0, outputHeight = 0;
        std::uint32_t inputLayout = 0;
        float sampleScale = 1, yOffset = 0, yScale = 1;
        float cOffset = 0.5f, cScale = 1, kr = 0.2126f, kb = 0.0722f;
        float chromaOffsetX = 0, chromaOffsetY = 0, padding1 = 0, padding2 = 0;
    };
    FFFResult EnsureTimedTextResources(TimedTextLayerSlot slot) noexcept;
    FFFResult EnsureTimedTextAtlas(std::uint32_t size) noexcept;
    FFFResult EnsureTimedTextInstanceCapacity(std::size_t count) noexcept;
    FFFResult DrawCachedVideo(ID3D11RenderTargetView* target) noexcept;
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
    void SetError(std::string message) noexcept;

    HWND window_;
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    IDXGISwapChain4* swapChain_;
    ID3D11VertexShader* vertexShader_;
    ID3D11PixelShader* pixelShader_;
    ID3D11PixelShader* timedTextPixelShader_;
    ID3D11SamplerState* sampler_;
    ID3D11Buffer* constants_;
    ID3D11Texture2D* sourceTextures_[3];
    ID3D11ShaderResourceView* sourceViews_[3];
    ID3D11VideoDevice* videoDevice_;
    ID3D11VideoContext* videoContext_;
    ID3D11VideoProcessorEnumerator* videoProcessorEnumerator_;
    ID3D11VideoProcessor* videoProcessor_;
    ID3D11Texture2D* videoProcessorRenderTexture_;
    ID3D11RenderTargetView* videoProcessorRenderTarget_;
    ID3D11Texture2D* timedTextTextures_[3];
    ID3D11RenderTargetView* timedTextTargets_[3];
    ID3D11ShaderResourceView* timedTextViews_[3];
    ID3D11Query* timedTextPipelineQueries_[3];
    ID3D11BlendState* timedTextBlend_;
    ID3D11Texture2D* timedTextAtlasTexture_;
    ID3D11ShaderResourceView* timedTextAtlasView_;
    ID3D11VertexShader* timedTextSpriteVertexShader_;
    ID3D11PixelShader* timedTextSpritePixelShader_;
    ID3D11Buffer* timedTextSpriteInstanceBuffer_;
    ID3D11ShaderResourceView* timedTextSpriteInstanceView_;
    ID2D1Factory1* d2dFactory_;
    ID2D1Device* d2dDevice_;
    ID2D1DeviceContext* d2dContext_;
    ID2D1Bitmap1* d2dTargets_[3];
    ID2D1Bitmap1* d2dAtlasTarget_;
    IDWriteFactory* writeFactory_;
    IDWriteRenderingParams* timedTextRenderingParams_;
    SwsContext* scaler_;
    std::uint32_t swapWidth_;
    std::uint32_t swapHeight_;
    bool swapHdr_;
    std::uint32_t swapOutputBits_;
    std::uint32_t sourceWidth_;
    std::uint32_t sourceHeight_;
    std::uint32_t sourceInputLayout_;
    std::uint32_t sourceBitDepth_;
    std::uint32_t sourceChromaWidthShift_;
    std::uint32_t sourceChromaHeightShift_;
    bool sourceExternal_;
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
    std::atomic<FFF3FPVideoScalingMode> actualVideoScalingMode_;
    FFF3FPColorMode requestedMode_;
    FFF3FPColorMode actualMode_;
    float sdrPeakNits_;
    float hdrPeakNits_;
    float paperWhiteNits_;
    float sourcePeakNits_;
    HdrProcessor hdrProcessor_;
    std::vector<std::uint8_t> convertedRgb_;
    mutable std::mutex deviceMutex_;
    mutable std::mutex presentMutex_;
    mutable std::mutex timedTextMutex_;
    std::condition_variable timedTextCondition_;
    std::thread timedTextThread_;
    bool timedTextThreadStop_;
    bool timedTextThreadRunning_;
    std::uint64_t presentationGeneration_;
    float presentationFrameRate_;
    // The producer publishes an immutable layer and renderers retain a shared
    // snapshot. This keeps the timed-text mutex short without copying every
    // command and string again on the video/present thread.
    // Subtitle, danmaku and player information have independent producers and
    // render surfaces. Player information is always the topmost GPU layer.
    // Composite order is fixed to video -> danmaku -> subtitle -> information.
    std::shared_ptr<const TimedTextRenderLayer> timedTextLayers_[3];
    std::uint64_t timedTextRenderedSequences_[3];
    std::uint32_t timedTextRenderedCommandCounts_[3];
    bool timedTextRenderedHdrHighlights_[3];
    std::uint32_t timedTextWidths_[3];
    std::uint32_t timedTextHeights_[3];
    // Counts successful final swap-chain presents that included each visible
    // layer. A texture redraw is not a presentation and must not advance this.
    std::uint32_t timedTextPresentCounts_[3];
    std::uint64_t backBufferAcquisitionCount_;
    bool timedTextPipelineQueryInFlight_[3];
    std::uint64_t timedTextCompositePixelInvocations_[3];
    CachedVideoSettings cachedVideoSettings_;
    bool hasCachedVideo_;
    std::atomic<std::uint64_t> videoGeneration_;
    std::atomic<std::uint64_t> presentedVideoGeneration_;
    std::atomic<std::uint64_t> presentedVideoFrames_;
    std::atomic<std::uint64_t> coalescedVideoFrames_;
    std::atomic<std::uint64_t> swapChainPresents_;
    std::atomic<std::uint64_t> presentWait100ns_;
    std::atomic<std::uint64_t> deviceLockWait100ns_;
    std::atomic<std::uint64_t> softwareConvert100ns_;
    std::atomic<bool> deviceRecoveryRequested_;
    std::function<void()> recoveryCallback_;
    HMONITOR hdrMonitor_;
    bool hdrSupportValid_;
    bool hdrSupported_;
    // Bounded caches are keyed by the immutable command content contract.  The
    // UI only changes coordinates for scrolling danmaku, so rebuilding a text
    // layout and two brushes at 60 Hz is unnecessary.
    std::unordered_map<std::uint64_t, IDWriteTextLayout*> timedTextLayouts_;
    std::deque<std::uint64_t> timedTextLayoutOrder_;
    std::unordered_map<std::uint32_t, ID2D1SolidColorBrush*> timedTextBrushes_;
    std::unordered_map<std::uint64_t, TimedTextSprite> timedTextSprites_;
    std::vector<TimedTextSpriteInstance> timedTextSpriteInstances_;
    std::uint32_t timedTextAtlasX_;
    std::uint32_t timedTextAtlasY_;
    std::uint32_t timedTextAtlasRowHeight_;
    std::uint32_t timedTextAtlasSize_;
    std::uint32_t timedTextSpriteInstanceCapacity_;
    std::uint64_t timedTextSpriteCacheHits_;
    std::uint64_t timedTextSpriteCacheMisses_;
    mutable std::mutex errorMutex_;
    std::string fallbackReason_;
    std::string lastError_;
};
