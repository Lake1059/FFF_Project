#pragma once

#include "3FP/Render/VideoShaderSettings.h"
#include "3FP/Render/D3D12/NvidiaD3D12VideoBridge.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

struct AVFrame;
struct AVBufferRef;

struct FFF3FPD3D12Overlay {
    UINT index;
    D3D12_VIEWPORT viewport;
    std::array<std::uint32_t, 8> constants;
};

struct FFF3FPD3D12Picture {
    D3D12_VIEWPORT viewport{};
    VideoShaderSettings video{};
    DynamicShaderSettings dynamic{};
    ElCompositionSettings auxiliary{};
    UINT planeCount = 0;
    UINT scaleFilter = 1;
    std::uint32_t backdropTint = 0;
    float backdropRadius = 0;
    UINT backdropPasses = 0, backdropDownsample = 0;
    bool allowScale = true;
    bool coverBackdrop = false;
};

// Owns the native video, scale and composite resources. D3D11On12 is created
// lazily for Direct2D/DirectWrite; it does not participate in video rendering.
class D3D12VideoRenderer final {
public:
    static constexpr UINT BufferCount = 3;
    static constexpr UINT OverlayCount = 5;
    static constexpr UINT VideoPlaneCount = 3;
    static constexpr UINT ResourceCount = 8; // shader t0..t7

    struct UploadPlane {
        const void* data = nullptr;
        std::ptrdiff_t rowPitch = 0;
        UINT width = 0, height = 0, depth = 1;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };
    using Overlay = FFF3FPD3D12Overlay;
    using Picture = FFF3FPD3D12Picture;
    struct Stats {
        UINT64 draws = 0, scalePasses = 0, overlayDraws = 0;
        UINT64 scaleCpu100ns = 0, wait100ns = 0, gpu100ns = 0, gpuSamples = 0;
        UINT64 localBytes = 0, uploadBytes = 0, readbackBytes = 0;
        UINT64 scaleBudgetSkips = 0;
    };

    D3D12VideoRenderer() = default;
    ~D3D12VideoRenderer();
    D3D12VideoRenderer(const D3D12VideoRenderer&) = delete;
    D3D12VideoRenderer& operator=(const D3D12VideoRenderer&) = delete;
    HRESULT Initialize(IDXGIAdapter* adapter) noexcept;
    ID3D12Device* Device() const noexcept { return device_.Get(); }
    ID3D12CommandQueue* Queue() const noexcept { return queue_.Get(); }
    HRESULT DeviceStatus() const noexcept;
    HRESULT PrepareBuffers(IDXGISwapChain4* chain) noexcept;
    HRESULT ReleaseBuffers() noexcept;
    HRESULT ResetVideo() noexcept;
    bool CanImport(const AVFrame* frame) const noexcept;
    HRESULT UploadPicture(std::span<const UploadPlane, ResourceCount> planes,
        const AVFrame* decoded, UINT64& bytes) noexcept;
    HRESULT UpdateColorResources(std::span<const UploadPlane, ResourceCount> planes) noexcept;
    void InvalidateBackdrop() noexcept { backdropGeneration_ = 0; }
    HRESULT Submit(IDXGISwapChain4* chain, const Picture* picture,
        std::span<const Overlay> overlays) noexcept;
    HRESULT ReadPicture(UINT width, UINT height, DXGI_FORMAT format,
        const Picture* picture, std::span<const Overlay> overlays,
        const D3D12_BOX& region, void* destination, UINT64 capacity) noexcept;
    HRESULT ReadOverlayAlpha(UINT index, UINT64& visible) noexcept;
    HRESULT EnsureTextDevice() noexcept;
    ID3D11Device* TextDevice() const noexcept { return textDevice_.Get(); }
    ID3D11DeviceContext* TextContext() const noexcept { return textContext_.Get(); }
    HRESULT CreateOverlay(UINT index, UINT width, UINT height, DXGI_FORMAT format,
        ID3D11Texture2D** texture) noexcept;
    HRESULT BeginOverlay(UINT index, ID3D11Texture2D* texture) noexcept;
    HRESULT EndOverlay(UINT index, ID3D11Texture2D* texture) noexcept;
    void ReleaseOverlay(UINT index, ID3D11Texture2D* texture) noexcept;
    bool CanAllocateLocal(UINT64 bytes) const noexcept;
    Stats GetStats() noexcept;
    std::pair<UINT64, UINT64> ScaleStats() const noexcept { return {stats_.scalePasses, stats_.scaleCpu100ns}; }

    // NVIDIA VSR / TrueHDR request for the next frame. The actual state is
    // reported only after the bridge's VideoProcessorBlt succeeded.
    void SetNvidiaRequests(const NvidiaD3D12VideoBridge::Request& request) noexcept;
    const NvidiaD3D12VideoBridge::Status& NvidiaStatus() const noexcept { return nvidiaStatus_; }
    void ResetNvidiaFailures() noexcept;

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Texture {
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_DESC desc{};
        UINT64 bytes = 0;
        HANDLE sharedHandle = nullptr;
        bool sharedHeap = false;
    };
    struct Buffer {
        ComPtr<ID3D12Resource> resource;
        UINT64 size = 0, bytes = 0;
        std::uint8_t* mapped = nullptr;
    };
    struct Slot {
        ComPtr<ID3D12CommandAllocator> allocator;
        Buffer buffer;
        AVBufferRef* decoded = nullptr;
        UINT64 fence = 0;
        bool timestamp = false;
    };
    struct View {
        ID3D12Resource* resource = nullptr;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT plane = 0;
    };
    struct ScalePass {
        Texture texture;
        UINT axis = 0;
    };
    static constexpr UINT MaxScalePasses = 28;
    struct ScaleChain {
        std::array<ScalePass, MaxScalePasses> passes;
        UINT count = 0;
        UINT width = 0, height = 0, destWidth = 0, destHeight = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };
    static constexpr UINT ReadSlot = BufferCount;
    static constexpr UINT DrawSlots = BufferCount + 1;
    static constexpr UINT UploadSlots = 2;
    static constexpr UINT DescriptorsPerDraw = ResourceCount + OverlayCount +
        VideoPlaneCount * MaxScalePasses + ResourceCount + 3;
    static constexpr UINT RtvsPerDraw = VideoPlaneCount * MaxScalePasses + 5;

    HRESULT Wait(UINT64 value) noexcept;
    HRESULT Drain() noexcept;
    HRESULT DrainQueue() noexcept;
    void Reclaim() noexcept;
    HRESULT Begin(Slot& slot) noexcept;
    HRESULT Execute(Slot& slot) noexcept;
    HRESULT EnsureTexture(Texture& texture, const D3D12_RESOURCE_DESC& desc,
        D3D12_RESOURCE_STATES initial, bool shared = false) noexcept;
    void Release(Texture& texture) noexcept;
    HRESULT EnsureBuffer(Buffer& buffer, UINT64 size, D3D12_HEAP_TYPE type) noexcept;
    void Release(Buffer& buffer) noexcept;
    HRESULT Upload(std::span<const UploadPlane, ResourceCount> planes,
        const AVFrame* decoded, bool replacePicture, UINT64& bytes) noexcept;
    HRESULT PreparePipelines(DXGI_FORMAT format) noexcept;
    HRESULT CreatePipeline(ID3D12RootSignature* root, D3D12_SHADER_BYTECODE ps,
        DXGI_FORMAT format, bool blend, ID3D12PipelineState** pipeline) noexcept;
    HRESULT PrepareScale(const Picture& picture, std::array<View, VideoPlaneCount>& inputs,
        bool& runScale) noexcept;
    HRESULT TryNvidiaBridge(const Picture* picture, std::array<View, VideoPlaneCount>& source,
        Picture& bridgePicture, bool& active) noexcept;
    void RecordScale(UINT drawIndex, UINT& viewOffset, UINT& rtvOffset,
        const Picture& picture, std::array<View, VideoPlaneCount>& inputs, bool runScale) noexcept;
    HRESULT PrepareBackdrop(const Picture* picture) noexcept;
    void RecordBackdrop(UINT drawIndex, UINT& viewOffset, UINT& rtvOffset,
        const Picture& picture, UINT videoViews, D3D12_GPU_VIRTUAL_ADDRESS constants,
        D3D12_CPU_DESCRIPTOR_HANDLE output, UINT outputWidth, UINT outputHeight,
        DXGI_FORMAT outputFormat) noexcept;
    HRESULT RecordDraw(UINT index, ID3D12Resource* target, const Picture* picture,
        std::span<const Overlay> overlays, D3D12_RESOURCE_STATES initial,
        D3D12_RESOURCE_STATES final) noexcept;
    HRESULT ReadTexture(ID3D12Resource* image, const D3D12_BOX& region,
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) noexcept;
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) noexcept;
    void WriteView(UINT drawIndex, UINT offset, View view, bool texture3d = false) noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE ViewHandle(UINT index, UINT offset) const noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index, UINT offset, ID3D12Resource* resource) noexcept;
    void SetViewport(D3D12_VIEWPORT viewport, UINT width, UINT height) noexcept;

    ComPtr<IDXGIAdapter3> budgetAdapter_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12DescriptorHeap> views_, targets_;
    ComPtr<ID3D12RootSignature> pictureRoot_, overlayRoot_;
    std::array<ComPtr<ID3D12PipelineState>, 3> picturePipelines_, overlayPipelines_, backdropPipelines_;
    std::array<ComPtr<ID3D12PipelineState>, 4> scalePipelines_;
    ComPtr<ID3D12PipelineState> blurPipeline_;
    std::array<Slot, DrawSlots> draws_;
    std::array<Slot, UploadSlots> uploads_;
    std::array<ComPtr<ID3D12Resource>, BufferCount> backBuffers_;
    std::array<Texture, ResourceCount> textures_;
    Texture decodedTexture_, readTarget_;
    std::array<ScaleChain, VideoPlaneCount> scales_;
    std::array<Texture, 2> backdrop_;
    Buffer readback_;
    std::array<Texture, OverlayCount> overlays_;
    std::array<UINT64, OverlayCount> overlayFences_{};
    // At most one retired surface per slot; it is charged until its fence completes.
    std::array<Texture, OverlayCount> retiredOverlays_;
    std::array<UINT64, OverlayCount> retiredFences_{};
    ComPtr<ID3D11On12Device> on12_;
    ComPtr<ID3D11On12Device2> on12Checkout_;
    ComPtr<ID3D11Device> textDevice_;
    ComPtr<ID3D11DeviceContext> textContext_;
    ComPtr<ID3D12QueryHeap> timestamps_;
    Buffer timestampReadback_;
    UINT viewStride_ = 0, targetStride_ = 0, uploadIndex_ = 0;
    HANDLE event_ = nullptr;

    // Shared source/ready/consumed fences for the NVIDIA bridge. The source
    // fence is created with the renderer so every upload is visible to a
    // bridge that may be created later (first frame after the host toggles VSR).
    ComPtr<ID3D12Fence> nvidiaSourceFence_;
    HANDLE nvidiaSourceFenceHandle_ = nullptr;
    UINT64 nvidiaSourceFenceValue_ = 0;
    UINT64 nvidiaReadyValue_ = 0;
    std::unique_ptr<NvidiaD3D12VideoBridge> nvidiaBridge_;
    bool nvidiaBridgeInitAttempted_ = false;
    NvidiaD3D12VideoBridge::Request nvidiaRequest_{};
    NvidiaD3D12VideoBridge::Status nvidiaStatus_{};
    bool nvidiaDrawActive_ = false;
    UINT nvidiaOutputSlot_ = UINT_MAX;
    ID3D12Resource* nvidiaOutput_ = nullptr;

    UINT64 value_ = 0, timestampHz_ = 0, backBufferBytes_ = 0;
    UINT64 pictureGeneration_ = 0, scaledGeneration_ = 0, backdropGeneration_ = 0;
    UINT scaledFilter_ = 0, backdropPasses_ = 0, backdropColorMode_ = UINT_MAX;
    float backdropSigma_ = 0;
    bool decodedActive_ = false;
    HRESULT submissionError_ = S_OK;
    Stats stats_{};
};
