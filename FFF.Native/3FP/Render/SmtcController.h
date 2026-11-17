// SmtcController.h -- process-scope Win32 COM ABI for Windows.Media.SystemMediaTransportControls.
//
// The managed host (FFF.Player/Core/系统媒体会话.vb) used to drive SMTC through the
// WinRT projection (Windows.Foundation / Windows.Media), which drags
// Microsoft.Windows.SDK.NET.dll + WinRT.Runtime.dll into the single-file binary.
// This controller talks to the same Windows.Media.SystemMediaTransportControls
// runtime class directly through its ABI: RoGetActivationFactory gives us the
// ISystemMediaTransportControlsInterop, GetForWindow returns the live
// ISystemMediaTransportControls, and everything after that is plain vtable
// calls on ComPtr-held interfaces. No projection, no WinRT.Runtime dependency.
//
// Threading contract:
//   * All entry points (Initialize/Shutdown/Publish*/SetButtonEnabled/...) MUST
//     be called on the same UI thread that owns the HWND. The host already
//     marshals every call onto its Windows Forms UI thread via
//     SynchronizationContext.Post; this controller does not add its own pump.
//   * Event callbacks (button pressed / seek / rate) are delivered by the
//     runtime on whatever thread it chooses. We store the host-supplied
//     function pointers atomically and invoke them synchronously on that
//     thread. The host's trampoline MUST marshal back to the UI thread itself
//     (it already does -- 处理系统媒体按钮 does BeginInvoke). We never call
//     back into a host UI method directly from here.
//
// Failure contract:
//   * Every entry point is noexcept. If Initialize has not run or Shutdown has
//     already run, the rest return quietly (no crash, no C++ exception, no
//     HRESULT surfaced to the host). The host treats any non-zero return from
//     Initialize as "no SMTC, keep playing without the card".
#pragma once

#include <cstdint>

namespace FffSmtc {

// One-time per-process setup. Grabs the interop factory, calls GetForWindow,
// holds ISystemMediaTransportControls + ISystemMediaTransportControls2, and
// hooks ButtonPressed / PlaybackPositionChangeRequested /
// PlaybackRateChangeRequested. Returns 0 on success, non-zero on failure.
// Idempotent: calling twice on the same HWND returns the cached state.
int Initialize(void* hwnd) noexcept;

// Tears down the whole session. Unhooks events, releases every ComPtr, and
// resets the singleton to the uninitialised state. Safe to call when not
// initialised (no-op). The host calls this from 关闭/Dispose.
void Shutdown() noexcept;

// Playback status. Values MUST match the host's 系统媒体播放状态 enum exactly:
//   0=Closed 1=Changing 2=Stopped 3=Playing 4=Paused
// (They are already the Windows.Media.MediaPlaybackStatus values, so no
// conversion happens.)
void SetPlaybackStatus(int status) noexcept;

// Card metadata. All strings may be nullptr/empty; mediaType is
// 1=Music, 2=Video (matches 系统媒体显示类型). appMediaId is the stable
// SHA-256 token the host derives from the media path (never the path itself).
// albumArtist/trackNumber are music-only; trackNumber==0 means "unknown".
void PublishCard(const wchar_t* appMediaId, const wchar_t* title,
                 const wchar_t* artist, const wchar_t* album,
                 const wchar_t* albumArtist, std::uint32_t trackNumber,
                 int mediaType) noexcept;

// Encoded PNG bytes (the host's existing 设置缩略图 contract). Empty/null
// bytes clear the thumbnail; a non-empty payload is wrapped in an in-memory
// stream and handed to DisplayUpdater::put_Thumbnail. Returns 0 on success,
// non-zero when the payload could not be materialised (best-effort; callers
// must not fail playback on this).
int PublishThumbnail(const std::uint8_t* pngBytes, std::uint32_t byteCount) noexcept;

// Clears the card and disables the panel (matches the host's 清空卡片).
void ClearCard() noexcept;

// Timeline + playback rate. Times are seconds (converted here to the
// 100ns-tick TimeSpan the ABI expects). rate<=0 means "leave rate unchanged".
// seekEnabled!=0 sets MinSeekTime/MaxSeekTime to 0/end; otherwise both are
// left at 0 (system disables the scrubber).
void PublishTimeline(double startSec, double endSec, double positionSec,
                     double rate, int seekEnabled) noexcept;

// Per-button enable. kind matches the host's 系统媒体按钮 enum exactly:
//   0=Play 1=Pause 2=Stop 4=FastForward 5=Rewind 6=Next 7=Previous
// (Record=3 / ChannelUp=8 / ChannelDown=9 are forced off here; the host does
// not expose them.)
void SetButtonEnabled(int kind, int enabled) noexcept;

// Host trampolines. All four are stored atomically and invoked from the
// runtime event thread. Signatures:
//   buttonCb(user, int buttonKind) -> int  (non-zero = handled)
//   seekCb(user, double positionSec) -> int
//   rateCb(user, double rate) -> int
// The function pointers are borrowed: the host guarantees they stay valid
// for the life of the session (it pins the delegate with GCHandle or a
// static trampoline -- the existing Marshal.GetFunctionPointerForDelegate
// pattern already requires that).
void SetCallbacks(void* buttonCb, void* seekCb, void* rateCb, void* user) noexcept;

} // namespace FffSmtc
