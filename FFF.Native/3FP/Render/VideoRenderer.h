#pragma once

#include "3FP/Api/FFF.Player.Api.h"

#include <cstdint>
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
struct IDXGISwapChain4;

class PlayerVideoRenderer final {
public:
    PlayerVideoRenderer() noexcept;
    ~PlayerVideoRenderer();

    FFFResult SetWindow(HWND window) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits) noexcept;
    FFFResult Render(const AVFrame* frame) noexcept;
    void Close() noexcept;

    FFF3FPColorMode ActualColorMode() const noexcept;
    std::string FallbackReason() const;
    std::string LastError() const;

private:
    FFFResult EnsureDevice() noexcept;
    FFFResult EnsureSwapChain(std::uint32_t width, std::uint32_t height) noexcept;
    FFFResult EnsurePipeline(std::uint32_t sourceWidth, std::uint32_t sourceHeight) noexcept;
    bool OutputSupportsHdr() noexcept;
    void SetHdrMetadata() noexcept;
    void ConvertPixels(const AVFrame* frame, std::uint32_t width, std::uint32_t height,
        bool hdrOutput) noexcept;
    void SetError(std::string message) noexcept;

    HWND window_;
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    IDXGISwapChain4* swapChain_;
    ID3D11VertexShader* vertexShader_;
    ID3D11PixelShader* pixelShader_;
    ID3D11SamplerState* sampler_;
    ID3D11Buffer* constants_;
    ID3D11Texture2D* sourceTexture_;
    ID3D11ShaderResourceView* sourceView_;
    SwsContext* scaler_;
    std::uint32_t swapWidth_;
    std::uint32_t swapHeight_;
    bool swapHdr_;
    std::uint32_t sourceWidth_;
    std::uint32_t sourceHeight_;
    FFF3FPColorMode requestedMode_;
    FFF3FPColorMode actualMode_;
    float sdrPeakNits_;
    float hdrPeakNits_;
    float paperWhiteNits_;
    std::vector<std::uint8_t> rgba64_;
    std::vector<std::uint8_t> output_;
    std::string fallbackReason_;
    std::string lastError_;
};
