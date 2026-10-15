#pragma once

// NVIDIA driver-level Video Processor private extensions.
//
// RTX Video Super Resolution is NOT a separate API: it is a vendor-private
// stream extension on the standard ID3D11VideoProcessor, reached through
// ID3D11VideoContext::VideoProcessorSetStreamExtension with an NVIDIA GUID.
//
// The GUID and payload layout below appear byte-for-byte identical in five
// independent implementations, which is why they are treated as a stable
// de-facto interface rather than a guess:
//
//   Chromium  ui/gl/swap_chain_presenter.cc       ToggleNvidiaVpSuperResolution()
//   mpv       video/filter/vf_d3d11vpp.c          enable_nvidia_rtx_extension()
//   VLC       modules/video_output/win32/d3d11_scaler.cpp
//   Kodi      xbmc/.../HwDecRender/DXVAHD.cpp     EnableNvidiaRTXVideoSuperResolution()
//   MPCVR     Source/D3D11VP.cpp                  SetSuperResNvidia()
//
// The quality level (1-4 / Auto) is NOT part of this payload. It is a global
// setting owned by the NVIDIA App / Control Panel, so applications only ever
// pass enable = 0 or 1.
//
// Scope note: this header is additive and does not participate in the
// PlayerApiVersion contract. It introduces no exported symbols of its own.

#include <cstdint>

#include <d3d11.h>
#include <dxgi.h>

namespace FFF3FP::NvidiaVideo {

// NVIDIA_PPE_INTERFACE_GUID ("PPE" = Post Processing / Pixel Engine).
inline constexpr GUID PpeInterfaceGuid{
    0xd43ce1b3, 0x1f4b, 0x48ac, {0xba, 0xee, 0xc3, 0xc2, 0x53, 0x75, 0xe6, 0xf7}};

inline constexpr UINT StreamExtensionVersionV1 = 0x1;
inline constexpr UINT StreamExtensionMethodSuperResolution = 0x2;

struct SuperResolutionStreamExtension {
    UINT version;
    UINT method;
    UINT enable;
};

inline constexpr UINT NvidiaVendorId = 0x10DEU;

/// Vendor id of the adapter backing `device`, or 0 when it cannot be read.
/// Chromium gates its NVIDIA path on exactly this value.
std::uint32_t AdapterVendorId(ID3D11Device* device) noexcept;

/// True when the adapter backing `device` is NVIDIA.
bool IsNvidiaAdapter(ID3D11Device* device) noexcept;

/// Enable or disable RTX Video Super Resolution on one video processor's
/// stream 0. Returns the driver's HRESULT.
///
/// NOTE ON DETECTION: there is no reliable "is VSR supported?" query on this
/// path, and the HRESULT turns out to carry no capability information at all.
/// Measured on an RTX 4060 Laptop with driver 616.92:
/// VideoProcessorGetStreamExtension returned S_OK -- with the payload reading
/// back {0,0,0} every time -- for the PPE GUID immediately after a successful
/// Set(enable=1), for a deliberately wrong DataSize (4 instead of 12), and even
/// for a GUID that cannot exist. So neither a successful Get nor a successful
/// Set proves anything. Chromium, mpv and VLC therefore all just call this
/// function and treat a failed VideoProcessorBlt as the only real signal.
/// Callers must do the same: attempt, and latch the feature off if the blit
/// fails. A host must not present the resulting "active" bit to a user as
/// evidence that the picture was enhanced; it only means the driver did not
/// refuse the request, and the quality level itself belongs to the NVIDIA App
/// or Control Panel, which this API cannot read.
HRESULT SetSuperResolution(ID3D11VideoContext* videoContext,
    ID3D11VideoProcessor* videoProcessor, bool enable) noexcept;

} // namespace FFF3FP::NvidiaVideo
