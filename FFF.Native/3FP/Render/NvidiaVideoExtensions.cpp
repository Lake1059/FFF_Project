#include "pch.h"
#include "3FP/Render/NvidiaVideoExtensions.h"

namespace FFF3FP::NvidiaVideo {

std::uint32_t AdapterVendorId(ID3D11Device* const device) noexcept {
    // Deliberately does not take a reference on failure paths it cannot use:
    // every acquired interface is released before returning.
    if (device == nullptr) return 0;

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void**>(&dxgiDevice))) || dxgiDevice == nullptr)
        return 0;

    IDXGIAdapter* adapter = nullptr;
    const auto gotAdapter = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if (FAILED(gotAdapter) || adapter == nullptr) return 0;

    DXGI_ADAPTER_DESC description{};
    const auto described = adapter->GetDesc(&description);
    adapter->Release();
    return SUCCEEDED(described) ? description.VendorId : 0;
}

bool IsNvidiaAdapter(ID3D11Device* const device) noexcept {
    return AdapterVendorId(device) == NvidiaVendorId;
}

HRESULT SetSuperResolution(ID3D11VideoContext* const videoContext,
    ID3D11VideoProcessor* const videoProcessor, const bool enable) noexcept {
    if (videoContext == nullptr || videoProcessor == nullptr) return E_POINTER;
    // Non-const: the D3D11 signature takes void*, and this SDK version does not
    // accept a const pointer. The driver only reads it.
    SuperResolutionStreamExtension extension{
        StreamExtensionVersionV1,
        StreamExtensionMethodSuperResolution,
        enable ? 1u : 0u,
    };
    return videoContext->VideoProcessorSetStreamExtension(videoProcessor, 0,
        &PpeInterfaceGuid, sizeof(extension), &extension);
}

} // namespace FFF3FP::NvidiaVideo
