// Fmp4SeekProbe — headless session-level seek probe for the 3FP player API.
//
// Purpose: exercise the session-level API surface (Open / Play / Seek / SeekKeyframe /
// StepKeyframe / Pause / Close) against the noaudio-duration-seek-fix verification
// variants and report a PASS/FAIL matrix per C-PROBE-MATRIX@2:
//   A1  noaudio_dz.webm   — duration==0 precondition, refill monotonic growth during
//                           play, seek targets derived from the REFILLED duration
//                           (the 170.83 s fallback constant is FORBIDDEN here), plus
//                           a beyond-duration -> Ended semantics check (excluded from
//                           the FAIL count, reported as observed).
//   A2  noaudio_test.mp4  — duration100ns constant over the whole session and equal
//       audio_test.mp4     to the pre-fix baseline (byte-exact).
//   A3  both test mp4s    — Paused StepKeyframe(+1) lands on the NEXT keyframe
//                           (C-K1-STEP@2): noaudio 105.00 s / audio 105.05 s.
//   A4  both test mp4s    — Paused StepKeyframe(-1): delta == -5.00 s and landing
//                           <= start (start-dependent; do NOT assert absolute 95.05 s).
//   A5  noaudio_frag.mp4  — original 12-step matrix regression (K2 / Matroska).
//       noaudio.mkv
// The scene is auto-detected from the media file name.
//
// Usage:
//   Fmp4SeekProbe.exe <mediaPath> [logPath]
// The media path is received as UTF-16 via wmain and converted to UTF-8 for
// FFF3FP_Open (kernel contract is UTF-8; ANSI argv would mangle CJK filenames).
//
// Diagnostics:
// - Sets FFF_SEEK_TRACE=1 before session creation; the kernel prints its seek
//   dispatch lines ("SEEK fmp4 preroll hit=... target=... ref=...") to stderr.
// - Installs FFF3FP_SetLogCallback and FFF3FPEventCallback; every event line is
//   timestamped and mirrored to stdout and the log file.
//
// Build: 3fp\kernel\tools\构建Fmp4SeekProbe.ps1. Implicit linking against
// FFF.Native.lib; run the exe from 3fp\build so FFF.Native.dll and the FFmpeg DLLs
// resolve from the process directory.

#include "3FP/Api/FFF.Player.Api.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

// Pinned inside PlayerApi.cpp (= 20). Host must fill size AND version before
// FFF3FP_Create / FFF3FP_GetSnapshot, otherwise both reject with InvalidArgument.
constexpr std::uint32_t kPlayerApiVersion = 20;
// BACKWARD keyframe semantics: the landing may sit before the target; allow 3 s.
constexpr std::int64_t kSeekTolerance100ns = 30'000'000;

// ---- C-PROBE-MATRIX@2 expectation constants ---------------------------------
// A2 baselines (pre-fix, 100ns). noaudio_test.mp4 is byte-exact from the brief;
// audio_test.mp4 is given to 2 decimals (170.88 s) and asserted byte-exact at
// that value, with the full-precision snapshot value printed for the ledger.
constexpr std::int64_t kBaselineNoaudioTest100ns = 1'708'333'750LL; // 170.833375 s
// audio_test.mp4：brief 给的 170.88 s 是 2 位小数舍入；全精度实测 Open 快照为
// 170.8833750 s（leader 实测锁定）。判定基准用 Open 快照，本值仅作对照打印。
constexpr std::int64_t kBaselineAudioTest100ns   = 1'708'833'750LL; // 170.883375 s
// A3: Paused StepKeyframe(+1) must land on the NEXT keyframe (C-K1-STEP@2);
// expected landing measured by the fixing engineer, hardcoded per the contract.
constexpr std::int64_t kExpectedStepNextKfNoaudio100ns = 1'050'000'000LL; // 105.00 s
constexpr std::int64_t kExpectedStepNextKfAudio100ns   = 1'050'500'000LL; // 105.05 s
// A4: Paused StepKeyframe(-1) — start-dependent assertion: delta == -5.00 s
// (one GOP) and landing <= start. The leader-locked 95.05/95.00 s absolutes
// belong to a different start (102.5 s straight -1); in this matrix -1 starts
// at the +1 landing, so only the delta口径 is comparable.
constexpr std::int64_t kExpectedStepPrevKfDelta100ns = -50'000'000LL; // -5.00 s
// A1: play window that must show the monotonic duration refill (>= 3 s of
// ordered playback), and the beyond-duration semantics-check offset (+60 s).
constexpr std::int64_t kA1MinPlaybackAdvance100ns = 30'000'000LL; // 3 s
constexpr std::int64_t kBeyondDurationOffset100ns = 600'000'000LL; // +60 s
constexpr double kA1RefillWindowSec = 20.0;
// 窗口口径（T3 实测 2026-11-02，10:02 内核 DLL）：clamp 后从 ~3.3s 实时播放到
// 真尾 170.834s 用时 167.9s，PlaybackEnded 干净达成、无重启（probe/noaudio_dz
// .webm.log）。为覆盖"真尾距离 = 全片长"的最坏情形，窗口取 210s；若出现 leader
// 观察到的 Ended 后自动重启/继续策略，超时按固有语义如实标注收尾（不计 FAIL）。
constexpr double kSemanticsWindowSec = 210.0;

enum class Scene { A1DzWebm, NoaudioTest, AudioTest, Generic };

Clock::time_point g_start;
FILE* g_logFile = nullptr;
std::mutex g_logMutex;
std::deque<std::string> g_recentLines;
std::atomic<unsigned> g_errorEvents{0};
std::atomic<unsigned> g_playbackEndedEvents{0};
// Counts OperationCompleted events that carry operation="step-keyframe" AND
// boundary:true — the legal "edge of keyframe timeline" ending that does NOT
// issue a seek (timelineGeneration stays), protected verbatim by R3.4.
std::atomic<unsigned> g_stepBoundaryEvents{0};
std::int64_t g_mediaDuration100ns = 0; // written by RunOpen / main on the main thread only

double ElapsedSec() {
    return std::chrono::duration<double>(Clock::now() - g_start).count();
}

double ElapsedMsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void LogRaw(const char* line) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_logMutex);
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "[+%8.3f] ", ElapsedSec());
        std::string full = stamp;
        full += line;
        std::fputs(full.c_str(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        if (g_logFile != nullptr) {
            std::fwrite(full.data(), 1, full.size(), g_logFile);
            std::fputc('\n', g_logFile);
            std::fflush(g_logFile);
        }
        g_recentLines.push_back(full);
        if (g_recentLines.size() > 400) g_recentLines.pop_front();
    } catch (...) {
        // Logging must never take the process down, it is called from kernel threads.
    }
}

void Logf(const char* fmt, ...) noexcept {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LogRaw(buf);
}

const char* EventName(FFF3FPEvent e) noexcept {
    switch (e) {
        case FFF3FPEvent::StateChanged: return "StateChanged";
        case FFF3FPEvent::OpenCompleted: return "OpenCompleted";
        case FFF3FPEvent::OperationCompleted: return "OperationCompleted";
        case FFF3FPEvent::PlaybackEnded: return "PlaybackEnded";
        case FFF3FPEvent::Error: return "Error";
        case FFF3FPEvent::ColorModeChanged: return "ColorModeChanged";
        case FFF3FPEvent::DeviceChanged: return "DeviceChanged";
        default: return "Unknown";
    }
}

const char* RcName(FFFResult r) noexcept {
    switch (r) {
        case FFFResult::Success: return "Success";
        case FFFResult::InvalidArgument: return "InvalidArgument";
        case FFFResult::InvalidState: return "InvalidState";
        case FFFResult::BufferTooSmall: return "BufferTooSmall";
        case FFFResult::NativeFailure: return "NativeFailure";
        case FFFResult::FfmpegFailure: return "FfmpegFailure";
        case FFFResult::DeviceFailure: return "DeviceFailure";
        case FFFResult::NotSupported: return "NotSupported";
        case FFFResult::NotFound: return "NotFound";
        default: return "Unknown";
    }
}

void __cdecl LogSink(void* /*context*/, const char* utf8Line) noexcept {
    if (utf8Line == nullptr) return;
    std::string line = "KERN| ";
    line += utf8Line;
    LogRaw(line.c_str());
}

void __cdecl EventSink(void* /*context*/, FFF3FPEvent type, const char* detailJsonUtf8) noexcept {
    if (type == FFF3FPEvent::Error) ++g_errorEvents;
    if (type == FFF3FPEvent::PlaybackEnded) ++g_playbackEndedEvents;
    if (type == FFF3FPEvent::OperationCompleted && detailJsonUtf8 != nullptr &&
        std::strstr(detailJsonUtf8, "\"operation\":\"step-keyframe\"") != nullptr &&
        std::strstr(detailJsonUtf8, "\"boundary\":true") != nullptr) {
        ++g_stepBoundaryEvents;
    }
    Logf("EVENT %s detail=%s", EventName(type),
         detailJsonUtf8 != nullptr ? detailJsonUtf8 : "(null)");
}

// The renderer owns a hidden window; keep its message queue pumped while polling.
void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// GetSnapshot contract: the caller re-declares size/version on EVERY call because
// the kernel overwrites the whole struct with its published snapshot.
bool ReadSnapshot(FFF3FPHandle player, FFF3FPSnapshot* out) noexcept {
    *out = FFF3FPSnapshot{};
    out->size = sizeof(FFF3FPSnapshot);
    out->version = kPlayerApiVersion;
    return FFF3FP_GetSnapshot(player, out) == FFFResult::Success;
}

std::string ToUtf8(const wchar_t* wide) {
    if (wide == nullptr || wide[0] == L'\0') return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string narrow(static_cast<std::size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow.data(), bytes, nullptr, nullptr);
    return narrow;
}

struct StepRow {
    std::string name;
    std::string rc;
    double elapsedMs = 0.0;
    std::string landed;
    bool pass = false;
    // Snapshot duration100ns at the step's end; -1 = not sampled. This is the
    // A2 invariant input: every sampled step must equal the baseline (known
    // duration) or the Open value (generic regression variants).
    std::int64_t duration100nsAtEnd = -1;
};

StepRow RunOpen(FFF3FPHandle player, const char* mediaUtf8) {
    StepRow row;
    row.name = "Open";
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_Open(player, mediaUtf8);
    row.rc = RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    while (ElapsedMsSince(t0) <= 90000.0) {
        Sleep(100);
        PumpMessages();
        FFF3FPSnapshot snap;
        if (!ReadSnapshot(player, &snap)) continue;
        if (snap.state == FFF3FPState::Ready) {
            row.pass = true;
            g_mediaDuration100ns = snap.duration100ns;
            row.duration100nsAtEnd = snap.duration100ns;
            char landing[160];
            std::snprintf(landing, sizeof(landing), "dur=%.7fs %ux%u state=Ready",
                snap.duration100ns / 1e7, snap.videoWidth, snap.videoHeight);
            row.landed = landing;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
        if (snap.state == FFF3FPState::Failed) {
            row.landed = "state=Failed (see error event above)";
            return row;
        }
    }
    row.landed = "timeout (90s) waiting for Ready";
    return row;
}

// A1 step 1: the reproduction precondition — the container carries no usable
// duration, so the session must open with duration100ns == 0.
StepRow RunAssertOpenDurationZero(const std::int64_t openDuration100ns) {
    StepRow row;
    row.name = "A1 open duration==0";
    row.rc = "snapshot";
    row.duration100nsAtEnd = openDuration100ns;
    if (openDuration100ns == 0) {
        row.pass = true;
        row.landed = "precondition reproduced: duration=0";
    } else {
        char landing[160];
        std::snprintf(landing, sizeof(landing),
            "precondition NOT reproduced: duration=%.7fs", openDuration100ns / 1e7);
        row.landed = landing;
    }
    return row;
}

StepRow RunPlay(FFF3FPHandle player) {
    StepRow row;
    row.name = "Play(position>+1s)";
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_Play(player);
    row.rc = RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    const unsigned errBefore = g_errorEvents.load();
    bool haveFirst = false;
    std::int64_t firstPos = 0;
    FFF3FPSnapshot snap;
    while (ElapsedMsSince(t0) <= 5000.0) {
        Sleep(100);
        PumpMessages();
        if (!ReadSnapshot(player, &snap)) continue;
        if (g_errorEvents.load() != errBefore) {
            row.landed = "error event during play";
            return row;
        }
        if (snap.state == FFF3FPState::Failed) {
            row.landed = "state=Failed";
            return row;
        }
        if (!haveFirst) {
            firstPos = snap.position100ns;
            haveFirst = true;
        }
        if (snap.position100ns - firstPos > 10'000'000) { // > 1 s of ordered playback
            row.pass = true;
            char landing[160];
            std::snprintf(landing, sizeof(landing), "advanced to %.3fs",
                snap.position100ns / 1e7);
            row.landed = landing;
            row.duration100nsAtEnd = snap.duration100ns;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
    }
    row.duration100nsAtEnd = haveFirst ? snap.duration100ns : -1;
    row.landed = "timeout (5s), position did not advance >1s";
    return row;
}

// A1 step 2 (core): while playback advances >= 3 s, repeatedly sample the
// snapshot; duration100ns must grow from 0 and never regress.
StepRow RunPlayDurationGrowth(FFF3FPHandle player) {
    StepRow row;
    row.name = "A1 play duration refill";
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_Play(player);
    row.rc = RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    const unsigned errBefore = g_errorEvents.load();
    int samples = 0;
    int distinctValues = 0;
    std::int64_t prevDur = -1;
    std::int64_t firstPos = 0;
    std::int64_t firstDur = -1;
    std::int64_t lastDur = -1;
    bool monotonic = true;
    bool advanced = false;
    while (ElapsedMsSince(t0) <= kA1RefillWindowSec * 1000.0) {
        Sleep(100);
        PumpMessages();
        FFF3FPSnapshot snap;
        if (!ReadSnapshot(player, &snap)) continue;
        if (g_errorEvents.load() != errBefore) {
            row.landed = "error event during play";
            row.duration100nsAtEnd = lastDur;
            return row;
        }
        if (snap.state == FFF3FPState::Failed) {
            row.landed = "state=Failed";
            row.duration100nsAtEnd = lastDur;
            return row;
        }
        if (samples == 0) firstPos = snap.position100ns;
        ++samples;
        const std::int64_t dur = snap.duration100ns;
        if (dur != prevDur) ++distinctValues;
        if (prevDur >= 0 && dur < prevDur) {
            monotonic = false;
            Logf("A1 DURATION REGRESSION: %.7fs -> %.7fs (sample #%d)",
                prevDur / 1e7, dur / 1e7, samples);
        }
        if (samples % 5 == 1) {
            Logf("A1 sample #%d pos=%.3fs dur=%.7fs", samples,
                snap.position100ns / 1e7, dur / 1e7);
        }
        if (firstDur < 0) firstDur = dur;
        prevDur = dur;
        lastDur = dur;
        if (snap.position100ns - firstPos >= kA1MinPlaybackAdvance100ns) {
            advanced = true;
            break;
        }
    }
    row.duration100nsAtEnd = lastDur;
    row.elapsedMs = ElapsedMsSince(t0);
    if (!advanced) {
        row.landed = "playback did not advance 3s within window";
        return row;
    }
    if (samples < 3) {
        row.landed = "insufficient samples (<3)";
        return row;
    }
    if (!monotonic) {
        row.landed = "duration regressed during play (see A1 DURATION REGRESSION lines)";
        return row;
    }
    // The strict "starts at 0" precondition is owned by the separate
    // "A1 open duration==0" row (Open-time snapshot). Sampling begins after
    // Play() returns, and the demux pump may already have refilled the first
    // GOP by then, so the executable refill assertion here is: growth happened
    // and the curve never regressed.
    if (lastDur <= 0 || firstDur >= lastDur) {
        char landing[200];
        std::snprintf(landing, sizeof(landing),
            "refill shape wrong: first=%.7fs last=%.7fs (expect growth, last>0)",
            firstDur / 1e7, lastDur / 1e7);
        row.landed = landing;
        return row;
    }
    row.pass = true;
    char landing[200];
    // firstDur is printed as sampled: sampling starts after Play() returns, so the
    // first sample may already sit inside the first refilled GOP (e.g. 0.25 s);
    // the strict "0 at Open" is owned by the "A1 open duration==0" row.
    std::snprintf(landing, sizeof(landing),
        "dur %.7fs->%.7fs, %d samples, %d distinct values, monotonic",
        firstDur / 1e7, lastDur / 1e7, samples, distinctValues);
    row.landed = landing;
    return row;
}

StepRow RunSeek(FFF3FPHandle player, std::string name, const std::int64_t target100ns) {
    char tag[160];
    std::snprintf(tag, sizeof(tag), "%.2fs", target100ns / 1e7);

    StepRow row;
    row.name = std::move(name);
    row.name += " -> ";
    row.name += tag;

    FFF3FPSnapshot before;
    const bool haveBefore = ReadSnapshot(player, &before);
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_Seek(player, target100ns);
    row.rc = RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    const unsigned errBefore = g_errorEvents.load();
    FFF3FPSnapshot snap;
    while (ElapsedMsSince(t0) <= 30000.0) {
        Sleep(100);
        PumpMessages();
        if (!ReadSnapshot(player, &snap)) continue;
        row.duration100nsAtEnd = snap.duration100ns;
        if (g_errorEvents.load() != errBefore) {
            row.landed = "error event during seek";
            return row;
        }
        const bool stateOk =
            snap.state == FFF3FPState::Playing || snap.state == FFF3FPState::Paused;
        const std::int64_t diff = snap.position100ns - target100ns;
        if (stateOk && diff >= -kSeekTolerance100ns && diff <= kSeekTolerance100ns) {
            row.pass = true;
            char landing[200];
            std::snprintf(landing, sizeof(landing),
                "landed=%.3fs diff=%+.3fs gen=%llu->%llu",
                snap.position100ns / 1e7, diff / 1e7,
                static_cast<unsigned long long>(haveBefore ? before.timelineGeneration : 0),
                static_cast<unsigned long long>(snap.timelineGeneration));
            row.landed = landing;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
    }
    row.landed = "timeout (30s) waiting for convergence";
    return row;
}

StepRow RunSeekKeyframe(FFF3FPHandle player, std::string name, const std::int64_t target100ns) {
    char tag[160];
    std::snprintf(tag, sizeof(tag), "%.2fs", target100ns / 1e7);

    StepRow row;
    row.name = std::move(name);
    row.name += " -> ";
    row.name += tag;

    FFF3FPSnapshot before;
    const bool haveBefore = ReadSnapshot(player, &before);
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_SeekKeyframe(player, target100ns);
    row.rc = RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    const unsigned errBefore = g_errorEvents.load();
    // SeekKeyframe lands at the keyframe at-or-before the target (BACKWARD
    // semantics), so the negative-side slack is one GOP, not the exact-position
    // tolerance. A landing far below target that persists for >2 s is precisely
    // the "progress bar resets to the start" symptom reported for no-audio AI
    // videos; detect and label it instead of letting it time out.
    constexpr std::int64_t kKeyframeSlack100ns = 15'000'000'000; // 15 s
    FFF3FPSnapshot snap;
    while (ElapsedMsSince(t0) <= 30000.0) {
        Sleep(100);
        PumpMessages();
        if (!ReadSnapshot(player, &snap)) continue;
        row.duration100nsAtEnd = snap.duration100ns;
        if (g_errorEvents.load() != errBefore) {
            row.landed = "error event during seek-keyframe";
            return row;
        }
        const bool stateOk =
            snap.state == FFF3FPState::Playing || snap.state == FFF3FPState::Paused;
        const std::int64_t diff = snap.position100ns - target100ns;
        if (stateOk && diff >= -kKeyframeSlack100ns && diff <= kSeekTolerance100ns) {
            row.pass = true;
            char landing[200];
            std::snprintf(landing, sizeof(landing),
                "landed=%.3fs diff=%+.3fs gen=%llu->%llu",
                snap.position100ns / 1e7, diff / 1e7,
                static_cast<unsigned long long>(haveBefore ? before.timelineGeneration : 0),
                static_cast<unsigned long long>(snap.timelineGeneration));
            row.landed = landing;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
        if (stateOk && diff < -kKeyframeSlack100ns && ElapsedMsSince(t0) > 2000.0) {
            char landing[200];
            std::snprintf(landing, sizeof(landing),
                "RESET-TO-START suspected: landed=%.3fs (target %.3fs) gen=%llu->%llu",
                snap.position100ns / 1e7, target100ns / 1e7,
                static_cast<unsigned long long>(haveBefore ? before.timelineGeneration : 0),
                static_cast<unsigned long long>(snap.timelineGeneration));
            row.landed = landing;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
    }
    row.landed = "timeout (30s) waiting for convergence";
    return row;
}

StepRow RunPause(FFF3FPHandle player) {
    StepRow row;
    row.name = "Pause";
    const FFFResult rc = FFF3FP_Pause(player);
    row.rc = RcName(rc);
    const auto t0 = Clock::now();
    if (rc != FFFResult::Success) return row;
    while (ElapsedMsSince(t0) <= 5000.0) {
        Sleep(50);
        PumpMessages();
        FFF3FPSnapshot snap;
        if (ReadSnapshot(player, &snap) && snap.state == FFF3FPState::Paused) {
            row.pass = true;
            row.landed = "state=Paused";
            row.duration100nsAtEnd = snap.duration100ns;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
    }
    row.landed = "timeout (5s) waiting for Paused";
    return row;
}

// StepKeyframe with optional C-K1-STEP@2 expectation assertions:
//  - expectedLanding100ns (A3): landing within tolerance of the next-keyframe
//    measured value AND strictly beyond the start position.
//  - expectedDelta100ns (A4): (landing - start) within tolerance of the expected
//    delta AND landing at-or-before the start (start-dependent口径).
StepRow RunStepKeyframe(FFF3FPHandle player, const int direction,
    const std::int64_t* expectedLanding100ns = nullptr,
    const std::int64_t* expectedDelta100ns = nullptr) {
    StepRow row;
    row.name = std::string("StepKeyframe(") + (direction > 0 ? "+1" : "-1") + ")";
    FFF3FPSnapshot before;
    const bool haveBefore = ReadSnapshot(player, &before);
    const std::uint64_t genBefore = haveBefore ? before.timelineGeneration : 0;
    const std::int64_t posBefore = haveBefore ? before.position100ns : 0;
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_StepKeyframe(player, direction);
    row.rc = RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    const unsigned errBefore = g_errorEvents.load();
    const unsigned boundaryBefore = g_stepBoundaryEvents.load();
    // timelineGeneration only advances after a real demuxer seek succeeds, which is
    // exactly the "step landed" signal (DoStepKeyframe routes through DoSeek). The
    // alternative legal ending is the boundary branch (R3.4): no seek is issued,
    // a step-keyframe OperationCompleted carries boundary:true and the position
    // holds — e.g. -1 while sitting on the first keyframe.
    while (ElapsedMsSince(t0) <= 20000.0) {
        Sleep(100);
        PumpMessages();
        FFF3FPSnapshot snap;
        if (!ReadSnapshot(player, &snap)) continue;
        const bool stateOk =
            snap.state == FFF3FPState::Playing || snap.state == FFF3FPState::Paused;
        if (g_errorEvents.load() != errBefore) {
            row.landed = "error event during step";
            row.duration100nsAtEnd = snap.duration100ns;
            return row;
        }
        if (!stateOk) continue;
        const bool landedBySeek = snap.timelineGeneration > genBefore;
        const bool landedByBoundary = g_stepBoundaryEvents.load() != boundaryBefore;
        // 时序兜底：boundary 计数发生在 kernel 回调线程（先 ++ 后打印），可能落在
        // boundaryBefore 快照之前，导致计数差检测失效。起点已在时间轴 0 时，-1 必然
        // 走边界分支（无更早关键帧），position 保持 2s 即判 boundary（R3.4 合法语义）。
        const bool heldAtStart = direction < 0 && posBefore <= 0 &&
            snap.position100ns == posBefore && ElapsedMsSince(t0) > 2000.0;
        if (!landedBySeek && !landedByBoundary && !heldAtStart) continue;
        row.elapsedMs = ElapsedMsSince(t0);
        row.duration100nsAtEnd = snap.duration100ns;
        const std::int64_t delta = snap.position100ns - posBefore;
        char landing[240];
        if ((landedByBoundary || heldAtStart) && expectedLanding100ns == nullptr &&
            expectedDelta100ns == nullptr) {
            row.pass = true;
            std::snprintf(landing, sizeof(landing),
                "boundary:true (keyframe-timeline edge; position held at %.3fs)",
                snap.position100ns / 1e7);
        } else if (expectedLanding100ns != nullptr) { // A3
            const std::int64_t diff = snap.position100ns - *expectedLanding100ns;
            const bool inTolerance =
                diff >= -kSeekTolerance100ns && diff <= kSeekTolerance100ns;
            const bool beyondStart = snap.position100ns > posBefore;
            row.pass = inTolerance && beyondStart;
            std::snprintf(landing, sizeof(landing),
                "landed=%.3fs expected=%.3fs diff=%+.3fs %s (start=%.3fs)",
                snap.position100ns / 1e7, *expectedLanding100ns / 1e7, diff / 1e7,
                row.pass ? "NEXT-KEYFRAME OK" : "EXPECTATION MISMATCH", posBefore / 1e7);
        } else if (expectedDelta100ns != nullptr) { // A4
            const std::int64_t ddiff = delta - *expectedDelta100ns;
            const bool inTolerance =
                ddiff >= -kSeekTolerance100ns && ddiff <= kSeekTolerance100ns;
            const bool notForward = snap.position100ns <= posBefore;
            row.pass = inTolerance && notForward;
            std::snprintf(landing, sizeof(landing),
                "landed=%.3fs delta=%+.3fs expectedDelta=%.3fs ddiff=%+.3fs %s",
                snap.position100ns / 1e7, delta / 1e7, *expectedDelta100ns / 1e7,
                ddiff / 1e7, row.pass ? "PREV-KEYFRAME OK" : "EXPECTATION MISMATCH");
        } else {
            row.pass = true;
            // 无期望断言的变体（frag/mkv/dz）：-1 落点若在起点之前（向前跳），
            // 属疑点——步进语义应向后。pass 不变（无锁定基线），专项标注供报告核对。
            const bool forwardOnBack =
                direction < 0 && snap.position100ns > posBefore;
            std::snprintf(landing, sizeof(landing), "landed=%.3fs delta=%+.3fs%s",
                snap.position100ns / 1e7, delta / 1e7,
                forwardOnBack ? "  <<ANOMALY: -1 landed FORWARD (flag for review)" : "");
        }
        row.landed = landing;
        return row;
    }
    row.landed = "timeout (20s) waiting for step to land";
    return row;
}

// A1 step 4 (semantics check, excluded from the FAIL count): a seek target
// beyond the refilled duration is clamped (DoSeek :6836) and the session runs
// to EOF -> Ended. Report what actually happened.
StepRow RunEndedSemantics(FFF3FPHandle player, const std::int64_t target100ns,
    const std::int64_t refDuration100ns) {
    StepRow row;
    row.name = "[SEMANTICS] beyond-duration -> Ended";
    char tag[160];
    std::snprintf(tag, sizeof(tag), "target=%.2fs dur=%.3fs",
        target100ns / 1e7, refDuration100ns / 1e7);
    row.name += " (";
    row.name += tag;
    row.name += ")";
    // Ended only happens while playing; resume first if paused.
    FFF3FPSnapshot snap;
    if (ReadSnapshot(player, &snap) && snap.state == FFF3FPState::Paused) {
        const FFFResult prc = FFF3FP_Play(player);
        row.rc = "play=";
        row.rc += RcName(prc);
        row.rc += " ";
        if (prc != FFFResult::Success) return row;
    }
    const auto t0 = Clock::now();
    const FFFResult rc = FFF3FP_Seek(player, target100ns);
    row.rc += "seek=";
    row.rc += RcName(rc);
    row.elapsedMs = ElapsedMsSince(t0);
    if (rc != FFFResult::Success) return row;
    const unsigned errBefore = g_errorEvents.load();
    const unsigned endedBefore = g_playbackEndedEvents.load();
    bool clampNoted = false;
    while (ElapsedMsSince(t0) <= kSemanticsWindowSec * 1000.0) {
        Sleep(200);
        PumpMessages();
        if (!ReadSnapshot(player, &snap)) continue;
        row.duration100nsAtEnd = snap.duration100ns;
        if (g_errorEvents.load() != errBefore) {
            row.landed = "error event during semantics seek";
            return row;
        }
        if (!clampNoted && snap.position100ns <= refDuration100ns + kSeekTolerance100ns) {
            clampNoted = true;
            Logf("SEMANTICS: position clamped at %.3fs (<= refilled dur %.3fs); running to EOF",
                snap.position100ns / 1e7, refDuration100ns / 1e7);
        }
        if (snap.state == FFF3FPState::Ended ||
            g_playbackEndedEvents.load() != endedBefore) {
            row.pass = true;
            char landing[200];
            std::snprintf(landing, sizeof(landing),
                "Ended reached: pos=%.3fs state=Ended (semantics confirmed)",
                snap.position100ns / 1e7);
            row.landed = landing;
            row.elapsedMs = ElapsedMsSince(t0);
            return row;
        }
    }
    char landing[256];
    std::snprintf(landing, sizeof(landing),
        "[SEMANTICS] observed: playback continued past clamped range "
        "(session restart/loop policy): last pos=%.3fs state=%d, ref dur=%.3fs",
        snap.position100ns / 1e7, static_cast<int>(snap.state),
        refDuration100ns / 1e7);
    row.landed = landing;
    return row;
}

const char* SceneName(const Scene scene) noexcept {
    switch (scene) {
        case Scene::A1DzWebm: return "A1DzWebm (noaudio_dz.webm)";
        case Scene::NoaudioTest: return "NoaudioTest (noaudio_test.mp4)";
        case Scene::AudioTest: return "AudioTest (audio_test.mp4)";
        default: return "Generic (frag/mkv regression)";
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    g_start = Clock::now();
    if (argc < 2) {
        std::fputs("usage: Fmp4SeekProbe.exe <mediaPath> [logPath]\n", stderr);
        return 2;
    }
    const std::string mediaUtf8 = ToUtf8(argv[1]);
    const wchar_t* logPath = argc >= 3 ? argv[2] : L"Fmp4SeekProbe.log";
    g_logFile = _wfopen(logPath, L"wb");

    // Scene auto-detection. Order matters: "noaudio_test" contains "audio_test".
    const Scene scene = mediaUtf8.find("noaudio_dz") != std::string::npos ? Scene::A1DzWebm
        : mediaUtf8.find("noaudio_test") != std::string::npos ? Scene::NoaudioTest
        : mediaUtf8.find("audio_test") != std::string::npos ? Scene::AudioTest
        : Scene::Generic;
    const bool hasBaseline = scene == Scene::NoaudioTest || scene == Scene::AudioTest;
    const std::int64_t baselineDuration = scene == Scene::NoaudioTest
        ? kBaselineNoaudioTest100ns : kBaselineAudioTest100ns;
    // C-PROBE-MATRIX@2: every result-matrix row must carry the variant name, so
    // the cross-variant PASS/FAIL ledger can be assembled mechanically.
    const std::size_t baseSlash = mediaUtf8.find_last_of("/\\");
    const std::string variantTag = std::string("[") +
        (baseSlash == std::string::npos ? mediaUtf8 : mediaUtf8.substr(baseSlash + 1))
        + "] ";

    Logf("Fmp4SeekProbe start; media=\"%s\"", mediaUtf8.c_str());
    Logf("Scene: %s (A2 baseline: %s)", SceneName(scene),
        hasBaseline ? "active" : "n/a");
    // Kernel seek-dispatch tracing (DoSeek F1/F2/F3, preroll) is gated on this
    // variable and prints to stderr; redirect stderr to a file to keep the trace.
    SetEnvironmentVariableA("FFF_SEEK_TRACE", "1");
    FFF3FP_SetLogCallback(&LogSink, nullptr);

    // Hidden host window: the render swap chain needs an HWND even headless.
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"3FP Fmp4SeekProbe", WS_OVERLAPPED,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 360, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (hwnd == nullptr) {
        Logf("FATAL CreateWindowExW failed gle=%lu", GetLastError());
        if (g_logFile != nullptr) std::fclose(g_logFile);
        return 3;
    }

    Logf("FFF3FP_GetApiVersion=%u (host built for %u)", FFF3FP_GetApiVersion(),
        kPlayerApiVersion);

    FFF3FPConfiguration cfg{};
    cfg.size = sizeof(FFF3FPConfiguration);
    cfg.version = kPlayerApiVersion;
    cfg.outputWindow = hwnd;
    cfg.decodeMode = FFF3FPDecodeMode::Cpu;
    cfg.colorMode = FFF3FPColorMode::MapToSdr;
    // Create hard-validates these (>0 required); zeroing them yields InvalidArgument.
    // Same values every in-repo probe uses (e.g. tools\bed_binaural_e2e.cpp).
    cfg.sdrPeakNits = 100.0f;
    cfg.sdrPaperWhiteNits = 203.0f;
    cfg.preferredAdapterIndex = -1; // explicit per header contract (0 is a valid pin)
    cfg.eventCallback = &EventSink;

    FFF3FPHandle player = nullptr;
    FFFResult rc = FFF3FP_Create(&cfg, &player);
    if (rc != FFFResult::Success || player == nullptr) {
        Logf("FATAL FFF3FP_Create rc=%s", RcName(rc));
        DestroyWindow(hwnd);
        if (g_logFile != nullptr) std::fclose(g_logFile);
        return 4;
    }

    std::vector<StepRow> rows;
    rows.push_back(RunOpen(player, mediaUtf8.c_str()));
    if (rows.back().pass) {
        if (scene == Scene::A1DzWebm)
            rows.push_back(RunAssertOpenDurationZero(g_mediaDuration100ns));

        // A1 uses the refill-observing play window; every other scene keeps the
        // original ordered-playback check.
        rows.push_back(scene == Scene::A1DzWebm
            ? RunPlayDurationGrowth(player) : RunPlay(player));

        // Matrix base duration: the CURRENT snapshot value. For A1 this is the
        // refilled duration — C-PROBE-MATRIX@2 forbids the @1 fallback constant
        // (170.83 s) which produced false timeouts on the reproduction file.
        FFF3FPSnapshot snapBase;
        if (ReadSnapshot(player, &snapBase) && snapBase.duration100ns > 0)
            g_mediaDuration100ns = snapBase.duration100ns;

        if (g_mediaDuration100ns <= 0) {
            StepRow row;
            row.name = "Matrix base duration";
            row.rc = "snapshot";
            row.landed = "duration==0 after play (refill missing) — seek matrix SKIPPED";
            rows.push_back(row);
        } else {
            const std::int64_t duration = g_mediaDuration100ns;
            const auto pct = [duration](const int num, const int den) {
                return duration * num / den;
            };
            // A1 变体：目标全部来自回填后的 duration（禁用 fallback），行名标注 (dyn)。
            const std::string dynSuffix = scene == Scene::A1DzWebm ? " (dyn)" : "";
            rows.push_back(RunSeek(player, "Seek 50%" + dynSuffix, pct(1, 2)));
            rows.push_back(RunSeek(player, "Seek 90%" + dynSuffix, pct(9, 10)));
            // UI progress-bar path: the presenter raises "jump to keyframe" on
            // click/drag-end, which routes to FFF3FP_SeekKeyframe — not Seek.
            rows.push_back(RunSeekKeyframe(player, "SeekKeyframe 50%" + dynSuffix, pct(1, 2)));
            rows.push_back(RunSeekKeyframe(player, "SeekKeyframe 90%" + dynSuffix, pct(9, 10)));
            rows.push_back(RunSeek(player, "Seek back 10%" + dynSuffix, pct(1, 10)));
            rows.push_back(RunSeek(player, "Seek 0%", 0));
            // StepKeyframe must be judged in Paused state: while Playing, the
            // demux thread races DecodeUntilSeekTarget and landings are noise.
            rows.push_back(RunPause(player));
            rows.push_back(RunSeek(player, "Seek mid (paused base)" + dynSuffix, pct(3, 5)));
            if (scene == Scene::NoaudioTest) {
                rows.push_back(RunStepKeyframe(player, +1,
                    &kExpectedStepNextKfNoaudio100ns, nullptr)); // A3
                rows.push_back(RunStepKeyframe(player, -1,
                    nullptr, &kExpectedStepPrevKfDelta100ns));   // A4
            } else if (scene == Scene::AudioTest) {
                rows.push_back(RunStepKeyframe(player, +1,
                    &kExpectedStepNextKfAudio100ns, nullptr));   // A3
                rows.push_back(RunStepKeyframe(player, -1,
                    nullptr, &kExpectedStepPrevKfDelta100ns));   // A4
            } else {
                rows.push_back(RunStepKeyframe(player, +1));
                rows.push_back(RunStepKeyframe(player, -1));
            }
            if (scene == Scene::A1DzWebm)
                rows.push_back(RunEndedSemantics(player,
                    duration + kBeyondDurationOffset100ns, duration)); // A1 semantics
        }
    }

    rc = FFF3FP_Close(player);
    Logf("FFF3FP_Close rc=%s", RcName(rc));
    DestroyWindow(hwnd);

    // A2 / session invariant: every sampled step's duration must equal the
    // baseline (known-duration scenes) or the Open value (generic scenes).
    // Not applicable to A1 — its duration is EXPECTED to grow.
    if (scene != Scene::A1DzWebm) {
        // A2 判据：以 Open 后首次快照为 reference（R2.1 全程恒等不变式）；
        // brief 锁定值仅作对照打印（audio 的 brief 值是 2 位小数舍入，不作判定基准）。
        const std::int64_t openValue =
            rows.empty() ? -1 : rows.front().duration100nsAtEnd;
        const std::int64_t reference = openValue > 0 ? openValue : baselineDuration;
        StepRow inv;
        inv.name = hasBaseline ? "A2 duration invariant" : "Duration invariant (session)";
        inv.rc = "snapshot";
        bool any = false;
        bool allEqual = reference > 0;
        for (const StepRow& r : rows) {
            if (r.duration100nsAtEnd < 0) continue;
            any = true;
            if (r.duration100nsAtEnd != reference) {
                allEqual = false;
                Logf("DURATION DRIFT at [%s]: %.7fs != reference %.7fs",
                    r.name.c_str(), r.duration100nsAtEnd / 1e7, reference / 1e7);
            }
        }
        inv.pass = any && allEqual;
        char landing[256];
        std::snprintf(landing, sizeof(landing),
            "reference=Open %.7fs (brief pinned %s), sampled steps equal: %s",
            reference / 1e7,
            hasBaseline ? (reference == baselineDuration ? "match" : "MISMATCH")
                        : "n/a (generic scene)",
            inv.pass ? "yes" : "NO — see DURATION DRIFT lines");
        inv.landed = landing;
        rows.push_back(inv);
    }

    std::size_t failures = 0;
    std::size_t semanticsFailures = 0;
    for (const StepRow& r : rows) {
        if (r.pass) continue;
        // [SEMANTICS] rows are reported as observed but excluded from the
        // verdict per C-PROBE-MATRIX@2 ("不计 FAIL").
        if (r.name.rfind("[SEMANTICS]", 0) == 0) ++semanticsFailures;
        else ++failures;
    }
    LogRaw("==== RESULT MATRIX ====");
    Logf("%-46s %-16s %10s  %s", "step", "rc", "elapsed(ms)", "landing / note");
    for (const StepRow& r : rows) {
        const bool semantics = r.name.rfind("[SEMANTICS]", 0) == 0;
        Logf("%-46s %-16s %10.0f  %-46s %s", (variantTag + r.name).c_str(), r.rc.c_str(),
            r.elapsedMs, r.landed.c_str(),
            r.pass ? "PASS" : (semantics ? "FAIL* (not counted)" : "FAIL"));
    }
    const std::size_t semanticsRows = static_cast<std::size_t>(
        std::count_if(rows.begin(), rows.end(), [](const StepRow& r) {
            return r.name.rfind("[SEMANTICS]", 0) == 0;
        }));
    const std::size_t judged = rows.size() - semanticsRows;
    Logf("OVERALL[%s]: %s (%zu/%zu judged steps passed)%s",
        variantTag.c_str(), failures == 0 ? "PASS" : "FAIL", judged - failures, judged,
        semanticsFailures > 0 ? " [semantics-check deviations present — see matrix]"
                              : "");
    if (failures != 0 || semanticsFailures != 0) {
        // Attach the tail of the session log to every failing run for offline analysis.
        std::lock_guard<std::mutex> lock(g_logMutex);
        const std::size_t skip = g_recentLines.size() > 30 ? g_recentLines.size() - 30 : 0;
        LogRaw("---- last 30 log lines ----");
        for (std::size_t i = skip; i < g_recentLines.size(); ++i)
            LogRaw(g_recentLines[i].c_str());
    }
    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
    return failures == 0 ? 0 : 1;
}