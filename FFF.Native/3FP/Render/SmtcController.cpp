// SmtcController.cpp -- see SmtcController.h for the contract.
//
// Implements the Win32 COM ABI for Windows.Media.SystemMediaTransportControls:
//   RoGetActivationFactory(className, IID_PPV_ARGS(&interop))
//   interop->GetForWindow(hwnd, IID_ISystemMediaTransportControls, &smtc)
// then drives ISystemMediaTransportControls / ISystemMediaTransportControls2 /
// ISystemMediaTransportControlsDisplayUpdater / ISystemMediaTransportControlsTimeline-
//Properties through plain vtable calls.

#include "pch.h"
#include "3FP/Render/SmtcController.h"

#include <roapi.h>
#include <asyncinfo.h>
#include <windows.foundation.h>
#include <windows.media.h>
#include <windows.storage.streams.h>
#include <SystemMediaTransportControlsInterop.h>

#include <atomic>
#include <cstdint>
#include <cwchar>
#include <new>
#include <string>
#include <winstring.h>

using Microsoft::WRL::ComPtr;

// Pull in the ABI names without bringing back the projection.
using namespace ABI::Windows::Media;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Storage::Streams;

namespace FffSmtc {
namespace {

// ---------------------------------------------------------------------------
// Small RAII wrappers around HSTRING / TimeSpan so the call sites stay linear.
// ---------------------------------------------------------------------------

struct HStringHolder {
    HSTRING raw = nullptr;
    HStringHolder() = default;
    explicit HStringHolder(const wchar_t* text) {
        if (text != nullptr) {
            WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), &raw);
        }
    }
    ~HStringHolder() { if (raw != nullptr) WindowsDeleteString(raw); }
    HStringHolder(const HStringHolder&) = delete;
    HStringHolder& operator=(const HStringHolder&) = delete;
    explicit operator bool() const { return raw != nullptr; }
};

inline TimeSpan SecondsToTimeSpan(double seconds) noexcept {
    TimeSpan ts{};
    // TimeSpan is int64 100-ns ticks. Clamp negative / NaN to 0.
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    ts.Duration = static_cast<std::int64_t>(seconds * 10'000'000.0);
    return ts;
}

// ---------------------------------------------------------------------------
// Event handlers. Each is a minimal non-delegating IUnknown that implements
// the exact TypedEventHandler ABI the runtime expects. We deliberately do NOT
// pull in wrl/event.h (Callback<T>) to avoid dragging the whole event-token
// machinery; the vtable here is exactly QueryInterface/AddRef/Release/Invoke.
// ---------------------------------------------------------------------------

struct ButtonHandler
    : public __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs {
    std::atomic<ULONG> refCount{1};
    HRESULT __stdcall QueryInterface(REFIID riid, void** out) noexcept override;
    ULONG   __stdcall AddRef() noexcept override;
    ULONG   __stdcall Release() noexcept override;
    HRESULT __stdcall Invoke(ISystemMediaTransportControls* sender,
                             ISystemMediaTransportControlsButtonPressedEventArgs* args) noexcept override;
};

struct PositionHandler
    : public __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgs {
    std::atomic<ULONG> refCount{1};
    HRESULT __stdcall QueryInterface(REFIID riid, void** out) noexcept override;
    ULONG   __stdcall AddRef() noexcept override;
    ULONG   __stdcall Release() noexcept override;
    HRESULT __stdcall Invoke(ISystemMediaTransportControls* sender,
                             IPlaybackPositionChangeRequestedEventArgs* args) noexcept override;
};

struct RateHandler
    : public __FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackRateChangeRequestedEventArgs {
    std::atomic<ULONG> refCount{1};
    HRESULT __stdcall QueryInterface(REFIID riid, void** out) noexcept override;
    ULONG   __stdcall AddRef() noexcept override;
    ULONG   __stdcall Release() noexcept override;
    HRESULT __stdcall Invoke(ISystemMediaTransportControls* sender,
                             IPlaybackRateChangeRequestedEventArgs* args) noexcept override;
};

// ---------------------------------------------------------------------------
// Process-global SMTC state. Exactly one live session per process; the host
// only ever drives one window's SMTC.
// ---------------------------------------------------------------------------

// Host trampoline signatures (must match the VB Declare side-for-side).
using ButtonCb = int(__stdcall*)(void* user, int buttonKind);
using DoubleArgCb = int(__stdcall*)(void* user, double value);

struct State {
    ComPtr<ISystemMediaTransportControls> smtc;
    ComPtr<ISystemMediaTransportControls2> smtc2;
    ComPtr<ISystemMediaTransportControlsDisplayUpdater> updater;

    EventRegistrationToken buttonToken{};
    EventRegistrationToken positionToken{};
    EventRegistrationToken rateToken{};

    // Handlers are owned by us; add_* AddRefs them, remove_* releases. We keep
    // the raw pointers so we can drop our own reference after unhooking.
    ButtonHandler* buttonHandler = nullptr;
    PositionHandler* positionHandler = nullptr;
    RateHandler* rateHandler = nullptr;

    std::atomic<void*> buttonCb{nullptr};
    std::atomic<void*> seekCb{nullptr};
    std::atomic<void*> rateCb{nullptr};
    std::atomic<void*> cbUser{nullptr};

    // Remember whether RoInitialize gave us a new init (so we know whether to
    // RoUninitialize on shutdown). We never force a threading model on a host
    // that already initialised COM; RPC_E_CHANGED_MODE is treated as success.
    bool roInitializedHere = false;
    bool eventsHooked = false;
};

State& G() noexcept {
    static State s;
    return s;
}

// ---------------------------------------------------------------------------
// IUnknown boilerplate (identical shape for all three handlers).
// ---------------------------------------------------------------------------

#define IMPL_HANDLER_UNKNOWN(T, BaseIid) \
    HRESULT T::QueryInterface(REFIID riid, void** out) noexcept { \
        if (out == nullptr) return E_POINTER; \
        if (riid == __uuidof(IUnknown) || riid == (BaseIid)) { \
            *out = static_cast<T*>(this); AddRef(); return S_OK; \
        } \
        *out = nullptr; return E_NOINTERFACE; \
    } \
    ULONG T::AddRef() noexcept { return refCount.fetch_add(1, std::memory_order_relaxed) + 1; } \
    ULONG T::Release() noexcept { \
        const ULONG after = refCount.fetch_sub(1, std::memory_order_acq_rel) - 1; \
        if (after == 0) delete this; \
        return after; \
    }

IMPL_HANDLER_UNKNOWN(ButtonHandler, __uuidof(__FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CSystemMediaTransportControlsButtonPressedEventArgs))
IMPL_HANDLER_UNKNOWN(PositionHandler, __uuidof(__FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackPositionChangeRequestedEventArgs))
IMPL_HANDLER_UNKNOWN(RateHandler, __uuidof(__FITypedEventHandler_2_Windows__CMedia__CSystemMediaTransportControls_Windows__CMedia__CPlaybackRateChangeRequestedEventArgs))

// ---------------------------------------------------------------------------
// Event Invoke bodies. Called on the runtime's event thread. We forward to
// the host trampoline immediately; the host is responsible for marshalling
// back to the UI thread. No COM calls back into the SMTC object on this path
// (we deliberately do NOT call e.g. put_PlaybackStatus here) so we cannot
// deadlock against the UI thread's writes.
// ---------------------------------------------------------------------------

HRESULT ButtonHandler::Invoke(ISystemMediaTransportControls*,
                              ISystemMediaTransportControlsButtonPressedEventArgs* args) noexcept {
    if (args == nullptr) return S_OK;
    SystemMediaTransportControlsButton button = SystemMediaTransportControlsButton_Play;
    if (FAILED(args->get_Button(&button))) return S_OK;
    auto cb = reinterpret_cast<ButtonCb>(G().buttonCb.load(std::memory_order_acquire));
    if (cb != nullptr) {
        cb(G().cbUser.load(std::memory_order_acquire), static_cast<int>(button));
    }
    return S_OK;
}

HRESULT PositionHandler::Invoke(ISystemMediaTransportControls*,
                                IPlaybackPositionChangeRequestedEventArgs* args) noexcept {
    if (args == nullptr) return S_OK;
    TimeSpan ts{};
    if (FAILED(args->get_RequestedPlaybackPosition(&ts))) return S_OK;
    auto cb = reinterpret_cast<DoubleArgCb>(G().seekCb.load(std::memory_order_acquire));
    if (cb != nullptr) {
        cb(G().cbUser.load(std::memory_order_acquire),
           static_cast<double>(ts.Duration) / 10'000'000.0);
    }
    return S_OK;
}

HRESULT RateHandler::Invoke(ISystemMediaTransportControls*,
                            IPlaybackRateChangeRequestedEventArgs* args) noexcept {
    if (args == nullptr) return S_OK;
    double rate = 1.0;
    if (FAILED(args->get_RequestedPlaybackRate(&rate))) return S_OK;
    auto cb = reinterpret_cast<DoubleArgCb>(G().rateCb.load(std::memory_order_acquire));
    if (cb != nullptr) {
        cb(G().cbUser.load(std::memory_order_acquire), rate);
    }
    return S_OK;
}

// Writes an HSTRING property. Empty/null text is skipped (the host's existing
// behaviour is to leave the previous value untouched when a field is empty).
template <typename Prop, typename Putter>
void PutString(Prop* props, Putter putter, const wchar_t* text) noexcept {
    if (props == nullptr || text == nullptr || text[0] == L'\0') return;
    HStringHolder h(text);
    if (h) {
        (props->*putter)(h.raw);
    }
}

// Resolve a WinRT activation factory by runtime-class name without pulling in
// the C++/WinRT projection. Used only for the two metadata factories below.
template <typename T>
bool GetFactory(const wchar_t* className, ComPtr<T>& out) noexcept {
    HSTRING_HEADER header{};
    HSTRING name = nullptr;
    if (FAILED(WindowsCreateStringReference(
            className, static_cast<UINT32>(wcslen(className)), &header, &name)) ||
        name == nullptr) {
        return false;
    }
    return SUCCEEDED(RoGetActivationFactory(name, IID_PPV_ARGS(out.GetAddressOf())));
}

// The thumbnail path is materialised separately by PublishThumbnail (PNG
// bytes from the host) so a normal card update never clears a thumbnail the
// host set independently.

} // namespace

// ===========================================================================
// Public entry points
// ===========================================================================

int Initialize(void* hwnd) noexcept {
    State& s = G();
    if (s.smtc != nullptr) return 0; // already up
    if (hwnd == nullptr) return 1;

    // Tolerate the host already running an STA (WinForms UI thread): that is
    // exactly the threading model SMTC wants. We only RoUninitialize if WE
    // made the init call succeed.
    const auto ro = RoInitialize(RO_INIT_MULTITHREADED);
    s.roInitializedHere = SUCCEEDED(ro);

    int result = 1;
    do {
        HSTRING_HEADER classHeader{};
        HSTRING className = nullptr;
        if (FAILED(WindowsCreateStringReference(
                RuntimeClass_Windows_Media_SystemMediaTransportControls,
                ARRAYSIZE(RuntimeClass_Windows_Media_SystemMediaTransportControls) - 1,
                &classHeader, &className)) || className == nullptr) break;

        ComPtr<ISystemMediaTransportControlsInterop> interop;
        if (FAILED(RoGetActivationFactory(
                className, IID_PPV_ARGS(&interop))) || interop == nullptr) break;

        ComPtr<ISystemMediaTransportControls> rawSmtc;
        if (FAILED(interop->GetForWindow(static_cast<HWND>(hwnd),
                                         IID_ISystemMediaTransportControls,
                                         reinterpret_cast<void**>(rawSmtc.GetAddressOf()))) ||
            rawSmtc == nullptr) break;

        s.smtc = rawSmtc;
        rawSmtc->QueryInterface(IID_PPV_ARGS(&s.smtc2));
        rawSmtc->get_DisplayUpdater(&s.updater);

        // Start invisible; the first PublishCard flips IsEnabled on. This
        // matches the host's existing "no phantom card on open failure" rule.
        s.smtc->put_IsEnabled(FALSE);
        s.smtc->put_PlaybackStatus(MediaPlaybackStatus_Closed);
        if (s.updater != nullptr) s.updater->ClearAll();

        // Hook events. We new the handlers (refcount starts at 1) and hand
        // them to add_* which AddRefs them. After remove_* the runtime drops
        // its ref; we then drop ours in Shutdown.
        s.buttonHandler = new (std::nothrow) ButtonHandler();
        s.positionHandler = new (std::nothrow) PositionHandler();
        s.rateHandler = new (std::nothrow) RateHandler();
        if (s.buttonHandler == nullptr || s.positionHandler == nullptr ||
            s.rateHandler == nullptr) break;

        if (FAILED(s.smtc->add_ButtonPressed(s.buttonHandler, &s.buttonToken))) break;
        if (s.smtc2 == nullptr) break;
        if (FAILED(s.smtc2->add_PlaybackPositionChangeRequested(s.positionHandler,
                                                                &s.positionToken))) break;
        if (FAILED(s.smtc2->add_PlaybackRateChangeRequested(s.rateHandler,
                                                            &s.rateToken))) break;
        s.eventsHooked = true;

        result = 0;
    } while (false);

    if (result != 0) {
        Shutdown();
    }
    return result;
}

void Shutdown() noexcept {
    State& s = G();

    if (s.eventsHooked) {
        // Unhook in reverse order. Ignore failures -- the window is going away
        // anyway and the runtime tears its own refs down with the app.
        if (s.smtc2 != nullptr) {
            s.smtc2->remove_PlaybackRateChangeRequested(s.rateToken);
            s.smtc2->remove_PlaybackPositionChangeRequested(s.positionToken);
        }
        if (s.smtc != nullptr) {
            s.smtc->remove_ButtonPressed(s.buttonToken);
        }
        s.eventsHooked = false;
    }

    // Drop our own references to the handlers. The runtime should have already
    // dropped its refs in remove_*, but be defensive.
    if (s.buttonHandler != nullptr) { s.buttonHandler->Release(); s.buttonHandler = nullptr; }
    if (s.positionHandler != nullptr) { s.positionHandler->Release(); s.positionHandler = nullptr; }
    if (s.rateHandler != nullptr) { s.rateHandler->Release(); s.rateHandler = nullptr; }

    s.updater.Reset();
    s.smtc2.Reset();
    s.smtc.Reset();

    s.buttonCb.store(nullptr, std::memory_order_release);
    s.seekCb.store(nullptr, std::memory_order_release);
    s.rateCb.store(nullptr, std::memory_order_release);
    s.cbUser.store(nullptr, std::memory_order_release);

    if (s.roInitializedHere) {
        RoUninitialize();
        s.roInitializedHere = false;
    }
}

void SetPlaybackStatus(int status) noexcept {
    State& s = G();
    if (s.smtc == nullptr) return;
    if (status < 0 || status > 4) return;
    s.smtc->put_PlaybackStatus(static_cast<MediaPlaybackStatus>(status));
}

void PublishCard(const wchar_t* appMediaId, const wchar_t* title,
                 const wchar_t* artist, const wchar_t* album,
                 const wchar_t* albumArtist, std::uint32_t trackNumber,
                 int mediaType) noexcept {
    State& s = G();
    if (s.smtc == nullptr || s.updater == nullptr) return;

    // mediaType: 1=Music, 2=Video. Anything else defaults to Music (matches
    // 系统媒体显示类型 default in the host).
    const auto type = (mediaType == 2) ? MediaPlaybackType_Video : MediaPlaybackType_Music;
    s.updater->put_Type(type);

    // AppMediaId is common to both display types and is what lets the system
    // correlate card updates with the same media. The host supplies a SHA-256
    // token, never the local path.
    PutString(s.updater.Get(), &ISystemMediaTransportControlsDisplayUpdater::put_AppMediaId,
              appMediaId);

    ComPtr<IMusicDisplayProperties> music;
    ComPtr<IVideoDisplayProperties> video;
    if (type == MediaPlaybackType_Music) {
        s.updater->get_MusicProperties(&music);
    } else {
        s.updater->get_VideoProperties(&video);
    }

    if (music != nullptr) {
        PutString(music.Get(), &IMusicDisplayProperties::put_Title, title);
        PutString(music.Get(), &IMusicDisplayProperties::put_Artist, artist);
        PutString(music.Get(), &IMusicDisplayProperties::put_AlbumArtist, albumArtist);

        ComPtr<IMusicDisplayProperties2> music2;
        if (SUCCEEDED(music.As(&music2))) {
            PutString(music2.Get(), &IMusicDisplayProperties2::put_AlbumTitle, album);
            // 0 is the documented "unknown" value; writing it also clears any
            // previous track number on a reused card.
            music2->put_TrackNumber(trackNumber);
        }
    } else if (video != nullptr) {
        PutString(video.Get(), &IVideoDisplayProperties::put_Title, title);
        // Video cards do not show artist/album/album-artist/track in the host's
        // existing usage; the C API still accepts them for symmetry.
    }

    s.updater->Update();
    // First successful card lights the panel.
    s.smtc->put_IsEnabled(TRUE);
}

int PublishThumbnail(const std::uint8_t* pngBytes, std::uint32_t byteCount) noexcept {
    State& s = G();
    if (s.updater == nullptr) return 1;

    if (pngBytes == nullptr || byteCount == 0) {
        s.updater->put_Thumbnail(nullptr);
        return 0;
    }

    // The host contract is encoded PNG bytes (the old 设置缩略图 API).
    // Materialise them in an InMemoryRandomAccessStream and hand the system a
    // RandomAccessStreamReference; no StorageFile activation or temp file is
    // involved, so this also works inside the single-file self-extract layout.
    HStringHolder className(RuntimeClass_Windows_Storage_Streams_InMemoryRandomAccessStream);
    if (!className) return 1;

    ComPtr<IInspectable> instance;
    if (FAILED(RoActivateInstance(className.raw, instance.GetAddressOf())) ||
        instance == nullptr) {
        return 1;
    }

    ComPtr<IRandomAccessStream> stream;
    if (FAILED(instance.As(&stream)) || stream == nullptr) return 1;

    ComPtr<IOutputStream> output;
    if (FAILED(stream->GetOutputStreamAt(0, output.GetAddressOf())) ||
        output == nullptr) {
        return 1;
    }

    ComPtr<IDataWriterFactory> writerFactory;
    if (!GetFactory(L"Windows.Storage.Streams.DataWriter", writerFactory)) return 1;

    ComPtr<IDataWriter> writer;
    if (FAILED(writerFactory->CreateDataWriter(output.Get(), writer.GetAddressOf())) ||
        writer == nullptr) {
        return 1;
    }

    if (FAILED(writer->WriteBytes(byteCount, const_cast<BYTE*>(pngBytes)))) return 1;

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<UINT32>> store;
    if (FAILED(writer->StoreAsync(store.GetAddressOf())) || store == nullptr) return 1;

    ComPtr<IAsyncInfo> storeInfo;
    if (FAILED(store.As(&storeInfo)) || storeInfo == nullptr) return 1;

    // PNG payloads are tiny; poll the operation on the UI thread instead of
    // building a completion-handler event just for this one-shot call. The
    // bound avoids an unbounded hang if the stream implementation stalls.
    AsyncStatus status = Started;
    for (int i = 0; i < 2000; ++i) {
        if (FAILED(storeInfo->get_Status(&status))) return 1;
        if (status != Started) break;
        Sleep(1);
    }
    if (status != Completed) return 1;

    UINT32 written = 0;
    if (FAILED(store->GetResults(&written)) || written < byteCount) return 1;

    ComPtr<IRandomAccessStreamReferenceStatics> referenceFactory;
    if (!GetFactory(L"Windows.Storage.Streams.RandomAccessStreamReference",
                    referenceFactory)) {
        return 1;
    }

    ComPtr<IRandomAccessStreamReference> reference;
    if (FAILED(referenceFactory->CreateFromStream(stream.Get(),
                                                  reference.GetAddressOf())) ||
        reference == nullptr) {
        return 1;
    }

    s.updater->put_Thumbnail(reference.Get());
    return 0;
}

void ClearCard() noexcept {
    State& s = G();
    if (s.smtc == nullptr) return;
    if (s.updater != nullptr) s.updater->ClearAll();
    s.smtc->put_PlaybackStatus(MediaPlaybackStatus_Closed);
    s.smtc->put_IsEnabled(FALSE);
}

void PublishTimeline(double startSec, double endSec, double positionSec,
                     double rate, int seekEnabled) noexcept {
    State& s = G();
    if (s.smtc2 == nullptr) return;

    // Activate a fresh TimelineProperties instance. The runtime class is
    // activatable; we do not cache it because the host recreates one every
    // tick anyway (and the SMTC ABI wants a fresh object per Update).
    ComPtr<ISystemMediaTransportControlsTimelineProperties> props;
    {
        HSTRING_HEADER header{};
        HSTRING cn = nullptr;
        if (FAILED(WindowsCreateStringReference(
                RuntimeClass_Windows_Media_SystemMediaTransportControlsTimelineProperties,
                ARRAYSIZE(RuntimeClass_Windows_Media_SystemMediaTransportControlsTimelineProperties) - 1,
                &header, &cn)) || cn == nullptr) return;
        ComPtr<IInspectable> inspectable;
        if (FAILED(RoActivateInstance(cn, &inspectable)) || inspectable == nullptr) return;
        if (FAILED(inspectable.As(&props)) || props == nullptr) return;
    }

    props->put_StartTime(SecondsToTimeSpan(startSec));
    props->put_EndTime(SecondsToTimeSpan(endSec));
    props->put_Position(SecondsToTimeSpan(positionSec));
    if (seekEnabled != 0) {
        props->put_MinSeekTime(SecondsToTimeSpan(0.0));
        props->put_MaxSeekTime(SecondsToTimeSpan(endSec));
    } else {
        props->put_MinSeekTime(SecondsToTimeSpan(0.0));
        props->put_MaxSeekTime(SecondsToTimeSpan(0.0));
    }
    s.smtc2->UpdateTimelineProperties(props.Get());

    if (std::isfinite(rate) && rate > 0.0) {
        s.smtc2->put_PlaybackRate(rate);
    }
}

void SetButtonEnabled(int kind, int enabled) noexcept {
    State& s = G();
    if (s.smtc == nullptr) return;
    const BOOL value = enabled ? TRUE : FALSE;
    switch (kind) {
        case 0: s.smtc->put_IsPlayEnabled(value); break;
        case 1: s.smtc->put_IsPauseEnabled(value); break;
        case 2: s.smtc->put_IsStopEnabled(value); break;
        case 4: s.smtc->put_IsFastForwardEnabled(value); break;
        case 5: s.smtc->put_IsRewindEnabled(value); break;
        case 6: s.smtc->put_IsNextEnabled(value); break;
        case 7: s.smtc->put_IsPreviousEnabled(value); break;
        // Record=3 / ChannelUp=8 / ChannelDown=9 are forced off here to match
        // the host's existing "no record/channel buttons" rule.
        default: break;
    }
}

void SetCallbacks(void* buttonCb, void* seekCb, void* rateCb, void* user) noexcept {
    State& s = G();
    s.buttonCb.store(buttonCb, std::memory_order_release);
    s.seekCb.store(seekCb, std::memory_order_release);
    s.rateCb.store(rateCb, std::memory_order_release);
    s.cbUser.store(user, std::memory_order_release);
}

} // namespace FffSmtc

// ===========================================================================
// C exports. Same extern "C" __declspec(dllexport) pattern as FFF.Player.Api.h.
// We do NOT go through FFF3FP_API here: these are process-wide (no player
// handle) and appended additively, so the host resolves them by name with
// GetProcAddress / P-Invoke and treats a missing export as "SMTC unavailable".
// ===========================================================================

extern "C" {

__declspec(dllexport) int __cdecl FFF3FP_SmtcInitialize(void* hwnd) noexcept {
    return FffSmtc::Initialize(hwnd);
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcShutdown() noexcept {
    FffSmtc::Shutdown();
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcSetPlaybackStatus(int status) noexcept {
    FffSmtc::SetPlaybackStatus(status);
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcPublishCard(const wchar_t* appMediaId,
                                                          const wchar_t* title,
                                                          const wchar_t* artist,
                                                          const wchar_t* album,
                                                          const wchar_t* albumArtist,
                                                          std::uint32_t trackNumber,
                                                          int mediaType) noexcept {
    FffSmtc::PublishCard(appMediaId, title, artist, album, albumArtist,
                         trackNumber, mediaType);
}

__declspec(dllexport) int __cdecl FFF3FP_SmtcPublishThumbnail(const void* pngBytes,
                                                              std::uint32_t byteCount) noexcept {
    return FffSmtc::PublishThumbnail(static_cast<const std::uint8_t*>(pngBytes),
                                     byteCount);
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcClearCard() noexcept {
    FffSmtc::ClearCard();
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcPublishTimeline(double startSec,
                                                              double endSec,
                                                              double positionSec,
                                                              double rate,
                                                              int seekEnabled) noexcept {
    FffSmtc::PublishTimeline(startSec, endSec, positionSec, rate, seekEnabled);
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcSetButtonEnabled(int kind, int enabled) noexcept {
    FffSmtc::SetButtonEnabled(kind, enabled);
}

__declspec(dllexport) void __cdecl FFF3FP_SmtcSetCallbacks(void* buttonCb,
                                                           void* seekCb,
                                                           void* rateCb,
                                                           void* user) noexcept {
    FffSmtc::SetCallbacks(buttonCb, seekCb, rateCb, user);
}

} // extern "C"
