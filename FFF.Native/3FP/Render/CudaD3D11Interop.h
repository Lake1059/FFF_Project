#pragma once

// CUDA -> D3D11 direct paste for decoded frames.
//
// Only relevant when a stream is decoded by the CUDA backend: the D3D11VA frame pool
// cannot represent the profiles that land there (HEVC 4:4:4, part of AV1), so those
// pictures used to make a full GPU->CPU->GPU round trip every frame through
// av_hwframe_transfer_data. The shipped FFmpeg has no av_hwframe_map route between
// AV_PIX_FMT_CUDA and AV_PIX_FMT_D3D11 (hwcontext_cuda registers no map_to/map_from at
// all, so the generic cross-format mapping in av_hwframe_map returns ENOSYS), so this
// class does the interop by hand: the renderer's own sampled textures are registered
// with the CUDA driver and the decoded planes are copied into them on the GPU.
//
// Everything here is generic graphics plumbing -- a device-to-device copy between two
// APIs -- with no codec or signal semantics of its own.

#include <cstdint>
#include <memory>

struct AVFrame;
struct ID3D11Device;
struct ID3D11Texture2D;

class CudaD3D11Interop final {
public:
    enum class Status : std::uint8_t {
        Success = 0,
        /// This combination will never work (wrong device, wrong format, gate off).
        /// Callers should stop asking for the current stream.
        Refused = 1,
        /// It failed now; the caller keeps its own fallback for this frame.
        Failed = 2,
    };

    CudaD3D11Interop() noexcept;
    ~CudaD3D11Interop() noexcept;
    CudaD3D11Interop(const CudaD3D11Interop&) = delete;
    CudaD3D11Interop& operator=(const CudaD3D11Interop&) = delete;

    /// On by default. FFF3FP_CUDA_D3D11_INTEROP=0 disables it and restores the
    /// unconditional download path; every per-stream refusal falls back on its own.
    static bool Enabled() noexcept;
    /// True for the plane layouts the renderer's three-plane shader arm samples as
    /// separate R8/R16 textures. Semi-planar CUDA surfaces (NV12/P010 style) are left
    /// on the existing download route.
    static bool SupportsPixelFormat(std::int32_t pixelFormat) noexcept;

    /// Learns the CUDA context a decoded frame belongs to and checks that it drives the
    /// same adapter as `device`. Cheap after the first call. Must run with the
    /// renderer's device lock held, like every other D3D11 access.
    Status Probe(const AVFrame* frame, ID3D11Device* device) noexcept;

    /// Registers the renderer's sampled plane textures with that CUDA context. The
    /// textures must be D3D11_USAGE_DEFAULT; registering drops any previous binding.
    Status Bind(ID3D11Texture2D* const* planes, std::uint32_t planeCount) noexcept;

    /// Copies every plane of `frame` into the bound textures. The copy is ordered after
    /// the decoder's own stream and completed before this returns.
    Status Import(const AVFrame* frame) noexcept;

    bool Bound() const noexcept;
    /// Unregisters everything. Must be called before the textures are released.
    void Reset() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
