#include "pch.h"
#include "3FP/Render/D3D12/NvidiaD3D12VideoBridge.h"
#include "3FP/Render/NvidiaVideoExtensions.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")

namespace {
constexpr UINT MiB = 1024 * 1024;

bool EvenSize(UINT& width, UINT& height) noexcept {
    if (width == 0 || height == 0) return false;
    width = (width + 1u) & ~1u;
    height = (height + 1u) & ~1u;
    return true;
}

bool IsYuv420(const DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010;
}

D3D11_TEXTURE2D_DESC SharedTextureDesc(const UINT width, const UINT height,
    const DXGI_FORMAT format) noexcept {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    return desc;
}
}

NvidiaD3D12VideoBridge::~NvidiaD3D12VideoBridge() {
    if (context_) {
        context_->ClearState();
        context_->Flush();
    }
    ReleaseProcessor();
    ReleaseOutputs();
    initialized_ = false;
}

void NvidiaD3D12VideoBridge::ReleaseProcessor() noexcept {
    inputView_.Reset();
    for (auto& output : outputs_) output.view.Reset();
    processor_.Reset();
    enumerator_.Reset();
    inputTexture_.Reset();
    inputHandle_ = nullptr;
    inputFormat_ = DXGI_FORMAT_UNKNOWN;
    inputWidth_ = inputHeight_ = 0;
    outputWidth_ = outputHeight_ = 0;
    outputFormat_ = DXGI_FORMAT_UNKNOWN;
    processorVsr_ = processorHdr_ = false;
}

void NvidiaD3D12VideoBridge::ReleaseOutputs() noexcept {
    if (context_) context_->Flush();
    // A resized window can invalidate the ring while a previous draw still
    // samples it. Geometry changes are rare, so wait for each slot's consumer
    // fence here rather than risk releasing a live D3D12 resource.
    if (consumedFenceD3D11_) {
        const auto completed = consumedFenceD3D11_->GetCompletedValue();
        for (const auto& output : outputs_) {
            if (output.consumed != 0 && completed < output.consumed) {
                HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (event != nullptr) {
                    if (SUCCEEDED(consumedFenceD3D11_->SetEventOnCompletion(output.consumed, event)))
                        WaitForSingleObject(event, 2000);
                    CloseHandle(event);
                }
            }
        }
    }
    for (auto& output : outputs_) output = {};
    outputsValid_ = false;
    outputWidthCreated_ = outputHeightCreated_ = 0;
    outputFormatCreated_ = DXGI_FORMAT_UNKNOWN;
}

HRESULT NvidiaD3D12VideoBridge::Initialize(ID3D12Device* device, IDXGIAdapter* adapter,
    const HANDLE sourceFenceHandle) noexcept {
    if (initialized_) return S_OK;
    if (device == nullptr || adapter == nullptr || sourceFenceHandle == nullptr) return E_INVALIDARG;

    DXGI_ADAPTER_DESC adapterDescription{};
    if (FAILED(adapter->GetDesc(&adapterDescription)) ||
        adapterDescription.VendorId != FFF3FP::NvidiaVideo::NvidiaVendorId) {
        status_.lastStage = "not an NVIDIA adapter";
        return E_FAIL;
    }
    status_.nvidiaAdapter = true;

    d3d12Device_ = device;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr)) {
        status_.lastStage = "D3D11CreateDevice";
        status_.lastHr = hr;
        return hr;
    }
    if (FAILED(hr = device_.As(&device1_)) || FAILED(hr = context_.As(&context4_)) ||
        FAILED(hr = device_.As(&videoDevice_)) || FAILED(hr = context_.As(&videoContext_))) {
        status_.lastStage = "video interfaces";
        status_.lastHr = hr;
        return hr;
    }
    (void)device_.As(&device5_);
    if (!device5_) return E_NOINTERFACE;

    hr = device5_->OpenSharedFence(sourceFenceHandle, IID_PPV_ARGS(&sourceFence_));
    if (FAILED(hr)) {
        status_.lastStage = "OpenSharedFence(source)";
        status_.lastHr = hr;
        return hr;
    }

    // Ready/consumed fences live on the D3D12 side so the render queue can
    // wait/signal them directly; the D3D11 device only opens their handles.
    hr = d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&readyFence_));
    if (SUCCEEDED(hr))
        hr = d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&consumedFence_));
    if (FAILED(hr)) {
        status_.lastStage = "CreateFence(D3D12 shared)";
        status_.lastHr = hr;
        return hr;
    }
    HANDLE readyHandle = nullptr;
    HANDLE consumedHandle = nullptr;
    hr = d3d12Device_->CreateSharedHandle(readyFence_.Get(), nullptr, GENERIC_ALL, nullptr, &readyHandle);
    if (SUCCEEDED(hr))
        hr = d3d12Device_->CreateSharedHandle(consumedFence_.Get(), nullptr, GENERIC_ALL,
            nullptr, &consumedHandle);
    if (SUCCEEDED(hr))
        hr = device5_->OpenSharedFence(readyHandle, IID_PPV_ARGS(&readyFenceD3D11_));
    if (SUCCEEDED(hr))
        hr = device5_->OpenSharedFence(consumedHandle, IID_PPV_ARGS(&consumedFenceD3D11_));
    if (readyHandle != nullptr) CloseHandle(readyHandle);
    if (consumedHandle != nullptr) CloseHandle(consumedHandle);
    if (FAILED(hr)) {
        status_.lastStage = "OpenSharedFence(ready/consumed)";
        status_.lastHr = hr;
        return hr;
    }

    initialized_ = true;
    status_.initialized = true;
    status_.nvidiaAdapter = true;
    status_.lastStage = "initialized";
    status_.lastHr = S_OK;
    return S_OK;
}

HRESULT NvidiaD3D12VideoBridge::CreateOutputs(const DXGI_FORMAT format, const UINT width,
    const UINT height) noexcept {
    if (outputsValid_ && outputFormatCreated_ == format &&
        outputWidthCreated_ == width && outputHeightCreated_ == height) return S_OK;
    ReleaseOutputs();
    UINT outputWidth = width;
    UINT outputHeight = height;
    if (!EvenSize(outputWidth, outputHeight)) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc = SharedTextureDesc(outputWidth, outputHeight, format);
    for (auto& output : outputs_) {
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &output.texture))) return E_FAIL;
        ComPtr<IDXGIResource1> resource;
        if (FAILED(output.texture.As(&resource))) return E_FAIL;
        HANDLE handle = nullptr;
        if (FAILED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle))) return E_FAIL;
        const auto opened = d3d12Device_->OpenSharedHandle(handle, IID_PPV_ARGS(&output.d3d12));
        CloseHandle(handle);
        if (FAILED(opened)) return opened;
        output.consumed = 0;
    }
    outputsValid_ = true;
    outputWidthCreated_ = outputWidth;
    outputHeightCreated_ = outputHeight;
    outputFormatCreated_ = format;
    return S_OK;
}

HRESULT NvidiaD3D12VideoBridge::CreateOutputViews() noexcept {
    if (!outputsValid_ || !enumerator_ || !videoDevice_) return E_FAIL;
    for (auto& output : outputs_) {
        if (!output.texture) return E_FAIL;
        if (output.view) continue;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{};
        view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (FAILED(videoDevice_->CreateVideoProcessorOutputView(output.texture.Get(),
                enumerator_.Get(), &view, &output.view))) return E_FAIL;
    }
    return S_OK;
}

HRESULT NvidiaD3D12VideoBridge::OpenSource(ComPtr<ID3D11Texture2D>& texture,
    const HANDLE handle) noexcept {
    texture.Reset();
    if (handle == nullptr) return E_INVALIDARG;
    return device1_->OpenSharedResource1(handle, IID_PPV_ARGS(texture.ReleaseAndGetAddressOf()));
}

HRESULT NvidiaD3D12VideoBridge::RebuildProcessor(ID3D11Texture2D* input,
    const DXGI_FORMAT inputFormat, const UINT sourceWidth, const UINT sourceHeight,
    const UINT outputWidth, const UINT outputHeight, const DXGI_FORMAT outputFormat,
    const bool vsr, const bool hdr) noexcept {
    ReleaseProcessor();
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {60, 1};
    content.InputWidth = sourceWidth;
    content.InputHeight = sourceHeight;
    content.OutputFrameRate = content.InputFrameRate;
    content.OutputWidth = outputWidth;
    content.OutputHeight = outputHeight;
    content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
    if (FAILED(videoDevice_->CreateVideoProcessorEnumerator(&content, &enumerator_))) return E_FAIL;
    if (FAILED(videoDevice_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_))) return E_FAIL;
    UINT inputSupport = 0, outputSupport = 0;
    if (FAILED(enumerator_->CheckVideoProcessorFormat(inputFormat, &inputSupport)) ||
        FAILED(enumerator_->CheckVideoProcessorFormat(outputFormat, &outputSupport)) ||
        (inputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
        (outputSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) return E_FAIL;

    videoContext_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);
    const RECT source{0, 0, static_cast<LONG>(sourceWidth), static_cast<LONG>(sourceHeight)};
    videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &source);
    const RECT target{0, 0, static_cast<LONG>(outputWidth), static_cast<LONG>(outputHeight)};
    videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &target);

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDescription{};
    inputViewDescription.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    if (FAILED(videoDevice_->CreateVideoProcessorInputView(input, enumerator_.Get(),
            &inputViewDescription, &inputView_))) return E_FAIL;
    if (hdr) {
        if (!FFF3FP::NvidiaVideo::IsTrueHdrSupported(videoContext_.Get(), processor_.Get()))
            return E_FAIL;
        if (FAILED(FFF3FP::NvidiaVideo::SetTrueHdr(videoContext_.Get(), processor_.Get(), true)))
            return E_FAIL;
    }
    if (vsr && FAILED(FFF3FP::NvidiaVideo::SetSuperResolution(videoContext_.Get(),
            processor_.Get(), true))) return E_FAIL;

    inputTexture_ = input;
    inputFormat_ = inputFormat;
    inputWidth_ = sourceWidth;
    inputHeight_ = sourceHeight;
    outputWidth_ = outputWidth;
    outputHeight_ = outputHeight;
    outputFormat_ = outputFormat;
    processorVsr_ = vsr;
    processorHdr_ = hdr;
    return S_OK;
}

HRESULT NvidiaD3D12VideoBridge::Process(ID3D12Resource* source, const HANDLE sourceHandle,
    const UINT64 sourceFenceValue, const Request& request, const UINT64 readyValue,
    ID3D12Resource** output, UINT& outputSlot) noexcept {
    if (output == nullptr) return E_POINTER;
    *output = nullptr;
    outputSlot = UINT_MAX;
    status_.initialized = initialized_;
    status_.lastHr = S_OK;
    status_.lastStage = "";
    status_.vsrActive = status_.hdrActive = status_.vsrYieldedToHdr = false;
    status_.vsrNotUpscaling = status_.vsrResolutionOutOfRange = false;
    status_.vsrRejected = vsrRejected_;
    status_.hdrRejected = hdrRejected_;
    status_.sourceWidth = request.sourceWidth;
    status_.sourceHeight = request.sourceHeight;
    status_.targetWidth = request.targetWidth;
    status_.targetHeight = request.targetHeight;
    status_.hdrOutputAvailable = request.hdrOutputAvailable;
    status_.sourceSdr = request.sourceSdr;
    if (!initialized_) {
        status_.lastStage = "bridge unavailable";
        return S_FALSE;
    }
    if (source == nullptr || sourceHandle == nullptr) {
        status_.lastStage = "no shared source";
        return S_FALSE;
    }
    const auto sourceDesc = source->GetDesc();
    if (!IsYuv420(sourceDesc.Format)) {
        status_.lastStage = "source format is not NV12/P010";
        return S_FALSE;
    }
    UINT targetWidth = request.targetWidth;
    UINT targetHeight = request.targetHeight;
    if ((request.rotation & 1u) != 0) std::swap(targetWidth, targetHeight);
    if (!EvenSize(targetWidth, targetHeight)) {
        status_.lastStage = "invalid target size";
        return S_FALSE;
    }
    status_.targetWidth = targetWidth;
    status_.targetHeight = targetHeight;

    bool hdr = request.hdrRequested;
    if (hdr && !request.hdrOutputAvailable) {
        hdr = false;
        status_.lastStage = "HDR output path is not active";
    }
    if (hdr && !request.sourceSdr) {
        hdr = false;
        status_.lastStage = "TrueHDR requires an SDR source";
    }
    if (hdr && hdrRejected_) {
        hdr = false;
        status_.lastStage = "TrueHDR previously rejected";
    }
    bool vsr = request.vsrRequested;
    if (vsr && vsrRejected_) {
        vsr = false;
        status_.lastStage = "VSR previously rejected";
    }
    if (vsr && (request.sourceHeight < 360 || request.sourceHeight > 1440)) {
        vsr = false;
        status_.vsrResolutionOutOfRange = true;
        status_.lastStage = "VSR source resolution out of range";
    } else if (vsr && targetWidth <= request.sourceWidth && targetHeight <= request.sourceHeight) {
        vsr = false;
        status_.vsrNotUpscaling = true;
        status_.lastStage = "VSR target is not an upscale";
    }
    if (!vsr && !hdr) {
        status_.vsrActive = false;
        status_.hdrActive = false;
        return S_FALSE;
    }
    if (request.sourceWidth == 0 || request.sourceHeight == 0) return S_FALSE;

    const auto outputFormat = hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : sourceDesc.Format;
    auto hr = CreateOutputs(outputFormat, targetWidth, targetHeight);
    if (FAILED(hr)) {
        status_.lastHr = hr;
        status_.lastStage = "CreateOutputs";
        return hr;
    }

    const auto sameProcessor = processor_ && inputHandle_ == sourceHandle &&
        inputFormat_ == sourceDesc.Format && inputWidth_ == request.sourceWidth &&
        inputHeight_ == request.sourceHeight && outputWidth_ == targetWidth &&
        outputHeight_ == targetHeight && outputFormat_ == outputFormat &&
        processorVsr_ == vsr && processorHdr_ == hdr;
    if (!sameProcessor) {
        if (FAILED(hr = OpenSource(inputTexture_, sourceHandle))) {
            status_.lastHr = hr;
            status_.lastStage = "OpenSharedResource1(source)";
            return hr;
        }
        // RebuildProcessor() starts by releasing the cached input texture.
        // Keep a local reference alive across that reset: passing
        // inputTexture_.Get() directly leaves RebuildProcessor with a
        // dangling pointer and the driver crashes in its first format query.
        ComPtr<ID3D11Texture2D> sourceTexture = inputTexture_;
        if (FAILED(hr = RebuildProcessor(sourceTexture.Get(), sourceDesc.Format,
                request.sourceWidth, request.sourceHeight, targetWidth, targetHeight,
                outputFormat, vsr, hdr))) {
            status_.lastHr = hr;
            status_.lastStage = hdr ? "RebuildProcessor(TrueHDR)" : "RebuildProcessor(VSR)";
            if (hdr) { hdrRejected_ = true; status_.hdrRejected = true; }
            if (vsr) { vsrRejected_ = true; status_.vsrRejected = true; }
            ReleaseProcessor();
            return hr;
        }
        inputHandle_ = sourceHandle;
    }
    if (FAILED(hr = CreateOutputViews())) {
        status_.lastHr = hr;
        status_.lastStage = "CreateOutputViews";
        return hr;
    }

    const UINT slot = outputSlot_++ % OutputCount;
    outputSlot = slot;
    auto& destination = outputs_[slot];

    auto fenceHr = context4_->Wait(sourceFence_.Get(), sourceFenceValue);
    if (SUCCEEDED(fenceHr) && destination.consumed != 0)
        fenceHr = context4_->Wait(consumedFenceD3D11_.Get(), destination.consumed);
    if (FAILED(fenceHr)) {
        status_.lastHr = fenceHr;
        status_.lastStage = "D3D11 fence wait";
        return fenceHr;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.pInputSurface = inputView_.Get();
    const auto blitHr = videoContext_->VideoProcessorBlt(processor_.Get(), destination.view.Get(),
        0, 1, &stream);
    if (FAILED(blitHr)) {
        status_.lastHr = blitHr;
        status_.lastStage = "VideoProcessorBlt";
        if (hdr) { hdrRejected_ = true; status_.hdrRejected = true; }
        if (vsr) { vsrRejected_ = true; status_.vsrRejected = true; }
        return blitHr;
    }

    status_.lastHr = context4_->Signal(readyFenceD3D11_.Get(), readyValue);
    if (FAILED(status_.lastHr)) {
        status_.lastStage = "Signal(ready)";
        return status_.lastHr;
    }
    context_->Flush();
    status_.vsrActive = vsr && !hdr;
    status_.hdrActive = hdr;
    status_.vsrYieldedToHdr = vsr && hdr;
    status_.vsrSupported = status_.vsrSupported || vsr;
    status_.hdrSupported = status_.hdrSupported || hdr;
    status_.outputWidth = targetWidth;
    status_.outputHeight = targetHeight;
    status_.outputFormat = outputFormat;
    status_.lastStage = hdr ? "TrueHDR blit" : "VSR blit";
    *output = destination.d3d12.Get();
    return S_OK;
}

HRESULT NvidiaD3D12VideoBridge::SignalConsumed(ID3D12CommandQueue* queue,
    const UINT outputSlot) noexcept {
    if (!initialized_ || queue == nullptr || outputSlot >= OutputCount) return E_INVALIDARG;
    const auto value = ++consumedValue_;
    const auto hr = queue->Signal(consumedFence_.Get(), value);
    if (SUCCEEDED(hr)) outputs_[outputSlot].consumed = value;
    return hr;
}

void NvidiaD3D12VideoBridge::ResetFailures() noexcept {
    vsrRejected_ = false;
    hdrRejected_ = false;
    status_.lastStage = "";
    status_.lastHr = S_OK;
}
