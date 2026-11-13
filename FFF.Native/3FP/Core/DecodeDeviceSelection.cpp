#include "pch.h"
#include "3FP/Core/DecodeDeviceSelection.h"
#include "3FP/Render/VideoRenderer.h"
extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

using Microsoft::WRL::ComPtr;

namespace {
ComPtr<IDXGIAdapter1> FindAdapter(const std::int32_t index) noexcept {
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        factory->EnumAdapters1(static_cast<UINT>(index), &adapter);
    return adapter;
}

int CudaOrdinal(IDXGIAdapter* const adapter) noexcept {
    const auto library = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return -1;
    using Init = int(__stdcall*)(unsigned int);
    using GetDevice = int(__stdcall*)(int*, IDXGIAdapter*);
    const auto init = reinterpret_cast<Init>(GetProcAddress(library, "cuInit"));
    const auto getDevice = reinterpret_cast<GetDevice>(GetProcAddress(library, "cuD3D11GetDevice"));
    int device = -1;
    if (!init || !getDevice || init(0) != 0 || getDevice(&device, adapter) != 0)
        device = -1;
    FreeLibrary(library);
    return device;
}

bool SameAdapter(ID3D11Device* device, IDXGIAdapter1* desired) noexcept {
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC actual{};
    DXGI_ADAPTER_DESC1 target{};
    return device && desired && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
        SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&actual)) &&
        SUCCEEDED(desired->GetDesc1(&target)) &&
        actual.AdapterLuid.HighPart == target.AdapterLuid.HighPart &&
        actual.AdapterLuid.LowPart == target.AdapterLuid.LowPart;
}

bool HardwareAdapter(const std::int32_t index, IDXGIFactory1* const factory) noexcept {
    if (index < 0 || factory == nullptr) return false;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(static_cast<UINT>(index), &adapter))) return false;
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(adapter->GetDesc1(&description))) return false;
    return (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
}
}

DecodeAdapterPlan BuildDecodeAdapterPlan(const std::int32_t preferredIndex,
    PlayerVideoRenderer& renderer, const bool multiAdapter) noexcept {
    DecodeAdapterPlan plan{};
    // Read-only: the host binds its output window after Open, and SetWindow does
    // not rebuild the device. Creating one here would pin the renderer to the
    // default adapter before the monitor match could run.
    plan.rendererIndex = renderer.DeviceAdapterIndex();
    const auto append = [&plan](const std::int32_t index) noexcept {
        for (std::size_t i = 0; i < plan.count; ++i)
            if (plan.indices[i] == index) return;
        if (plan.count >= DecodeAdapterPlan::Capacity) return;
        plan.indices[plan.count++] = index;
    };
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) factory.Reset();
    // A manual index that names a software adapter or no adapter at all cannot
    // carry a hardware decoder; drop it instead of spending a trial on WARP.
    const std::int32_t manual = preferredIndex >= 0 && factory.Get() != nullptr &&
        HardwareAdapter(preferredIndex, factory.Get()) ? preferredIndex : -1;
    if (manual >= 0) append(manual);
    // -1 is the playback GPU: the renderer's own D3D11 device for zero-copy
    // presentation, and FFmpeg's default CUDA device. Keep it even when the manual
    // index names that same adapter, because its CUDA ordinal lookup may fail while
    // the default CUDA device still works. append() deduplicates.
    append(-1);
    if (!multiAdapter || factory.Get() == nullptr) return plan;
    for (std::int32_t index = 0;
        index < static_cast<std::int32_t>(DecodeAdapterPlan::Capacity) &&
        plan.count < DecodeAdapterPlan::Capacity; ++index) {
        if (index == plan.rendererIndex || index == manual) continue;
        if (HardwareAdapter(index, factory.Get())) append(index);
    }
    return plan;
}

int CreateDecodeHardwareDevice(AVBufferRef** const output, const int deviceType,
    const std::int32_t adapterIndex, PlayerVideoRenderer& renderer) noexcept {
    *output = nullptr;
    const auto type = static_cast<AVHWDeviceType>(deviceType);
    if (type == AV_HWDEVICE_TYPE_D3D11VA) {
        const auto shared = renderer.CreateD3D11HardwareDeviceContext(output);
        if (adapterIndex < 0) return shared == FFFResult::Success ? 0 : AVERROR(ENODEV);
        const auto desired = FindAdapter(adapterIndex);
        if (!desired) { av_buffer_unref(output); return AVERROR(ENODEV); }
        if (*output) {
            const auto context = reinterpret_cast<AVHWDeviceContext*>((*output)->data);
            const auto d3d = static_cast<AVD3D11VADeviceContext*>(context->hwctx);
            if (SameAdapter(d3d->device, desired.Get())) return 0;
            av_buffer_unref(output);
        }
        const auto name = std::to_string(adapterIndex);
        const auto created = av_hwdevice_ctx_create(output, type, name.c_str(), nullptr, 0);
        if (created < 0) { av_buffer_unref(output); return created; }
        // FFmpeg resolves the device string through its own DXGI enumeration.
        // Confirm by LUID that the device it opened really is the requested
        // adapter: a factory ordering difference must not silently decode on a
        // GPU the caller did not ask for.
        const auto* context = reinterpret_cast<const AVHWDeviceContext*>((*output)->data);
        const auto* d3d = static_cast<const AVD3D11VADeviceContext*>(context->hwctx);
        if (d3d == nullptr || !SameAdapter(d3d->device, desired.Get())) {
            av_buffer_unref(output);
            return AVERROR(ENODEV);
        }
        return 0;
    }
    if (type == AV_HWDEVICE_TYPE_CUDA && adapterIndex >= 0) {
        const auto desired = FindAdapter(adapterIndex);
        const auto ordinal = desired ? CudaOrdinal(desired.Get()) : -1;
        if (ordinal < 0) return AVERROR(ENODEV);
        const auto name = std::to_string(ordinal);
        return av_hwdevice_ctx_create(output, type, name.c_str(), nullptr, 0);
    }
    return av_hwdevice_ctx_create(output, type, nullptr, nullptr, 0);
}

bool CanPresentHardwareFrameDirectly(const AVFrame* const frame,
    PlayerVideoRenderer& renderer) noexcept {
    if (!frame || frame->format != AV_PIX_FMT_D3D11 || !frame->data[0]) return false;
    return renderer.OwnsD3D11Texture(reinterpret_cast<ID3D11Texture2D*>(frame->data[0]));
}
