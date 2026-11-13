#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "3FP/Prefetch/PrefetchProtocol.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
}

// This process NEVER loads the FFF native kernel, and NEVER touches a window,
// GPU, audio device, playlist, WASAPI, injection pump or AVFrame. WARM only
// pre-reads neighbour file bytes; PROBE only opens a container read-only with
// avformat (bounded probe, no decoder, no frame read) to describe stream POD. The player
// process never loads these FFmpeg DLLs on the worker's behalf.

namespace {

// PROCESS_MODE_BACKGROUND_BEGIN is 0x00100000; define a local fallback for
// SDKs that do not declare it.
#ifndef PROCESS_MODE_BACKGROUND_BEGIN
#define PROCESS_MODE_BACKGROUND_BEGIN 0x00100000
#endif

constexpr std::uint32_t kFrameMax = FFF_PREFETCH_FRAME_MAX_BYTES;
constexpr std::uint32_t kPathCapacity = FFF_PREFETCH_PATH_CAPACITY;
constexpr DWORD kReadChunk = 64u * 1024u;
constexpr std::uint64_t kMaxWarmBytes = 8ull * 1024u * 1024u;

// ---------- pipe helpers: framing is [u32 LE length][payload] ----------

bool ReadExact(HANDLE h, void* dst, DWORD bytes) {
    auto* p = static_cast<unsigned char*>(dst);
    DWORD done = 0;
    while (done < bytes) {
        DWORD got = 0;
        if (!ReadFile(h, p + done, bytes - done, &got, nullptr)) return false;
        if (got == 0) return false;  // EOF or pipe closed
        done += got;
    }
    return true;
}

bool WriteExact(HANDLE h, const void* src, DWORD bytes) {
    auto* p = static_cast<const unsigned char*>(src);
    DWORD done = 0;
    while (done < bytes) {
        DWORD put = 0;
        if (!WriteFile(h, p + done, bytes - done, &put, nullptr)) return false;
        if (put == 0) return false;
        done += put;
    }
    return true;
}

// Drain exactly 'bytes' payload bytes of a malformed/unwanted frame so the
// next length prefix stays aligned. Never writes a result for such a frame.
bool DiscardExact(HANDLE h, std::uint32_t bytes) {
    unsigned char scratch[4096];
    std::uint32_t left = bytes;
    while (left > 0) {
        DWORD chunk = left > sizeof(scratch) ? static_cast<DWORD>(sizeof(scratch)) : left;
        if (!ReadExact(h, scratch, chunk)) return false;
        left -= chunk;
    }
    return true;
}

// ---------- "do not steal resources" knobs (process-local only) ----------

void ApplyBackgroundKnobs() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");

    // (1) Enter background processing mode. Best effort, process-local.
    SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);

    // (2) Very low memory priority. SetProcessInformation is Win8+; resolve it
    // dynamically so an older SDK still builds. ProcessMemoryPriority == 0,
    // MEMORY_PRIORITY_VERY_LOW == 1, MEMORY_PRIORITY_INFORMATION == ULONG.
    if (k32) {
        using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
        auto fn = reinterpret_cast<SetProcessInformationFn>(
            GetProcAddress(k32, "SetProcessInformation"));
        if (fn) {
            struct { ULONG MemoryPriority; } mem{ 1 };
            fn(GetCurrentProcess(), 0, &mem, static_cast<DWORD>(sizeof(mem)));
        }
    }

    // (3) Hard CPU cap of ~25% of one core via a Job Object. Any failed step is
    // silently skipped and the process is only assigned when the cap is set.
    // JOBOBJECT_CPU_RATE_CONTROL_INFORMATION is ABI-equivalent to
    // {DWORD ControlFlags; DWORD CpuRate;}; ENABLE|HARD_CAP == 0x1|0x4;
    // JobObjectCpuRateControlInformation == 15; CpuRate 2500 == 25 percent.
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        struct { DWORD ControlFlags; DWORD CpuRate; } cpu{ 0x1u | 0x4u, 2500u };
        bool configured = false;
        if (k32) {
            using SetInformationJobObjectFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
            auto fn = reinterpret_cast<SetInformationJobObjectFn>(
                GetProcAddress(k32, "SetInformationJobObject"));
            if (fn && fn(hJob, 15, &cpu, static_cast<DWORD>(sizeof(cpu))))
                configured = true;
        }
        if (configured) AssignProcessToJobObject(hJob, GetCurrentProcess());
        // Keep hJob open for the process lifetime; the OS releases it at exit.
    }

    // (4) Lowest priority for the single worker thread.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
}

// FILE_IO_PRIORITY_HINT_INFO is ABI-equivalent to { ULONG PriorityHint; };
// IoPriorityHintVeryLow == 0, FileIoPriorityHintInfo == 12. Resolve the API
// dynamically (Vista+) so an older SDK still builds; failure is ignored.
void SetLowIoPriority(HANDLE h) {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    using SetFileInformationByHandleFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    auto fn = reinterpret_cast<SetFileInformationByHandleFn>(
        GetProcAddress(k32, "SetFileInformationByHandle"));
    if (!fn) return;
    struct { ULONG PriorityHint; } hint{ 0 };
    fn(h, 12, &hint, static_cast<DWORD>(sizeof(hint)));
}

// ---------- path helpers ----------

std::string ClampPath(const char* pathUtf8, std::uint32_t pathBytes) {
    std::uint32_t n = (std::min)(pathBytes, kPathCapacity - 1u);
    std::string s;
    s.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        char c = pathUtf8[i];
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wlen);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

struct OpenedFile {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::uint64_t fileSizeBytes = 0;
    std::int32_t status = 0;
};

OpenedFile OpenForPrefetch(const std::wstring& widePath) {
    OpenedFile o;
    if (widePath.empty()) { o.status = -1; return o; }
    HANDLE h = CreateFileW(widePath.c_str(), FILE_READ_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) { o.status = -1; return o; }
    o.handle = h;
    SetLowIoPriority(h);
    LARGE_INTEGER li{};
    if (GetFileSizeEx(h, &li) && li.QuadPart > 0)
        o.fileSizeBytes = static_cast<std::uint64_t>(li.QuadPart);
    return o;
}

// ---------- WARM ----------

void DoWarm(HANDLE h, FFFPrefetchResult& r) {
    std::vector<unsigned char> buf(kReadChunk);
    std::uint64_t total = 0;
    while (total < kMaxWarmBytes) {
        DWORD want = static_cast<DWORD>((std::min)(kMaxWarmBytes - total,
            static_cast<std::uint64_t>(kReadChunk)));
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) break;
        total += got;
    }
    r.warmedBytes = static_cast<std::uint32_t>(total);
}

// ---------- PROBE: light avformat open, no decoder, no frame read ----------

bool IsStillImageCodec(std::int32_t codecId) {
    switch (codecId) {
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_PNG:
    case AV_CODEC_ID_BMP:
    case AV_CODEC_ID_GIF:
    case AV_CODEC_ID_WEBP:
    case AV_CODEC_ID_TIFF:
    case AV_CODEC_ID_JPEG2000:
    case AV_CODEC_ID_JPEGLS:
    case AV_CODEC_ID_JPEGXL:
    case AV_CODEC_ID_QOI:
    // AVIF/HEIF 静图的单图轨分别是 AV1/HEVC：缺了它们，PROBE 的 still 标志会把
    // 内核按静图处理的文件报成普通视频（信息性字段，但两侧行为应一致）。
    case AV_CODEC_ID_AV1:
    case AV_CODEC_ID_HEVC:
        return true;
    default:
        return false;
    }
}

void DoProbe(const std::string& pathUtf8, FFFPrefetchResult& r) {
    // Defaults for a container avformat cannot describe.
    r.streamCount = 0;
    r.videoStreamIndex = -1;
    r.videoCodecId = -1;
    r.width = 0;
    r.height = 0;
    r.flags = FFF_PREFETCH_FLAG_OTHER;

    // Direct UTF-8 path: FFmpeg converts it to UTF-16 internally on Windows
    // (av_fopen_utf8) and recognises DOS drive letters as the file protocol.
    // Explicit probe bounds keep this side lane from reading deep into a large
    // container. No find_stream_info, no decoder and no frame is ever touched.
    AVFormatContext* fmt = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "probesize", "2097152", 0);
    av_dict_set(&opts, "max_analyze_duration", "1000000", 0);
    const int opened = avformat_open_input(&fmt, pathUtf8.c_str(), nullptr, &opts);
    if (opened < 0 || fmt == nullptr) {
        av_dict_free(&opts);  // failure path: free remaining options
        r.status = -3;  // light open failed; still return a well-formed result
        avformat_close_input(&fmt);
        return;
    }
    av_dict_free(&opts);  // success path: free remaining options

    bool hasVideo = false;
    bool hasAudio = false;
    const unsigned int count = fmt->nb_streams;
    for (unsigned int i = 0; i < count; ++i) {
        AVStream* st = fmt->streams[i];
        if (!st || !st->codecpar) continue;
        const AVCodecParameters* cp = st->codecpar;
        if (cp->codec_type == AVMEDIA_TYPE_VIDEO && !hasVideo) {
            hasVideo = true;
            r.videoStreamIndex = static_cast<std::int32_t>(i);
            r.videoCodecId = static_cast<std::int32_t>(cp->codec_id);
            r.width = cp->width;
            r.height = cp->height;
        } else if (cp->codec_type == AVMEDIA_TYPE_AUDIO) {
            hasAudio = true;
        }
    }
    r.streamCount = static_cast<std::uint32_t>(count);

    std::uint32_t flags = 0;
    if (hasVideo) flags |= FFF_PREFETCH_FLAG_VIDEO;
    if (hasAudio) flags |= FFF_PREFETCH_FLAG_AUDIO;
    if (hasVideo && !hasAudio && count == 1 && IsStillImageCodec(r.videoCodecId))
        flags |= FFF_PREFETCH_FLAG_STILL;
    r.flags = flags ? flags : FFF_PREFETCH_FLAG_OTHER;
    r.status = 0;
    avformat_close_input(&fmt);
}

// ---------- result + log ----------

FFFPrefetchResult MakeBaseResult(const FFFPrefetchTask& t) {
    FFFPrefetchResult r{};
    r.size = static_cast<std::uint32_t>(sizeof(FFFPrefetchResult));
    r.version = FFF_PREFETCH_PROTOCOL_VERSION;
    r.kind = t.kind;
    r.generation = t.generation;
    r.order = t.order;
    r.status = 0;
    r.videoStreamIndex = -1;
    r.videoCodecId = -1;
    r.pathBytes = (std::min)(t.pathBytes, kPathCapacity - 1u);
    std::memcpy(r.pathUtf8, t.pathUtf8, r.pathBytes);
    r.pathUtf8[r.pathBytes] = '\0';
    return r;
}

HANDLE OpenLogFile() {
    wchar_t modulePath[32768];
    DWORD len = GetModuleFileNameW(nullptr, modulePath, 32768);
    if (len == 0 || len >= 32768) return INVALID_HANDLE_VALUE;
    std::wstring dir(modulePath, len);
    std::size_t slash = dir.find_last_of(L"\\/");
    std::wstring logPath = (slash == std::wstring::npos)
        ? L"prefetch.log"
        : dir.substr(0, slash + 1) + L"prefetch.log";
    return CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
}

void AppendLog(HANDLE hLog, DWORD pid, std::uint32_t gen, std::uint32_t kind,
               std::int32_t order, std::uint64_t bytes, const char* action) {
    if (hLog == INVALID_HANDLE_VALUE) return;
    char line[256];
    int n = std::snprintf(line, sizeof(line),
        "[prefetch] pid=%lu gen=%u kind=%u order=%d bytes=%llu action=%s\r\n",
        static_cast<unsigned long>(pid), gen, kind, static_cast<int>(order),
        static_cast<unsigned long long>(bytes), action);
    if (n <= 0) return;
    DWORD toWrite = static_cast<DWORD>((std::min)(n, static_cast<int>(sizeof(line))));
    DWORD written = 0;
    WriteFile(hLog, line, toWrite, &written, nullptr);
}

}  // namespace

int main() {
    ApplyBackgroundKnobs();
    av_log_set_level(AV_LOG_QUIET);  // keep FFmpeg diagnostics off stderr

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hLog = OpenLogFile();
    const DWORD pid = GetCurrentProcessId();

    for (;;) {
        std::uint32_t len = 0;
        // x64 Windows is little-endian, so a raw uint32_t is already LE on the wire.
        if (!ReadExact(hIn, &len, 4)) break;  // stdin EOF / pipe break -> clean exit

        if (len > kFrameMax || len != static_cast<std::uint32_t>(sizeof(FFFPrefetchTask))) {
            // Malformed frame: drain the payload, write no result, keep in sync.
            if (!DiscardExact(hIn, len)) break;
            continue;
        }

        FFFPrefetchTask task{};
        if (!ReadExact(hIn, &task, len)) break;

        if (task.size != static_cast<std::uint32_t>(sizeof(FFFPrefetchTask)) ||
            task.version != FFF_PREFETCH_PROTOCOL_VERSION) {
            continue;  // stale/foreign payload already consumed; no result
        }

        if (task.kind == FFF_PREFETCH_TASK_SHUTDOWN) break;

        FFFPrefetchResult r = MakeBaseResult(task);
        if (task.kind == FFF_PREFETCH_TASK_WARM || task.kind == FFF_PREFETCH_TASK_PROBE) {
            std::string pathUtf8 = ClampPath(task.pathUtf8, task.pathBytes);
            std::wstring widePath = Utf8ToWide(pathUtf8);
            OpenedFile f = OpenForPrefetch(widePath);
            r.fileSizeBytes = f.fileSizeBytes;
            r.status = f.status;
            if (f.status == 0 && f.handle != INVALID_HANDLE_VALUE) {
                if (task.kind == FFF_PREFETCH_TASK_WARM) DoWarm(f.handle, r);
                else DoProbe(pathUtf8, r);
            } else if (task.kind == FFF_PREFETCH_TASK_PROBE) {
                r.flags = FFF_PREFETCH_FLAG_OTHER;
            }
            if (f.handle != INVALID_HANDLE_VALUE) CloseHandle(f.handle);
        } else {
            r.status = -2;  // unknown kind: skipped
        }

        const char* action = (task.kind == FFF_PREFETCH_TASK_PROBE) ? "probe" : "warm";
        std::uint64_t logBytes = (task.kind == FFF_PREFETCH_TASK_PROBE)
            ? r.fileSizeBytes : r.warmedBytes;
        AppendLog(hLog, pid, task.generation, task.kind, task.order, logBytes, action);

        std::uint32_t outLen = static_cast<std::uint32_t>(sizeof(FFFPrefetchResult));
        if (!WriteExact(hOut, &outLen, 4)) break;
        if (!WriteExact(hOut, &r, outLen)) break;
    }

    AppendLog(hLog, pid, 0, 0, 0, 0, "exit");
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    return 0;
}
