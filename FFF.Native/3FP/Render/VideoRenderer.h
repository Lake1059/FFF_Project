#pragma once

#include "3FP/Api/FFF.Player.Api.h"

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct AVFrame;
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
};

FFFResult EvaluateVideoColorTransform(FFF3FPColorTransform& transform) noexcept;

class PlayerVideoRenderer final {
public:
    PlayerVideoRenderer() noexcept;
    ~PlayerVideoRenderer();

    FFFResult SetWindow(HWND window) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits) noexcept;
    FFFResult ForceSdrOutputForSdrSource() noexcept;
    FFFResult Render(const AVFrame* frame) noexcept;
    FFFResult PresentTimedText() noexcept;
    FFFResult SetTimedTextLayer(TimedTextRenderLayer layer, TimedTextLayerSlot slot) noexcept;
    FFFResult GetTimedTextStatus(FFF3FPTimedTextStatus& status, TimedTextLayerSlot slot) noexcept;
    void ResetMedia() noexcept;
    void Close() noexcept;

    FFF3FPColorMode ActualColorMode() const noexcept;
    float SourcePeakNits() const noexcept;
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
    FFFResult EnsureSwapChain(std::uint32_t width, std::uint32_t height) noexcept;
    FFFResult ReconfigureSwapChain(bool hdr) noexcept;
    FFFResult EnsurePipeline(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
        std::uint32_t inputLayout, std::uint32_t bitDepth,
        std::uint32_t chromaWidthShift, std::uint32_t chromaHeightShift) noexcept;
    FFFResult AcquireBackBufferTarget(ID3D11Texture2D** buffer,
        ID3D11RenderTargetView** target) noexcept;
    FFFResult EnsureVideoBaseResources() noexcept;
    FFFResult EnsureTimedTextResources() noexcept;
    FFFResult DrawTimedText(TimedTextLayerSlot slot) noexcept;
    void TimedTextThread() noexcept;
    void StopTimedTextThread() noexcept;
    void CompositeTimedText(ID3D11RenderTargetView* target, TimedTextLayerSlot slot) noexcept;
    void ReleaseTimedTextResources() noexcept;
    void ReleaseVideoBaseResources() noexcept;
    bool OutputSupportsHdr() noexcept;
    void SetHdrMetadata() noexcept;
    void ClearSurface() noexcept;
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
    ID3D11Texture2D* videoBaseTexture_;
    ID3D11RenderTargetView* videoBaseTarget_;
    ID3D11Texture2D* timedTextTextures_[2];
    ID3D11RenderTargetView* timedTextTargets_[2];
    ID3D11ShaderResourceView* timedTextViews_[2];
    ID3D11Query* timedTextPipelineQueries_[2];
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
    ID2D1Bitmap1* d2dTargets_[2];
    ID2D1Bitmap1* d2dAtlasTarget_;
    IDWriteFactory* writeFactory_;
    SwsContext* scaler_;
    std::uint32_t swapWidth_;
    std::uint32_t swapHeight_;
    bool swapHdr_;
    std::uint32_t sourceWidth_;
    std::uint32_t sourceHeight_;
    std::uint32_t sourceInputLayout_;
    std::uint32_t sourceBitDepth_;
    std::uint32_t sourceChromaWidthShift_;
    std::uint32_t sourceChromaHeightShift_;
    FFF3FPColorMode requestedMode_;
    FFF3FPColorMode actualMode_;
    float sdrPeakNits_;
    float hdrPeakNits_;
    float paperWhiteNits_;
    float sourcePeakNits_;
    std::vector<std::uint8_t> rgba64_;
    mutable std::mutex deviceMutex_;
    mutable std::mutex timedTextMutex_;
    std::condition_variable timedTextCondition_;
    std::thread timedTextThread_;
    bool timedTextThreadStop_;
    std::uint64_t presentationGeneration_;
    float presentationFrameRate_;
    // The producer publishes an immutable layer and renderers retain a shared
    // snapshot. This keeps the timed-text mutex short without copying every
    // command and string again on the video/present thread.
    // Subtitle and danmaku have independent producers and render surfaces.
    // Composite order is fixed to video -> danmaku -> subtitle.
    std::shared_ptr<const TimedTextRenderLayer> timedTextLayers_[2];
    std::uint64_t timedTextRenderedSequences_[2];
    std::uint32_t timedTextRenderedCommandCounts_[2];
    std::uint32_t timedTextWidths_[2];
    std::uint32_t timedTextHeights_[2];
    // Counts successful final swap-chain presents that included each visible
    // layer. A texture redraw is not a presentation and must not advance this.
    std::uint32_t timedTextPresentCounts_[2];
    std::uint64_t backBufferAcquisitionCount_;
    bool timedTextPipelineQueryInFlight_[2];
    std::uint64_t timedTextCompositePixelInvocations_[2];
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
    std::uint64_t timedTextSpriteCacheHits_;
    std::uint64_t timedTextSpriteCacheMisses_;
    mutable std::mutex errorMutex_;
    std::string fallbackReason_;
    std::string lastError_;
};
