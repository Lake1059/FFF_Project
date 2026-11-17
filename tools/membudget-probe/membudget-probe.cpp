// 实机测量探针：内存预算池（docs/108）、视频队列动态重算（docs/110）、
// 音频借用（docs/111 D-04）与呈现 epoch（docs/111 D-01）的实测口径。
//
// 为什么要有它：Agent 无法把 FFF.Player / 3FCompare 的窗口化播放脚本化，而这些
// 指标（队列长度波动、重算频率、epoch 丢弃计数、audioRejectedFrames）只有真正
// 跑起来才会出现。探针照宿主的方式建一个真窗口（DXGI present 需要窗口与消息泵），
// 打开真实媒体，一边播一边按固定节奏 seek，把 snapshot 里的计数器与内核 trace 行
// 一起吐出来，再由测量脚本判定阈值。
//
// 用法：
//   视频模式：membudget-probe.exe <媒体文件> [播放秒数] [videoProfile 0/1/2]
//   图片目录模式：membudget-probe.exe --images <目录> [最多张数]
//     逐张打开目录下 png/jpg（文件名排序），每张打开前重发预取计划（当前文件在
//     最前、其余作邻居），观察 still lane 的命中/未命中与 LRU/trim 计数。
//
// 环境变量（照内核约定）：FFF3FP_MEMARB=0/1（总开关）、FFF_MEMARB_TRACE=1（trace）、
// FFF_NOMMCSS=1（关两侧 MMCSS）、FFF_SEEK_BATCH / FFF_SEEK_PREVIEW 保持默认。
// 本轮新增压力臂开关（内核自读，探针只在首行回显便于报告对齐）：
//   FFF3FP_MEMARB_CAP_MB（总预算硬压，只降不升）、FFF_AUDIO_CAP_KB（音频 PCM 硬上限）。
#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Api/FFF.Player.Prefetch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

namespace {

// The neighbour the plan warmed, so the run can re-open it and observe the cache
// actually being hit (a plan alone proves nothing: hits move only on a read).
std::string gFirstNeighbour;

constexpr std::uint32_t kPlayerApiVersion = 20;   // PlayerApi.cpp: PlayerApiVersion
constexpr std::uint32_t kSnapshotVersion = 23;    // PlayerApi.cpp: SnapshotVersion

LRESULT CALLBACK ProbeWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;      // the renderer paints; no flicker to add
    case WM_CLOSE: return 0;           // the probe closes itself, never the window
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

HWND CreateProbeWindow(const int width, const int height) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProbeWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MembudgetProbeWindow";
    RegisterClassExW(&wc);
    RECT rect{};
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return CreateWindowExW(0, L"MembudgetProbeWindow", L"membudget-probe", WS_OVERLAPPEDWINDOW,
        40, 40, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance,
        nullptr);
}

// DXGI present stalls without a pump: the renderer posts its frame clock off the
// message loop, so a pump-less probe measures a frozen player instead of the pool.
void RunMessagePump() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

std::string LastError(FFF3FPHandle player) {
    std::uint32_t required = 0;
    if (FFF3FP_GetLastError(player, nullptr, 0, &required) != FFFResult::BufferTooSmall ||
        required == 0 || required > 64 * 1024)
        return {};
    std::string text(required, '\0');
    std::uint32_t written = 0;
    if (FFF3FP_GetLastError(player, text.data(), required, &written) != FFFResult::Success)
        return {};
    return text;
}

struct Counters {
    std::uint64_t decoded = 0;
    std::uint64_t presented = 0;
    std::uint64_t dropped = 0;
    std::uint64_t audioRejected = 0;
    std::uint64_t audioDecoded = 0;
    std::int64_t audioBuffered100ns = 0;
    std::uint64_t audioUnderruns = 0;
    std::uint64_t presents = 0;
    std::uint64_t presentWait100ns = 0;
    std::uint32_t queuedMin = 0xffffffffu;
    std::uint32_t queuedMax = 0;

    void Fold(const FFF3FPSnapshot& snap) {
        decoded = snap.decodedVideoFrames;
        presented = snap.presentedVideoFrames;
        dropped = snap.droppedVideoFrames;
        audioRejected = snap.audioRejectedFrames;
        audioDecoded = snap.decodedAudioFrames;
        audioBuffered100ns = snap.bufferedAudio100ns;
        audioUnderruns = snap.audioUnderruns;
        presents = snap.swapChainPresents;
        presentWait100ns = snap.presentWait100ns;
        if (snap.queuedVideoFrames < queuedMin) queuedMin = snap.queuedVideoFrames;
        if (snap.queuedVideoFrames > queuedMax) queuedMax = snap.queuedVideoFrames;
    }
};

const char* StateName(const FFF3FPState state) {
    switch (state) {
    case FFF3FPState::Idle: return "Idle";
    case FFF3FPState::Opening: return "Opening";
    case FFF3FPState::Ready: return "Ready";
    case FFF3FPState::Playing: return "Playing";
    case FFF3FPState::Paused: return "Paused";
    case FFF3FPState::Ended: return "Ended";
    case FFF3FPState::Failed: return "Failed";
    case FFF3FPState::Closed: return "Closed";
    default: return "?";
    }
}

// 首行回显压力臂开关：内核自己读这些 env，探针不解释，只把当时的值打出来，
// 报告里好把"哪一臂用了哪档"对齐到日志。未设置打 "-"。
void PrintEnvLine() {
    const char* memarb = std::getenv("FFF3FP_MEMARB");
    const char* capMb = std::getenv("FFF3FP_MEMARB_CAP_MB");
    const char* audioCap = std::getenv("FFF_AUDIO_CAP_KB");
    std::printf("[probe] env MEMARB=%s CAP_MB=%s AUDIO_CAP_KB=%s\n",
        memarb ? memarb : "-", capMb ? capMb : "-", audioCap ? audioCap : "-");
}

// 预取四计数直读：hits/misses 是 u64，lruEvictions/budgetTrims 借用
// reserved[0]/reserved[1]（u32，见 FFF.Player.Prefetch.h 的 ABI 约定）。
bool ReadPrefetchStatus(FFF3FPPrefetchStatus& st) {
    st = FFF3FPPrefetchStatus{};
    st.size = sizeof(st);
    st.version = FFF3FP_PREFETCH_STATUS_VERSION;
    return FFF3FP_GetPrefetchStatus(&st) == FFFResult::Success;
}

// 两模式结束都打这一行（脚本统一抓它）。
void PrintPrefetchLine(const FFF3FPPrefetchStatus& st) {
    std::printf("[probe] prefetch hits=%llu misses=%llu lru=%u trim=%u cached=%llu budget=%llu\n",
        static_cast<unsigned long long>(st.hits), static_cast<unsigned long long>(st.misses),
        st.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_LRU_EVICTIONS],
        st.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_BUDGET_TRIMS],
        static_cast<unsigned long long>(st.cachedBytes),
        static_cast<unsigned long long>(st.budgetBytes));
}

std::string Ut8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// 打开后等到 Ready（或 Failed）。返回 true = Ready。
bool WaitReady(FFF3FPHandle player, FFF3FPSnapshot& snap, const int maxWaitMs) {
    for (int waited = 0; waited < maxWaitMs; waited += 50) {
        if (FFF3FP_GetSnapshot(player, &snap) == FFFResult::Success &&
            (static_cast<int>(snap.state) >= static_cast<int>(FFF3FPState::Ready) ||
                snap.state == FFF3FPState::Failed))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return snap.state != FFF3FPState::Failed &&
        static_cast<int>(snap.state) >= static_cast<int>(FFF3FPState::Ready);
}

// 图片目录模式：枚举 png/jpg（文件名排序），逐张「发计划 → Open → 等 Ready →
// Redraw → 读 status → Close」。计划的 flags 必须带 STILL_BROWSING：内核的 in-process
// still lane 只在该 flag 下预热邻居图片（PrefetchService.cpp），否则缓存永远是冷的，
// hits 恒 0——这正是本模式要抬起来的指标。
int RunImagesMode(FFF3FPHandle player, const std::string& dirUtf8, const int maxImages) {
    std::vector<std::string> images;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(
                std::filesystem::u8path(dirUtf8))) {
            if (!entry.is_regular_file()) continue;
            std::string lower = Ut8(entry.path().extension());
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower != ".png" && lower != ".jpg" && lower != ".jpeg") continue;
            images.push_back(Ut8(std::filesystem::absolute(entry.path())));
        }
    } catch (const std::exception& e) {
        std::printf("[probe] images enum-failed: %s\n", e.what());
        return 5;
    }
    std::sort(images.begin(), images.end());
    if (maxImages > 0 && static_cast<int>(images.size()) > maxImages)
        images.resize(maxImages);
    std::printf("[probe] images found=%zu dir=%s\n", images.size(), dirUtf8.c_str());
    if (images.empty()) return 5;

    FFF3FPPrefetchStatus base{};
    const bool haveBase = ReadPrefetchStatus(base);
    long long baseHits = haveBase ? static_cast<long long>(base.hits) : -1;
    long long baseMisses = haveBase ? static_cast<long long>(base.misses) : -1;
    std::uint32_t baseLru = haveBase ?
        base.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_LRU_EVICTIONS] : 0;
    std::uint32_t baseTrim = haveBase ?
        base.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_BUDGET_TRIMS] : 0;

    int opened = 0;
    int readyCount = 0;
    FFF3FPSnapshot snap{};
    snap.size = sizeof(snap);
    snap.version = kSnapshotVersion;

    for (std::size_t i = 0; i < images.size(); ++i) {
        // 当前文件在最前，其余按文件名顺序作邻居；generation 逐张自增，作废旧预热。
        std::string planPaths = images[i];
        for (std::size_t j = 0; j < images.size(); ++j) {
            if (j == i) continue;
            planPaths += "\n";
            planPaths += images[j];
        }
        FFF3FPPrefetchPlan plan{};
        plan.size = sizeof(plan);
        plan.version = FFF3FP_PREFETCH_PLAN_VERSION;
        plan.generation = static_cast<std::uint32_t>(i + 1);
        plan.flags = FFF3FP_PREFETCH_FLAG_STILL_BROWSING;
        plan.pathsUtf8 = planPaths.c_str();
        const auto planResult = FFF3FP_SetPrefetchPlan(&plan);
        std::printf("[probe] plan idx=%zu result=%d\n", i, static_cast<int>(planResult));

        const auto openResult = FFF3FP_Open(player, images[i].c_str());
        ++opened;
        const bool ready = WaitReady(player, snap, 3000);
        if (ready) ++readyCount;
        FFF3FP_Redraw(player);

        FFF3FPPrefetchStatus st{};
        if (ReadPrefetchStatus(st)) {
            const char* leaf = std::strrchr(images[i].c_str(), '/');
            if (!leaf) leaf = std::strrchr(images[i].c_str(), '\\');
            std::printf("[probe] img idx=%zu file=%s open=%d state=%s hits=%llu misses=%llu "
                "lru=%u trim=%u cached=%llu budget=%llu\n", i,
                leaf ? leaf + 1 : images[i].c_str(), static_cast<int>(openResult),
                StateName(snap.state),
                static_cast<unsigned long long>(st.hits),
                static_cast<unsigned long long>(st.misses),
                st.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_LRU_EVICTIONS],
                st.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_BUDGET_TRIMS],
                static_cast<unsigned long long>(st.cachedBytes),
                static_cast<unsigned long long>(st.budgetBytes));
        } else {
            std::printf("[probe] img idx=%zu open=%d state=%s prefetch-unavailable\n",
                i, static_cast<int>(openResult), StateName(snap.state));
        }
        FFF3FP_Close(player);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    FFF3FPPrefetchStatus end{};
    const bool haveEnd = ReadPrefetchStatus(end);
    const long long hits1 = haveEnd ? static_cast<long long>(end.hits) : 0;
    const long long misses1 = haveEnd ? static_cast<long long>(end.misses) : 0;
    const long long dHits = haveBase ? hits1 - baseHits : hits1;
    const long long dMisses = haveBase ? misses1 - baseMisses : misses1;
    const std::uint32_t lru1 = haveEnd ?
        end.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_LRU_EVICTIONS] : 0;
    const std::uint32_t trim1 = haveEnd ?
        end.reserved[FFF3FP_PREFETCH_STATUS_RESERVED_BUDGET_TRIMS] : 0;
    const double denom = static_cast<double>(dHits + dMisses);
    const double hitRate = denom > 0.0 ? static_cast<double>(dHits) / denom : 0.0;
    std::printf("[probe] images-summary opened=%d ready=%d hits0=%lld hits1=%lld misses0=%lld "
        "misses1=%lld lru0=%u lru1=%u trim0=%u trim1=%u hitrate=%.3f\n",
        opened, readyCount, baseHits, hits1, baseMisses, misses1, baseLru, lru1,
        baseTrim, trim1, hitRate);
    if (haveEnd) PrintPrefetchLine(end);
    return 0;
}

// 随机 seek 风暴：混合序列（连续双 seek 间隔 <50ms、90% 处回跳 10%、seek 后立即
// Pause 再 Play），每次打目标 PTS 与返回码；结束打心跳 present 计数证明没死锁。
// epoch 单调性探针读不到（snapshot 无此字段），由报告从 [epoch] advance trace 抓。
int RunStormMode(FFF3FPHandle player, const std::string& media, const double seconds,
    const int seekCount) {
    FFF3FPSnapshot snap{};
    snap.size = sizeof(snap);
    snap.version = kSnapshotVersion;

    FFF3FP_ConfigurePlaybackPerformance(player, -1, 2, 0);
    if (FFF3FP_Open(player, media.c_str()) != FFFResult::Success) {
        std::printf("storm open-failed\n");
        return 5;
    }
    if (!WaitReady(player, snap, 10000)) {
        std::printf("storm not-ready state=%s\n", StateName(snap.state));
        return 7;
    }
    const std::int64_t duration = snap.duration100ns;
    FFF3FP_Play(player);

    int seeks = 0;
    int failed = 0;
    const auto started = std::chrono::steady_clock::now();
    double nextSeek = 1.0;
    const char* lastCmd = "none";
    // (相对秒, presents, state) 采样环：心跳只在 Playing 态下计入（Paused/Ended 没
    // 心跳是正常的，不构成死锁证据）。
    struct HbSample { double t; std::uint64_t presents; FFF3FPState state; };
    std::vector<HbSample> heartbeat;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - started).count();
        if (elapsed >= seconds) break;
        if (FFF3FP_GetSnapshot(player, &snap) == FFFResult::Success) {
            heartbeat.push_back({ elapsed, snap.swapChainPresents, snap.state });
        }
        if (elapsed >= nextSeek && seeks < seekCount) {
            // 确定性伪随机（同参数可复跑）：kind=3 打 90%、kind=4 回跳 10% 并接
            // Pause→Play；kind=3 同时再补一个 <50ms 的紧接 seek（与主 seek 同序号）。
            const int kind = seeks % 7;
            double frac = 0.05 + 0.9 * ((seeks * 37u) % 100u) / 100.0;
            if (kind == 3) frac = 0.90;
            if (kind == 4) frac = 0.10;
            const auto target = static_cast<std::int64_t>(static_cast<double>(duration) * frac);
            const auto r = FFF3FP_Seek(player, target);
            std::printf("[probe] storm-seek i=%d pts=%lld result=%d t=%.2f\n", seeks,
                static_cast<long long>(target), static_cast<int>(r), elapsed);
            if (r != FFFResult::Success) ++failed;
            FFF3FP_Redraw(player);
            lastCmd = "seek";
            if (kind == 3) {   // <50 ms 内的紧接第二 seek（回 10% 处，与上一 seek 同序号）
                const auto target2 = static_cast<std::int64_t>(static_cast<double>(duration) * 0.10);
                const auto r2 = FFF3FP_Seek(player, target2);
                std::printf("[probe] storm-seek i=%d pts=%lld result=%d t=%.2f burst\n", seeks,
                    static_cast<long long>(target2), static_cast<int>(r2), elapsed);
                if (r2 != FFFResult::Success) ++failed;
                FFF3FP_Redraw(player);
                lastCmd = "burst";
            }
            if (kind == 4) {   // seek 后立即 Pause 再 Play
                FFF3FP_Pause(player);
                std::printf("[probe] storm-pause-after-seek i=%d t=%.2f\n", seeks, elapsed);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                FFF3FP_Play(player);
                lastCmd = "pause-play";
            }
            ++seeks;
            nextSeek += (seconds / seekCount) * (0.7 + 0.06 * ((seeks * 13u) % 10u));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 收尾统一回到 Play，避免"最后一条动作是 Pause"把末态停在 Paused 造成误判。
    FFF3FP_Play(player);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    FFF3FP_Stop(player);
    FFF3FP_GetSnapshot(player, &snap);
    // 最后 1 s 窗口内、相邻两点前态为 Playing 时的 presents 增量之和 = Playing 心跳。
    std::uint64_t hb = 0;
    double endT = heartbeat.empty() ? 0.0 : heartbeat.back().t;
    for (size_t k = 1; k < heartbeat.size(); ++k) {
        if (heartbeat[k].t < endT - 1.0) continue;
        if (heartbeat[k - 1].state == FFF3FPState::Playing) {
            const std::int64_t d = static_cast<std::int64_t>(heartbeat[k].presents)
                - static_cast<std::int64_t>(heartbeat[k - 1].presents);
            if (d > 0) hb += static_cast<std::uint64_t>(d);
        }
    }
    std::printf("[probe] storm seeks=%d failed=%d maxEpoch=-1(trace) finalState=%s lastCmd=%s "
        "forcedPlay=1 heartbeatWhilePlaying=%llu\n",
        seeks, failed, StateName(snap.state), lastCmd,
        static_cast<unsigned long long>(hb));
    FFF3FP_Close(player);
    return 0;
}

// 把内核 fprintf(stderr) 的 trace 行经匿名 pipe 截获：原样 tee 回真实 stderr（日志
// 里仍保留全部 trace），同时抓每 session 恰好一条的 [arbiter] attach budget=.. MiB
// 与 detach 汇总行 [arbiter] budget total=..mib granted=..mib（MemoryBudgetArbiter
// ::DetachSession，行里没有 "detach" 字样，按最后一条 budget 行取 granted）。
struct StderrTee {
    HANDLE realStdErr = nullptr;
    HANDLE pipeRead = nullptr;
    HANDLE pipeWrite = nullptr;
    HANDLE dumpFile = nullptr;
    HANDLE readerThread = nullptr;
    int savedFd2 = -1;
    int pipeWfd = -1;
    std::uint64_t attachMib = 0;        // 最近一条 attach
    std::uint64_t detachGrantedMib = 0; // 最近一条 detach 汇总 granted
    std::uint64_t detachTotalMib = 0;
    bool detachSeen = false;

    // B-1 用：[epoch] present 行全量收集（reader 线程写，主线程加锁读）。
    struct EpochPresent { std::int64_t pts; std::uint64_t epoch; std::uint64_t stale; std::uint64_t afterBump; };
    std::mutex epochMu;
    std::vector<EpochPresent> presents;

    static DWORD WINAPI ReaderMain(LPVOID arg) {
        auto* self = static_cast<StderrTee*>(arg);
        char buf[2048];
        std::string line;
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(self->pipeRead, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            for (DWORD i = 0; i < n; ++i) {
                if (buf[i] == '\n') { self->Forward(line); line.clear(); }
                else if (buf[i] != '\r') line.push_back(buf[i]);
            }
        }
        if (!line.empty()) self->Forward(line);
        return 0;
    }
    void Forward(const std::string& line) {
        DWORD w = 0;
        if (!line.empty()) {
            WriteFile(realStdErr, line.data(), static_cast<DWORD>(line.size()), &w, nullptr);
            WriteFile(realStdErr, "\n", 1u, &w, nullptr);
            // 兜底：原样落 dump 文件，主日志的 pipe 合并丢行时还有一份完整 trace。
            if (dumpFile != nullptr && dumpFile != INVALID_HANDLE_VALUE) {
                WriteFile(dumpFile, line.data(), static_cast<DWORD>(line.size()), &w, nullptr);
                WriteFile(dumpFile, "\n", 1u, &w, nullptr);
            }
        }
        if (line.rfind("[arbiter] attach budget=", 0) == 0) {
            attachMib = std::strtoull(line.c_str() + 24, nullptr, 10);
        } else if (line.rfind("[arbiter] budget total=", 0) == 0) {
            detachSeen = true;
            detachTotalMib = std::strtoull(line.c_str() + 23, nullptr, 10);
            const char* g = std::strstr(line.c_str(), " granted=");
            if (g) detachGrantedMib = std::strtoull(g + 9, nullptr, 10);
        } else if (line.rfind("[epoch] present ", 0) == 0) {
            EpochPresent e{};
            if (std::sscanf(line.c_str() + 16, " pts=%lld epoch=%llu stale=%llu afterBump=%llu",
                    &e.pts, &e.epoch, &e.stale, &e.afterBump) == 4) {
                std::lock_guard<std::mutex> lk(epochMu);
                presents.push_back(e);
            }
        }
    }
    bool Start(const char* dumpPath = nullptr) {
        realStdErr = GetStdHandle(STD_ERROR_HANDLE);
        if (dumpPath) {
            dumpFile = CreateFileA(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        if (!CreatePipe(&pipeRead, &pipeWrite, &sa, 4096)) return false;
        savedFd2 = _dup(2);
        pipeWfd = _open_osfhandle(reinterpret_cast<intptr_t>(pipeWrite), _O_WRONLY);
        if (pipeWfd == -1) return false;
        if (_dup2(pipeWfd, 2) != 0) return false;
        readerThread = CreateThread(nullptr, 0, ReaderMain, this, 0, nullptr);
        return readerThread != nullptr;
    }
    void Stop() {
        if (savedFd2 != -1) { _dup2(savedFd2, 2); _close(savedFd2); savedFd2 = -1; }
        if (pipeWfd != -1) { _close(pipeWfd); pipeWfd = -1; }  // 关写端 → ReadFile 退出
        if (readerThread) { WaitForSingleObject(readerThread, 500); CloseHandle(readerThread); readerThread = nullptr; }
        if (pipeRead) { CloseHandle(pipeRead); pipeRead = nullptr; }
        if (dumpFile != nullptr && dumpFile != INVALID_HANDLE_VALUE) { CloseHandle(dumpFile); dumpFile = nullptr; }
    }
};

// Open→Ready→Play 1s→Stop→Close 循环 N 次：盯句柄数（区分首开一次性 init 与稳态
// 每轮泄漏）、每 session 的 detach granted 是否归零、以及当轮整机可用内存（attach
// 预算随 avail 重算，拿它判定漂移是否自洽，而不是拿首末 attach 值硬比）。
int RunLoopMode(FFF3FPHandle player, const std::string& media, const int count) {
    FFF3FPSnapshot snap{};
    snap.size = sizeof(snap);
    snap.version = kSnapshotVersion;
    HANDLE hProcess = GetCurrentProcess();
    DWORD hcBefore = 0;
    GetProcessHandleCount(hProcess, &hcBefore);
    FFF3FPPrefetchStatus pfFirst{};
    ReadPrefetchStatus(pfFirst);

    StderrTee tee;
    tee.Start("loop-trace.dump.txt");
    std::printf("[probe] loop-start handleCount=%lu budget=%llu\n",
        static_cast<unsigned long>(hcBefore),
        static_cast<unsigned long long>(pfFirst.budgetBytes));

    DWORD hcAfterFirst = 0;
    std::uint64_t sessionsSeen = 0;
    std::uint64_t detachGrantedNonZero = 0;

    for (int i = 0; i < count; ++i) {
        const auto r = FFF3FP_Open(player, media.c_str());
        const bool ready = WaitReady(player, snap, 3000);
        FFF3FP_Play(player);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        FFF3FP_Stop(player);
        FFF3FP_Close(player);
        // detach 汇总行由 Close 触发、经 pipe 异步到达：留 200ms 让它落地再读数。
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        DWORD hc = 0;
        GetProcessHandleCount(hProcess, &hc);
        if (i == 0) hcAfterFirst = hc;
        if (tee.detachSeen) {
            ++sessionsSeen;
            if (tee.detachGrantedMib != 0) ++detachGrantedNonZero;
        }
        MEMORYSTATUSEX msx{};
        msx.dwLength = sizeof(msx);
        GlobalMemoryStatusEx(&msx);
        FFF3FPPrefetchStatus pf{};
        ReadPrefetchStatus(pf);
        std::printf("[probe] loop i=%d open=%d state=%s handleCount=%lu cached=%llu budget=%llu "
            "attachBudget=%lluMiB finalGranted=%lluMiB detachSeen=%d availPhys=%lluMiB commitAvail=%lluMiB\n",
            i, static_cast<int>(r), ready ? StateName(snap.state) : "Timeout",
            static_cast<unsigned long>(hc),
            static_cast<unsigned long long>(pf.cachedBytes),
            static_cast<unsigned long long>(pf.budgetBytes),
            static_cast<unsigned long long>(tee.attachMib),
            static_cast<unsigned long long>(tee.detachGrantedMib),
            tee.detachSeen ? 1 : 0,
            static_cast<unsigned long long>(msx.ullAvailPhys / (1024 * 1024)),
            static_cast<unsigned long long>(msx.ullAvailPageFile / (1024 * 1024)));
    }
    DWORD hcLast = 0;
    GetProcessHandleCount(hProcess, &hcLast);
    FFF3FPPrefetchStatus pfLast{};
    ReadPrefetchStatus(pfLast);
    tee.Stop();

    const long firstOpenDelta = static_cast<long>(hcAfterFirst) - static_cast<long>(hcBefore);
    const long steadyDelta = static_cast<long>(hcLast) - static_cast<long>(hcAfterFirst);
    std::printf("[probe] loop-summary iterations=%d handleBefore=%lu handleAfterFirst=%lu handleLast=%lu "
        "firstOpenDelta=%ld steadyDelta=%ld sessionsSeen=%llu detachGrantedNonZero=%llu "
        "budgetFirst=%llu budgetLast=%llu budgetDelta=%lld\n", count,
        static_cast<unsigned long>(hcBefore),
        static_cast<unsigned long>(hcAfterFirst),
        static_cast<unsigned long>(hcLast),
        firstOpenDelta, steadyDelta,
        static_cast<unsigned long long>(sessionsSeen),
        static_cast<unsigned long long>(detachGrantedNonZero),
        static_cast<unsigned long long>(pfFirst.budgetBytes),
        static_cast<unsigned long long>(pfLast.budgetBytes),
        static_cast<long long>(pfLast.budgetBytes) - static_cast<long long>(pfFirst.budgetBytes));
    return 0;
}

// B-1：旧帧窗口定量化。Play 稳定 3 s 后，对 30%/60%/85% 三个 seek 目标，各记录
// seek 前 30 条与 seek 后 30 条 [epoch] present 行；旧帧窗口 = seek 之后第一条
// pts≥T 的 present 之前、仍 pts<T 的 present 条数。pts 取不到（-1）则 ptsValid=0，
// 报告侧判"无法判定"。
int RunB1Mode(FFF3FPHandle player, const std::string& media) {
    FFF3FPSnapshot snap{};
    snap.size = sizeof(snap);
    snap.version = kSnapshotVersion;
    FFF3FP_ConfigurePlaybackPerformance(player, -1, 2, 0);
    if (FFF3FP_Open(player, media.c_str()) != FFFResult::Success) {
        std::printf("b1 open-failed\n");
        return 5;
    }
    if (!WaitReady(player, snap, 10000)) {
        std::printf("b1 not-ready state=%s\n", StateName(snap.state));
        return 7;
    }
    const std::int64_t duration = snap.duration100ns;
    // [epoch] present 的 pts 是流 time_base 单位（不是 100ns），而 Seek 目标是 100ns；
    // 用快照里的 frameTimeBase 把 T 换算到同一坐标系再比大小。
    const double tbUnitsPerSec = (snap.frameTimeBaseNumerator > 0 && snap.frameTimeBaseDenominator > 0)
        ? static_cast<double>(snap.frameTimeBaseDenominator) / static_cast<double>(snap.frameTimeBaseNumerator)
        : 10000000.0;
    FFF3FP_Play(player);

    StderrTee tee;
    tee.Start("b1-trace.dump.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));  // 稳定 3 s

    const double fracs[3] = { 0.30, 0.60, 0.85 };
    for (int t = 0; t < 3; ++t) {
        const auto target = static_cast<std::int64_t>(static_cast<double>(duration) * fracs[t]);
        const std::int64_t targetPts = static_cast<std::int64_t>(
            (static_cast<double>(target) / 10000000.0) * tbUnitsPerSec);
        std::vector<StderrTee::EpochPresent> pre;
        size_t mark = 0;
        {
            std::lock_guard<std::mutex> lk(tee.epochMu);
            mark = tee.presents.size();
            const size_t n = mark;
            pre.assign(tee.presents.begin() + (n > 30 ? n - 30 : 0), tee.presents.begin() + n);
        }
        const auto r = FFF3FP_Seek(player, target);
        FFF3FP_Redraw(player);
        // 等 seek 后 30 条 present（上限 15 s）
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        std::vector<StderrTee::EpochPresent> post;
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lk(tee.epochMu);
                if (tee.presents.size() >= mark + 30) {
                    post.assign(tee.presents.begin() + mark, tee.presents.begin() + mark + 30);
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (post.empty()) {
            std::lock_guard<std::mutex> lk(tee.epochMu);
            post.assign(tee.presents.begin() + mark, tee.presents.end());
        }
        std::printf("[probe] b1 target=%.2f T=%lld seekResult=%d preCount=%zu postCount=%zu\n",
            fracs[t], static_cast<long long>(target), static_cast<int>(r), pre.size(), post.size());
        for (size_t i = 0; i < pre.size(); ++i) {
            std::printf("[probe] b1-pre idx=%zu pts=%lld epoch=%llu stale=%llu afterBump=%llu\n", i,
                static_cast<long long>(pre[i].pts), static_cast<unsigned long long>(pre[i].epoch),
                static_cast<unsigned long long>(pre[i].stale), static_cast<unsigned long long>(pre[i].afterBump));
        }
        for (size_t i = 0; i < post.size(); ++i) {
            std::printf("[probe] b1-post idx=%zu pts=%lld epoch=%llu stale=%llu afterBump=%llu\n", i,
                static_cast<long long>(post[i].pts), static_cast<unsigned long long>(post[i].epoch),
                static_cast<unsigned long long>(post[i].stale), static_cast<unsigned long long>(post[i].afterBump));
        }
        long below = 0;
        size_t oldWin = post.size();
        // 以 epoch 跳变为新帧到达点（与文件 time_base 单位无关）：旧帧窗口 = seek 后
        // epoch 仍等于 seek 前 epoch 的 present 条数（旧画面继续翻页的帧数）。
        std::int64_t epochBefore = pre.empty() ? -1 : pre.back().epoch;
        std::int64_t landingPts = -1;
        for (size_t i = 0; i < post.size(); ++i) {
            if (static_cast<std::int64_t>(post[i].epoch) > epochBefore) {
                oldWin = i;
                landingPts = post[i].pts;
                break;
            }
        }
        if (landingPts >= 0) {
            for (const auto& e : post) if (e.pts < landingPts) ++below;
        } else {
            for (const auto& e : post) if (e.pts >= 0 && e.pts < targetPts) ++below;
        }
        const bool ptsValid = !post.empty() && post[0].pts >= 0;
        std::printf("[probe] b1-summary target=%.2f T=%lld epochBefore=%lld landingPts=%lld "
            "oldFrameWindow=%zu post30BelowLanding=%ld ptsValid=%d\n",
            fracs[t], static_cast<long long>(target), epochBefore,
            static_cast<long long>(landingPts), oldWin, below, ptsValid ? 1 : 0);
    }
    {
        std::lock_guard<std::mutex> lk(tee.epochMu);
        bool nondecEpoch = true;   // 允许持平（同一帧重复呈现），只判不回落
        bool nondecStale = true;
        for (size_t i = 1; i < tee.presents.size(); ++i) {
            if (tee.presents[i].epoch < tee.presents[i - 1].epoch) nondecEpoch = false;
            if (tee.presents[i].stale < tee.presents[i - 1].stale) nondecStale = false;
        }
        std::printf("[probe] b1-all totalPresents=%zu epochNonDecreasing=%d staleNonDecreasing=%d afterBumpFinal=%llu\n",
            tee.presents.size(), nondecEpoch ? 1 : 0, nondecStale ? 1 : 0,
            tee.presents.empty() ? 0ull : static_cast<unsigned long long>(tee.presents.back().afterBump));
    }
    tee.Stop();
    FFF3FP_Stop(player);
    FFF3FP_Close(player);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    PrintEnvLine();

    // --images <目录> [最多张数] 走图片目录模式；--storm/--loop 走证伪臂；
    // 其余位置参数一律按视频模式解析。
    std::vector<std::string> args(argv + 1, argv + argc);
    const auto imagesFlag = std::find(args.begin(), args.end(), "--images");
    const bool imagesMode = imagesFlag != args.end();
    const auto stormFlag = std::find(args.begin(), args.end(), "--storm");
    const bool stormMode = stormFlag != args.end();
    const auto loopFlag = std::find(args.begin(), args.end(), "--loop");
    const bool loopMode = loopFlag != args.end();
    const auto b1Flag = std::find(args.begin(), args.end(), "--b1");
    const bool b1Mode = b1Flag != args.end();
    std::string imgDir;
    int maxImages = 0;
    if (imagesMode) {
        if (imagesFlag + 1 != args.end()) imgDir = *(imagesFlag + 1);
        if (imagesFlag + 2 != args.end()) maxImages = std::atoi((*(imagesFlag + 2)).c_str());
        if (imgDir.empty()) {
            std::fprintf(stderr, "usage: membudget-probe --images <目录> [最多张数]\n");
            return 2;
        }
    }
    std::string stormMedia, loopMedia, b1Media;
    double stormSeconds = 30.0;
    int stormSeeks = 60;
    int loopCount = 10;
    if (stormMode) {
        if (stormFlag + 1 != args.end()) stormMedia = *(stormFlag + 1);
        if (stormFlag + 2 != args.end()) stormSeconds = std::atof((*(stormFlag + 2)).c_str());
        if (stormFlag + 3 != args.end()) stormSeeks = std::atoi((*(stormFlag + 3)).c_str());
        if (stormMedia.empty()) {
            std::fprintf(stderr, "usage: membudget-probe --storm <媒体> [秒数] [seek次数]\n");
            return 2;
        }
    }
    if (loopMode) {
        if (loopFlag + 1 != args.end()) loopMedia = *(loopFlag + 1);
        if (loopFlag + 2 != args.end()) loopCount = std::atoi((*(loopFlag + 2)).c_str());
        if (loopMedia.empty()) {
            std::fprintf(stderr, "usage: membudget-probe --loop <媒体> [次数]\n");
            return 2;
        }
    }
    if (b1Mode) {
        if (b1Flag + 1 != args.end()) b1Media = *(b1Flag + 1);
        if (b1Media.empty()) {
            std::fprintf(stderr, "usage: membudget-probe --b1 <媒体>\n");
            return 2;
        }
    }

    if (!imagesMode && argc < 2) {
        std::fprintf(stderr, "usage: membudget-probe <媒体> [seconds] [videoProfile 0/1/2]\n");
        return 2;
    }
    const char* media = imagesMode ? nullptr : argv[1];
    const double seconds = !imagesMode && argc > 2 ? std::atof(argv[2]) : 12.0;
    // 第三个参数是视频缓冲档位（0 平衡 / 1 更宽 / 2 压测）。压测档会把视频授予量
    // 压到更小的目标，正是动态重算与死区抑制唯一会被看见的场合。
    const std::uint32_t videoProfile = !imagesMode && argc > 3
        ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 0u;

    FFF3FPConfiguration config{};
    config.size = sizeof(config);
    config.version = kPlayerApiVersion;
    config.preferredAdapterIndex = -1;
    config.decodeMode = FFF3FPDecodeMode::D3D11;
    // Create rejects a zeroed colour preference (peaks must be finite and positive),
    // so the probe states the SDR defaults a host would: 203 nits peak, 100 paper white.
    config.sdrPeakNits = 203.0f;
    config.hdrPeakNits = 0.0f;
    config.sdrPaperWhiteNits = 100.0f;

    FFF3FPHandle player = nullptr;
    auto result = FFF3FP_Create(&config, &player);
    if (result != FFFResult::Success || player == nullptr) {
        std::printf("FAIL create=%d\n", static_cast<int>(result));
        return 3;
    }

    const auto window = CreateProbeWindow(1280, 720);
    if (window == nullptr) {
        std::printf("FAIL window\n");
        FFF3FP_Destroy(player);
        return 3;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    std::thread pump(RunMessagePump);
    pump.detach();
    std::printf("output-window=%d\n", static_cast<int>(FFF3FP_SetOutputWindow(player, window)));
    if (!imagesMode && videoProfile != 0)
        std::printf("video-profile=%d result=%d\n", static_cast<int>(videoProfile),
            static_cast<int>(FFF3FP_ConfigurePlaybackPerformance(player, -1, videoProfile, 0)));

    if (imagesMode) {
        const int rc = RunImagesMode(player, imgDir, maxImages);
        FFF3FP_Destroy(player);
        PostMessageW(window, WM_QUIT, 0, 0);
        return rc;
    }
    if (stormMode) {
        const int rc = RunStormMode(player, stormMedia, stormSeconds, stormSeeks);
        FFF3FP_Destroy(player);
        PostMessageW(window, WM_QUIT, 0, 0);
        return rc;
    }
    if (loopMode) {
        const int rc = RunLoopMode(player, loopMedia, loopCount);
        FFF3FP_Destroy(player);
        PostMessageW(window, WM_QUIT, 0, 0);
        return rc;
    }
    if (b1Mode) {
        const int rc = RunB1Mode(player, b1Media);
        FFF3FP_Destroy(player);
        PostMessageW(window, WM_QUIT, 0, 0);
        return rc;
    }

    result = FFF3FP_Open(player, media);
    std::printf("open=%d\n", static_cast<int>(result));
    if (result != FFFResult::Success) {
        // The Σfloor refusal is the case worth reading: it must carry an explicit code
        // and the numbers behind it, not a generic NativeFailure.
        std::printf("last-error=%s\n", LastError(player).c_str());
        FFF3FP_Destroy(player);
        PostMessageW(window, WM_QUIT, 0, 0);
        return result == FFFResult::ResourceExhausted ? 4 : 5;
    }

    FFF3FPSnapshot snap{};
    snap.size = sizeof(snap);
    snap.version = kSnapshotVersion;
    for (int i = 0; i < 200; ++i) {
        if (FFF3FP_GetSnapshot(player, &snap) == FFFResult::Success &&
            (static_cast<int>(snap.state) >= static_cast<int>(FFF3FPState::Ready) ||
                snap.state == FFF3FPState::Failed))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (snap.state == FFF3FPState::Failed || static_cast<int>(snap.state) < static_cast<int>(FFF3FPState::Ready)) {
        // Open is queued: the C entry point returns immediately, so a refused open
        // shows up as Failed + the error text, not as a non-zero Open result.
        std::printf("FAIL open-refused state=%s last-error=%s\n", StateName(snap.state),
            LastError(player).c_str());
        FFF3FP_Destroy(player);
        return 7;
    }
    std::printf("duration100ns=%lld video=%ux%u\n", static_cast<long long>(snap.duration100ns),
        snap.videoWidth, snap.videoHeight);
    {
        // 预取计划照宿主口径下发：当前文件在前，同目录同扩展名的近邻在后，最多四个。
        // 没有计划就没有工作，命中率永远是 0——这一步正是要把它抬起来。
        std::string planPaths(media);
        std::string firstNeighbour;
        int neighbours = 0;
        try {
            const std::filesystem::path mediaPath(media);
            for (const auto& entry : std::filesystem::directory_iterator(mediaPath.parent_path())) {
                if (neighbours >= 4) break;
                if (!entry.is_regular_file()) continue;
                if (entry.path() == mediaPath) continue;
                if (entry.path().extension() != mediaPath.extension()) continue;
                const std::u8string utf8 = entry.path().u8string();
                const std::string neighbour(reinterpret_cast<const char*>(utf8.data()), utf8.size());
                if (firstNeighbour.empty()) firstNeighbour = neighbour;
                planPaths += "\n";
                planPaths += neighbour;
                ++neighbours;
            }
        } catch (...) { /* 目录不可枚举时退化为只预取当前文件 */ }
        FFF3FPPrefetchPlan plan{};
        plan.size = sizeof(plan);
        plan.version = FFF3FP_PREFETCH_PLAN_VERSION;
        plan.generation = 1;
        plan.flags = FFF3FP_PREFETCH_FLAG_PLAYING;
        plan.pathsUtf8 = planPaths.c_str();
        std::printf("prefetch-plan=%d neighbours=%d\n",
            static_cast<int>(FFF3FP_SetPrefetchPlan(&plan)), neighbours);
        gFirstNeighbour = firstNeighbour;
    }
    FFF3FP_Play(player);

    Counters counters{};
    const auto started = std::chrono::steady_clock::now();
    double nextSeekSecond = 2.0;
    // Five seeks two-thirds of the way apart, then a "seek then pause immediately"
    // round: the six presentation shapes the epoch gate has to survive.
    const double seekFractions[] = {0.25, 0.75, 0.50, 0.10, 0.90};
    std::size_t seekIndex = 0;
    bool pausedAfterSeekDone = false;
    auto nextSample = started;
    double nextTickSecond = 2.0;   // [probe] tick 每 2 s 一行（压力臂脚本抓它）

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - started).count();
        if (elapsed >= seconds) break;

        if (FFF3FP_GetSnapshot(player, &snap) == FFFResult::Success) counters.Fold(snap);

        if (elapsed >= nextSeekSecond && seekIndex < std::size(seekFractions)) {
            const auto target = static_cast<std::int64_t>(
                static_cast<double>(snap.duration100ns) * seekFractions[seekIndex]);
            const auto seekResult = FFF3FP_Seek(player, target);
            std::printf("seek[%zu] to=%lld result=%d t=%.1f\n", seekIndex,
                static_cast<long long>(target), static_cast<int>(seekResult), elapsed);
            // 让 Present 线程在"缓存纹理仍是旧 epoch"的时刻被唤醒：真实宿主里这是
            // 用户拖拽后的一次重绘/叠层更新。没有它，Present 只在有解码帧到达时才醒，
            // 闸门永远不会被检验到。
            FFF3FP_Redraw(player);
            std::printf("redraw-after-seek[%zu]\n", seekIndex);
            ++seekIndex;
            nextSeekSecond += 1.0;
        }
        // 场景 4：seek 后立即暂停 —— 队列被清空且不会有新帧上传，呈现侧必须保持
        // 上一帧画面而不是继续翻旧帧，也不应该出现翻转风暴。
        if (seekIndex >= std::size(seekFractions) && !pausedAfterSeekDone && elapsed >= 7.5) {
            FFF3FP_Seek(player, static_cast<std::int64_t>(snap.duration100ns) / 3);
            const auto pauseResult = FFF3FP_Pause(player);
            std::printf("seek-pause result=%d t=%.1f\n", static_cast<int>(pauseResult), elapsed);
            FFF3FP_Redraw(player);
            std::printf("redraw-after-seek-pause\n");
            pausedAfterSeekDone = true;
        }
        if (pausedAfterSeekDone && elapsed >= 9.0) {
            FFF3FP_Play(player);
            pausedAfterSeekDone = false;
            std::printf("resume t=%.1f\n", elapsed);
        }

        // 每 2 s 一行轻量 tick：抖动臂从这里取 queued/audioRejected 的时间序列，
        // 与内核 [arbiter] trace 对齐时间轴。
        if (elapsed >= nextTickSecond) {
            std::printf("[probe] tick t=%.1f state=%s queued=%u audioRejected=%llu\n",
                elapsed, StateName(snap.state), snap.queuedVideoFrames,
                static_cast<unsigned long long>(snap.audioRejectedFrames));
            nextTickSecond += 2.0;
        }

        if (now >= nextSample) {
            std::printf("t=%.1f state=%s queued=%u decoded=%llu presented=%llu dropped=%llu "
                "audioRejected=%llu presents=%llu\n", elapsed, StateName(snap.state),
                snap.queuedVideoFrames, static_cast<unsigned long long>(snap.decodedVideoFrames),
                static_cast<unsigned long long>(snap.presentedVideoFrames),
                static_cast<unsigned long long>(snap.droppedVideoFrames),
                static_cast<unsigned long long>(snap.audioRejectedFrames),
                static_cast<unsigned long long>(snap.swapChainPresents));
            nextSample = now + std::chrono::milliseconds(250);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    FFF3FP_Stop(player);
    FFF3FP_GetSnapshot(player, &snap);
    counters.Fold(snap);
    if (!gFirstNeighbour.empty()) {
        // 打开被预热的邻居并播一小段：命中率只有在真正读一次时才会动，只下发计划
        // 是不会变的（这也是宿主的真实用法：前后翻页）。
        if (FFF3FP_Open(player, gFirstNeighbour.c_str()) == FFFResult::Success) {
            for (int i = 0; i < 60; ++i) {
                if (FFF3FP_GetSnapshot(player, &snap) == FFFResult::Success &&
                    static_cast<int>(snap.state) >= static_cast<int>(FFF3FPState::Ready))
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            FFF3FP_Play(player);
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            std::printf("neighbour-reopen state=%s\n", StateName(snap.state));
            FFF3FP_Stop(player);
        } else {
            std::printf("neighbour-reopen failed\n");
        }
    }
    std::printf("summary-audio decodedAudio=%llu bufferedAudio=%lld underruns=%llu\n",
        static_cast<unsigned long long>(counters.audioDecoded),
        static_cast<long long>(counters.audioBuffered100ns),
        static_cast<unsigned long long>(counters.audioUnderruns));
    {
        // 预取命中率：计划照宿主的做法下发（当前文件在前、同目录邻居在后），
        // 命中/未命中只有这里能读出来。
        FFF3FPPrefetchStatus prefetch{};
        prefetch.size = sizeof(prefetch);
        prefetch.version = FFF3FP_PREFETCH_STATUS_VERSION;
        if (FFF3FP_GetPrefetchStatus(&prefetch) == FFFResult::Success)
            std::printf("summary-prefetch enabled=%u hits=%llu misses=%llu cachedBytes=%llu "
                "budgetBytes=%llu frames=%u probes=%u\n",
                prefetch.enabled, static_cast<unsigned long long>(prefetch.hits),
                static_cast<unsigned long long>(prefetch.misses),
                static_cast<unsigned long long>(prefetch.cachedBytes),
                static_cast<unsigned long long>(prefetch.budgetBytes),
                prefetch.cachedStillFrames, prefetch.cachedProbeEntries);
        else
            std::printf("summary-prefetch unavailable\n");
    }
    // 本轮新增：两模式统一的四计数行（含 reserved[0]/[1] 的 lru/trim），脚本统一抓。
    {
        FFF3FPPrefetchStatus st{};
        if (ReadPrefetchStatus(st)) PrintPrefetchLine(st);
    }
    std::printf("SUMMARY decoded=%llu presented=%llu dropped=%llu audioRejected=%llu "
        "presents=%llu queuedMin=%u queuedMax=%u presentWait100ns=%llu\n",
        static_cast<unsigned long long>(counters.decoded),
        static_cast<unsigned long long>(counters.presented),
        static_cast<unsigned long long>(counters.dropped),
        static_cast<unsigned long long>(counters.audioRejected),
        static_cast<unsigned long long>(counters.presents),
        counters.queuedMin, counters.queuedMax,
        static_cast<unsigned long long>(counters.presentWait100ns));

    FFF3FP_Close(player);
    FFF3FP_Destroy(player);
    PostMessageW(window, WM_QUIT, 0, 0);
    return 0;
}
