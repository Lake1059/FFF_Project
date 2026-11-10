#pragma once
#include <cstddef>
#include <cstdint>

struct AVBufferRef;
class PlayerVideoRenderer;

// DXGI indices and CUDA ordinals are different namespaces. Resolve the selected
// adapter through CUDA's D3D11 interop instead of assuming both indices match.
int CreateDecodeHardwareDevice(AVBufferRef** output, int deviceType,
    std::int32_t adapterIndex, PlayerVideoRenderer& renderer) noexcept;

// D3D11 textures from a different device must be downloaded before presentation.
bool CanPresentHardwareFrameDirectly(const struct AVFrame* frame,
    PlayerVideoRenderer& renderer) noexcept;

// An ordered plan of DXGI adapter indices to try for hardware decoding.
// index == -1 is the historical device: the renderer's own D3D11 device for
// zero-copy presentation, and FFmpeg's default CUDA device. It is always the
// first automatic candidate, so the measured fast path stays unchanged.
struct DecodeAdapterPlan {
    static constexpr std::size_t Capacity = 16;
    std::int32_t indices[Capacity]{};
    std::size_t count = 0;
    // DXGI index of the adapter owning the playback D3D11 device, or -1.
    std::int32_t rendererIndex = -1;
};

// preferredIndex is the host's manual setting (index >= 0) or -1.
// multiAdapter=false keeps the single-device order; true appends the remaining
// hardware adapters in DXGI order, with the playback adapter excluded because
// it is already covered by the shared-device candidate.
DecodeAdapterPlan BuildDecodeAdapterPlan(std::int32_t preferredIndex,
    PlayerVideoRenderer& renderer, bool multiAdapter) noexcept;
