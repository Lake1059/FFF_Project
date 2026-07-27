#pragma once

#include "3FP/Api/FFF.Player.Api.h"

#include <cstdint>
#include <mutex>
#include <string>
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
struct ID3D11ShaderResourceView;
struct ID3D11BlendState;
struct IDXGISwapChain4;
struct ID2D1Factory1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct ID2D1Bitmap1;
struct IDWriteFactory;

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
    std::wstring text;
    std::wstring fontFamily;
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
    std::vector<TimedTextRenderCommand> commands;
};

class PlayerVideoRenderer final {
public:
    PlayerVideoRenderer() noexcept;
    ~PlayerVideoRenderer();

    FFFResult SetWindow(HWND window) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits) noexcept;
    FFFResult Render(const AVFrame* frame) noexcept;
    FFFResult PresentTimedText() noexcept;
    FFFResult SetTimedTextLayer(TimedTextRenderLayer layer) noexcept;
    FFFResult GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept;
    void Close() noexcept;

    FFF3FPColorMode ActualColorMode() const noexcept;
    std::string FallbackReason() const;
    std::string LastError() const;

private:
    FFFResult EnsureDevice() noexcept;
    FFFResult EnsureSwapChain(std::uint32_t width, std::uint32_t height) noexcept;
    FFFResult EnsurePipeline(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
        std::uint32_t inputLayout, std::uint32_t bitDepth) noexcept;
    FFFResult EnsureVideoBaseResources() noexcept;
    FFFResult EnsureTimedTextResources() noexcept;
    FFFResult DrawTimedText() noexcept;
    void CompositeTimedText(ID3D11RenderTargetView* target) noexcept;
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
    ID3D11Texture2D* timedTextTexture_;
    ID3D11ShaderResourceView* timedTextView_;
    ID3D11BlendState* timedTextBlend_;
    ID2D1Factory1* d2dFactory_;
    ID2D1Device* d2dDevice_;
    ID2D1DeviceContext* d2dContext_;
    ID2D1Bitmap1* d2dTarget_;
    IDWriteFactory* writeFactory_;
    SwsContext* scaler_;
    std::uint32_t swapWidth_;
    std::uint32_t swapHeight_;
    bool swapHdr_;
    std::uint32_t sourceWidth_;
    std::uint32_t sourceHeight_;
    std::uint32_t sourceInputLayout_;
    std::uint32_t sourceBitDepth_;
    FFF3FPColorMode requestedMode_;
    FFF3FPColorMode actualMode_;
    float sdrPeakNits_;
    float hdrPeakNits_;
    float paperWhiteNits_;
    std::vector<std::uint8_t> rgba64_;
    mutable std::mutex deviceMutex_;
    mutable std::mutex timedTextMutex_;
    TimedTextRenderLayer timedTextLayer_;
    std::uint64_t timedTextRenderedSequence_;
    std::uint32_t timedTextRenderedCommandCount_;
    std::uint32_t timedTextWidth_;
    std::uint32_t timedTextHeight_;
    std::uint32_t timedTextPresentCount_;
    std::string fallbackReason_;
    std::string lastError_;
};
