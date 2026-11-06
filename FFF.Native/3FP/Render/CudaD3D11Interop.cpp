#include "pch.h"
#include "3FP/Render/CudaD3D11Interop.h"
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

namespace {

// One-shot diagnostic switch: FFF_CUDA_INTEROP_TRACE=1 makes the import arm report
// which driver call refused, so a silent conversion to the download route can be
// told apart from a wrong geometry.
bool InteropTraceEnabled() noexcept {
    static const bool enabled = [] {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"FFF_CUDA_INTEROP_TRACE", value, 8) != 0 &&
            value[0] == L'1' && value[1] == L'\0';
    }();
    return enabled;
}

// --- CUDA driver API, declared locally -------------------------------------------
//
// The kernel must not take a build dependency on the CUDA SDK: third_party ships no
// cuda.h, and libavutil's own hwcontext_cuda.h pulls it in. Only a handful of entry
// points are needed and they are resolved at runtime from nvcuda.dll, which is the
// same mechanism DecodeDeviceSelection.cpp already uses for cuD3D11GetDevice. The
// declarations below mirror cuda.h; every struct is layout-pinned by a static_assert
// so an ABI mismatch is a compile error rather than a corrupted copy.

using CUresult = int;
using CUdevice = int;
using CUdeviceptr = unsigned long long;

struct CUctx_st;
struct CUstream_st;
struct CUarray_st;
struct CUgraphicsResource_st;

using CUcontext = CUctx_st*;
using CUstream = CUstream_st*;
using CUarray = CUarray_st*;
using CUgraphicsResource = CUgraphicsResource_st*;

constexpr CUresult CudaSuccess = 0;
constexpr int MemoryTypeDevice = 2;   // CU_MEMORYTYPE_DEVICE
constexpr int MemoryTypeArray = 3;    // CU_MEMORYTYPE_ARRAY
constexpr unsigned RegisterFlagsNone = 0;

// CUDA_MEMCPY2D
struct CudaMemcpy2D final {
    std::size_t srcXInBytes;
    std::size_t srcY;
    int srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    std::size_t srcPitch;
    std::size_t dstXInBytes;
    std::size_t dstY;
    int dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    std::size_t dstPitch;
    std::size_t widthInBytes;
    std::size_t height;
};
// 2 * (2*8 + 4 + 4 pad + 3*8 + 8) + 2*8
static_assert(sizeof(CudaMemcpy2D) == 128, "CUDA_MEMCPY2D layout pin");

// AVCUDADeviceContext from libavutil/hwcontext_cuda.h. Only the three leading members
// exist and all three are opaque handles there, so mirroring them needs no CUDA SDK.
struct CudaDeviceHwContext final {
    void* cudaContext;
    void* stream;
    void* internal;
};
static_assert(sizeof(CudaDeviceHwContext) == 3 * sizeof(void*), "AVCUDADeviceContext layout pin");

struct CudaDriver final {
    HMODULE module = nullptr;
    CUresult(__stdcall* init)(unsigned int) = nullptr;
    CUresult(__stdcall* ctxPushCurrent)(CUcontext) = nullptr;
    CUresult(__stdcall* ctxPopCurrent)(CUcontext*) = nullptr;
    CUresult(__stdcall* ctxGetCurrent)(CUcontext*) = nullptr;
    CUresult(__stdcall* ctxGetDevice)(CUdevice*) = nullptr;
    CUresult(__stdcall* d3d11GetDevice)(CUdevice*, IDXGIAdapter*) = nullptr;
    CUresult(__stdcall* memcpy2D)(const CudaMemcpy2D*) = nullptr;
    CUresult(__stdcall* memcpy2DAsync)(const CudaMemcpy2D*, CUstream) = nullptr;
    CUresult(__stdcall* ctxSynchronize)() = nullptr;
    CUresult(__stdcall* streamSynchronize)(CUstream) = nullptr;
    CUresult(__stdcall* graphicsD3D11RegisterResource)(CUgraphicsResource*, ID3D11Resource*,
        unsigned int) = nullptr;
    CUresult(__stdcall* graphicsUnregisterResource)(CUgraphicsResource) = nullptr;
    CUresult(__stdcall* graphicsMapResources)(unsigned int, CUgraphicsResource*, CUstream) = nullptr;
    CUresult(__stdcall* graphicsUnmapResources)(unsigned int, CUgraphicsResource*, CUstream) = nullptr;
    CUresult(__stdcall* graphicsSubResourceGetMappedArray)(CUarray*, CUgraphicsResource,
        unsigned int, unsigned int) = nullptr;

    bool Complete() const noexcept {
        return module != nullptr && init != nullptr && ctxPushCurrent != nullptr &&
            ctxPopCurrent != nullptr && ctxGetCurrent != nullptr && ctxGetDevice != nullptr &&
            d3d11GetDevice != nullptr && memcpy2D != nullptr &&
            graphicsD3D11RegisterResource != nullptr && graphicsUnregisterResource != nullptr &&
            graphicsMapResources != nullptr && graphicsUnmapResources != nullptr &&
            graphicsSubResourceGetMappedArray != nullptr;
    }
};

template <typename Function>
bool Resolve(HMODULE const module, const char* const name, Function& slot) noexcept {
    slot = reinterpret_cast<Function>(GetProcAddress(module, name));
    return slot != nullptr;
}

bool LoadDriver(CudaDriver& driver) noexcept {
    if (driver.module != nullptr) return driver.Complete();
    driver.module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (driver.module == nullptr) return false;
    const auto module = driver.module;
    const bool resolved =
        Resolve(module, "cuInit", driver.init) &&
        Resolve(module, "cuCtxPushCurrent", driver.ctxPushCurrent) &&
        Resolve(module, "cuCtxPopCurrent", driver.ctxPopCurrent) &&
        Resolve(module, "cuCtxGetCurrent", driver.ctxGetCurrent) &&
        Resolve(module, "cuCtxGetDevice", driver.ctxGetDevice) &&
        Resolve(module, "cuD3D11GetDevice", driver.d3d11GetDevice) &&
        // cuda.h maps cuMemcpy2D to cuMemcpy2D_v2 before compiling; the unversioned
        // nvcuda.dll export is the pre-4.0 entry point. Resolve the versioned symbol
        // first, exactly as the macro would, and keep the old one only as a fallback.
        (Resolve(module, "cuMemcpy2D_v2", driver.memcpy2D) ||
            Resolve(module, "cuMemcpy2D", driver.memcpy2D)) &&
        (Resolve(module, "cuMemcpy2DAsync_v2", driver.memcpy2DAsync) ||
            Resolve(module, "cuMemcpy2DAsync", driver.memcpy2DAsync)) &&
        Resolve(module, "cuCtxSynchronize", driver.ctxSynchronize) &&
        Resolve(module, "cuStreamSynchronize", driver.streamSynchronize) &&
        Resolve(module, "cuGraphicsD3D11RegisterResource",
            driver.graphicsD3D11RegisterResource) &&
        Resolve(module, "cuGraphicsUnregisterResource", driver.graphicsUnregisterResource) &&
        Resolve(module, "cuGraphicsMapResources", driver.graphicsMapResources) &&
        Resolve(module, "cuGraphicsUnmapResources", driver.graphicsUnmapResources) &&
        Resolve(module, "cuGraphicsSubResourceGetMappedArray",
            driver.graphicsSubResourceGetMappedArray);
    if (resolved && driver.init(0) == CudaSuccess) return true;
    FreeLibrary(driver.module);
    driver = CudaDriver{};
    return false;
}

// The adapter behind a D3D11 device, for the same-LUID check the decoder selection
// already applies before it hands a stream to CUDA.
Microsoft::WRL::ComPtr<IDXGIAdapter> DeviceAdapter(ID3D11Device* const device) noexcept {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (device != nullptr && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))))
        dxgi->GetAdapter(&adapter);
    return adapter;
}

std::uint32_t BytesPerSample(const DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM: return 1;
    case DXGI_FORMAT_R16_UNORM: return 2;
    default: return 0;
    }
}

} // namespace

struct CudaD3D11Interop::State final {
    CudaDriver driver;
    CUcontext context = nullptr;
    CUstream stream = nullptr;
    bool probed = false;
    bool probeOk = false;
    std::uint32_t planeCount = 0;
    CUgraphicsResource resources[3]{};
    std::uint32_t planeWidth[3]{};
    std::uint32_t planeHeight[3]{};
    std::uint32_t planeRowBytes[3]{};
};

CudaD3D11Interop::CudaD3D11Interop() noexcept : state_(std::make_unique<State>()) {}

CudaD3D11Interop::~CudaD3D11Interop() noexcept {
    Reset();
}

bool CudaD3D11Interop::Enabled() noexcept {
    static const bool enabled = [] {
        // On by default: the route is a pure optimisation and every refusal -- missing
        // nvcuda.dll, a non-NVIDIA adapter, an unmatched adapter, an unsupported
        // pixel format or a failed registration -- falls back to the download path for
        // that stream. FFF3FP_CUDA_D3D11_INTEROP=0 restores the old unconditional
        // transfer, which is also the escape hatch if a driver misbehaves.
        wchar_t value[8]{};
        if (GetEnvironmentVariableW(L"FFF3FP_CUDA_D3D11_INTEROP", value, 8) == 0) return true;
        return !(value[0] == L'0' && value[1] == L'\0');
    }();
    return enabled;
}

bool CudaD3D11Interop::SupportsPixelFormat(const std::int32_t pixelFormat) noexcept {
    switch (static_cast<AVPixelFormat>(pixelFormat)) {
    // Planar YUV, 8 bit: one R8_UNORM texture per plane.
    case AV_PIX_FMT_YUV420P: case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV422P: case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV444P: case AV_PIX_FMT_YUVJ444P:
    // Planar YUV, more than 8 bit: one R16_UNORM texture per plane. The samples are
    // copied as-is; the shader applies the same sample scale the CPU route uses.
    case AV_PIX_FMT_YUV420P10LE: case AV_PIX_FMT_YUV420P12LE: case AV_PIX_FMT_YUV420P16LE:
    case AV_PIX_FMT_YUV422P10LE: case AV_PIX_FMT_YUV422P12LE: case AV_PIX_FMT_YUV422P16LE:
    case AV_PIX_FMT_YUV444P10LE: case AV_PIX_FMT_YUV444P12LE: case AV_PIX_FMT_YUV444P16LE:
        return true;
    // The CUDA hwframe pool reports 10/12-bit 4:4:4 as the MSB-aligned variants.
    case AV_PIX_FMT_YUV444P10MSBLE: case AV_PIX_FMT_YUV444P12MSBLE:
        return true;
    default:
        return false;
    }
}

CudaD3D11Interop::Status CudaD3D11Interop::Probe(const AVFrame* const frame,
    ID3D11Device* const device) noexcept {
    if (!Enabled() || frame == nullptr || device == nullptr) return Status::Refused;
    // One-shot diagnostic injection: FFF_CUDA_INTEROP_PROBE_REFUSE=1 makes the
    // device-side probe refuse every stream, which drives Render's CUDA fallback
    // (download + UploadPlanarFrames + latch) without needing a misbehaving
    // driver. Same family as FFF_CUDA_INTEROP_TRACE; off by default.
    static const bool probeRefuse = [] {
        wchar_t value[8]{};
        return GetEnvironmentVariableW(L"FFF_CUDA_INTEROP_PROBE_REFUSE", value, 8) != 0 &&
            value[0] == L'1' && value[1] == L'\0';
    }();
    if (probeRefuse) return Status::Refused;
    if (frame->format != AV_PIX_FMT_CUDA || frame->hw_frames_ctx == nullptr) return Status::Refused;
    const auto* const frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
    if (frames == nullptr || frames->device_ref == nullptr) return Status::Refused;
    const auto* const hwDevice = reinterpret_cast<const AVHWDeviceContext*>(frames->device_ref->data);
    if (hwDevice == nullptr || hwDevice->type != AV_HWDEVICE_TYPE_CUDA || hwDevice->hwctx == nullptr)
        return Status::Refused;
    const auto* const hw = static_cast<const CudaDeviceHwContext*>(hwDevice->hwctx);
    const auto cudaContext = static_cast<CUcontext>(hw->cudaContext);
    const auto cudaStream = static_cast<CUstream>(hw->stream);
    if (cudaContext == nullptr) return Status::Refused;
    // One context per stream, and the pointer is stable for the life of the decoder, so
    // the expensive half of this runs once per stream instead of once per frame.
    if (state_->probed && state_->context == cudaContext) {
        state_->stream = cudaStream;
        return state_->probeOk ? Status::Success : Status::Refused;
    }
    // Remember the context even when the answer is no: a stream that will never
    // qualify must not pay a driver round trip on every frame.
    state_->probed = true;
    state_->probeOk = false;
    state_->context = cudaContext;
    state_->stream = cudaStream;
    auto& driver = state_->driver;
    if (!LoadDriver(driver)) return Status::Refused;
    // Push the decoder's context: a wrong handle is reported by the driver, never
    // dereferenced, and the identity check below is what decides whether to trust it.
    if (driver.ctxPushCurrent(cudaContext) != CudaSuccess) return Status::Refused;
    CUcontext current = nullptr;
    const bool current_ok = driver.ctxGetCurrent(&current) == CudaSuccess &&
        current == cudaContext;
    CUdevice contextDevice = 0;
    const bool device_ok = current_ok && driver.ctxGetDevice(&contextDevice) == CudaSuccess;
    const auto adapter = DeviceAdapter(device);
    CUdevice adapterDevice = -1;
    const bool adapter_ok = device_ok && adapter != nullptr &&
        driver.d3d11GetDevice(&adapterDevice, adapter.Get()) == CudaSuccess &&
        adapterDevice == contextDevice;
    CUcontext popped = nullptr;
    driver.ctxPopCurrent(&popped);
    if (!adapter_ok) return Status::Refused;
    state_->probeOk = true;
    return Status::Success;
}

CudaD3D11Interop::Status CudaD3D11Interop::Bind(ID3D11Texture2D* const* const planes,
    const std::uint32_t planeCount) noexcept {
    Reset();
    if (!state_->probeOk || state_->context == nullptr) return Status::Refused;
    if (planes == nullptr || planeCount == 0 || planeCount > 3) return Status::Refused;
    auto& driver = state_->driver;
    if (driver.ctxPushCurrent(state_->context) != CudaSuccess) return Status::Failed;
    auto status = Status::Success;
    std::uint32_t registered = 0;
    for (; registered < planeCount; ++registered) {
        D3D11_TEXTURE2D_DESC description{};
        if (planes[registered] == nullptr) { status = Status::Refused; break; }
        planes[registered]->GetDesc(&description);
        const auto bytesPerSample = BytesPerSample(description.Format);
        // CUDA interop requires a DEFAULT texture: a DYNAMIC one has no stable storage
        // the driver can hand to another API.
        if (bytesPerSample == 0 || description.Usage != D3D11_USAGE_DEFAULT ||
            description.MipLevels != 1 || description.ArraySize != 1) {
            status = Status::Refused;
            break;
        }
        if (driver.graphicsD3D11RegisterResource(&state_->resources[registered],
                planes[registered], RegisterFlagsNone) != CudaSuccess) {
            status = Status::Failed;
            break;
        }
        state_->planeWidth[registered] = description.Width;
        state_->planeHeight[registered] = description.Height;
        state_->planeRowBytes[registered] = description.Width * bytesPerSample;
    }
    if (status != Status::Success) {
        for (std::uint32_t plane = 0; plane < registered; ++plane)
            driver.graphicsUnregisterResource(state_->resources[plane]);
        for (auto& resource : state_->resources) resource = nullptr;
    } else {
        state_->planeCount = planeCount;
    }
    CUcontext popped = nullptr;
    driver.ctxPopCurrent(&popped);
    return status;
}

CudaD3D11Interop::Status CudaD3D11Interop::Import(const AVFrame* const frame) noexcept {
    if (state_->planeCount == 0 || frame == nullptr) return Status::Refused;
    if (frame->format != AV_PIX_FMT_CUDA || frame->hw_frames_ctx == nullptr) return Status::Refused;
    // The bound textures describe the picture that was requested. A frame of a different
    // size would either truncate it or read past the decoded buffer, so the geometry has
    // to agree before a single byte moves.
    if (static_cast<std::uint32_t>(frame->width) != state_->planeWidth[0] ||
        static_cast<std::uint32_t>(frame->height) != state_->planeHeight[0]) {
        if (InteropTraceEnabled()) std::fprintf(stderr,
            "[cuda] import geometry %dx%d != bound %ux%u\n", frame->width, frame->height,
            state_->planeWidth[0], state_->planeHeight[0]);
        return Status::Refused;
    }
    for (std::uint32_t plane = 0; plane < state_->planeCount; ++plane) {
        if (frame->data[plane] == nullptr ||
            static_cast<std::size_t>(frame->linesize[plane]) < state_->planeRowBytes[plane]) {
            if (InteropTraceEnabled()) std::fprintf(stderr,
                "[cuda] import plane%u unusable: data=%p linesize=%d rowBytes=%u\n", plane,
                static_cast<void*>(frame->data[plane]), frame->linesize[plane],
                state_->planeRowBytes[plane]);
            return Status::Refused;
        }
    }
    auto& driver = state_->driver;
    if (driver.ctxPushCurrent(state_->context) != CudaSuccess) return Status::Failed;
    if (InteropTraceEnabled()) {
        CUcontext current = nullptr;
        CUdevice device = -1;
        const auto getResult = driver.ctxGetCurrent(&current);
        const auto deviceResult = driver.ctxGetDevice(&device);
        std::fprintf(stderr,
            "[cuda] import ctx=%p current=%p (get=%d) dev=%d (devGet=%d) stream=%p\n",
            reinterpret_cast<void*>(state_->context), reinterpret_cast<void*>(current),
            static_cast<int>(getResult), static_cast<int>(device),
            static_cast<int>(deviceResult), reinterpret_cast<void*>(state_->stream));
    }
    auto status = Status::Success;
    std::uint32_t mapped = 0;
    // Map and copy on the same stream: a copy issued on a different stream than the
    // map would not be ordered against it, and the driver refuses to reason about it.
    for (; mapped < state_->planeCount; ++mapped) {
        const auto mapResult = driver.graphicsMapResources(1, &state_->resources[mapped],
            state_->stream);
        if (mapResult != CudaSuccess) {
            if (InteropTraceEnabled()) std::fprintf(stderr,
                "[cuda] import map plane%u failed (%d)\n", mapped, static_cast<int>(mapResult));
            break;
        }
    }
    if (mapped != state_->planeCount) {
        status = Status::Failed;
    } else {
        for (std::uint32_t plane = 0; plane < state_->planeCount; ++plane) {
            CUarray array = nullptr;
            const auto arrayResult = driver.graphicsSubResourceGetMappedArray(&array,
                state_->resources[plane], 0, 0);
            if (arrayResult != CudaSuccess || array == nullptr) {
                if (InteropTraceEnabled()) std::fprintf(stderr,
                    "[cuda] import mappedArray plane%u failed (%d)\n", plane,
                    static_cast<int>(arrayResult));
                status = Status::Failed;
                break;
            }
            CudaMemcpy2D copy{};
            copy.srcMemoryType = MemoryTypeDevice;
            copy.srcDevice = static_cast<CUdeviceptr>(
                reinterpret_cast<std::uintptr_t>(frame->data[plane]));
            copy.srcPitch = static_cast<std::size_t>(frame->linesize[plane]);
            copy.dstMemoryType = MemoryTypeArray;
            copy.dstArray = array;
            copy.widthInBytes = state_->planeRowBytes[plane];
            copy.height = state_->planeHeight[plane];
            // Ordered on the stream the map was issued on, then synchronised: the bytes
            // must have landed before the unmap publishes them to D3D11.
            CUresult copyResult = static_cast<CUresult>(0xFFFF);
            if (state_->stream != nullptr) {
                copyResult = driver.memcpy2DAsync(&copy, state_->stream);
                if (copyResult == CudaSuccess) copyResult = driver.streamSynchronize(state_->stream);
            } else {
                copyResult = driver.memcpy2D(&copy);
            }
            if (copyResult != CudaSuccess) {
                if (InteropTraceEnabled()) std::fprintf(stderr,
                    "[cuda] import copy plane%u failed (%d) srcPitch=%zu rowBytes=%u h=%u\n",
                    plane, static_cast<int>(copyResult), copy.srcPitch,
                    state_->planeRowBytes[plane], state_->planeHeight[plane]);
                status = Status::Failed;
                break;
            }
        }
    }
    for (std::uint32_t plane = 0; plane < mapped; ++plane)
        driver.graphicsUnmapResources(1, &state_->resources[plane], state_->stream);
    CUcontext popped = nullptr;
    driver.ctxPopCurrent(&popped);
    return status;
}

bool CudaD3D11Interop::Bound() const noexcept {
    // Registered planes are the ones Import can write into; zero means the pipeline is
    // still on the CPU-writable arm.
    return state_->planeCount != 0;
}

void CudaD3D11Interop::Reset() noexcept {
    if (state_->planeCount == 0) return;
    auto& driver = state_->driver;
    // Unregistering is context-bound: the context that registered a resource is the
    // only one allowed to drop it.
    if (driver.ctxPushCurrent != nullptr && state_->context != nullptr &&
        driver.ctxPushCurrent(state_->context) == CudaSuccess) {
        for (std::uint32_t plane = 0; plane < state_->planeCount; ++plane)
            driver.graphicsUnregisterResource(state_->resources[plane]);
        CUcontext popped = nullptr;
        driver.ctxPopCurrent(&popped);
    } else if (state_->planeCount != 0 && InteropTraceEnabled()) {
        std::fprintf(stderr, "[cuda] reset could not re-enter the context; "
            "%u registration(s) left with the driver\n", state_->planeCount);
    }
    for (auto& resource : state_->resources) resource = nullptr;
    for (auto& bytes : state_->planeRowBytes) bytes = 0;
    for (auto& width : state_->planeWidth) width = 0;
    for (auto& height : state_->planeHeight) height = 0;
    state_->planeCount = 0;
}
