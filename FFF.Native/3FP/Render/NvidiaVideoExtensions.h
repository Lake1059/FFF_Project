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

// RTX Video HDR ("TrueHDR"), NVIDIA's automatic SDR->HDR enhancement. It is a
// second vendor-private stream extension on the very same
// ID3D11VideoProcessor, with a different GUID and payload:
//
//   probe  VideoProcessorGetStreamExtension(guid, sizeof(UINT), &out)
//          supported <=> the UINT reads back 1. Unlike the PPE GUID, this one
//          DOES answer the capability query -- but the HRESULT still lies
//          (S_OK on adapters without the feature), so the payload is the only
//          usable signal.
//   set    VideoProcessorSetStreamExtension with a 12-byte payload
//          {version = 4, method = 3, enable : 1 | reserved : 31}.
//
// Interface facts taken from mpv (video/filter/vf_d3d11vpp.c) and Chromium's
// ToggleNvidiaVpHdr, which agree byte for byte:
//   * SDR input only (mpv commit 5abce15f1: the extension is requested only
//     for non-HDR frames).
//   * Output contract: R10G10B10A2_UNORM samples, BT.2020 primaries, PQ
//     transfer, 1000-nit peak. The caller MUST re-tag the frame accordingly;
//     mpv 0.41.0 shipped a build that called the extension without re-tagging
//     and produced a screenshot bit-identical to the un-enhanced one -- the
//     driver honoured the call and the picture did not change.
//   * Needs an HDR10-capable display with Windows HDR enabled to be visible.
inline constexpr GUID TrueHdrInterfaceGuid{
    0xfdd62bb4, 0x620b, 0x4fd7, {0x9a, 0xb3, 0x1e, 0x59, 0xd0, 0xd5, 0x44, 0xb3}};

inline constexpr UINT StreamExtensionVersionTrueHdr = 0x4;
inline constexpr UINT StreamExtensionMethodTrueHdr = 0x3;

struct TrueHdrStreamExtension {
    UINT version;
    UINT method;
    UINT enable;  // bit 0 is the switch; the remaining 31 bits are reserved and must be 0
};

/// TrueHDR availability probe. Supported <=> the driver's read-back UINT is 1.
/// The HRESULT is deliberately ignored: on this GUID it returns S_OK even
/// where the feature does not exist, so a caller that trusts it would report
/// a capability the adapter does not have.
bool IsTrueHdrSupported(ID3D11VideoContext* videoContext,
    ID3D11VideoProcessor* videoProcessor) noexcept;

/// Enable or disable TrueHDR on one video processor's stream 0. Returns the
/// driver's HRESULT from the Set call. Same discipline as SetSuperResolution:
/// success is permission to attempt, never proof of effect -- the caller must
/// verify the blit AND re-tag the output samples as PQ/BT.2020.
HRESULT SetTrueHdr(ID3D11VideoContext* videoContext,
    ID3D11VideoProcessor* videoProcessor, bool enable) noexcept;

} // namespace FFF3FP::NvidiaVideo
