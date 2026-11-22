#include "pch.h"
#include "3FP/Render/D3D12/D3D12VideoRenderer.h"
#include "3FP/Render/D3D12/BackdropBlurBytecode.h"
#include "3FP/Render/ShaderBytecode.h"

#include <bit>
#include <cmath>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_d3d12va.h>
}

#pragma comment(lib, "d3d12.lib")

namespace {
constexpr UINT64 MiB = 1024 * 1024;
constexpr UINT64 MaxCpuBuffers = 640 * MiB;
constexpr UINT64 RetainedReadbackBytes = 8 * MiB;
constexpr UINT ConstantStride = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

UINT64 Elapsed100ns(const std::chrono::steady_clock::time_point start) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count() / 100;
}

UINT OutputIndex(const DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: return 0;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 1;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 2;
    default: return UINT_MAX;
    }
}

D3D12_RESOURCE_DESC TextureDesc(const UINT width, const UINT height, const DXGI_FORMAT format,
    const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, const UINT depth = 1) noexcept {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = depth == 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE2D : D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(depth);
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    return desc;
}

D3D12_HEAP_PROPERTIES Heap(const D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    return heap;
}

}

D3D12VideoRenderer::~D3D12VideoRenderer() {
    if (textContext_) {
        textContext_->ClearState();
        textContext_->Flush();
    }
    (void)DrainQueue();
    for (auto& slot : uploads_) av_buffer_unref(&slot.decoded);
    if (event_ != nullptr) CloseHandle(event_);
}

HRESULT D3D12VideoRenderer::Initialize(IDXGIAdapter* adapter) noexcept {
    auto hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) return hr;
    if (adapter != nullptr) (void)adapter->QueryInterface(IID_PPV_ARGS(&budgetAdapter_));
    D3D12_COMMAND_QUEUE_DESC queue{};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(hr = device_->CreateCommandQueue(&queue, IID_PPV_ARGS(&queue_))) ||
        FAILED(hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_))))
        return hr;
    queue_->SetName(L"3FP D3D12 graphics queue");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) return HRESULT_FROM_WIN32(GetLastError());
    const auto allocator = [this](Slot& slot) noexcept {
        return device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator));
    };
    for (auto& slot : draws_) if (FAILED(hr = allocator(slot))) return hr;
    for (auto& slot : uploads_) if (FAILED(hr = allocator(slot))) return hr;
    if (FAILED(hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            draws_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&commands_))) ||
        FAILED(hr = commands_->Close())) return hr;

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = DrawSlots * DescriptorsPerDraw;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(hr = device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&views_)))) return hr;
    viewStride_ = device_->GetDescriptorHandleIncrementSize(heap.Type);
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = DrawSlots * RtvsPerDraw;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(hr = device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&targets_)))) return hr;
    targetStride_ = device_->GetDescriptorHandleIncrementSize(heap.Type);

    // Opt-in sampling avoids timestamp query traffic during ordinary playback.
    wchar_t timing[2]{};
    if (GetEnvironmentVariableW(L"FFF3FP_D3D12_GPU_TIMING", timing, ARRAYSIZE(timing)) == 1 &&
        timing[0] == L'1' && SUCCEEDED(queue_->GetTimestampFrequency(&timestampHz_))) {
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = BufferCount * 2;
        if (FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_))) ||
            FAILED(EnsureBuffer(timestampReadback_, query.Count * sizeof(UINT64), D3D12_HEAP_TYPE_READBACK)))
            timestamps_.Reset();
    }
    return S_OK;
}

HRESULT D3D12VideoRenderer::DeviceStatus() const noexcept {
    return device_ ? device_->GetDeviceRemovedReason() : S_OK;
}

HRESULT D3D12VideoRenderer::Wait(const UINT64 value) noexcept {
    if (FAILED(submissionError_)) return submissionError_;
    if (value == 0) return S_OK;
    const auto completed = fence_->GetCompletedValue();
    if (completed == UINT64_MAX) return DXGI_ERROR_DEVICE_REMOVED;
    if (completed >= value) return S_OK;
    const auto start = std::chrono::steady_clock::now();
    const auto hr = fence_->SetEventOnCompletion(value, event_);
    if (FAILED(hr)) return hr;
    const auto wait = WaitForSingleObject(event_, 2000);
    stats_.wait100ns += Elapsed100ns(start);
    if (wait == WAIT_OBJECT_0 && fence_->GetCompletedValue() >= value) return DeviceStatus();
    return wait == WAIT_FAILED ? HRESULT_FROM_WIN32(GetLastError()) : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
}

void D3D12VideoRenderer::Reclaim() noexcept {
    if (!fence_) return;
    const auto completed = fence_->GetCompletedValue();
    for (auto& slot : uploads_) if (slot.fence <= completed) av_buffer_unref(&slot.decoded);
    for (UINT index = 0; index < OverlayCount; ++index)
        if (retiredFences_[index] <= completed) Release(retiredOverlays_[index]);
    if (!timestamps_ || completed == UINT64_MAX) return;
    for (UINT index = 0; index < BufferCount; ++index) {
        auto& slot = draws_[index];
        if (slot.timestamp && slot.fence <= completed) {
            const auto offset = index * 2 * sizeof(UINT64);
            D3D12_RANGE range{offset, offset + 2 * sizeof(UINT64)};
            void* mapped = nullptr;
            if (SUCCEEDED(timestampReadback_.resource->Map(0, &range, &mapped))) {
                const auto* ticks = static_cast<const UINT64*>(mapped) + index * 2;
                if (ticks[1] >= ticks[0] && timestampHz_ > 0) {
                    stats_.gpu100ns += static_cast<UINT64>(
                        static_cast<double>(ticks[1] - ticks[0]) * 1e7 / timestampHz_);
                    ++stats_.gpuSamples;
                }
                D3D12_RANGE noWrite{};
                timestampReadback_.resource->Unmap(0, &noWrite);
            }
            slot.timestamp = false;
        }
    }
}

HRESULT D3D12VideoRenderer::Drain() noexcept {
    const auto hr = Wait(value_);
    if (SUCCEEDED(hr)) Reclaim();
    return hr;
}

HRESULT D3D12VideoRenderer::DrainQueue() noexcept {
    // The fence attached to a command list precedes DXGI Present, and a
    // D3D11On12 flush can also submit work outside our command lists. A new
    // queue-tail signal is required before ResizeBuffers or device teardown.
    if (!queue_ || !fence_ || !event_) return S_OK;
    if (FAILED(submissionError_)) return submissionError_;
    submissionError_ = queue_->Signal(fence_.Get(), value_ + 1);
    if (FAILED(submissionError_)) return submissionError_;
    ++value_;
    return Drain();
}

HRESULT D3D12VideoRenderer::Begin(Slot& slot) noexcept {
    auto hr = Wait(slot.fence);
    if (FAILED(hr)) return hr;
    Reclaim();
    hr = slot.allocator->Reset();
    return FAILED(hr) ? hr : commands_->Reset(slot.allocator.Get(), nullptr);
}

HRESULT D3D12VideoRenderer::Execute(Slot& slot) noexcept {
    const auto close = commands_->Close();
    if (FAILED(close)) return close;
    ID3D12CommandList* lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    submissionError_ = queue_->Signal(fence_.Get(), value_ + 1);
    if (SUCCEEDED(submissionError_)) slot.fence = ++value_;
    return submissionError_;
}

bool D3D12VideoRenderer::CanAllocateLocal(const UINT64 bytes) const noexcept {
    if (bytes == UINT64_MAX || bytes > UINT64_MAX - stats_.localBytes - backBufferBytes_) return false;
    if (!budgetAdapter_) return true;
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (FAILED(budgetAdapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) ||
        memory.Budget == 0) return true;
    const auto reserve = std::clamp(memory.Budget / 8, 64 * MiB, 512 * MiB);
    const auto used = std::max(memory.CurrentUsage, stats_.localBytes + backBufferBytes_);
    return memory.Budget > reserve && used <= memory.Budget - reserve &&
        bytes <= memory.Budget - reserve - used;
}

HRESULT D3D12VideoRenderer::EnsureTexture(Texture& texture, const D3D12_RESOURCE_DESC& desc,
    const D3D12_RESOURCE_STATES initial) noexcept {
    const auto& current = texture.desc;
    if (texture.resource && desc.Width == current.Width && desc.Height == current.Height &&
        desc.DepthOrArraySize == current.DepthOrArraySize && desc.Format == current.Format &&
        desc.Flags == current.Flags) return S_OK;
    if (texture.resource) {
        const auto hr = Drain();
        if (FAILED(hr)) return hr;
        Release(texture);
    }
    const auto bytes = device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    if (!CanAllocateLocal(bytes)) return E_OUTOFMEMORY;
    const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        initial, nullptr, IID_PPV_ARGS(&texture.resource));
    if (SUCCEEDED(hr)) {
        texture.desc = desc;
        texture.bytes = bytes;
        stats_.localBytes += bytes;
        texture.resource->SetName(L"3FP D3D12 owned texture");
    }
    return hr;
}

void D3D12VideoRenderer::Release(Texture& texture) noexcept {
    stats_.localBytes -= texture.bytes;
    texture = {};
}

HRESULT D3D12VideoRenderer::EnsureBuffer(Buffer& buffer, const UINT64 size,
    const D3D12_HEAP_TYPE type) noexcept {
    if (buffer.size >= size) return S_OK;
    Release(buffer);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const auto bytes = device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    if (bytes > MaxCpuBuffers || stats_.uploadBytes + stats_.readbackBytes > MaxCpuBuffers - bytes)
        return E_OUTOFMEMORY;
    const auto heap = Heap(type);
    auto hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&buffer.resource));
    if (FAILED(hr)) return hr;
    if (type == D3D12_HEAP_TYPE_UPLOAD) {
        D3D12_RANGE noRead{};
        void* mapped = nullptr;
        hr = buffer.resource->Map(0, &noRead, &mapped);
        if (FAILED(hr)) { buffer.resource.Reset(); return hr; }
        buffer.mapped = static_cast<std::uint8_t*>(mapped);
        stats_.uploadBytes += bytes;
    } else {
        stats_.readbackBytes += bytes;
    }
    buffer.size = size;
    buffer.bytes = bytes;
    buffer.resource->SetName(type == D3D12_HEAP_TYPE_UPLOAD ? L"3FP D3D12 upload buffer" :
        L"3FP D3D12 readback buffer");
    return S_OK;
}

void D3D12VideoRenderer::Release(Buffer& buffer) noexcept {
    if (buffer.mapped != nullptr) {
        buffer.resource->Unmap(0, nullptr);
        stats_.uploadBytes -= buffer.bytes;
    } else {
        stats_.readbackBytes -= buffer.bytes;
    }
    buffer = {};
}

D3D12VideoRenderer::Stats D3D12VideoRenderer::GetStats() noexcept {
    Reclaim();
    auto result = stats_;
    result.localBytes += backBufferBytes_;
    return result;
}

HRESULT D3D12VideoRenderer::PrepareBuffers(IDXGISwapChain4* chain) noexcept {
    auto hr = S_OK;
    backBufferBytes_ = 0;
    for (UINT index = 0; index < BufferCount; ++index) {
        auto& buffer = backBuffers_[index];
        hr = chain->GetBuffer(index, IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) return hr;
        buffer->SetName(L"3FP D3D12 swap-chain buffer");
        auto desc = buffer->GetDesc();
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const auto bytes = device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        if (bytes != UINT64_MAX) backBufferBytes_ += bytes;
    }
    return PreparePipelines(backBuffers_[0]->GetDesc().Format);
}

HRESULT D3D12VideoRenderer::ReleaseBuffers() noexcept {
    if (textContext_) textContext_->Flush();
    const auto hr = DrainQueue();
    if (FAILED(hr)) return hr;
    backBuffers_ = {};
    backBufferBytes_ = 0;
    Release(readTarget_);
    Release(readback_);
    return S_OK;
}

HRESULT D3D12VideoRenderer::ResetVideo() noexcept {
    const auto hr = Drain();
    if (FAILED(hr)) return hr;
    for (auto& texture : textures_) Release(texture);
    Release(decodedTexture_);
    Release(readTarget_);
    Release(readback_);
    for (auto& slot : uploads_) Release(slot.buffer);
    for (auto& scale : scales_) {
        for (auto& pass : scale.passes) Release(pass.texture);
        scale = {};
    }
    for (auto& texture : backdrop_) Release(texture);
    ++pictureGeneration_;
    scaledGeneration_ = backdropGeneration_ = 0;
    decodedActive_ = false;
    stats_.draws = stats_.scalePasses = stats_.overlayDraws = stats_.scaleBudgetSkips = 0;
    stats_.wait100ns = stats_.gpu100ns = stats_.gpuSamples = stats_.scaleCpu100ns = 0;
    return S_OK;
}

void D3D12VideoRenderer::Transition(ID3D12Resource* resource, const D3D12_RESOURCE_STATES before,
    const D3D12_RESOURCE_STATES after) noexcept {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands_->ResourceBarrier(1, &barrier);
}

bool D3D12VideoRenderer::CanImport(const AVFrame* frame) const noexcept {
    if (!frame || frame->format != AV_PIX_FMT_D3D12 || !frame->data[0] || !frame->buf[0]) return false;
    const auto* decoded = reinterpret_cast<const AVD3D12VAFrame*>(frame->data[0]);
    if (!decoded->texture || !decoded->sync_ctx.fence || !frame->hw_frames_ctx) return false;
    ComPtr<ID3D12Device> owner;
    if (FAILED(decoded->texture->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != device_.Get())
        return false;
    const auto desc = decoded->texture->GetDesc();
    return decoded->subresource_index >= 0 && decoded->subresource_index < desc.DepthOrArraySize &&
        (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0 &&
        desc.MipLevels == 1 && (desc.Format == DXGI_FORMAT_NV12 || desc.Format == DXGI_FORMAT_P010) &&
        static_cast<UINT>(frame->width) <= desc.Width && static_cast<UINT>(frame->height) <= desc.Height;
}

HRESULT D3D12VideoRenderer::UploadPicture(std::span<const UploadPlane, ResourceCount> planes,
    const AVFrame* decoded, UINT64& bytes) noexcept {
    return Upload(planes, decoded, true, bytes);
}

HRESULT D3D12VideoRenderer::UpdateColorResources(std::span<const UploadPlane, ResourceCount> planes) noexcept {
    UINT64 bytes = 0;
    return Upload(planes, nullptr, false, bytes);
}

HRESULT D3D12VideoRenderer::Upload(std::span<const UploadPlane, ResourceCount> planes,
    const AVFrame* decoded, const bool replacePicture, UINT64& bytes) noexcept {
    bytes = 0;
    auto& slot = uploads_[uploadIndex_];
    auto hr = Wait(slot.fence);
    if (FAILED(hr)) return hr;
    Reclaim();
    if (replacePicture) {
        // Keep exactly one source representation resident. This only drains
        // on a source-layout change (e.g. hardware decoder fallback).
        bool retire = !decoded && decodedTexture_.resource;
        for (UINT i = 0; i < VideoPlaneCount; ++i)
            retire = retire || (!planes[i].data && textures_[i].resource);
        if (retire) {
            if (FAILED(hr = Drain())) return hr;
            if (!decoded) Release(decodedTexture_);
            for (UINT i = 0; i < VideoPlaneCount; ++i)
                if (!planes[i].data) Release(textures_[i]);
        }
    }
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, ResourceCount> footprints{};
    std::array<UINT, ResourceCount> rows{};
    std::array<UINT64, ResourceCount> rowBytes{};
    UINT64 end = 0;
    for (UINT i = 0; i < ResourceCount; ++i) {
        const auto& plane = planes[i];
        if (!plane.data) continue;
        if (!plane.width || !plane.height || !plane.depth || plane.depth > 2048) return E_INVALIDARG;
        const auto desc = TextureDesc(plane.width, plane.height, plane.format,
            D3D12_RESOURCE_FLAG_NONE, plane.depth);
        const auto offset = (end + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
            ~(static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) - 1);
        UINT64 size = 0;
        device_->GetCopyableFootprints(&desc, 0, 1, offset, &footprints[i], &rows[i], &rowBytes[i], &size);
        if (size > MaxCpuBuffers || offset > MaxCpuBuffers - size ||
            plane.rowPitch < -PTRDIFF_MAX ||
            static_cast<UINT64>(plane.rowPitch < 0 ? -plane.rowPitch : plane.rowPitch) < rowBytes[i])
            return E_INVALIDARG;
        end = offset + size;
        hr = EnsureTexture(textures_[i], desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (FAILED(hr)) return hr;
    }

    const AVD3D12VAFrame* surface = nullptr;
    UINT copyWidth = 0, copyHeight = 0;
    if (decoded) {
        if (!CanImport(decoded)) return DXGI_ERROR_UNSUPPORTED;
        surface = reinterpret_cast<const AVD3D12VAFrame*>(decoded->data[0]);
        const auto source = surface->texture->GetDesc();
        copyWidth = (static_cast<UINT>(decoded->width) + 1u) & ~1u;
        copyHeight = (static_cast<UINT>(decoded->height) + 1u) & ~1u;
        if (copyWidth > source.Width || copyHeight > source.Height) return E_INVALIDARG;
        hr = EnsureTexture(decodedTexture_, TextureDesc(copyWidth, copyHeight, source.Format),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (FAILED(hr)) return hr;
        // The video decoder can still reference this picture while the copy queue
        // reads it. Its allocation enables simultaneous access. Do not change
        // the decoder surface's state; use COMMON read promotion on our queue.
        slot.decoded = av_buffer_ref(decoded->buf[0]);
        if (!slot.decoded) return E_OUTOFMEMORY;
    }
    if (end == 0 && !surface) return replacePicture ? E_INVALIDARG : S_OK;
    // A 4:4:4/float still can be larger than the two-slot budget. Reuse one slot
    // rather than allocating an equally large second staging buffer on redraw.
    if (end > MaxCpuBuffers / UploadSlots) {
        auto& other = uploads_[1u - uploadIndex_];
        hr = Wait(other.fence);
        if (FAILED(hr)) { av_buffer_unref(&slot.decoded); return hr; }
        Release(other.buffer);
    }
    if (slot.buffer.size > std::max(16 * MiB, end * 2)) Release(slot.buffer);
    if (end && FAILED(hr = EnsureBuffer(slot.buffer, end, D3D12_HEAP_TYPE_UPLOAD))) {
        av_buffer_unref(&slot.decoded);
        return hr;
    }
    // Begin() reclaims finished imports. Attach the frame ref only afterwards.
    auto* lease = std::exchange(slot.decoded, nullptr);
    hr = Begin(slot);
    if (FAILED(hr)) { av_buffer_unref(&lease); return hr; }

    for (UINT i = 0; i < ResourceCount; ++i) {
        const auto& plane = planes[i];
        if (!plane.data) continue;
        const auto& fp = footprints[i];
        auto* out = slot.buffer.mapped + fp.Offset;
        const auto* in = static_cast<const std::uint8_t*>(plane.data);
        for (UINT z = 0; z < plane.depth; ++z) {
            for (UINT y = 0; y < rows[i]; ++y) {
                const auto row = static_cast<std::ptrdiff_t>(z) * rows[i] + y;
                std::memcpy(out + row * fp.Footprint.RowPitch, in + row * plane.rowPitch,
                    static_cast<std::size_t>(rowBytes[i]));
            }
        }
        auto* resource = textures_[i].resource.Get();
        Transition(resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = slot.buffer.resource.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = resource;
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands_->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        Transition(resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (i < VideoPlaneCount) bytes += rowBytes[i] * rows[i] * plane.depth;
    }
    if (surface) {
        const auto src = surface->texture->GetDesc();
        auto* dest = decodedTexture_.resource.Get();
        Transition(dest, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        for (UINT plane = 0; plane < 2; ++plane) {
            D3D12_TEXTURE_COPY_LOCATION from{};
            from.pResource = surface->texture;
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.SubresourceIndex = surface->subresource_index + plane * src.DepthOrArraySize;
            D3D12_TEXTURE_COPY_LOCATION to{};
            to.pResource = dest;
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.SubresourceIndex = plane;
            const D3D12_BOX crop{0, 0, 0, copyWidth >> plane, copyHeight >> plane, 1};
            commands_->CopyTextureRegion(&to, 0, 0, 0, &from, &crop);
        }
        Transition(dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        bytes = static_cast<UINT64>(copyWidth) * copyHeight * (src.Format == DXGI_FORMAT_NV12 ? 3u : 6u) / 2;
        if (surface->sync_ctx.fence->GetCompletedValue() < surface->sync_ctx.fence_value)
            hr = queue_->Wait(surface->sync_ctx.fence, surface->sync_ctx.fence_value);
    }
    if (FAILED(hr)) {
        (void)commands_->Close();
        av_buffer_unref(&lease);
        return hr;
    }
    slot.decoded = lease;
    hr = Execute(slot);
    if (FAILED(hr)) return hr;
    if (replacePicture) {
        decodedActive_ = decoded != nullptr;
        ++pictureGeneration_;
    } else {
        backdropGeneration_ = 0;
    }
    if (end <= MaxCpuBuffers / UploadSlots) uploadIndex_ = 1u - uploadIndex_;
    return S_OK;
}

HRESULT D3D12VideoRenderer::CreatePipeline(ID3D12RootSignature* root, const D3D12_SHADER_BYTECODE ps,
    const DXGI_FORMAT format, const bool blend, ID3D12PipelineState** pipeline) noexcept {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root;
    desc.VS = {FFFVertexShaderBytecode, sizeof(FFFVertexShaderBytecode)};
    desc.PS = ps;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = format;
    desc.SampleDesc.Count = 1;
    auto& output = desc.BlendState.RenderTarget[0];
    output.BlendEnable = blend;
    output.SrcBlend = output.SrcBlendAlpha = D3D12_BLEND_ONE;
    output.DestBlend = output.DestBlendAlpha = blend ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_ZERO;
    output.BlendOp = output.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    output.LogicOp = D3D12_LOGIC_OP_NOOP;
    output.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    return device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pipeline));
}

HRESULT D3D12VideoRenderer::PreparePipelines(const DXGI_FORMAT format) noexcept {
    const auto index = OutputIndex(format);
    if (index == UINT_MAX) return DXGI_ERROR_UNSUPPORTED;
    auto hr = S_OK;
    if (!pictureRoot_ || !overlayRoot_) {
        D3D12_STATIC_SAMPLER_DESC sampler[3]{};
        for (UINT s = 0; s < ARRAYSIZE(sampler); ++s) {
            auto& item = sampler[s];
            item.Filter = s == 1 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            item.AddressU = s == 2 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            item.AddressV = item.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            item.ShaderRegister = s;
            item.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            item.MaxLOD = D3D12_FLOAT32_MAX;
            item.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ResourceCount, 0, 0, 0};
        D3D12_ROOT_PARAMETER params[4]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {1, &range};
        for (UINT i = 1; i < ARRAYSIZE(params); ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[i].Descriptor.ShaderRegister = i == 1 ? 0 : i;
        }
        for (auto& param : params) param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC layout{};
        layout.NumParameters = ARRAYSIZE(params);
        layout.pParameters = params;
        layout.NumStaticSamplers = ARRAYSIZE(sampler);
        layout.pStaticSamplers = sampler;
        const auto create = [this, &layout](ID3D12RootSignature** out) noexcept {
            ComPtr<ID3DBlob> serial;
            const auto hr = D3D12SerializeRootSignature(&layout, D3D_ROOT_SIGNATURE_VERSION_1, &serial, nullptr);
            if (FAILED(hr)) return hr;
            return device_->CreateRootSignature(0, serial->GetBufferPointer(), serial->GetBufferSize(), IID_PPV_ARGS(out));
        };
        if (!pictureRoot_ && FAILED(hr = create(&pictureRoot_))) return hr;
        layout.NumParameters = 2;
        layout.NumStaticSamplers = 1;
        range.NumDescriptors = 1;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {0, 0, 12};
        if (!overlayRoot_ && FAILED(hr = create(&overlayRoot_))) return hr;
    }
    if (!picturePipelines_[index] && FAILED(hr = CreatePipeline(pictureRoot_.Get(),
            {FFFPixelShaderBytecode, sizeof(FFFPixelShaderBytecode)}, format, false,
            &picturePipelines_[index]))) return hr;
    if (!overlayPipelines_[index] && FAILED(hr = CreatePipeline(overlayRoot_.Get(),
            {FFFTimedTextPixelShaderBytecode, sizeof(FFFTimedTextPixelShaderBytecode)}, format,
            true, &overlayPipelines_[index]))) return hr;
    return S_OK;
}

void D3D12VideoRenderer::WriteView(const UINT drawIndex, const UINT offset, View input,
    const bool texture3d) noexcept {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = input.format;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (texture3d) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipLevels = 1;
    } else {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        desc.Texture2D.PlaneSlice = input.plane;
    }
    auto handle = views_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(drawIndex * DescriptorsPerDraw + offset) * viewStride_;
    device_->CreateShaderResourceView(input.resource, &desc, handle);
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12VideoRenderer::ViewHandle(const UINT index, const UINT offset) const noexcept {
    auto handle = views_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index * DescriptorsPerDraw + offset) * viewStride_;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12VideoRenderer::Rtv(const UINT index, const UINT offset,
    ID3D12Resource* resource) noexcept {
    auto handle = targets_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index * RtvsPerDraw + offset) * targetStride_;
    device_->CreateRenderTargetView(resource, nullptr, handle);
    return handle;
}

void D3D12VideoRenderer::SetViewport(const D3D12_VIEWPORT viewport, const UINT width,
    const UINT height) noexcept {
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    commands_->RSSetViewports(1, &viewport);
    commands_->RSSetScissorRects(1, &scissor);
}

HRESULT D3D12VideoRenderer::PrepareScale(const Picture& picture,
    std::array<View, VideoPlaneCount>& inputs, bool& runScale) noexcept {
    runScale = false;
    if (!picture.allowScale) return S_OK;
    auto width = static_cast<UINT>(std::max(picture.viewport.Width, 1.0f));
    auto height = static_cast<UINT>(std::max(picture.viewport.Height, 1.0f));
    if (static_cast<UINT>(picture.video.viewRotation) & 1u) std::swap(width, height);
    bool changed = false;
    const auto releaseChains = [this] {
        for (auto& chain : scales_) {
            for (auto& pass : chain.passes) Release(pass.texture);
            chain = {};
        }
        scaledGeneration_ = 0;
    };
    constexpr DXGI_FORMAT formats[] = {DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
    for (UINT plane = 0; plane < picture.planeCount; ++plane) {
        const auto desc = inputs[plane].resource->GetDesc();
        const auto sw = static_cast<UINT>(desc.Width) >> inputs[plane].plane;
        const auto sh = desc.Height >> inputs[plane].plane;
        const auto dw = std::min(width, sw);
        const auto dh = std::min(height, sh);
        const UINT kind = picture.video.inputLayout == 0 || picture.video.inputLayout == 3 ?
            (inputs[plane].format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 3u : 2u) :
            (picture.video.inputLayout == 2 && plane == 1 ? 1u : 0u);
        auto& chain = scales_[plane];
        const auto format = formats[kind];
        if (chain.width == sw && chain.height == sh && chain.destWidth == dw && chain.destHeight == dh &&
            chain.format == format) continue;
        if (chain.count != 0) {
            const auto hr = Drain();
            if (FAILED(hr)) return hr;
            for (auto& pass : chain.passes) Release(pass.texture);
        }
        chain = {};
        changed = true;
        auto cw = sw;
        auto ch = sh;
        const auto step = [&](const UINT w, const UINT h, const UINT axis) noexcept {
            if (cw == w && ch == h) return S_OK;
            if (chain.count >= MaxScalePasses) return E_INVALIDARG;
            auto& pass = chain.passes[chain.count];
            auto hr = EnsureTexture(pass.texture, TextureDesc(w, h, format,
                    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            if (FAILED(hr)) return hr;
            pass.axis = axis;
            ++chain.count;
            cw = w;
            ch = h;
            return S_OK;
        };
        auto hr = S_OK;
        if ((sw != dw || sh != dh) && !scalePipelines_[kind])
            hr = CreatePipeline(overlayRoot_.Get(), {FFFScalePixelShaderBytecode, sizeof(FFFScalePixelShaderBytecode)},
                format, false, &scalePipelines_[kind]);
        while (SUCCEEDED(hr) && (cw != dw || ch != dh)) {
            const auto nw = std::max(dw, (cw + 1) / 2);
            const auto nh = std::max(dh, (ch + 1) / 2);
            if (static_cast<UINT64>(nw) * ch <= static_cast<UINT64>(cw) * nh) {
                hr = step(nw, ch, 0);
                if (SUCCEEDED(hr)) hr = step(nw, nh, 1);
            } else {
                hr = step(cw, nh, 1);
                if (SUCCEEDED(hr)) hr = step(nw, nh, 0);
            }
        }
        if (FAILED(hr)) {
            if (hr != E_OUTOFMEMORY) return hr;
            const auto drain = Drain();
            if (FAILED(drain)) return drain;
            releaseChains();
            // Avoid retrying expensive allocations at every flip for this geometry.
            for (UINT i = 0; i < picture.planeCount; ++i) {
                const auto src = inputs[i].resource->GetDesc();
                auto& skip = scales_[i];
                skip.width = static_cast<UINT>(src.Width) >> inputs[i].plane;
                skip.height = src.Height >> inputs[i].plane;
                skip.destWidth = std::min(width, skip.width);
                skip.destHeight = std::min(height, skip.height);
                skip.format = formats[picture.video.inputLayout == 0 || picture.video.inputLayout == 3 ?
                    (inputs[i].format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 3u : 2u) :
                    (picture.video.inputLayout == 2 && i == 1 ? 1u : 0u)];
            }
            ++stats_.scaleBudgetSkips;
            return S_OK;
        }
        chain.width = sw;
        chain.height = sh;
        chain.destWidth = dw;
        chain.destHeight = dh;
        chain.format = format;
    }
    runScale = changed || scaledGeneration_ != pictureGeneration_ || scaledFilter_ != picture.scaleFilter;
    return S_OK;
}

void D3D12VideoRenderer::RecordScale(const UINT index, UINT& viewOffset, UINT& rtvOffset,
    const Picture& picture, std::array<View, VideoPlaneCount>& inputs, const bool runScale) noexcept {
    if (!picture.allowScale) return;
    const auto start = std::chrono::steady_clock::now();
    if (runScale) commands_->SetGraphicsRootSignature(overlayRoot_.Get());
    for (UINT i = 0; i < picture.planeCount; ++i) {
        const auto& chain = scales_[i];
        if (chain.count == 0) continue;
        auto sw = chain.width;
        auto sh = chain.height;
        auto view = inputs[i];
        if (runScale) {
            const auto kind = chain.format == DXGI_FORMAT_R16_FLOAT ? 0u :
                chain.format == DXGI_FORMAT_R16G16_FLOAT ? 1u :
                chain.format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 2u : 3u;
            commands_->SetPipelineState(scalePipelines_[kind].Get());
            for (UINT p = 0; p < chain.count; ++p) {
                const auto& pass = chain.passes[p];
                const auto dw = static_cast<UINT>(pass.texture.desc.Width);
                const auto dh = pass.texture.desc.Height;
                WriteView(index, viewOffset, view);
                commands_->SetGraphicsRootDescriptorTable(0, ViewHandle(index, viewOffset++));
                const UINT constants[] = {std::bit_cast<UINT>(static_cast<float>(sw)),
                    std::bit_cast<UINT>(static_cast<float>(sh)), std::bit_cast<UINT>(static_cast<float>(dw)),
                    std::bit_cast<UINT>(static_cast<float>(dh)), pass.axis, picture.scaleFilter, 0, 0};
                commands_->SetGraphicsRoot32BitConstants(1, ARRAYSIZE(constants), constants, 0);
                auto* target = pass.texture.resource.Get();
                Transition(target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                const auto rtv = Rtv(index, rtvOffset++, target);
                commands_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                SetViewport({0, 0, static_cast<float>(dw), static_cast<float>(dh), 0, 1}, dw, dh);
                commands_->DrawInstanced(3, 1, 0, 0);
                Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                view = {target, chain.format};
                sw = dw;
                sh = dh;
                ++stats_.scalePasses;
            }
        }
        inputs[i] = {chain.passes[chain.count - 1].texture.resource.Get(), chain.format};
    }
    if (runScale) {
        scaledGeneration_ = pictureGeneration_;
        scaledFilter_ = picture.scaleFilter;
        stats_.scaleCpu100ns += Elapsed100ns(start);
    }
}

HRESULT D3D12VideoRenderer::PrepareBackdrop(const Picture* picture) noexcept {
    if (!picture || !picture->coverBackdrop) return S_OK;
    const auto factor = std::max(1u, picture->backdropDownsample);
    const auto width = std::clamp(static_cast<UINT>(picture->video.sourceWidth) / factor, 1u, 2048u);
    const auto height = std::clamp(static_cast<UINT>(picture->video.sourceHeight) / factor, 1u, 2048u);
    const auto desc = TextureDesc(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    if (backdrop_[0].desc.Width != width || backdrop_[0].desc.Height != height) backdropGeneration_ = 0;
    auto hr = EnsureTexture(backdrop_[0], desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (FAILED(hr)) return hr;
    hr = PreparePipelines(desc.Format);
    if (FAILED(hr)) return hr;
    if (!picture->backdropPasses) return S_OK;
    hr = EnsureTexture(backdrop_[1], desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (FAILED(hr)) return hr;
    if (!blurPipeline_)
        hr = CreatePipeline(overlayRoot_.Get(), {FFFBackdropBlurBytecode, sizeof(FFFBackdropBlurBytecode)},
            desc.Format, false, &blurPipeline_);
    return hr;
}

void D3D12VideoRenderer::RecordBackdrop(const UINT index, UINT& viewOffset, UINT& rtvOffset,
    const Picture& picture, const UINT videoViews, const D3D12_GPU_VIRTUAL_ADDRESS constants,
    const D3D12_CPU_DESCRIPTOR_HANDLE output, const UINT outputWidth, const UINT outputHeight,
    const DXGI_FORMAT outputFormat) noexcept {
    const auto& desc = backdrop_[0].desc;
    const auto width = static_cast<UINT>(desc.Width);
    const auto height = desc.Height;
    const float cacheScale = std::max(picture.video.sourceWidth / width,
        picture.video.sourceHeight / height);
    const float sigma = std::max(0.1f, picture.backdropRadius * std::sqrt(picture.backdropPasses / 3.0f)
        / std::max(1.0f, cacheScale));
    if (backdropGeneration_ != pictureGeneration_ || backdropSigma_ != sigma ||
        backdropPasses_ != picture.backdropPasses || backdropColorMode_ != picture.video.colorMode) {
        auto* source = backdrop_[0].resource.Get();
        Transition(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const auto rtv = Rtv(index, rtvOffset++, source);
        commands_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        SetViewport({0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1}, width, height);
        commands_->SetGraphicsRootSignature(pictureRoot_.Get());
        commands_->SetGraphicsRootDescriptorTable(0, ViewHandle(index, videoViews));
        commands_->SetGraphicsRootConstantBufferView(1, constants + 3 * ConstantStride);
        commands_->SetGraphicsRootConstantBufferView(2, constants + ConstantStride);
        commands_->SetGraphicsRootConstantBufferView(3, constants + 2 * ConstantStride);
        commands_->SetPipelineState(picturePipelines_[OutputIndex(desc.Format)].Get());
        commands_->DrawInstanced(3, 1, 0, 0);
        Transition(source, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        if (picture.backdropPasses) {
            commands_->SetGraphicsRootSignature(overlayRoot_.Get());
            commands_->SetPipelineState(blurPipeline_.Get());
            for (UINT axis = 0; axis < 2; ++axis) {
                const auto input = backdrop_[axis].resource.Get();
                auto* dest = backdrop_[1u - axis].resource.Get();
                WriteView(index, viewOffset, {input, desc.Format});
                commands_->SetGraphicsRootDescriptorTable(0, ViewHandle(index, viewOffset++));
                const auto w = std::bit_cast<UINT>(static_cast<float>(width));
                const auto h = std::bit_cast<UINT>(static_cast<float>(height));
                const UINT blur[] = {w, h, w, h, axis, 3, std::bit_cast<UINT>(sigma), 0};
                commands_->SetGraphicsRoot32BitConstants(1, ARRAYSIZE(blur), blur, 0);
                Transition(dest, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
                const auto target = Rtv(index, rtvOffset++, dest);
                commands_->OMSetRenderTargets(1, &target, FALSE, nullptr);
                commands_->DrawInstanced(3, 1, 0, 0);
                Transition(dest, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        }
        backdropGeneration_ = pictureGeneration_;
        backdropSigma_ = sigma;
        backdropPasses_ = picture.backdropPasses;
        backdropColorMode_ = picture.video.colorMode;
    }
    WriteView(index, viewOffset, {backdrop_[0].resource.Get(), desc.Format});
    const UINT parameters[] = {picture.video.colorMode, 0, 0, picture.backdropTint,
        std::bit_cast<UINT>(picture.video.sdrPeak), std::bit_cast<UINT>(picture.video.hdrPeak), std::bit_cast<UINT>(picture.video.paperWhite), std::bit_cast<UINT>(picture.video.targetPeak),
        std::bit_cast<UINT>(static_cast<float>(width)), std::bit_cast<UINT>(static_cast<float>(height)),
        std::bit_cast<UINT>(static_cast<float>(outputWidth)), std::bit_cast<UINT>(static_cast<float>(outputHeight))};
    commands_->SetGraphicsRootSignature(overlayRoot_.Get());
    commands_->SetGraphicsRootDescriptorTable(0, ViewHandle(index, viewOffset++));
    commands_->SetGraphicsRoot32BitConstants(1, ARRAYSIZE(parameters), parameters, 0);
    commands_->SetPipelineState(backdropPipelines_[OutputIndex(outputFormat)].Get());
    commands_->OMSetRenderTargets(1, &output, FALSE, nullptr);
    SetViewport({0, 0, static_cast<float>(outputWidth), static_cast<float>(outputHeight), 0, 1}, outputWidth, outputHeight);
    commands_->DrawInstanced(3, 1, 0, 0);
}

HRESULT D3D12VideoRenderer::RecordDraw(const UINT index, ID3D12Resource* target,
    const Picture* picture, std::span<const Overlay> overlays, const D3D12_RESOURCE_STATES initial,
    const D3D12_RESOURCE_STATES final) noexcept {
    const auto desc = target->GetDesc();
    const auto formatIndex = OutputIndex(desc.Format);
    auto hr = PreparePipelines(desc.Format);
    if (FAILED(hr)) return hr;
    auto& slot = draws_[index];
    std::array<View, VideoPlaneCount> source{};
    constexpr DXGI_FORMAT nullFormats[ResourceCount] = {DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT};
    bool scale = false;
    bool backdrop = picture && picture->coverBackdrop;
    if (picture) {
        if (picture->planeCount == 0 || picture->planeCount > VideoPlaneCount) return E_INVALIDARG;
        if (decodedActive_) {
            if (picture->planeCount != 2 || !decodedTexture_.resource) return E_INVALIDARG;
            const auto eightBit = decodedTexture_.desc.Format == DXGI_FORMAT_NV12;
            source[0] = {decodedTexture_.resource.Get(), eightBit ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R16_UNORM, 0};
            source[1] = {decodedTexture_.resource.Get(), eightBit ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R16G16_UNORM, 1};
        } else {
            for (UINT i = 0; i < picture->planeCount; ++i) {
                if (!textures_[i].resource) return E_INVALIDARG;
                source[i] = {textures_[i].resource.Get(), textures_[i].desc.Format};
            }
        }
        hr = PrepareScale(*picture, source, scale);
        if (FAILED(hr)) return hr;
        if (backdrop) {
            hr = PrepareBackdrop(picture);
            if (hr == E_OUTOFMEMORY) {
                const auto drain = Drain();
                if (FAILED(drain)) return drain;
                for (auto& texture : backdrop_) Release(texture);
                backdropGeneration_ = 0;
                backdrop = false;
            } else if (FAILED(hr)) return hr;
            if (backdrop && !backdropPipelines_[formatIndex]) {
                hr = CreatePipeline(overlayRoot_.Get(), {FFFCoverBackdropPixelShaderBytecode,
                    sizeof(FFFCoverBackdropPixelShaderBytecode)}, desc.Format, false, &backdropPipelines_[formatIndex]);
                if (FAILED(hr)) return hr;
            }
        }
        hr = Wait(slot.fence);
        if (FAILED(hr)) return hr;
        hr = EnsureBuffer(slot.buffer, (backdrop ? 4 : 3) * ConstantStride, D3D12_HEAP_TYPE_UPLOAD);
        if (FAILED(hr)) return hr;
    }
    hr = Begin(slot);
    if (FAILED(hr)) return hr;
    const auto timing = timestamps_ && index < BufferCount;
    if (timing) commands_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2);
    UINT viewOffset = 0, rtvOffset = 0;
    ID3D12DescriptorHeap* heaps[] = {views_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const auto rtv = Rtv(index, rtvOffset++, target);
    const auto width = static_cast<UINT>(desc.Width);
    const auto height = desc.Height;
    Transition(target, initial, D3D12_RESOURCE_STATE_RENDER_TARGET);
    constexpr float black[4] = {0, 0, 0, 1};
    constexpr float transparent[4]{};
    commands_->ClearRenderTargetView(rtv, !picture && index == ReadSlot ? transparent : black, 0, nullptr);

    if (picture) {
        const auto constants = slot.buffer.resource->GetGPUVirtualAddress();
        std::memcpy(slot.buffer.mapped, &picture->video, sizeof(picture->video));
        std::memcpy(slot.buffer.mapped + ConstantStride, &picture->dynamic, sizeof(picture->dynamic));
        std::memcpy(slot.buffer.mapped + 2 * ConstantStride, &picture->auxiliary, sizeof(picture->auxiliary));
        const auto bindViews = [&](const std::array<View, VideoPlaneCount>& planes) noexcept {
            const auto base = viewOffset;
            for (UINT i = 0; i < ResourceCount; ++i) {
                auto view = i < planes.size() ? planes[i] : View{};
                const bool active = (i == 3 && picture->dynamic.enabled) || (i == 4 && picture->dynamic.transformEnabled) ||
                    (i >= 5 && picture->auxiliary.enabled);
                if (active && textures_[i].resource) view = {textures_[i].resource.Get(), textures_[i].desc.Format};
                if (!view.resource) view.format = nullFormats[i];
                WriteView(index, viewOffset++, view, i == 4);
            }
            return base;
        };
        if (backdrop) {
            auto settings = picture->video;
            settings.reserved = 1;
            settings.outputWidth = static_cast<float>(backdrop_[0].desc.Width);
            settings.outputHeight = static_cast<float>(backdrop_[0].desc.Height);
            settings.imageModeX = settings.viewRotation = 0; settings.projection360 = 0;
            std::memcpy(slot.buffer.mapped + 3 * ConstantStride, &settings, sizeof(settings));
            const auto views = bindViews(source);
            RecordBackdrop(index, viewOffset, rtvOffset, *picture, views, constants, rtv, width, height, desc.Format);
        }
        RecordScale(index, viewOffset, rtvOffset, *picture, source, scale);
        const auto videoViews = bindViews(source);
        commands_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        SetViewport(picture->viewport, width, height);
        commands_->SetGraphicsRootSignature(pictureRoot_.Get());
        commands_->SetGraphicsRootDescriptorTable(0, ViewHandle(index, videoViews));
        for (UINT i = 0; i < 3; ++i)
            commands_->SetGraphicsRootConstantBufferView(i + 1, constants + i * ConstantStride);
        commands_->SetPipelineState(picturePipelines_[formatIndex].Get());
        commands_->DrawInstanced(3, 1, 0, 0);
    }

    if (!overlays.empty()) {
        commands_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        commands_->SetGraphicsRootSignature(overlayRoot_.Get());
        commands_->SetPipelineState(overlayPipelines_[formatIndex].Get());
        for (const auto& overlay : overlays) {
            if (overlay.index >= OverlayCount || !overlays_[overlay.index].resource ||
                overlay.viewport.Width <= 0 || overlay.viewport.Height <= 0) continue;
            const auto& texture = overlays_[overlay.index];
            WriteView(index, viewOffset, {texture.resource.Get(), texture.desc.Format});
            commands_->SetGraphicsRootDescriptorTable(0, ViewHandle(index, viewOffset++));
            commands_->SetGraphicsRoot32BitConstants(1, static_cast<UINT>(overlay.constants.size()),
                overlay.constants.data(), 0);
            SetViewport(overlay.viewport, width, height);
            commands_->DrawInstanced(3, 1, 0, 0);
        }
    }
    Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET, final);
    if (timing) {
        commands_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2 + 1);
        commands_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            index * 2, 2, timestampReadback_.resource.Get(), index * 2 * sizeof(UINT64));
    }
    slot.timestamp = timing;
    return S_OK;
}

HRESULT D3D12VideoRenderer::Submit(IDXGISwapChain4* chain, const Picture* picture,
    std::span<const Overlay> overlays) noexcept {
    const auto index = chain->GetCurrentBackBufferIndex();
    if (!backBuffers_[index]) return DXGI_ERROR_INVALID_CALL;
    auto hr = RecordDraw(index, backBuffers_[index].Get(), picture, overlays,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
    if (FAILED(hr)) return hr;
    hr = Execute(draws_[index]);
    if (SUCCEEDED(hr)) {
        if (picture) ++stats_.draws;
        for (const auto& overlay : overlays) {
            if (overlay.index >= OverlayCount || !overlays_[overlay.index].resource) continue;
            overlayFences_[overlay.index] = value_;
            ++stats_.overlayDraws;
        }
    }
    return hr;
}

HRESULT D3D12VideoRenderer::EnsureTextDevice() noexcept {
    if (textDevice_) return S_OK;
    IUnknown* queues[] = {queue_.Get()};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    auto hr = D3D11On12CreateDevice(device_.Get(), D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, queues, ARRAYSIZE(queues), 0, &device, &context, nullptr);
    if (FAILED(hr)) return hr;
    hr = device.As(&on12_);
    if (FAILED(hr)) return hr;
    (void)device.As(&on12Checkout_);
    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(context.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
    textDevice_ = std::move(device);
    textContext_ = std::move(context);
    return S_OK;
}

HRESULT D3D12VideoRenderer::CreateOverlay(const UINT index, const UINT width, const UINT height,
    const DXGI_FORMAT format, ID3D11Texture2D** texture) noexcept {
    if (index >= OverlayCount || !texture || overlays_[index].bytes) return E_INVALIDARG;
    *texture = nullptr;
    auto hr = EnsureTextDevice();
    if (FAILED(hr)) return hr;
    Reclaim();
    auto& overlay = overlays_[index];
    const auto desc = TextureDesc(width, height, format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    if (on12Checkout_) {
        // D2D creates the allocation through D3D11On12; the native renderer
        // checks that same texture out after rasterization. No copy or sharing
        // with an independent D3D11 device is involved.
        const auto bytes = device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        if (!CanAllocateLocal(bytes)) return E_OUTOFMEMORY;
        D3D11_TEXTURE2D_DESC text{};
        text.Width = width;
        text.Height = height;
        text.MipLevels = text.ArraySize = 1;
        text.Format = format;
        text.SampleDesc.Count = 1;
        text.Usage = D3D11_USAGE_DEFAULT;
        text.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = textDevice_->CreateTexture2D(&text, nullptr, texture);
        if (SUCCEEDED(hr)) {
            overlay.desc = desc;
            overlay.bytes = bytes;
            stats_.localBytes += bytes;
        }
        return hr;
    }
    hr = EnsureTexture(overlay, desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (FAILED(hr)) return hr;
    overlay.resource->SetName(L"3FP D3D12 timed-text layer");
    D3D11_RESOURCE_FLAGS flags{};
    flags.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = on12_->CreateWrappedResource(overlay.resource.Get(), &flags,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, IID_PPV_ARGS(texture));
    if (FAILED(hr)) Release(overlay);
    // CreateWrappedResource starts acquired. The first D2D write uses it in
    // RENDER_TARGET; subsequent draws transition explicitly before Acquire.
    return hr;
}

HRESULT D3D12VideoRenderer::BeginOverlay(const UINT index, ID3D11Texture2D* texture) noexcept {
    if (index >= OverlayCount || !texture || !overlays_[index].bytes) return E_INVALIDARG;
    if (on12Checkout_) {
        auto& overlay = overlays_[index];
        if (!overlay.resource) return S_OK;
        ID3D12Fence* fences[] = {fence_.Get()};
        const auto hr = on12Checkout_->ReturnUnderlyingResource(texture, 1, &overlayFences_[index], fences);
        if (SUCCEEDED(hr)) overlay.resource.Reset();
        return hr;
    }
    if (overlayFences_[index] == 0) return S_OK;
    auto& slot = uploads_[uploadIndex_];
    auto hr = Begin(slot);
    if (FAILED(hr)) return hr;
    Transition(overlays_[index].resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    hr = Execute(slot);
    if (FAILED(hr)) return hr;
    ID3D11Resource* resources[] = {texture};
    on12_->AcquireWrappedResources(resources, 1);
    return S_OK;
}

HRESULT D3D12VideoRenderer::EndOverlay(const UINT index, ID3D11Texture2D* texture) noexcept {
    if (!on12Checkout_) {
        ID3D11Resource* resources[] = {texture};
        on12_->ReleaseWrappedResources(resources, 1);
    }
    if (on12Checkout_) {
        const auto hr = on12Checkout_->UnwrapUnderlyingResource(texture, queue_.Get(),
            IID_PPV_ARGS(&overlays_[index].resource));
        if (FAILED(hr)) return hr;
    }
    // Checkout records a transition to COMMON; the wrapped-resource path
    // records its output transition too. Flush after either, not before it.
    textContext_->Flush();
    // D2D writes are not submitted through Execute(); fence them as well so
    // a layer cleared before its first composite can be retired safely.
    submissionError_ = queue_->Signal(fence_.Get(), value_ + 1);
    if (SUCCEEDED(submissionError_)) overlayFences_[index] = ++value_;
    return submissionError_;
}

void D3D12VideoRenderer::ReleaseOverlay(const UINT index, ID3D11Texture2D* texture) noexcept {
    if (index >= OverlayCount || !overlays_[index].bytes) return;
    Reclaim();
    const auto value = overlayFences_[index];
    if (on12Checkout_ && texture && overlays_[index].resource) {
        ID3D12Fence* fences[] = {fence_.Get()};
        (void)on12Checkout_->ReturnUnderlyingResource(texture, 1, &overlayFences_[index], fences);
    }
    if (value > fence_->GetCompletedValue()) {
        if (retiredOverlays_[index].resource) {
            if (FAILED(Wait(retiredFences_[index]))) return;
            Release(retiredOverlays_[index]);
        }
        retiredOverlays_[index] = std::exchange(overlays_[index], {});
        retiredFences_[index] = value;
    } else {
        Release(overlays_[index]);
    }
    overlayFences_[index] = 0;
}

HRESULT D3D12VideoRenderer::ReadTexture(ID3D12Resource* image, const D3D12_BOX& region,
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) noexcept {
    auto desc = image->GetDesc();
    if (region.left >= region.right || region.top >= region.bottom ||
        region.right > desc.Width || region.bottom > desc.Height) return E_INVALIDARG;
    desc.Width = region.right - region.left;
    desc.Height = region.bottom - region.top;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    UINT64 required = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &required);
    auto hr = EnsureBuffer(readback_, required, D3D12_HEAP_TYPE_READBACK);
    if (FAILED(hr)) return hr;
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = image;
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.pResource = readback_.resource.Get();
    to.PlacedFootprint = footprint;
    commands_->CopyTextureRegion(&to, 0, 0, 0, &from, &region);
    return S_OK;
}

HRESULT D3D12VideoRenderer::ReadPicture(const UINT width, const UINT height, const DXGI_FORMAT format,
    const Picture* picture, std::span<const Overlay> overlays, const D3D12_BOX& region,
    void* destination, const UINT64 capacity) noexcept {
    if (!destination || region.left >= region.right || region.top >= region.bottom ||
        region.right > width || region.bottom > height || OutputIndex(format) == UINT_MAX) return E_INVALIDARG;
    const auto row = static_cast<UINT64>(region.right - region.left) *
        (format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u);
    const auto required = row * (region.bottom - region.top);
    if (required > capacity) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    auto hr = EnsureTexture(readTarget_, TextureDesc(width, height, format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (FAILED(hr)) return hr;
    hr = RecordDraw(ReadSlot, readTarget_.resource.Get(), picture, overlays,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (FAILED(hr)) return hr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    hr = ReadTexture(readTarget_.resource.Get(), region, fp);
    if (FAILED(hr)) { (void)commands_->Close(); return hr; }
    auto& slot = draws_[ReadSlot];
    if (FAILED(hr = Execute(slot)) || FAILED(hr = Wait(slot.fence))) return hr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(fp.Footprint.RowPitch) * fp.Footprint.Height};
    void* mapped = nullptr;
    hr = readback_.resource->Map(0, &range, &mapped);
    if (FAILED(hr)) return hr;
    for (UINT y = 0; y < fp.Footprint.Height; ++y)
        std::memcpy(static_cast<std::uint8_t*>(destination) + y * row,
            static_cast<const std::uint8_t*>(mapped) + static_cast<UINT64>(y) * fp.Footprint.RowPitch,
            static_cast<std::size_t>(row));
    D3D12_RANGE noWrite{};
    readback_.resource->Unmap(0, &noWrite);
    if (readback_.bytes > RetainedReadbackBytes) Release(readback_);
    if (readTarget_.bytes > RetainedReadbackBytes) Release(readTarget_);
    return S_OK;
}

HRESULT D3D12VideoRenderer::ReadOverlayAlpha(const UINT index, UINT64& visible) noexcept {
    visible = 0;
    if (index >= OverlayCount || !overlays_[index].resource || !overlayFences_[index]) return E_INVALIDARG;
    auto& slot = draws_[ReadSlot];
    auto hr = Begin(slot);
    if (FAILED(hr)) return hr;
    const auto& texture = overlays_[index];
    auto* source = texture.resource.Get();
    const auto state = on12Checkout_ ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Transition(source, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    const D3D12_BOX box{0, 0, 0, static_cast<UINT>(texture.desc.Width), texture.desc.Height, 1};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    hr = ReadTexture(source, box, fp);
    Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    if (FAILED(hr)) { (void)commands_->Close(); return hr; }
    if (FAILED(hr = Execute(slot)) || FAILED(hr = Wait(slot.fence))) return hr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(fp.Footprint.RowPitch) * box.bottom};
    void* mapped = nullptr;
    hr = readback_.resource->Map(0, &range, &mapped);
    if (FAILED(hr)) return hr;
    const bool half = texture.desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    for (UINT y = 0; y < box.bottom; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped) + static_cast<UINT64>(y) * fp.Footprint.RowPitch;
        for (UINT x = 0; x < box.right; ++x)
            visible += half ? (reinterpret_cast<const std::uint16_t*>(row)[x * 4u + 3u] & 0x7fffu) != 0 :
                row[x * 4u + 3u] != 0;
    }
    D3D12_RANGE noWrite{};
    readback_.resource->Unmap(0, &noWrite);
    if (readback_.bytes > RetainedReadbackBytes) Release(readback_);
    return S_OK;
}
