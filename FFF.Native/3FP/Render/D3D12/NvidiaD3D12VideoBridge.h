#pragma once

#include <array>
#include <cstdint>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Bridges NVIDIA's D3D11-only video-processor extensions (RTX VSR and RTX
// Video HDR) into the D3D12 presentation path. The D3D12 renderer keeps its
// own D3D12VA decode and shared decode target; this class owns an independent
// D3D11 video device and a ring of shared D3D11 output surfaces.
class NvidiaD3D12VideoBridge final {
public:
    struct Request {
        bool vsrRequested = false;
        bool hdrRequested = false;
        // True only when the D3D12 renderer is actually presenting through its
        // scRGB/HDR chain (actualMode == MapToHdr && swapHdr).
        bool hdrOutputAvailable = false;
        bool sourceSdr = true;
        UINT sourceWidth = 0, sourceHeight = 0;
        UINT targetWidth = 0, targetHeight = 0;
        UINT rotation = 0;
    };

    struct Status {
        bool initialized = false;
        bool nvidiaAdapter = false;
        bool vsrSupported = false;
        bool hdrSupported = false;
        bool vsrActive = false;
        bool hdrActive = false;
        bool vsrYieldedToHdr = false;
        bool vsrNotUpscaling = false;
        bool vsrResolutionOutOfRange = false;
        bool vsrRejected = false;
        bool hdrRejected = false;
        bool hdrOutputAvailable = false;
        bool sourceSdr = true;
        HRESULT lastHr = S_OK;
        const char* lastStage = "";
        UINT sourceWidth = 0, sourceHeight = 0;
        UINT targetWidth = 0, targetHeight = 0;
        UINT outputWidth = 0, outputHeight = 0;
        DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
    };

    static constexpr UINT OutputCount = 3;

    NvidiaD3D12VideoBridge() = default;
    ~NvidiaD3D12VideoBridge();
    NvidiaD3D12VideoBridge(const NvidiaD3D12VideoBridge&) = delete;
    NvidiaD3D12VideoBridge& operator=(const NvidiaD3D12VideoBridge&) = delete;

    HRESULT Initialize(ID3D12Device* device, IDXGIAdapter* adapter,
        HANDLE sourceFenceHandle) noexcept;

    // Records the D3D11 waits and VideoProcessorBlt, then signals readyFence.
    // The caller must wait its D3D12 queue on readyFence/readyValue before
    // recording the draw that samples *output.
    HRESULT Process(ID3D12Resource* source, HANDLE sourceHandle,
        UINT64 sourceFenceValue, const Request& request, UINT64 readyValue,
        ID3D12Resource** output, UINT& outputSlot) noexcept;

    // Called after the D3D12 queue has consumed a bridge output slot.
    HRESULT SignalConsumed(ID3D12CommandQueue* queue, UINT outputSlot) noexcept;

    ID3D12Fence* ReadyFence() const noexcept { return readyFence_.Get(); }
    ID3D12Fence* ConsumedFence() const noexcept { return consumedFence_.Get(); }
    const Status& GetStatus() const noexcept { return status_; }
    void ResetFailures() noexcept;

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Output {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11VideoProcessorOutputView> view;
        ComPtr<ID3D12Resource> d3d12;
        UINT64 consumed = 0;
    };

    HRESULT CreateOutputs(DXGI_FORMAT format, UINT width, UINT height) noexcept;
    HRESULT CreateOutputViews() noexcept;
    void ReleaseOutputs() noexcept;
    HRESULT RebuildProcessor(ID3D11Texture2D* input, DXGI_FORMAT inputFormat,
        UINT sourceWidth, UINT sourceHeight, UINT outputWidth, UINT outputHeight,
        DXGI_FORMAT outputFormat, bool vsr, bool hdr) noexcept;
    void ReleaseProcessor() noexcept;
    HRESULT OpenSource(ComPtr<ID3D11Texture2D>& texture, HANDLE handle) noexcept;

    ComPtr<ID3D12Device> d3d12Device_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11Device1> device1_;
    ComPtr<ID3D11Device5> device5_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11DeviceContext4> context4_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;

    ComPtr<ID3D11Fence> sourceFence_;
    ComPtr<ID3D11Fence> readyFenceD3D11_;
    ComPtr<ID3D11Fence> consumedFenceD3D11_;
    ComPtr<ID3D12Fence> readyFence_;
    ComPtr<ID3D12Fence> consumedFence_;
    UINT64 consumedValue_ = 0;

    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> inputTexture_;
    ComPtr<ID3D11VideoProcessorInputView> inputView_;
    HANDLE inputHandle_ = nullptr;
    DXGI_FORMAT inputFormat_ = DXGI_FORMAT_UNKNOWN;
    UINT inputWidth_ = 0, inputHeight_ = 0;
    UINT outputWidth_ = 0, outputHeight_ = 0;
    DXGI_FORMAT outputFormat_ = DXGI_FORMAT_UNKNOWN;
    bool processorVsr_ = false;
    bool processorHdr_ = false;

    std::array<Output, OutputCount> outputs_;
    bool outputsValid_ = false;
    UINT outputWidthCreated_ = 0, outputHeightCreated_ = 0;
    DXGI_FORMAT outputFormatCreated_ = DXGI_FORMAT_UNKNOWN;
    UINT outputSlot_ = 0;

    bool vsrRejected_ = false;
    bool hdrRejected_ = false;
    Status status_{};
    bool initialized_ = false;
};
