#include "pch.h"
#include "3FP/Prefetch/PrefetchService.h"

#include "3FP/Prefetch/MemoryGuard.h"
#include "3FP/Prefetch/PrefetchProtocol.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>

extern void FFF3FP_KernelLogImpl(const char* utf8Line) noexcept;

namespace FFFPrefetch {
namespace {

int g_prefetchModuleAnchor = 0;

bool EnvOn(const wchar_t* name, bool fallback) noexcept {
    wchar_t value[8]{};
    const DWORD written = GetEnvironmentVariableW(name, value, 8);
    if (written == 0 || written >= 8) return fallback;
    if (value[0] == L'0' && value[1] == L'\0') return false;
    if (value[0] == L'1' && value[1] == L'\0') return true;
    return fallback;
}

std::uint32_t EnvUint(const wchar_t* name, std::uint32_t fallback,
    std::uint32_t low, std::uint32_t high) noexcept {
    wchar_t value[16]{};
    const DWORD written = GetEnvironmentVariableW(name, value, 16);
    if (written == 0 || written >= 16) return fallback;
    std::uint32_t parsed = 0;
    bool anyDigit = false;
    for (DWORD i = 0; i < written; ++i) {
        if (value[i] < L'0' || value[i] > L'9') break;
        anyDigit = true;
        parsed = parsed * 10u + static_cast<std::uint32_t>(value[i] - L'0');
        if (parsed > 99999u) { parsed = 99999u; break; }
    }
    if (!anyDigit) return fallback;
    if (parsed < low) return low;
    if (parsed > high) return high;
    return parsed;
}

void Trace(const char* action, std::uint32_t generation, std::uint64_t bytes) noexcept {
    static const bool enabled = EnvOn(L"FFF3FP_PREFETCH_TRACE", false);
    if (!enabled) return;
    char line[192]{};
    std::snprintf(line, sizeof(line), "[prefetch] gen=%u action=%s bytes=%llu",
        generation, action, static_cast<unsigned long long>(bytes));
    FFF3FP_KernelLogImpl(line);
}

std::wstring Utf8ToWide(const std::string& utf8) noexcept {
    if (utf8.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
        static_cast<int>(utf8.size()), wide.data(), needed) <= 0) return std::wstring();
    return wide;
}

bool StatFile(const std::string& pathUtf8, FileFingerprint* fingerprint) noexcept {
    if (fingerprint == nullptr || pathUtf8.empty()) return false;
    const std::wstring wide = Utf8ToWide(pathUtf8);
    if (wide.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &data)) return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    ULARGE_INTEGER size{};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    ULARGE_INTEGER stamp{};
    stamp.HighPart = data.ftLastWriteTime.dwHighDateTime;
    stamp.LowPart = data.ftLastWriteTime.dwLowDateTime;
    fingerprint->sizeBytes = size.QuadPart;
    fingerprint->lastWrite100ns = stamp.QuadPart;
    return true;
}

// Container-level counterpart to IsStillPath. The extension whitelist above
// decides whether a path is *allowed* to be decoded on this lane; this decides
// whether the container actually is a still. An extension can lie, and the
// split is a hard scope rule: video is only ever read for information and for
// the GPU/CPU route decision on the main open path, never decoded here. AV1 and
// HEVC are included because AVIF and HEIC are single-frame containers that
// carry them.
bool IsStillImageCodec(int codecId) noexcept {
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
    case AV_CODEC_ID_AV1:
    case AV_CODEC_ID_HEVC:
        return true;
    default:
        return false;
    }
}

bool IsStillPath(const std::string& path) noexcept {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* const kExtensions[] = {
        "jpg", "jpeg", "png", "bmp", "gif", "webp", "avif", "heic", "heif",
        "jxl", "tif", "tiff"
    };
    for (const char* candidate : kExtensions) {
        if (ext == candidate) return true;
    }
    return false;
}

// A cache key has to identify a FILE, not a spelling of one. The plan arrives
// with whatever the host's playlist holds, while the read side queries with
// mediaPath_ -- which PlayerSession::Open has already pushed through
// GetFullPathNameW, and that rewrites every '/' as '\\'. Keying on the raw
// spelling therefore turns a plan built from forward slashes into a silent
// total no-op: the cache fills, reports hits=0 forever, and nothing says why.
// Only the separator is canonicalised here. Dot segments and relative paths
// are NOT resolved: that is NormalizeLocalPath's job, and the host sends
// absolute playlist paths.
std::string CacheKey(const std::string& pathUtf8) {
    std::string key = pathUtf8;
    for (char& c : key) {
        if (c == '/') c = '\\';
    }
    return key;
}

std::uint64_t FrameBytes(const AVFrame* frame) noexcept {
    if (frame == nullptr) return 0;
    const int exact = av_image_get_buffer_size(
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, 32);
    if (exact > 0) return static_cast<std::uint64_t>(exact);
    const AVPixFmtDescriptor* desc =
        av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    std::uint64_t bytes = 0;
    for (int plane = 0; plane < 4; ++plane) {
        if (frame->data[plane] == nullptr || frame->linesize[plane] <= 0) continue;
        int height = frame->height;
        if (desc != nullptr && plane > 0 && plane <= 2) {
            height = AV_CEIL_RSHIFT(height, desc->log2_chroma_h);
        }
        bytes += static_cast<std::uint64_t>(frame->linesize[plane]) *
            static_cast<std::uint64_t>(height > 0 ? height : 1);
    }
    return bytes + 4096u;
}

// Decodes exactly one CPU frame on the side lane. No PlayerSession, no D3D11
// device, no presenter, no AVFormatContext ownership transfer, no snapshot_.
AVFrame* DecodeFirstStill(const std::string& pathUtf8, std::uint32_t generation) noexcept {
    AVFormatContext* format = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "probesize", "8388608", 0);
    av_dict_set(&options, "max_analyze_duration", "2000000", 0);
    const int opened = avformat_open_input(&format, pathUtf8.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0 || format == nullptr) {
        if (format != nullptr) avformat_close_input(&format);
        return nullptr;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        avformat_close_input(&format);
        return nullptr;
    }
    // Self-check before any decoder is opened: one stream only. A container
    // that also carries audio (or extra video tracks) is a video, whatever its
    // name says, and decoding it here would violate the split.
    if (format->nb_streams != 1u) {
        Trace("still-skip-streams", generation, format->nb_streams);
        avformat_close_input(&format);
        return nullptr;
    }
    const int stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) {
        avformat_close_input(&format);
        return nullptr;
    }
    // Match OpenDecoder's CPU codec choice: AV1 stills are decoded by
    // libdav1d there, and a different decoder can emit different planes.
    const auto codecId = format->streams[stream]->codecpar->codec_id;
    if (!IsStillImageCodec(static_cast<int>(codecId))) {
        Trace("still-skip-codec", generation, static_cast<std::uint64_t>(codecId));
        avformat_close_input(&format);
        return nullptr;
    }
    const AVCodec* codec = codecId == AV_CODEC_ID_AV1
        ? avcodec_find_decoder_by_name("libdav1d") : nullptr;
    if (codec == nullptr) codec = avcodec_find_decoder(codecId);
    if (codec == nullptr) {
        avformat_close_input(&format);
        return nullptr;
    }
    AVCodecContext* decoder = avcodec_alloc_context3(codec);
    if (decoder == nullptr ||
        avcodec_parameters_to_context(decoder, format->streams[stream]->codecpar) < 0) {
        if (decoder != nullptr) avcodec_free_context(&decoder);
        avformat_close_input(&format);
        return nullptr;
    }
    // OpenDecoder sets this; without it best_effort_timestamp is computed in a
    // different time base and the hit frame would carry a wrong position.
    decoder->pkt_timebase = format->streams[stream]->time_base;
    if (avcodec_open2(decoder, codec, nullptr) < 0) {
        if (decoder != nullptr) avcodec_free_context(&decoder);
        avformat_close_input(&format);
        return nullptr;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    AVFrame* result = nullptr;
    if (packet != nullptr && decoded != nullptr) {
        for (int i = 0; i < 64 && result == nullptr; ++i) {
            if (av_read_frame(format, packet) < 0) break;
            if (packet->stream_index == stream &&
                avcodec_send_packet(decoder, packet) >= 0 &&
                avcodec_receive_frame(decoder, decoded) == 0) {
                if (decoded->hw_frames_ctx != nullptr) {
                    AVFrame* cpu = av_frame_alloc();
                    if (cpu != nullptr && av_hwframe_transfer_data(cpu, decoded, 0) >= 0) {
                        av_frame_copy_props(cpu, decoded);
                        result = cpu;
                    } else if (cpu != nullptr) {
                        av_frame_free(&cpu);
                    }
                } else {
                    result = av_frame_clone(decoded);   // keeps all side data
                }
            }
            av_packet_unref(packet);
        }
        if (result == nullptr) {
            avcodec_send_packet(decoder, nullptr);
            if (avcodec_receive_frame(decoder, decoded) == 0 &&
                decoded->hw_frames_ctx == nullptr) {
                result = av_frame_clone(decoded);
            }
        }
    }
    if (decoded != nullptr) av_frame_free(&decoded);
    if (packet != nullptr) av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    return result;
}

void FillTaskPath(FFFPrefetchTask& task, const std::string& path) noexcept {
    const std::uint32_t count = static_cast<std::uint32_t>((std::min)(
        path.size(), static_cast<std::size_t>(FFF_PREFETCH_PATH_CAPACITY - 1)));
    std::memcpy(task.pathUtf8, path.data(), count);
    task.pathUtf8[count] = '\0';
    task.pathBytes = count;
}

std::wstring ModuleDirectory() noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&g_prefetchModuleAnchor), &module) || module == nullptr) {
        return std::wstring();
    }
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return std::wstring();
    std::wstring full(buffer.data(), length);
    const std::size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return full.substr(0, slash + 1);
}

} // namespace

PrefetchService& PrefetchService::Instance() noexcept {
    // Intentionally leaked: a static object with live worker threads must not
    // be destroyed from DllMain during process teardown. The process owns both
    // the DLL and the service for its whole lifetime.
    static PrefetchService* const instance = new PrefetchService();
    return *instance;
}

PrefetchService::PrefetchService() noexcept {
    disabled_ = !EnvOn(L"FFF3FP_PREFETCH", true);
    hostEnabled_ = EnvOn(L"FFF3FP_PREFETCH_HOST", true);
    mode_ = EnvUint(L"FFF3FP_PREFETCH_MODE", 2u, 0u, 3u);
    const std::uint32_t megabytes = EnvUint(L"FFF3FP_PREFETCH_MAX_MB", 256u, 16u, 4096u);
    budgetBytes_ = static_cast<std::uint64_t>(megabytes) * 1024ull * 1024ull;
    try {
        worker_ = std::thread([this] { WorkerLoop(); });
    } catch (...) {
        disabled_ = true;   // worker unavailable -> prefetch silently off
    }
    try {
        guardThread_ = std::thread([this] { GuardLoop(); });
    } catch (...) {
        // No wait line: pressure is still sampled when a plan arrives and after
        // each task, so the cache stays correct, merely less reactive while idle.
    }
}

void PrefetchService::GuardLoop() noexcept {
    // One wait line, never a timer. Low is a persistent level meaning "almost
    // out of physical memory": release everything, then wait for High before
    // re-arming, so the dead band between the two cannot cause a spin.
    MemoryGuard::Instance().Evaluate();
    void* low = MemoryGuard::Instance().LowHandle();
    void* high = MemoryGuard::Instance().HighHandle();
    if (low == nullptr || high == nullptr) return;  // API absent -> plan-time checks only
    for (;;) {
        if (WaitForSingleObject(static_cast<HANDLE>(low), INFINITE) != WAIT_OBJECT_0) return;
        EvaluatePressure();
        if (WaitForSingleObject(static_cast<HANDLE>(high), INFINITE) != WAIT_OBJECT_0) return;
        EvaluatePressure();
    }
}

bool PrefetchService::Cancelled(std::uint32_t generation) noexcept {
    return cancel_.load(std::memory_order_acquire) ||
        generation_.load(std::memory_order_acquire) != generation;
}

bool PrefetchService::AllowsWork() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return !disabled_ && !stop_ && !paused_ &&
        MemoryGuard::Instance().AllowsPrefetch();
}

bool PrefetchService::HelperAlive() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return helperRunning_ && stdinHandle_ != nullptr && stdoutHandle_ != nullptr;
}

bool PrefetchService::ShouldStopHelper() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return helperStopRequested_;
}

void PrefetchService::ReleaseAllLocked() noexcept {
    for (auto& item : stills_) {
        AVFrame* frame = item.second.frame;
        if (frame != nullptr) {
            av_frame_free(&frame);
            ++releasedFrames_;
        }
    }
    stills_.clear();
    probes_.clear();
    routes_.clear();
    cachedBytes_ = 0;
}

void PrefetchService::TrimToHalfLocked() noexcept {
    const std::uint64_t target = budgetBytes_ / 2u;
    while (cachedBytes_ > target && !stills_.empty()) {
        auto victim = stills_.begin();
        for (auto it = stills_.begin(); it != stills_.end(); ++it) {
            if (it->second.order > victim->second.order) victim = it;
        }
        AVFrame* frame = victim->second.frame;
        if (frame != nullptr) {
            av_frame_free(&frame);
            ++releasedFrames_;
        }
        cachedBytes_ = (cachedBytes_ > victim->second.bytes)
            ? cachedBytes_ - victim->second.bytes : 0;
        stills_.erase(victim);
    }
    probes_.clear();
    // Route conclusions are tiny PODs but still derived data: dropping
    // them costs nothing beyond one more trial decode after pressure eases.
    routes_.clear();
}

void PrefetchService::EnforceBudgetLocked(std::uint64_t incoming) noexcept {
    while (cachedBytes_ + incoming > budgetBytes_ && !stills_.empty()) {
        auto victim = stills_.begin();
        for (auto it = stills_.begin(); it != stills_.end(); ++it) {
            if (it->second.lastUse < victim->second.lastUse) victim = it;
        }
        AVFrame* frame = victim->second.frame;
        if (frame != nullptr) {
            av_frame_free(&frame);
            ++releasedFrames_;
        }
        cachedBytes_ = (cachedBytes_ > victim->second.bytes)
            ? cachedBytes_ - victim->second.bytes : 0;
        stills_.erase(victim);
    }
}

void PrefetchService::PruneToPlanLocked(const std::vector<std::string>& paths) noexcept {
    std::unordered_map<std::string, bool> wanted;
    wanted.reserve(paths.size() + 1);
    for (const auto& path : paths) wanted[path] = true;
    for (auto it = stills_.begin(); it != stills_.end();) {
        if (wanted.find(it->first) == wanted.end()) {
            AVFrame* frame = it->second.frame;
            if (frame != nullptr) {
                av_frame_free(&frame);
                ++releasedFrames_;
            }
            cachedBytes_ = (cachedBytes_ > it->second.bytes)
                ? cachedBytes_ - it->second.bytes : 0;
            it = stills_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = probes_.begin(); it != probes_.end();) {
        it = (wanted.find(it->first) == wanted.end()) ? probes_.erase(it) : (++it);
    }
    // routes_ is intentionally NOT pruned: a refusal describes the file,
    // not the current neighbour list, and must survive plan generations.
}

void PrefetchService::EvaluatePressure() noexcept {
    const PressureTier tier = MemoryGuard::Instance().Evaluate();
    std::lock_guard<std::mutex> lock(mutex_);
    if (tier == PressureTier::Critical) {
        ReleaseAllLocked();
        cancel_.store(true, std::memory_order_release);
        paused_ = true;
        helperStopRequested_ = true;
        Trace("release-all", generation_.load(std::memory_order_acquire), 0);
    } else if (tier == PressureTier::Tight) {
        TrimToHalfLocked();
        cancel_.store(true, std::memory_order_release);
        paused_ = true;
        Trace("trim-half", generation_.load(std::memory_order_acquire), cachedBytes_);
    } else {
        // Clearing the pause only ALLOWS work again. Nothing is rebuilt here:
        // reconstruction is content-driven, i.e. the next real plan/switch.
        paused_ = false;
    }
}

void PrefetchService::MarkHelperFailure() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++helperFailures_;
    helperDisabled_ = true;      // no auto-respawn for the rest of the session
    helperRunning_ = false;
}

bool PrefetchService::WriteTask(const FFFPrefetchTask& task) noexcept {
    HANDLE pipe = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipe = static_cast<HANDLE>(stdinHandle_);
    }
    if (pipe == nullptr) return false;
    const std::uint32_t length = static_cast<std::uint32_t>(sizeof(task));
    DWORD written = 0;
    if (!WriteFile(pipe, &length, sizeof(length), &written, nullptr) ||
        written != sizeof(length)) return false;
    written = 0;
    return WriteFile(pipe, &task, length, &written, nullptr) && written == length;
}

bool PrefetchService::ReadResult(FFFPrefetchResult* out) noexcept {
    if (out == nullptr) return false;
    HANDLE pipe = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pipe = static_cast<HANDLE>(stdoutHandle_);
    }
    if (pipe == nullptr) return false;
    const ULONGLONG deadline = GetTickCount64() + 3000ull;
    const auto readExact = [pipe, deadline](void* dst, DWORD need) noexcept -> bool {
        char* cursor = static_cast<char*>(dst);
        DWORD done = 0;
        while (done < need) {
            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
            if (available == 0) {
                if (GetTickCount64() >= deadline) return false;
                Sleep(2);
                continue;
            }
            const DWORD chunk = (std::min)(available, need - done);
            DWORD got = 0;
            if (!ReadFile(pipe, cursor + done, chunk, &got, nullptr) || got == 0) return false;
            done += got;
        }
        return true;
    };
    std::uint32_t length = 0;
    if (!readExact(&length, sizeof(length))) return false;
    if (length != sizeof(FFFPrefetchResult) || length > FFF_PREFETCH_FRAME_MAX_BYTES) return false;
    FFFPrefetchResult result{};
    if (!readExact(&result, length)) return false;
    if (result.size != sizeof(FFFPrefetchResult) ||
        result.version != FFF_PREFETCH_PROTOCOL_VERSION) return false;
    *out = result;
    return true;
}

void PrefetchService::NoteResult(const FFFPrefetchResult& result) noexcept {
    // WARM results carry no container description. Writing one into probes_
    // would leave a zero-initialised POD (videoStreamIndex = 0 instead of
    // -1) whenever the following PROBE read fails, and would inflate
    // cachedProbeEntries. Only a real PROBE may populate the table.
    if (result.kind != FFF_PREFETCH_TASK_PROBE) {
        if (result.kind == FFF_PREFETCH_TASK_WARM && result.status == 0)
            Trace("warm", result.generation,
                static_cast<std::uint64_t>(result.warmedBytes));
        return;
    }
    if (result.status != 0 || result.pathBytes == 0 ||
        result.pathBytes >= FFF_PREFETCH_PATH_CAPACITY) return;
    const std::string path(result.pathUtf8, result.pathBytes);
    FileFingerprint fingerprint{};
    if (!StatFile(path, &fingerprint)) return;
    ProbePod pod{};
    pod.flags = result.flags;
    pod.streamCount = result.streamCount;
    pod.videoStreamIndex = result.videoStreamIndex;
    pod.videoCodecId = result.videoCodecId;
    pod.width = result.width;
    pod.height = result.height;
    pod.fileSizeBytes = result.fileSizeBytes;
    pod.generation = result.generation;
    pod.fingerprint = fingerprint;
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation_.load(std::memory_order_acquire) != result.generation) return;
    probes_[path] = pod;
    Trace("probe", result.generation, static_cast<std::uint64_t>(result.warmedBytes));
}

void PrefetchService::EnsureHelper() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (helperRunning_ || helperDisabled_ || !hostEnabled_ || disabled_) return;
    }
    const std::wstring directory = ModuleDirectory();
    if (directory.empty()) { MarkHelperFailure(); return; }
    const std::wstring exe = directory + L"FFF.PrefetchHost.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MarkHelperFailure();       // helper missing -> in-process lane only
        return;
    }
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    security.lpSecurityDescriptor = nullptr;
    HANDLE inputRead = nullptr, inputWrite = nullptr;
    HANDLE outputRead = nullptr, outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &security, 0)) { MarkHelperFailure(); return; }
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        CloseHandle(inputRead);
        CloseHandle(inputWrite);
        MarkHelperFailure();
        return;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = nullptr;
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + exe + L"\"";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    const BOOL created = CreateProcessW(exe.c_str(), commandBuffer.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (!created) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        MarkHelperFailure();
        return;
    }
    CloseHandle(process.hThread);
    std::lock_guard<std::mutex> lock(mutex_);
    processHandle_ = process.hProcess;
    stdinHandle_ = inputWrite;
    stdoutHandle_ = outputRead;
    helperRunning_ = true;
    helperStopRequested_ = false;
}

void PrefetchService::StopHelper() noexcept {
    void* process = nullptr;
    void* input = nullptr;
    void* output = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        helperRunning_ = false;
        helperStopRequested_ = false;
        process = processHandle_;
        input = stdinHandle_;
        output = stdoutHandle_;
        processHandle_ = stdinHandle_ = stdoutHandle_ = nullptr;
    }
    if (input != nullptr) {
        FFFPrefetchTask shutdown{};
        shutdown.size = static_cast<std::uint32_t>(sizeof(shutdown));
        shutdown.version = FFF_PREFETCH_PROTOCOL_VERSION;
        shutdown.kind = FFF_PREFETCH_TASK_SHUTDOWN;
        const std::uint32_t length = shutdown.size;
        DWORD written = 0;
        WriteFile(static_cast<HANDLE>(input), &length, sizeof(length), &written, nullptr);
        WriteFile(static_cast<HANDLE>(input), &shutdown, length, &written, nullptr);
        CloseHandle(static_cast<HANDLE>(input));
    }
    if (process != nullptr) {
        if (WaitForSingleObject(static_cast<HANDLE>(process), 1000) == WAIT_TIMEOUT) {
            TerminateProcess(static_cast<HANDLE>(process), 0);
        }
        CloseHandle(static_cast<HANDLE>(process));
    }
    if (output != nullptr) CloseHandle(static_cast<HANDLE>(output));
}

void PrefetchService::WorkerLoop() noexcept {
    for (;;) {
        std::vector<std::string> paths;
        std::uint32_t flags = 0;
        std::uint32_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || planDirty_; });
            if (stop_) return;
            planDirty_ = false;
            paths = plan_;
            flags = planFlags_;
            generation = generation_.load(std::memory_order_acquire);
            cancel_.store(false, std::memory_order_release);
        }
        if (ShouldStopHelper()) StopHelper();
        if (disabled_ || paths.empty()) {
            StopHelper();
            continue;
        }
        EvaluatePressure();
        if (!AllowsWork()) continue;

        // Helper lane: one task -> exactly one result, synchronous, single line.
        if (mode_ >= 2 && hostEnabled_) {
            EnsureHelper();
            if (HelperAlive()) {
                for (std::size_t i = 0; i < paths.size(); ++i) {
                    if (Cancelled(generation) || !AllowsWork()) break;
                    FFFPrefetchTask task{};
                    task.size = static_cast<std::uint32_t>(sizeof(task));
                    task.version = FFF_PREFETCH_PROTOCOL_VERSION;
                    task.generation = generation;
                    task.order = static_cast<std::int32_t>(i);
                    FillTaskPath(task, paths[i]);
                    task.kind = FFF_PREFETCH_TASK_WARM;
                    if (!WriteTask(task)) { MarkHelperFailure(); StopHelper(); break; }
                    FFFPrefetchResult warmResult{};
                    if (!ReadResult(&warmResult)) { MarkHelperFailure(); StopHelper(); break; }
                    NoteResult(warmResult);
                    task.kind = FFF_PREFETCH_TASK_PROBE;
                    if (!WriteTask(task)) { MarkHelperFailure(); StopHelper(); break; }
                    FFFPrefetchResult probeResult{};
                    if (!ReadResult(&probeResult)) { MarkHelperFailure(); StopHelper(); break; }
                    NoteResult(probeResult);
                }
            }
        }

        // In-process still lane: only in the still-browsing state, CPU only.
        if (mode_ >= 1 && (flags & FFF3FP_PREFETCH_FLAG_STILL_BROWSING) != 0) {
            for (std::size_t i = 1; i < paths.size(); ++i) {
                if (Cancelled(generation) || !AllowsWork()) break;
                const std::string& path = paths[i];
                if (!IsStillPath(path)) continue;
                FileFingerprint fingerprint{};
                if (!StatFile(path, &fingerprint)) continue;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (fingerprint.sizeBytes > budgetBytes_ / 2u ||
                        stills_.find(path) != stills_.end()) continue;
                }
                AVFrame* frame = DecodeFirstStill(path, generation);
                if (frame == nullptr) continue;
                const std::uint64_t bytes = FrameBytes(frame);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (Cancelled(generation)) {
                        av_frame_free(&frame);
                        break;
                    }
                    EnforceBudgetLocked(bytes);
                    StillEntry entry{};
                    entry.frame = frame;
                    entry.bytes = bytes;
                    entry.lastUse = ++useTick_;
                    entry.order = static_cast<std::uint32_t>(i);
                    entry.generation = generation;
                    entry.fingerprint = fingerprint;
                    stills_[path] = entry;
                    cachedBytes_ += bytes;
                }
                Trace("still", generation, bytes);
                EvaluatePressure();
            }
        }
    }
}

FFFResult PrefetchService::SetPlan(const FFF3FPPrefetchPlan* plan) noexcept {
    if (plan == nullptr) return FFFResult::InvalidArgument;
    if (plan->size < sizeof(FFF3FPPrefetchPlan) ||
        plan->version != FFF3FP_PREFETCH_PLAN_VERSION) return FFFResult::InvalidArgument;

    std::vector<std::string> paths;
    if (plan->pathsUtf8 != nullptr && plan->pathsUtf8[0] != '\0') {
        // Bounded scan: this is an ABI boundary, so never let strchr/strlen run
        // past kScanLimit on a malformed (non-terminated) caller buffer.
        constexpr std::size_t kScanLimit = 65536u;
        const char* cursor = plan->pathsUtf8;
        std::size_t consumed = 0;
        while (consumed < kScanLimit && paths.size() < 16u) {
            std::size_t length = 0;
            while (length < kScanLimit - consumed && cursor[length] != '\0' &&
                cursor[length] != '\n') ++length;
            if (length > 0 && length < FFF_PREFETCH_PATH_CAPACITY) {
                paths.push_back(CacheKey(std::string(cursor, length)));
            }
            consumed += length;
            if (cursor[length] == '\0') break;
            ++consumed;                       // the separator itself
            cursor += length + 1;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation_.store(plan->generation, std::memory_order_release);
        cancel_.store(true, std::memory_order_release);
        probes_.clear();            // stale POD must not survive a generation
        plan_ = paths;
        planFlags_ = plan->flags;
        planDirty_ = true;
        PruneToPlanLocked(paths);
    }
    EvaluatePressure();
    cv_.notify_all();
    Trace("plan", plan->generation, static_cast<std::uint64_t>(paths.size()));
    return FFFResult::Success;
}

FFFResult PrefetchService::GetStatus(FFF3FPPrefetchStatus* status) noexcept {
    if (status == nullptr) return FFFResult::InvalidArgument;
    if (status->size < sizeof(FFF3FPPrefetchStatus) ||
        status->version != FFF3FP_PREFETCH_STATUS_VERSION) return FFFResult::InvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    status->enabled = (!disabled_ && !paused_) ? 1u : 0u;
    status->tier = static_cast<std::uint32_t>(MemoryGuard::Instance().Tier());
    status->hostRunning = helperRunning_ ? 1u : 0u;
    status->workerRunning = (worker_.joinable() && !stop_) ? 1u : 0u;
    status->cachedStillFrames = static_cast<std::uint32_t>(stills_.size());
    status->cachedProbeEntries = static_cast<std::uint32_t>(probes_.size());
    status->cachedBytes = cachedBytes_;
    status->budgetBytes = budgetBytes_;
    status->hits = hits_;
    status->misses = misses_;
    status->releasedFrames = releasedFrames_;
    status->helperFailures = helperFailures_;
    return FFFResult::Success;
}

bool PrefetchService::TakeStill(const std::string& pathUtf8, AVFrame** out) noexcept {
    if (out == nullptr) return false;
    *out = nullptr;
    FileFingerprint fingerprint{};
    if (!StatFile(pathUtf8, &fingerprint)) return false;
    const std::string key = CacheKey(pathUtf8);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stills_.find(key);
    if (it == stills_.end() || it->second.frame == nullptr ||
        !(it->second.fingerprint == fingerprint)) {
        ++misses_;
        return false;
    }
    AVFrame* clone = av_frame_clone(it->second.frame);
    if (clone == nullptr) {
        ++misses_;
        return false;
    }
    it->second.lastUse = ++useTick_;
    ++hits_;
    *out = clone;
    return true;
}

bool PrefetchService::TakeProbe(const std::string& pathUtf8, ProbePod* out) noexcept {
    if (out == nullptr) return false;
    FileFingerprint fingerprint{};
    if (!StatFile(pathUtf8, &fingerprint)) return false;
    const std::string key = CacheKey(pathUtf8);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = probes_.find(key);
    if (it == probes_.end() || !(it->second.fingerprint == fingerprint)) {
        ++misses_;
        return false;
    }
    ++hits_;
    *out = it->second;
    return true;
}

void PrefetchService::NoteUnsupportedRoute(const std::string& pathUtf8,
    const std::uint64_t routeKey) noexcept {
    if (routeKey == 0 || pathUtf8.empty()) return;
    FileFingerprint fingerprint{};
    if (!StatFile(pathUtf8, &fingerprint)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (disabled_ || paused_) return;
    RoutePod pod{};
    pod.routeKey = routeKey;
    pod.generation = generation_.load(std::memory_order_acquire);
    pod.fingerprint = fingerprint;
    routes_[CacheKey(pathUtf8)] = pod;
    Trace("route-note", pod.generation, routeKey);
}

bool PrefetchService::TakeUnsupportedRoute(const std::string& pathUtf8,
    const std::uint64_t routeKey) noexcept {
    if (routeKey == 0 || pathUtf8.empty()) return false;
    FileFingerprint fingerprint{};
    if (!StatFile(pathUtf8, &fingerprint)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (disabled_) return false;
    const auto it = routes_.find(CacheKey(pathUtf8));
    if (it == routes_.end() || it->second.routeKey != routeKey ||
        !(it->second.fingerprint == fingerprint)) return false;
    Trace("route-hit", it->second.generation, routeKey);
    return true;
}

bool PrefetchService::HasStillEntry(const std::string& pathUtf8) noexcept {
    if (pathUtf8.empty()) return false;
    FileFingerprint fingerprint{};
    if (!StatFile(pathUtf8, &fingerprint)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (disabled_) return false;
    const auto it = stills_.find(CacheKey(pathUtf8));
    return it != stills_.end() && it->second.frame != nullptr &&
        it->second.fingerprint == fingerprint;
}

} // namespace FFFPrefetch
