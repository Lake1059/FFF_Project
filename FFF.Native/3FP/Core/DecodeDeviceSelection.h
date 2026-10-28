#pragma once
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
