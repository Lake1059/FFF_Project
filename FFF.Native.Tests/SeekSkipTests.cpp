#include "3FP/Api/FFF.Player.Api.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;
constexpr std::int64_t kNoPts = INT64_MIN;

namespace {
std::atomic<std::uint32_t> g_seekEvents{0};

void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int PickHardwareAdapter(std::wstring* name) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) return -1;
    int best = -1;
    unsigned long long bestMemory = 0;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(i, &adapter)) || !adapter) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
        if (best < 0 || desc.DedicatedVideoMemory > bestMemory) {
            best = static_cast<int>(i);
            bestMemory = desc.DedicatedVideoMemory;
            if (name != nullptr) *name = desc.Description;
        }
    }
    return best;
}

HWND CreateProbeWindow() {
    static const wchar_t* kClass = L"FFF3FPSeekSkipTestWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    HWND window = CreateWindowExW(0, kClass, L"3FP D3D12 seek test",
        WS_OVERLAPPEDWINDOW, 40, 40, 960, 540, nullptr, nullptr, wc.hInstance, nullptr);
    if (window != nullptr) {
        ShowWindow(window, SW_SHOWNORMAL);
        UpdateWindow(window);
    }
    return window;
}

void __cdecl OnPlayerEvent(void*, FFF3FPEvent type, const char* detail) {
    if (type != FFF3FPEvent::OperationCompleted || detail == nullptr) return;
    if (std::strstr(detail, "\"operation\":\"seek\"") != nullptr ||
        std::strstr(detail, "\"operation\":\"seek-frame\"") != nullptr)
        g_seekEvents.fetch_add(1, std::memory_order_release);
}

bool Snapshot(FFF3FPHandle player, FFF3FPSnapshot& snapshot) {
    snapshot = {};
    snapshot.size = sizeof(snapshot);
    snapshot.version = 23;
    return FFF3FP_GetSnapshot(player, &snapshot) == FFFResult::Success;
}

bool WaitState(FFF3FPHandle player, FFF3FPState wanted, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        PumpMessages();
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (snapshot.state == wanted) return true;
        if (snapshot.state == FFF3FPState::Failed) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool WaitSeekEvent(std::uint32_t baseline, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        PumpMessages();
        if (g_seekEvents.load(std::memory_order_acquire) > baseline) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool WaitFrame(FFF3FPHandle player, std::int64_t frameIndex, std::int64_t framePts,
    int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < deadline) {
        PumpMessages();
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) return false;
        if (snapshot.state == FFF3FPState::Failed) return false;
        if (snapshot.frameIndex == frameIndex &&
            (framePts == kNoPts || snapshot.framePts == framePts)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

struct Result {
    std::int64_t frameIndex = -1;
    std::int64_t framePts = kNoPts;
    std::uint64_t presented = 0;
    std::uint32_t decodeMode = 0;
    std::uint32_t uploadPath = 0;
    std::uint32_t renderBackend = 0;
};

struct Expectation {
    std::int64_t frameIndex;
    std::int64_t framePts;
};

bool RunSeek(const char* file, const bool frameSeek, const std::int64_t target,
    const bool skipEnabled, const Expectation& expected, Result& result,
    const std::int64_t warmFrame = -1, const bool gpu12 = false) {
    SetEnvironmentVariableW(L"FFF_SEEK_SKIP", skipEnabled ? L"1" : L"0");
    SetEnvironmentVariableW(L"FFF_SEEK_TRACE", L"0");
    SetEnvironmentVariableW(L"FFF_SEEK_PROFILE", L"0");
    g_seekEvents.store(0, std::memory_order_release);

    std::wstring adapterName;
    const int adapterIndex = gpu12 ? PickHardwareAdapter(&adapterName) : -1;
    HWND window = gpu12 ? CreateProbeWindow() : nullptr;
    if (gpu12 && (window == nullptr || adapterIndex < 0)) {
        std::fprintf(stderr, "D3D12 probe prerequisites unavailable: adapter=%d window=%p\n",
            adapterIndex, window);
        if (window != nullptr) DestroyWindow(window);
        return false;
    }

    FFF3FPConfiguration config{};
    config.size = sizeof(config);
    config.version = 20;
    config.decodeMode = gpu12 ? FFF3FPDecodeMode::Gpu : FFF3FPDecodeMode::Cpu;
    config.colorMode = FFF3FPColorMode::MapToSdr;
    config.sdrPeakNits = 100.0f;
    config.hdrPeakNits = 1000.0f;
    config.sdrPaperWhiteNits = 100.0f;
    config.preferredAdapterIndex = gpu12 ? adapterIndex : -1;
    config.outputWindow = gpu12 ? window : nullptr;
    config.eventCallback = OnPlayerEvent;
    config.eventCallbackContext = nullptr;

    FFF3FPHandle player = nullptr;
    if (FFF3FP_Create(&config, &player) != FFFResult::Success || player == nullptr) {
        std::fprintf(stderr, "create failed\n");
        if (window != nullptr) DestroyWindow(window);
        return false;
    }
    if (gpu12 && FFF3FP_SetVideoRenderBackend(player, 1) != FFFResult::Success) {
        std::fprintf(stderr, "D3D12 render backend rejected\n");
        FFF3FP_Destroy(player);
        DestroyWindow(window);
        return false;
    }
    if (gpu12) {
        std::printf("D3D12 adapter=%d name=%ls\n", adapterIndex, adapterName.c_str());
    }
    bool opened = false;
    do {
        if (FFF3FP_Open(player, file) != FFFResult::Success ||
            !WaitState(player, FFF3FPState::Ready, 15000)) {
            std::fprintf(stderr, "open failed: %s\n", file);
            break;
        }
        opened = true;
        if (warmFrame >= 0) {
            const auto warmBaseline = g_seekEvents.load(std::memory_order_acquire);
            if (FFF3FP_SeekFrame(player, warmFrame) != FFFResult::Success ||
                !WaitSeekEvent(warmBaseline, 15000) ||
                !WaitFrame(player, warmFrame, kNoPts, 5000)) {
                std::fprintf(stderr, "warm-index seek failed: %s\n", file);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        const auto baseline = g_seekEvents.load(std::memory_order_acquire);
        if (frameSeek) {
            if (FFF3FP_SeekFrame(player, target) != FFFResult::Success) {
                std::fprintf(stderr, "seek-frame call failed: %lld\n",
                    static_cast<long long>(target));
                break;
            }
        } else {
            if (FFF3FP_Seek(player, target) != FFFResult::Success) {
                std::fprintf(stderr, "seek call failed\n");
                break;
            }
        }
        if (!WaitSeekEvent(baseline, 15000)) {
            std::fprintf(stderr, "seek event timeout: %s\n", file);
            break;
        }
        if (!WaitFrame(player, expected.frameIndex, expected.framePts, 5000)) {
            FFF3FPSnapshot snapshot{};
            (void)Snapshot(player, snapshot);
            std::fprintf(stderr, "target mismatch: %s got frame=%lld pts=%lld want=%lld/%lld\n",
                file, static_cast<long long>(snapshot.frameIndex),
                static_cast<long long>(snapshot.framePts),
                static_cast<long long>(expected.frameIndex),
                static_cast<long long>(expected.framePts));
            break;
        }
        FFF3FPSnapshot snapshot{};
        if (!Snapshot(player, snapshot)) break;
        result.frameIndex = snapshot.frameIndex;
        result.framePts = snapshot.framePts;
        result.presented = snapshot.presentedVideoFrames;
        result.decodeMode = static_cast<std::uint32_t>(snapshot.decodeMode);
        result.uploadPath = snapshot.uploadPath;
        (void)FFF3FP_GetVideoRenderBackend(player, &result.renderBackend);
        if (gpu12 && result.decodeMode != static_cast<std::uint32_t>(FFF3FPDecodeMode::Gpu)) {
            std::fprintf(stderr, "D3D12 decode did not engage: mode=%u backend=%u uploadPath=%u\n",
                result.decodeMode, result.renderBackend, result.uploadPath);
            break;
        }
        if (gpu12 && result.uploadPath != 10) {
            std::fprintf(stderr, "D3D12VA upload path not exercised: uploadPath=%u backend=%u\n",
                result.uploadPath, result.renderBackend);
            break;
        }
        FFF3FP_Close(player);
        FFF3FP_Destroy(player);
        if (window != nullptr) DestroyWindow(window);
        return true;
    } while (false);

    if (opened) FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    if (window != nullptr) DestroyWindow(window);
    return false;
}

bool CheckPair(const char* label, const char* file, const bool frameSeek,
    const std::int64_t target, const Expectation& expected,
    const std::int64_t warmFrame = -1, const bool gpu12 = false) {
    Result full{};
    Result skipped{};
    if (!RunSeek(file, frameSeek, target, false, expected, full, warmFrame, gpu12)) return false;
    if (!RunSeek(file, frameSeek, target, true, expected, skipped, warmFrame, gpu12)) return false;
    if (full.frameIndex != skipped.frameIndex || full.framePts != skipped.framePts) {
        std::fprintf(stderr, "%s: skip changed target: %lld/%lld -> %lld/%lld\n", label,
            static_cast<long long>(full.frameIndex), static_cast<long long>(full.framePts),
            static_cast<long long>(skipped.frameIndex), static_cast<long long>(skipped.framePts));
        return false;
    }
    if (gpu12 && (full.decodeMode != skipped.decodeMode ||
                  full.uploadPath != skipped.uploadPath)) {
        std::fprintf(stderr, "%s: D3D12 route changed between arms: mode %u/%u path %u/%u\n",
            label, full.decodeMode, skipped.decodeMode, full.uploadPath, skipped.uploadPath);
        return false;
    }
    std::printf("PASS %s%s frame=%lld pts=%lld presented=%llu/%llu decodeMode=%u uploadPath=%u backend=%u\n",
        label, gpu12 ? " [D3D12VA]" : "",
        static_cast<long long>(skipped.frameIndex), static_cast<long long>(skipped.framePts),
        static_cast<unsigned long long>(full.presented),
        static_cast<unsigned long long>(skipped.presented),
        skipped.decodeMode, skipped.uploadPath, skipped.renderBackend);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <media-dir> [--d3d12]\n", argv[0]);
        return 2;
    }
    const bool d3d12 = argc >= 3 && std::strcmp(argv[2], "--d3d12") == 0;
    char h264Path[1024]{};
    char interlacedPath[1024]{};
    char hevcPath[1024]{};
    std::snprintf(h264Path, sizeof(h264Path), "%s\\h264.mp4", argv[1]);
    std::snprintf(interlacedPath, sizeof(interlacedPath), "%s\\interlaced.mp4", argv[1]);
    std::snprintf(hevcPath, sizeof(hevcPath), "%s\\hevc_open.mp4", argv[1]);

    // 1080p H.264, 60 s, GOP 60, B frames. 9.5 s reports 45 pictures from the
    // 9.0 s keyframe, matching the existing snapshot semantics.
    if (!CheckPair("h264 exact 9.5s", h264Path, false, 95'000'000,
        Expectation{45, 145'920}))
        return 1;
    // Interlaced H.264, 10 s. 4.5 s is frame 15; NONREF must stay disabled.
    if (!CheckPair("interlaced exact 4.5s", interlacedPath, false, 45'000'000,
        Expectation{15, 69'120}))
        return 1;
    // Open-GOP HEVC, 10 s. 7.5 s is frame 45 in this fixture's decoding window.
    if (!CheckPair("hevc open exact 7.5s", hevcPath, false, 75'000'000,
        Expectation{45, 115'200}))
        return 1;
    // Frame navigation outside the rolling index (rebuilds from the stream start).
    if (!CheckPair("frame 905 rebuild", h264Path, true, 905, Expectation{905, 463'360}))
        return 1;
    // Warm the index with frame 530, then navigate inside it at 500.
    if (!CheckPair("frame 500 in window", h264Path, true, 500,
        Expectation{500, 256'000}, 530))
        return 1;
    // D3D12VA is default-on for NONREF seeking now. Exercise the real decoder
    // and renderer with a window instead of the CPU fixture path. This is
    // opt-in at the CLI level because a machine without a D3D12 adapter must
    // still be able to run the software regression.
    if (d3d12) {
        if (!CheckPair("d3d12 h264 exact 9.5s", h264Path, false, 95'000'000,
            Expectation{45, 145'920}, -1, true))
            return 1;
        if (!CheckPair("d3d12 hevc open exact 7.5s", hevcPath, false, 75'000'000,
            Expectation{45, 115'200}, -1, true))
            return 1;
        if (!CheckPair("d3d12 frame 905 rebuild", h264Path, true, 905,
            Expectation{905, 463'360}, -1, true))
            return 1;
    }
    std::printf("Native seek-skip regression: %d passed\n", d3d12 ? 8 : 5);
    return 0;
}
