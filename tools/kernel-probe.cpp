#include <windows.h>
#include "3FP/Api/FFF.Player.Api.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

// Runs without player settings; loads either baseline or candidate via its ABI.
int wmain(int argc, wchar_t** argv) {
    if (argc != 11 && argc != 12) {
        std::fwprintf(stderr, L"Usage: kernel-probe DLL VIDEO WIDTH HEIGHT DECODE QUALITY COLOR PROJECTION SECONDS PIXELS [stress]\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring dllPath = argv[1];
    SetDllDirectoryW(dllPath.substr(0, dllPath.find_last_of(L"\\/")).c_str());
    const auto dll = LoadLibraryW(argv[1]);
    if (!dll) { std::fprintf(stderr, "LoadLibrary: %lu\n", GetLastError()); return 3; }
#define LOAD(name) const auto name = reinterpret_cast<decltype(&FFF3FP_##name)>(GetProcAddress(dll, "FFF3FP_" #name)); if (!name) return 4
    LOAD(GetApiVersion); LOAD(Create); LOAD(Open); LOAD(GetSnapshot); LOAD(GetLastError);
    LOAD(ReadVideoPixelRegion); LOAD(Set360View); LOAD(Play); LOAD(Pause); LOAD(SeekFrame); LOAD(Destroy);
    LOAD(SetLogCallback);
    LOAD(SetOutputWindow); LOAD(SetColorMode); LOAD(SetTimedTextLayer);
#undef LOAD
    SetLogCallback([](void*, const char* line) noexcept { std::fprintf(stderr, "%s\n", line); }, nullptr);
    const int width = _wtoi(argv[3]), height = _wtoi(argv[4]);
    const auto window = CreateWindowExW(0, L"STATIC", L"Kernel regression probe",
        WS_POPUP | WS_VISIBLE, 0, 0, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    FFF3FPConfiguration config{};
    config.size = sizeof(config); config.version = GetApiVersion(); config.outputWindow = window;
    config.decodeMode = static_cast<FFF3FPDecodeMode>(_wtoi(argv[5]));
    config.videoScalingQuality = static_cast<FFF3FPVideoScalingQuality>(_wtoi(argv[6]));
    config.colorMode = static_cast<FFF3FPColorMode>(_wtoi(argv[7]));
    config.sdrPeakNits = 100; config.hdrPeakNits = 1000; config.sdrPaperWhiteNits = 203;
    FFF3FPHandle player{};
    if (Create(&config, &player) != FFFResult::Success) return 5;
    auto check = [&](FFFResult result) {
        if (result == FFFResult::Success) return true;
        char error[2048]{}; GetLastError(player, error, sizeof(error), nullptr);
        std::fprintf(stderr, "Result %d: %s\n", int(result), error); return false;
    };
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, nullptr, 0, nullptr, nullptr);
    std::string path(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, path.data(), bytes, nullptr, nullptr);
    if (!check(Open(player, path.c_str()))) return 6;
    auto pump = [] {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    };
    auto snapshot = [&] {
        FFF3FPSnapshot value{}; value.size = sizeof(value); value.version = 8;
        if (!check(GetSnapshot(player, &value))) value.state = FFF3FPState::Failed;
        return value;
    };
    const auto start = std::chrono::steady_clock::now();
    FFF3FPSnapshot state{};
    for (;;) {
        pump(); state = snapshot();
        if (state.state == FFF3FPState::Failed) { check(FFFResult::NativeFailure); return 7; }
        if (state.state == FFF3FPState::Ready) break;
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(45)) {
            std::fprintf(stderr, "Ready timeout: state=%u accepted=%llu presents=%llu\n",
                unsigned(state.state), state.presentedVideoFrames, state.swapChainPresents);
            return 8;
        }
        Sleep(2);
    }
    if (!check(Set360View(player, _wtoi(argv[8]), 15, 5, 75))) return 6;
    auto seek = [&](std::int64_t frame) {
        const auto before = snapshot();
        if (!check(SeekFrame(player, frame))) return false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            pump(); const auto after = snapshot();
            if (after.state == FFF3FPState::Failed) return false;
            if (after.timelineGeneration > before.timelineGeneration &&
                after.frameIndex == frame && after.swapChainPresents > before.swapChainPresents)
                return true;
            Sleep(2);
        }
        return false;
    };
    // Upstream videos become Ready before any frame is decoded. Request frame 0
    // explicitly rather than changing the production open/play contract.
    if (!seek(0)) return 12;
    const double first = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double seconds = _wtof(argv[9]);
    const bool stress = argc == 12 && std::wstring(argv[11]) == L"stress";
    if (seconds > 0) {
        if (!check(Play(player))) return 9;
        const auto playback = std::chrono::steady_clock::now();
        int phase = -1;
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - playback).count() < seconds) {
            pump(); state = snapshot();
            if (state.state == FFF3FPState::Failed) return 10;
            const int current = static_cast<int>(std::chrono::duration<double>(
                std::chrono::steady_clock::now() - playback).count() * 4);
            if (stress && current != phase) {
                phase = current;
                SetWindowPos(window, nullptr, 0, 0, phase % 2 ? width : width / 2,
                    phase % 2 ? height : height / 2, SWP_NOMOVE | SWP_NOZORDER);
                if (!check(SetOutputWindow(player, window)) ||
                    !check(SetColorMode(player, static_cast<FFF3FPColorMode>(phase % 3),
                        100, 1000, 203, 1))) return 17;
                FFF3FPTimedTextCommand command{};
                command.size = sizeof(command); command.version = 1;
                command.type = FFF3FPTimedTextCommandType::Text;
                command.x = 20; command.y = 20; command.width = 400; command.height = 80;
                command.fontSize = 32; command.foregroundArgb = 0xFFFFFFFF;
                command.textUtf8 = "Scheduling stress"; command.fontFamilyUtf8 = "Segoe UI";
                command.contentId = 1;
                FFF3FPTimedTextLayer layer{};
                layer.size = sizeof(layer); layer.version = 1;
                layer.canvasWidth = width; layer.canvasHeight = height;
                layer.commandCount = phase % 2; layer.commands = &command;
                layer.sequence = phase + 1; layer.targetFrameRate = 60;
                if (!check(SetTimedTextLayer(player, &layer))) return 18;
            }
            Sleep(2);
        }
        if (stress) {
            SetWindowPos(window, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
            if (!check(SetOutputWindow(player, window))) return 17;
        }
        if (state.state == FFF3FPState::Playing && !check(Pause(player))) return 11;
    } else {
        // Exercise a discontinuous upload, then return to a deterministic frame.
        if (!seek(5)) return 12;
        if (!seek(0)) return 13;
    }
    Sleep(100); pump(); state = snapshot();
    std::uint32_t bits{};
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4);
    if (!check(ReadVideoPixelRegion(player, 0, 0, width, height, pixels.data(),
            static_cast<std::uint32_t>(pixels.size()), &bits))) return 14;
    FILE* output{};
    if (_wfopen_s(&output, argv[10], L"wb") || !output) return 15;
    const auto written = std::fwrite(pixels.data(), sizeof(float), pixels.size(), output);
    std::fclose(output);
    if (written != pixels.size()) return 16;
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    ULARGE_INTEGER k{}, u{}; k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    std::printf("first_s=%.4f cpu_s=%.4f decoded=%llu accepted=%llu dropped=%llu coalesced=%llu presents=%llu position_s=%.4f bits=%u decode=%u present_wait_ms=%.3f device_lock_wait_ms=%.3f audio_underruns=%llu\n",
        first, (k.QuadPart + u.QuadPart) / 1e7, state.decodedVideoFrames, state.presentedVideoFrames,
        state.droppedVideoFrames, state.coalescedVideoFrames, state.swapChainPresents,
        state.position100ns / 1e7, bits, unsigned(state.decodeMode),
        state.presentWait100ns / 1e4, state.deviceLockWait100ns / 1e4, state.audioUnderruns);
    Destroy(player); DestroyWindow(window); FreeLibrary(dll);
    return 0;
}
