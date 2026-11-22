#include "pch.h"
#include "3FP/Core/AvifIlocShim.h"
#include "3FP/Core/AvifItemProbe.h"
#include "3FP/Core/PlayerSession.h"
#include "3FP/Audio/AsrAudioTap.h"
#include "3FP/Prefetch/PrefetchService.h"
#include "Shared/Ffmpeg/IocpMediaReader.h"
#include "Shared/Ffmpeg/SharedFileInput.h"
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

namespace {
int checks = 0;
void Check(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
    ++checks;
    std::printf("PASS %s\n", name);
}

class EnvironmentVariableGuard final {
public:
    EnvironmentVariableGuard(const wchar_t* name, const wchar_t* value) : name_(name) {
        SetLastError(ERROR_SUCCESS);
        const auto required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required > 0) {
            std::vector<wchar_t> previous(static_cast<std::size_t>(required));
            const auto length = GetEnvironmentVariableW(name, previous.data(), required);
            if (length > 0 && length < required) {
                hadValue_ = true;
                previous_.assign(previous.data(), length);
            }
        } else {
            hadValue_ = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        }
        if (!SetEnvironmentVariableW(name, value))
            throw std::runtime_error("test environment variable could not be set");
    }

    void SetValue(const wchar_t* value) {
        if (!SetEnvironmentVariableW(name_.c_str(), value))
            throw std::runtime_error("test environment variable could not be changed");
    }

    ~EnvironmentVariableGuard() {
        SetEnvironmentVariableW(name_.c_str(), hadValue_ ? previous_.c_str() : nullptr);
    }

    EnvironmentVariableGuard(const EnvironmentVariableGuard&) = delete;
    EnvironmentVariableGuard& operator=(const EnvironmentVariableGuard&) = delete;

private:
    std::wstring name_;
    std::wstring previous_;
    bool hadValue_ = false;
};

struct ScopedDeleteFile final {
    explicit ScopedDeleteFile(std::string value) : path(std::move(value)) {}
    ~ScopedDeleteFile() { if (!path.empty()) std::remove(path.c_str()); }
    std::string path;
};

struct IocpTestHookReset final {
    ~IocpTestHookReset() {
        IocpMediaReader::TestSetShortRead(0, 0);
        IocpMediaReader::TestSetCompletionDelayMs(0);
        IocpMediaReader::TestSetPendingReadDelayMs(0);
    }
};

int AlwaysInterrupt(void*) noexcept { return 1; }

struct OpenEventCapture final {
    std::atomic<int> completed{0};
};

void __cdecl CaptureOpenCompleted(void* context, FFF3FPEvent type, const char*) {
    if (context != nullptr && type == FFF3FPEvent::OpenCompleted)
        static_cast<OpenEventCapture*>(context)->completed.fetch_add(1,
            std::memory_order_relaxed);
}

using Bytes = std::vector<std::uint8_t>;
void UInt(Bytes& bytes, std::uint64_t value, int width) {
    for (int i = width - 1; i >= 0; --i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void Join(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
Bytes Box(const char* type, Bytes body, bool extended = false) {
    Bytes result;
    UInt(result, extended ? 1 : body.size() + 8, 4);
    result.insert(result.end(), type, type + 4);
    if (extended) UInt(result, body.size() + 16, 8);
    Join(result, body);
    return result;
}
Bytes Avif(int version, bool extended, bool zeroWidth = false, bool tooMany = false) {
    Bytes ftyp{'a','v','i','f',0,0,0,0};
    Bytes result = Box("ftyp", ftyp, extended);
    Bytes iloc{static_cast<std::uint8_t>(version),0,0,0,static_cast<std::uint8_t>(zeroWidth ? 0 : 0x44),0};
    UInt(iloc, tooMany ? 2 : 1, version < 2 ? 2 : 4);
    for (int item = 0; item < (tooMany ? 2 : 1); ++item) {
        UInt(iloc, item + 1, version < 2 ? 2 : 4);
        if (version > 0) UInt(iloc, 0, 2);
        UInt(iloc, 0, 2);
        UInt(iloc, tooMany ? 40000 : 2, 2);
        if (!zeroWidth) {
            for (int extent = 0; extent < (tooMany ? 40000 : 2); ++extent) {
                UInt(iloc, tooMany ? 0 : extent * 4, 4);
                UInt(iloc, 4, 4);
            }
        }
    }
    Bytes meta(4, 0);
    Join(meta, Box("iloc", iloc, extended));
    Join(result, Box("meta", meta, extended));
    Join(result, Box("mdat", Bytes(8, 0)));
    if (!zeroWidth && !tooMany) {
        // Point the two extents at actual media bytes, not container metadata.
        const auto offset = result.size() - 8;
        const auto field = (extended ? 24 : 16) + (extended ? 16 : 8) + 4 +
            (extended ? 16 : 8) + 6 + (version < 2 ? 2 : 4) +
            (version < 2 ? 2 : 4) + (version ? 2 : 0) + 4;
        for (int e = 0; e < 2; ++e) for (int b = 0; b < 4; ++b)
            result[field + e * 8 + b] = static_cast<std::uint8_t>((offset + e * 4) >> ((3 - b) * 8));
    }
    return result;
}
void AvifTests() {
    for (int version = 0; version <= 2; ++version) for (bool extended : {false, true}) {
        auto bytes = Avif(version, extended);
        const auto before = bytes;
        std::string detail;
        // Behavioral contract: a layered AVIF stores the primary item as several extents, and
        // FFmpeg's mov demuxer refuses iloc extent_count>1. The repair must collapse that to
        // exactly one extent (mergedExtents==1) WITHOUT growing or shrinking the file (in-place
        // rewrite) and WITHOUT leaving the file identical (bytes must actually change).
        std::int32_t merged = -1;
        Check(AvifIlocShim::RepairInPlace(bytes, detail, &merged), "AVIF ordinary/extended v0-v2 repair");
        Check(bytes.size() == before.size() && bytes != before, "AVIF repair rewrites the container in place (same size, changed bytes)");
        Check(merged == 1, "AVIF layered primary item collapsed to a single extent");
        auto repaired = bytes;
        Check(!AvifIlocShim::RepairInPlace(bytes, detail) && bytes == repaired, "AVIF already single extent unchanged");
    }
    std::vector<Bytes> malformed{Avif(0, false, true), Avif(2, true, false, true)};
    Bytes backwards;
    UInt(backwards, 1, 4);
    backwards.insert(backwards.end(), {'f','r','e','e'});
    UInt(backwards, UINT64_MAX, 8);
    Join(backwards, Avif(0, false));
    malformed.push_back(backwards);
    for (std::uint64_t size : {0ull, 8ull, 15ull, 0x8000000000000000ull, UINT64_MAX}) {
        auto box = backwards;
        for (int b = 0; b < 8; ++b) box[8 + b] = static_cast<std::uint8_t>(size >> ((7-b)*8));
        malformed.push_back(box);
    }
    auto valid = Avif(0, true);
    for (std::size_t length = 0; length < valid.size(); ++length)
        malformed.emplace_back(valid.begin(), valid.begin() + length);
    for (auto bytes : malformed) {
        const auto original = bytes;
        std::string detail;
        Check(!AvifIlocShim::RepairInPlace(bytes, detail) && bytes == original, "malformed AVIF rejected unchanged");
    }
}
}

void AvifDeadlineTests(const std::string& root) {
    const auto path = root + "/small.avif";
    const auto expired = GetTickCount64() - 1;
    AVIOInterruptCB interrupt{&AlwaysInterrupt, nullptr};
    std::string detail;

    AvifItemInfo info{};
    info.valid = true;
    Check(!AvifItemProbe::Read(path.c_str(), 16 * 1024 * 1024, info, detail,
        expired, nullptr), "AVIF probe rejects expired deadline");
    Check(!info.valid && info.primaryItemId == 0,
        "AVIF probe does not publish expired result");

    info.valid = true;
    Check(!AvifItemProbe::Read(path.c_str(), 16 * 1024 * 1024, info, detail,
        0, &interrupt), "AVIF probe rejects immediate cancellation");
    Check(!info.valid && info.primaryItemId == 0,
        "AVIF probe does not publish cancelled result");

    std::vector<std::uint8_t> bytes{1, 2, 3};
    std::int32_t mergedExtents = 99;
    Check(!AvifIlocShim::ReadAndRepair(path.c_str(), 256 * 1024 * 1024,
        bytes, detail, &mergedExtents, expired, nullptr),
        "AVIF repair rejects expired deadline");
    Check(bytes.empty() && mergedExtents == 0,
        "AVIF repair does not publish expired result");

    bytes = {1, 2, 3};
    mergedExtents = 99;
    Check(!AvifIlocShim::ReadAndRepair(path.c_str(), 256 * 1024 * 1024,
        bytes, detail, &mergedExtents, 0, &interrupt),
        "AVIF repair rejects immediate cancellation");
    Check(bytes.empty() && mergedExtents == 0,
        "AVIF repair does not publish cancelled result");
}

struct PlayerSecurityRegression {
    static void StopWorker(PlayerSession& s) {
        { std::lock_guard lock(s.mutex_); s.terminate_ = true; }
        s.commandCondition_.notify_all();
        s.worker_.join();
        s.terminate_ = false;
    }
    static void Commands(PlayerSession& s) {
        std::deque<PlayerSession::Command> pending;
        { std::lock_guard lock(s.mutex_); pending.swap(s.commands_); }
        for (auto& command : pending) command();
    }
    static void Paths(const std::string& local) {
        for (const auto* prefix : {"\\/", "/\\", "\\\\", "//", "\\\\?\\UNC\\", "\\\\.\\"}) {
            std::string normalized, error;
            Check(!PlayerSession::NormalizeLocalPath((std::string(prefix) + "invalid.example/share/file.mp4").c_str(), normalized, error), "native UNC aliases rejected");
        }
        std::string normalized, error;
        Check(PlayerSession::NormalizeLocalPath(local.c_str(), normalized, error), "native local path accepted");
        Check(PlayerSession::NormalizeNetworkPath(local.c_str(), normalized, error),
            "native network entry accepts local path");
        Check(!PlayerSession::NormalizeNetworkPath("\\\\.\\pipe\\invalid", normalized, error),
            "native network entry rejects device paths");
        Check(!PlayerSession::NormalizeNetworkPath("\\\\?\\GLOBALROOT\\invalid", normalized, error),
            "native network entry rejects extended paths");
        Check(!PlayerSession::NormalizeNetworkPath("https://invalid.example/file.mp4", normalized, error),
            "native network entry rejects URL schemes");
    }
    static void OpeningCancellation(const std::string& path) {
        OpenEventCapture closeEvents;
        FFF3FPConfiguration closeConfig{};
        closeConfig.size = sizeof(closeConfig);
        closeConfig.preferredAdapterIndex = -1;
        closeConfig.eventCallback = &CaptureOpenCompleted;
        closeConfig.eventCallbackContext = &closeEvents;
        PlayerSession closeSession(closeConfig);
        StopWorker(closeSession);
        Check(closeSession.Open(path.c_str()) == FFFResult::Success,
            "opening cancellation accepted queued open");
        Check(closeSession.state_.load() == FFF3FPState::Opening,
            "open enters Opening before worker command");
        Check(closeSession.Close() == FFFResult::Success,
            "close cancels queued open");
        Check(PlayerSession::ProbeInterrupt(&closeSession) == 1,
            "close immediately interrupts in-progress input");
        Commands(closeSession);
        Check(closeSession.state_.load() == FFF3FPState::Closed &&
            closeSession.format_ == nullptr,
            "close during opening leaves no format and reaches Closed");
        Check(closeEvents.completed.load(std::memory_order_relaxed) == 0,
            "cancelled open does not publish success");

        OpenEventCapture stopEvents;
        FFF3FPConfiguration stopConfig{};
        stopConfig.size = sizeof(stopConfig);
        stopConfig.preferredAdapterIndex = -1;
        stopConfig.eventCallback = &CaptureOpenCompleted;
        stopConfig.eventCallbackContext = &stopEvents;
        PlayerSession stopSession(stopConfig);
        StopWorker(stopSession);
        Check(stopSession.Open(path.c_str()) == FFFResult::Success,
            "opening stop fixture accepted queued open");
        Check(stopSession.Stop() == FFFResult::Success,
            "stop during opening is accepted");
        Check(PlayerSession::ProbeInterrupt(&stopSession) == 1,
            "stop immediately interrupts in-progress input");
        Commands(stopSession);
        Check(stopSession.state_.load() == FFF3FPState::Idle &&
            stopSession.format_ == nullptr,
            "stop during opening leaves idle session without format");
        Check(stopEvents.completed.load(std::memory_order_relaxed) == 0,
            "stopped open does not publish success");
    }

    static void Tap() {
        FFF3FPConfiguration config{};
        config.size = sizeof(config);
        PlayerSession s(config);
        StopWorker(s);
        const auto* codec = avcodec_find_decoder(AV_CODEC_ID_PCM_F32LE);
        s.audioDecoder_ = avcodec_alloc_context3(codec);
        s.audioDecoder_->sample_rate = 48000;
        av_channel_layout_default(&s.audioDecoder_->ch_layout, 2);
        Check(avcodec_open2(s.audioDecoder_, codec, nullptr) == 0, "shadow PCM decoder opened");
        std::uint8_t pcm[16 * sizeof(float) + AV_INPUT_BUFFER_PADDING_SIZE]{};
        for (int i = 0; i < 100; ++i) {
            Check(s.EnableAudioTap(nullptr) == FFFResult::Success, "tap enable");
            Commands(s);
            std::weak_ptr<AsrAudioTap> old = s.asrTapWorker_;
            Check(s.audioRenderer_ == nullptr, "shadow lane without PCM renderer");
            Check(s.EnableAudioTap(nullptr) == FFFResult::Success, "tap replacement queued");
            Check(!old.expired(), "worker owns old tap until command");
            s.FeedAsrShadow(pcm, 16 * sizeof(float), 0);
            Check(s.asrTapWorker_->BlocksProduced() == 1, "retired tap still produces safely");
            Commands(s);
            Check(old.expired(), "retired tap released after command");
            old = s.asrTapWorker_;
            s.DisableAudioTap();
            s.EnableAudioTap(nullptr);
            Check(!old.expired(), "disable retains tap until queued uninstall");
            Commands(s);
            Check(old.expired() && s.asrTapWorker_ != nullptr, "disable-enable command ordering");
            s.DisableAudioTap(); Commands(s);
            Check(s.asrTapWorker_ == nullptr, "tap disabled");
        }
        s.DoClose();
    }
    static void Tempo() {
        const auto low = std::numeric_limits<std::int64_t>::min() + 1;
        const auto high = std::numeric_limits<std::int64_t>::max();
        for (const auto [position, origin] : std::vector<std::pair<std::int64_t, std::int64_t>>{
            {0, 0}, {-100, 0}, {-10000000, 0}, {AV_NOPTS_VALUE, 0},
            {0, high}, {low, 0}, {low, high}, {low, high - 9999998}}) {
            FFF3FPConfiguration config{}; config.size = sizeof(config);
            PlayerSession s(config); StopWorker(s);
            s.playbackRate_.store(1.25);
            s.tempoOrigin_.store(origin);
            auto* frame = av_frame_alloc();
            Check(frame != nullptr, "tempo frame allocated");
            frame->format = AV_SAMPLE_FMT_FLT; frame->sample_rate = 48000; frame->nb_samples = 16;
            av_channel_layout_default(&frame->ch_layout, 2);
            Check(av_frame_get_buffer(frame, 0) >= 0, "tempo frame buffer allocated");
            std::memset(frame->data[0], 0, 16 * 2 * sizeof(float));
            Check(s.TempoAudio(frame, position, 0) == FFFResult::Success, "tempo timestamp boundary accepted without invalid trim");
            av_frame_free(&frame);
            s.DoClose();
        }
    }

    static void Iocp(const std::string& path) {
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp fixture opened");
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file, &size) && size.QuadPart > 0, "iocp fixture sized");
        std::string error;
        auto reader = IocpMediaReader::Create(file, size.QuadPart, error);
        Check(reader != nullptr, "iocp reader created");
        // Demand-driven target: 8 MiB asks for 8 slots, 2 MiB for 2. The
        // public slot setter pins the target for the rest of this fixture.
        reader->SetTargetBytes(8 * 1024 * 1024);
        Check(reader->TargetSlots() == 8, "iocp target grows with demand");
        reader->SetTargetBytes(2 * 1024 * 1024);
        Check(reader->TargetSlots() == 2, "iocp target shrinks with demand");
        reader->SetTargetSlots(8);
        Check(reader->TargetSlots() == 8, "iocp target pinned for high queue");
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(64 * 1024);
        const auto limit = (std::min)(size.QuadPart, static_cast<LONGLONG>(4 * 1024 * 1024));
        std::int64_t total = 0;
        while (total < limit) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 3000, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp sequential read");
            total += count;
        }
        Check(total == limit, "iocp sequential length");
        DWORD seekError = 0;
        Check(reader->Seek(size.QuadPart / 2, 3000, seekError), "iocp seek to middle");
        DWORD middleError = 0;
        const auto middle = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
            interrupt, 3000, middleError);
        Check(middle > 0, "iocp read after seek");
        Check(reader->Seek(0, 3000, seekError), "iocp seek to start");
        DWORD startError = 0;
        const auto start = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
            interrupt, 3000, startError);
        Check(start > 0 && reader->Position() == start, "iocp read after rewind");
        Check(reader->Seek(size.QuadPart, 3000, seekError), "iocp seek to end");
        DWORD eofError = 0;
        Check(reader->Read(buffer.data(), static_cast<int>(buffer.size()),
            interrupt, 3000, eofError) == AVERROR_EOF, "iocp end of file");
        reader->SetTargetSlots(2);
        Check(reader->Seek(0, 3000, seekError), "iocp seek after shrink");
        auto metrics = reader->MetricsSnapshot();
        Check(metrics.targetSlots == 2 && metrics.allocatedSlots == 2,
            "iocp shrink trims unused slots on seek");
        DWORD shrinkError = 0;
        Check(reader->Read(buffer.data(), static_cast<int>(buffer.size()),
            interrupt, 3000, shrinkError) > 0, "iocp read after shrink");
        reader.reset();
        CloseHandle(file);
    }

    static void IocpBudget(const std::string& path) {
        IocpTestHookReset hookReset;
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp budget fixture opened");
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file, &size) && size.QuadPart > 0, "iocp budget fixture sized");
        std::string error;
        // Probe readers are created with a two-slot ceiling; the file size cap
        // must still hold for a short fixture.
        auto probe = IocpMediaReader::Create(file, size.QuadPart, error, 2);
        Check(probe != nullptr, "iocp probe reader created");
        auto probeMetrics = probe->MetricsSnapshot();
        Check(probeMetrics.targetSlots == 2 && probeMetrics.allocatedSlots == 2,
            "iocp probe reader allocates only two slots");
        probe.reset();
        CloseHandle(file);

        const auto readerFile = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(readerFile != INVALID_HANDLE_VALUE, "iocp budget reader fixture reopened");
        auto reader = IocpMediaReader::Create(readerFile, size.QuadPart, error);
        Check(reader != nullptr, ("iocp budget reader created: " + error).c_str());
        reader->SetTargetSlots(8);
        auto metrics = reader->MetricsSnapshot();
        const auto sizeSlots = static_cast<int>((size.QuadPart + 1024 * 1024 - 1) / (1024 * 1024));
        Check(metrics.allocatedSlots <= (std::max)(2, (std::min)(8, sizeSlots)),
            "iocp allocation follows file size");
        reader->SetTargetSlots(2);
        DWORD seekError = 0;
        Check(reader->Seek(0, 3000, seekError), "iocp budget seek");
        metrics = reader->MetricsSnapshot();
        Check(metrics.allocatedSlots == 2 && metrics.allocatedBytes == 2 * 1024 * 1024,
            "iocp shrink releases idle slots");

        // Two deliberately short completions must become holes and still
        // return the exact original byte sequence.
        IocpMediaReader::TestSetCompletionDelayMs(0);
        const int shortCap = (std::max)(1, static_cast<int>(
            (std::min)(size.QuadPart / 4, static_cast<LONGLONG>(16 * 1024))));
        IocpMediaReader::TestSetShortRead(shortCap, 2);
        reader->SetTargetSlots(2);
        Check(reader->Seek(0, 3000, seekError), "iocp short-read seek");
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(64 * 1024);
        std::int64_t total = 0;
        while (total < size.QuadPart) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 3000, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp short-read sequential read");
            total += count;
        }
        IocpMediaReader::TestSetShortRead(0, 0);
        metrics = reader->MetricsSnapshot();
        Check(total == size.QuadPart, "iocp short-read preserves length");
        Check(metrics.shortReads >= 2, "iocp short-read fault observed");
        Check(metrics.holesCreated >= 2 && metrics.holeCount <= 2,
            "iocp short-read holes bounded by slots");
        Check(!metrics.holeLimitHit, "iocp short-read hole budget not exhausted");
        reader.reset();
        CloseHandle(readerFile);
    }

    static void IocpTrim(const std::string& path) {
        EnvironmentVariableGuard trimDelay(L"FFF_NET_IO_TRIM_DELAY_MS", L"100");
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp trim fixture opened");
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file, &size) && size.QuadPart > 8 * 1024 * 1024,
            "iocp trim fixture large enough");
        std::string error;
        auto reader = IocpMediaReader::Create(file, size.QuadPart, error);
        Check(reader != nullptr, "iocp trim reader created");
        reader->SetTargetSlots(8);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(1024 * 1024);
        std::int64_t total = 0;
        while (total < 8 * 1024 * 1024) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 3000, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp trim sequential read");
            total += count;
        }
        reader->SetTargetSlots(2);
        while (total < size.QuadPart) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 3000, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp trim drain read");
            total += count;
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (reader->MetricsSnapshot().allocatedSlots == 2) break;
            Sleep(100);
        }
        const auto metrics = reader->MetricsSnapshot();
        Check(metrics.targetSlots == 2 && metrics.allocatedSlots == 2,
            "iocp idle target drop trims slots without a seek");
        reader.reset();
        CloseHandle(file);
    }

    static void IocpAdaptive(const std::string& path) {
        IocpTestHookReset hookReset;
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp adaptive fixture opened");
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file, &size) && size.QuadPart > 8 * 1024 * 1024,
            "iocp adaptive fixture large enough");
        std::string error;
        auto reader = IocpMediaReader::Create(file, size.QuadPart, error);
        Check(reader != nullptr, "iocp adaptive reader created");
        reader->SetTargetBytes(2 * 1024 * 1024);
        Check(reader->TargetSlots() == 2, "iocp adaptive starts at hint");
        reader->SetAdaptiveReadAhead(true);
        // A 200 ms completion delay makes the two-slot window demonstrably
        // starved; the feedback loop must raise the target without waiting for
        // the bitrate hint to change.
        IocpMediaReader::TestSetCompletionDelayMs(200);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(1024 * 1024);
        std::int64_t total = 0;
        const auto limit = (std::min)(size.QuadPart, static_cast<LONGLONG>(24 * 1024 * 1024));
        while (total < limit) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 3000, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp adaptive sequential read");
            total += count;
        }
        IocpMediaReader::TestSetCompletionDelayMs(0);
        auto metrics = reader->MetricsSnapshot();
        Check(metrics.targetSlots >= 3, "iocp adaptive target grows under latency");
        Check(metrics.waitEvents > 0, "iocp adaptive observed waits");
        Check(metrics.allocatedSlots <= 12, "iocp adaptive target remains bounded");
        reader.reset();
        CloseHandle(file);
    }

    static void IocpJitterPattern(const std::string& root) {
        IocpTestHookReset hookReset;
        const auto path = CreatePatternFile(root, 12);
        ScopedDeleteFile deleteFixture(path);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp jitter fixture opened");
        std::string error;
        auto reader = IocpMediaReader::Create(file, 12LL * 1024 * 1024, error);
        Check(reader != nullptr, "iocp jitter reader created");
        reader->SetTargetBytes(2 * 1024 * 1024);
        reader->SetAdaptiveReadAhead(true);
        // Rotate 250-500 ms completion delays so the reader sees both high
        // latency and latency variance instead of a single fixed delay.
        constexpr int delays[] = {300, 450, 250, 500, 350, 400};
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(1024 * 1024);
        std::int64_t offset = 0;
        for (int index = 0; index < 12; ++index) {
            IocpMediaReader::TestSetCompletionDelayMs(delays[index % 6]);
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 4000, errorCode);
            Check(count == static_cast<int>(buffer.size()), "iocp jitter sequential read");
            VerifyPattern(buffer.data(), count, offset);
            offset += count;
        }
        IocpMediaReader::TestSetCompletionDelayMs(0);
        const auto metrics = reader->MetricsSnapshot();
        Check(metrics.targetSlots >= 3, "iocp jitter target grows under latency variance");
        Check(metrics.readLatencyP95Ms >= 250, "iocp jitter observes p95 latency");
        Check(metrics.allocatedSlots <= 24, "iocp jitter allocation remains bounded");
        reader.reset();
        CloseHandle(file);
        std::remove(path.c_str());
    }

    static void IocpAdaptiveShrinkUnderHighP95(const std::string& root) {
        IocpTestHookReset hookReset;
        constexpr std::int64_t MiB = 1024 * 1024;
        constexpr std::int64_t fileBytes = 64 * MiB;
        const auto path = CreatePatternFile(root, fileBytes);
        ScopedDeleteFile deleteFixture(path);
        EnvironmentVariableGuard maxSlots(L"FFF_NET_IO_MAX_SLOTS", L"24");
        EnvironmentVariableGuard depth(L"FFF_NET_IO_DEPTH", nullptr);
        EnvironmentVariableGuard trimDelay(L"FFF_NET_IO_TRIM_DELAY_MS", L"0");
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp adaptive-shrink fixture opened");
        std::string error;
        IocpMediaReader::TestSetPendingReadDelayMs(200);
        auto reader = IocpMediaReader::Create(file, fileBytes, error);
        Check(reader != nullptr, "iocp adaptive-shrink reader created");
        reader->SetAdaptiveReadAhead(true);
        reader->SetStreamBitRate(160000000);
        Check(reader->TargetSlots() == 11, "iocp adaptive-shrink high-rate target established");

        bool highLatencyObserved = false;
        for (int attempt = 0; attempt < 120; ++attempt) {
            const auto metrics = reader->MetricsSnapshot();
            if (metrics.readLatencyP95Ms >= 150) {
                highLatencyObserved = true;
                break;
            }
            Sleep(10);
        }
        Check(highLatencyObserved, "iocp adaptive-shrink high P95 sampled");
        reader->SetStreamBitRate(8000000);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(MiB));
        std::int64_t offset = 0;
        bool shrunkWithHighP95 = false;
        for (int attempt = 0; attempt < 40; ++attempt) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 600, errorCode);
            Check(count == static_cast<int>(buffer.size()),
                "iocp adaptive-shrink low-rate read succeeds");
            VerifyPattern(buffer.data(), count, offset);
            offset += count;
            const auto metrics = reader->MetricsSnapshot();
            if (metrics.targetSlots == 2 && metrics.allocatedSlots == 2 &&
                metrics.readLatencyP95Ms >= 150) {
                shrunkWithHighP95 = true;
                break;
            }
            Sleep(300);
        }
        Check(shrunkWithHighP95,
            "iocp adaptive target and allocation shrink while P95 remains high");
        reader.reset();
        CloseHandle(file);
    }

    static std::uint8_t PatternByte(const std::int64_t position) noexcept {
        // SplitMix64-style finalizer over the full absolute byte offset. Taking
        // the high byte avoids the old modulo-256 sequence, which repeated at
        // every 256-byte displacement and let misbased blocks pass validation.
        auto value = static_cast<std::uint64_t>(position) + 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        value ^= value >> 31;
        return static_cast<std::uint8_t>(value >> 56);
    }

    static std::string CreatePatternFile(const std::string& root,
        const std::int64_t totalBytes) {
        const auto path = root + "/io_pattern.bin";
        const auto file = CreateFileA(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "pattern fixture created");
        std::vector<std::uint8_t> chunk(1024 * 1024);
        std::int64_t written = 0;
        while (written < totalBytes) {
            const auto requested = static_cast<DWORD>((std::min<std::int64_t>)(
                static_cast<std::int64_t>(chunk.size()), totalBytes - written));
            for (DWORD index = 0; index < requested; ++index)
                chunk[static_cast<std::size_t>(index)] = PatternByte(written + index);
            DWORD bytes = 0;
            Check(WriteFile(file, chunk.data(), requested, &bytes, nullptr) != FALSE,
                "pattern fixture chunk written");
            Check(bytes > 0, "pattern fixture made progress");
            written += static_cast<std::int64_t>(bytes);
        }
        CloseHandle(file);
        return path;
    }

    static std::string CreatePatternFile(const std::string& root, const int mebibytes) {
        return CreatePatternFile(root,
            static_cast<std::int64_t>(mebibytes) * 1024 * 1024);
    }

    static bool PatternMatches(const std::uint8_t* data, const int count,
        const std::int64_t offset) noexcept {
        if (data == nullptr || count < 0 || offset < 0) return false;
        for (int index = 0; index < count; ++index)
            if (data[index] != PatternByte(offset + index)) return false;
        return true;
    }

    static void VerifyPattern(const std::uint8_t* data, const int count,
        const std::int64_t offset) {
        if (!PatternMatches(data, count, offset))
            throw std::runtime_error("iocp byte pattern mismatch");
    }

    static void IocpPattern(const std::string& root) {
        IocpTestHookReset hookReset;
        constexpr std::int64_t MiB = 1024 * 1024;
        constexpr std::int64_t fileBytes = 12 * MiB + 123;
        const auto path = CreatePatternFile(root, fileBytes);
        ScopedDeleteFile deleteFixture(path);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp pattern fixture opened");
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file, &size) && size.QuadPart == fileBytes,
            "iocp non-aligned pattern fixture sized");
        std::string error;
        IocpMediaReader::TestSetShortRead(24577, 2);
        auto reader = IocpMediaReader::Create(file, size.QuadPart, error);
        Check(reader != nullptr, "iocp pattern reader created");
        reader->SetTargetSlots(8);
        AVIOInterruptCB interrupt{};

        // Negative controls prove wrong block bases, off-by-one offsets, and
        // actual corruption all fail the exact per-byte verifier.
        std::array<std::uint8_t, 512> probe{};
        for (std::size_t index = 0; index < probe.size(); ++index)
            probe[index] = PatternByte(static_cast<std::int64_t>(index));
        Check(!PatternMatches(probe.data(), static_cast<int>(probe.size()), 256),
            "pattern verifier rejects wrong block offset");
        Check(!PatternMatches(probe.data(), static_cast<int>(probe.size()), 1),
            "pattern verifier rejects off-by-one absolute offset");
        probe[17] ^= 0x5a;
        Check(!PatternMatches(probe.data(), static_cast<int>(probe.size()), 0),
            "pattern verifier rejects corrupted content");
        for (std::size_t index = 0; index < probe.size(); ++index)
            probe[index] = PatternByte(0x100000000LL + static_cast<std::int64_t>(index));
        Check(!PatternMatches(probe.data(), static_cast<int>(probe.size()), 0),
            "pattern verifier mixes high absolute offset bits");

        std::int64_t bytesServed = 0;
        const auto readRange = [&](const std::int64_t start, const std::int64_t length,
            const int requestBytes, const char* label) {
            std::vector<std::uint8_t> buffer(static_cast<std::size_t>(requestBytes));
            std::int64_t consumed = 0;
            while (consumed < length) {
                const auto request = static_cast<int>((std::min<std::int64_t>)(
                    requestBytes, length - consumed));
                DWORD errorCode = 0;
                const auto count = reader->Read(buffer.data(), request, interrupt,
                    3000, errorCode);
                Check(count > 0, label);
                VerifyPattern(buffer.data(), count, start + consumed);
                consumed += count;
            }
            Check(consumed == length, "iocp requested range length exact");
            bytesServed += consumed;
        };

        // Two short completion caps exercise hole refill. Consumer requests cover
        // 24577 and 65537 plus a multi-slot 6 MiB+123 request; a full pass checks
        // every file byte at its absolute offset, including a non-aligned seek.
        readRange(0, 4 * 24577, 24577, "iocp 24577-byte short-read request");
        IocpMediaReader::TestSetShortRead(0, 0);
        DWORD seekError = 0;
        Check(reader->Seek(0, 3000, seekError), "iocp reset for full pattern pass");
        readRange(0, fileBytes, static_cast<int>(MiB), "iocp full absolute-pattern pass");
        IocpMediaReader::TestSetShortRead(65537, 2);
        Check(reader->Seek(123, 3000, seekError), "iocp seek to non-aligned byte offset");
        readRange(123, 65537, 65537, "iocp 65537-byte short-read request");
        IocpMediaReader::TestSetShortRead(0, 0);
        constexpr std::int64_t largeRequest = 6 * MiB + 123;
        const auto largeStart = 2 * MiB + 123;
        Check(reader->Seek(largeStart, 3000, seekError),
            "iocp seek to second non-aligned block");
        readRange(largeStart, largeRequest, static_cast<int>(largeRequest),
            "iocp 6MiB+123-byte read request");

        const auto metrics = reader->MetricsSnapshot();
        Check(metrics.shortReads >= 4, "iocp pattern short completions observed");
        Check(metrics.bytesServed == bytesServed,
            "iocp pattern served exact checked byte count");
        reader.reset();
        CloseHandle(file);
    }

    static void IocpHoleLimitSeekRecovery(const std::string& root) {
        IocpTestHookReset hookReset;
        constexpr std::int64_t MiB = 1024 * 1024;
        constexpr std::int64_t fileBytes = 32 * MiB + 123;
        const auto path = CreatePatternFile(root, fileBytes);
        ScopedDeleteFile deleteFixture(path);
        EnvironmentVariableGuard maxSlots(L"FFF_NET_IO_MAX_SLOTS", L"24");
        EnvironmentVariableGuard depth(L"FFF_NET_IO_DEPTH", nullptr);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp hole-limit fixture opened");
        std::string error;
        IocpMediaReader::TestSetPendingReadDelayMs(500);
        auto reader = IocpMediaReader::Create(file, fileBytes, error, 24);
        Check(reader != nullptr, "iocp 24-slot hole-limit reader created");
        bool allSlotsPending = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (reader->MetricsSnapshot().inFlight == 24) {
                allSlotsPending = true;
                break;
            }
            Sleep(10);
        }
        Check(allSlotsPending, "iocp timeout fixture reaches 24 in-flight reads");
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(64 * 1024);
        DWORD errorCode = 0;
        const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
            interrupt, 100, errorCode);
        Check(count == AVERROR(ETIMEDOUT), "iocp 24-slot read times out");
        const auto timedOut = reader->MetricsSnapshot();
        Check(timedOut.holeLimitHit && timedOut.holeBytes > 16 * MiB,
            "iocp timeout exceeds 16 MiB hole budget");

        // Disable delay for re-armed post-seek reads; already submitted packets
        // retain their 500 ms due time and must be drained before reset completes.
        IocpMediaReader::TestSetPendingReadDelayMs(0);
        DWORD seekError = 0;
        Check(reader->Seek(123, 3000, seekError),
            "iocp seek succeeds after timeout created excessive holes");
        const auto recovered = reader->MetricsSnapshot();
        Check(!recovered.holeLimitHit && recovered.holeCount == 0 &&
            recovered.holeBytes == 0,
            "iocp seek clears sticky hole-limit error and ranges");

        std::int64_t offset = 123;
        const auto end = offset + 4 * MiB + 123;
        while (offset < end) {
            DWORD readError = 0;
            const auto requested = static_cast<int>((std::min<std::int64_t>)(
                static_cast<std::int64_t>(buffer.size()), end - offset));
            const auto read = reader->Read(buffer.data(), requested,
                interrupt, 3000, readError);
            Check(read > 0, "iocp reader remains usable after hole-limit seek");
            VerifyPattern(buffer.data(), read, offset);
            offset += read;
        }
        Check(offset == end, "iocp post-recovery continued read length");
        reader.reset();
        CloseHandle(file);
    }

    static void IocpStopLateCompletionBudgetRecovery(const std::string& root) {
        IocpTestHookReset hookReset;
        constexpr std::int64_t fileBytes = 4LL * 1024 * 1024;
        const auto path = CreatePatternFile(root, fileBytes);
        ScopedDeleteFile deleteFixture(path);
        EnvironmentVariableGuard maxSlots(L"FFF_NET_IO_MAX_SLOTS", L"24");
        EnvironmentVariableGuard depth(L"FFF_NET_IO_DEPTH", nullptr);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp late-stop fixture opened");
        const auto budgetBefore = IocpMediaReader::TestResidentSlotBytes();
        std::string error;
        IocpMediaReader::TestSetPendingReadDelayMs(3500);
        auto reader = IocpMediaReader::Create(file, fileBytes, error, 2);
        Check(reader != nullptr, "iocp late-stop reader created");
        const auto pending = reader->MetricsSnapshot();
        Check(pending.inFlight == 2 && pending.maxInFlight == 2,
            "iocp late-stop has delayed pending completions");
        Check(IocpMediaReader::TestResidentSlotBytes() >= budgetBefore + 2 * 1024 * 1024,
            "iocp resident budget charged while reads are pending");
        const auto stopStart = GetTickCount64();
        reader.reset(); // Stop's 3 s bound expires; worker owns late completions.
        const auto stopElapsed = GetTickCount64() - stopStart;
        Check(stopElapsed >= 2800 && stopElapsed < 5000,
            "iocp stop honors bounded wait with delayed completions");
        IocpMediaReader::TestSetPendingReadDelayMs(0);
        bool budgetRecovered = false;
        for (int attempt = 0; attempt < 60; ++attempt) {
            if (IocpMediaReader::TestResidentSlotBytes() <= budgetBefore) {
                budgetRecovered = true;
                break;
            }
            Sleep(100);
        }
        Check(budgetRecovered,
            "iocp late completions drain and resident slot budget returns");
        CloseHandle(file);
    }

    static void IocpUnknownSizeEof(const std::string& root) {
        const auto path = CreatePatternFile(root, 4);
        ScopedDeleteFile deleteFixture(path);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp unknown-size fixture opened");
        std::string error;
        auto reader = IocpMediaReader::Create(file, -1, error);
        Check(reader != nullptr, "iocp unknown-size reader created");
        reader->SetTargetSlots(2);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(64 * 1024);
        std::int64_t offset = 0;
        const auto started = GetTickCount64();
        while (true) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 500, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp unknown-size read");
            VerifyPattern(buffer.data(), count, offset);
            offset += count;
            Check(GetTickCount64() - started < 10000, "iocp unknown-size EOF is not a timeout");
        }
        Check(offset == 4LL * 1024 * 1024, "iocp unknown-size exact EOF");
        reader.reset();
        CloseHandle(file);
        std::remove(path.c_str());
    }

    static void IocpPermanentError(const std::string& root) {
        const auto path = CreatePatternFile(root, 2);
        // A write-only handle makes every ReadFile fail synchronously. The
        // consumer must see EIO, not wait for the 8 s network timeout.
        const auto file = CreateFileA(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp error fixture opened write-only");
        std::string error;
        auto reader = IocpMediaReader::Create(file, 2LL * 1024 * 1024, error);
        Check(reader != nullptr, "iocp error reader created");
        reader->SetTargetSlots(2);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(64 * 1024);
        DWORD errorCode = 0;
        const auto started = GetTickCount64();
        const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
            interrupt, 500, errorCode);
        Check(count == AVERROR(EIO), "iocp permanent read error propagates");
        Check(GetTickCount64() - started < 3000, "iocp permanent error is immediate");
        Check(errorCode != 0, "iocp permanent error keeps win32 status");
        const auto metrics = reader->MetricsSnapshot();
        Check(metrics.errorCount >= 1, "iocp permanent error counted");
        reader.reset();
        CloseHandle(file);
        std::remove(path.c_str());
    }

    static void IocpEofTrim(const std::string& root) {
        EnvironmentVariableGuard trimDelay(L"FFF_NET_IO_TRIM_DELAY_MS", L"100");
        const auto path = CreatePatternFile(root, 16);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp EOF trim fixture opened");
        std::string error;
        auto reader = IocpMediaReader::Create(file, 16LL * 1024 * 1024, error);
        Check(reader != nullptr, "iocp EOF trim reader created");
        reader->SetTargetSlots(8);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(1024 * 1024);
        std::int64_t offset = 0;
        while (offset < 16LL * 1024 * 1024) {
            DWORD errorCode = 0;
            const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                interrupt, 3000, errorCode);
            if (count == AVERROR_EOF) break;
            Check(count > 0, "iocp EOF trim read");
            offset += count;
        }
        Check(offset == 16LL * 1024 * 1024, "iocp EOF trim exact length");
        for (int attempt = 0; attempt < 30; ++attempt) {
            if (reader->MetricsSnapshot().allocatedSlots == 2) break;
            Sleep(100);
        }
        const auto metrics = reader->MetricsSnapshot();
        Check(metrics.targetSlots == 8, "iocp EOF keeps configured target");
        Check(metrics.allocatedSlots == 2, "iocp EOF trims allocation to two slots");
        reader.reset();
        CloseHandle(file);
        std::remove(path.c_str());
    }

    static void IocpSeekPattern(const std::string& root) {
        IocpTestHookReset hookReset;
        const auto path = CreatePatternFile(root, 24);
        ScopedDeleteFile deleteFixture(path);
        const auto file = CreateFileA(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "iocp seek fixture opened");
        std::string error;
        auto reader = IocpMediaReader::Create(file, 24LL * 1024 * 1024, error);
        Check(reader != nullptr, "iocp seek pattern reader created");
        reader->SetTargetSlots(8);
        IocpMediaReader::TestSetCompletionDelayMs(60);
        AVIOInterruptCB interrupt{};
        std::vector<std::uint8_t> buffer(64 * 1024);
        for (const auto target : {0LL, 6LL * 1024 * 1024, 0LL, 12LL * 1024 * 1024}) {
            DWORD seekError = 0;
            Check(reader->Seek(target, 3000, seekError), "iocp seek pattern reset");
            std::int64_t offset = target;
            for (int chunk = 0; chunk < 16; ++chunk) {
                DWORD errorCode = 0;
                const auto count = reader->Read(buffer.data(), static_cast<int>(buffer.size()),
                    interrupt, 3000, errorCode);
                Check(count > 0, "iocp seek pattern read");
                VerifyPattern(buffer.data(), count, offset);
                offset += count;
            }
        }
        IocpMediaReader::TestSetCompletionDelayMs(0);
        const auto metrics = reader->MetricsSnapshot();
        Check(metrics.resets >= 4, "iocp seek pattern resets counted");
        Check(metrics.resetFailures == 0, "iocp seek pattern resets complete");
        reader.reset();
        CloseHandle(file);
        std::remove(path.c_str());
    }

    static void SharedInputEventPattern(const std::string& root) {
        const auto path = CreatePatternFile(root, 8);
        ScopedDeleteFile deleteFixture(path);
        EnvironmentVariableGuard ioMode(L"FFF_NET_IO", L"event");
        std::string error;
        AVIOInterruptCB cancelled{&AlwaysInterrupt, nullptr};
        auto rejected = SharedFileInput::Open(path.c_str(), error, 0, true, &cancelled);
        Check(rejected == nullptr, "shared input rejects immediate cancellation");
        auto input = SharedFileInput::Open(path.c_str(), error, 0, true);
        Check(input != nullptr, "event fallback input opened");
        Check(input->Context() != nullptr, "event fallback avio context");
        std::vector<std::uint8_t> buffer(64 * 1024);
        std::int64_t offset = 0;
        while (true) {
            const auto count = avio_read(input->Context(), buffer.data(),
                static_cast<int>(buffer.size()));
            if (count == AVERROR_EOF) break;
            Check(count > 0, "event fallback read");
            VerifyPattern(buffer.data(), count, offset);
            offset += count;
        }
        Check(offset == 8LL * 1024 * 1024, "event fallback exact length");
        input.reset(); // Close its event reader and file handle before fixture deletion.
    }

    static void Network(const char* path) {
        // Opt-in only: the default test run must never touch a real share. The
        // caller supplies a UNC path; this exercises the same SharedFileInput
        // classification and asynchronous open path the player uses.
        std::string error;
        auto input = SharedFileInput::Open(path, error, 0);
        Check(input != nullptr, "network UNC input opened");
        Check(input->Context() != nullptr, "network UNC avio context");
        std::vector<std::uint8_t> buffer(64 * 1024);
        std::int64_t total = 0;
        while (total < 2 * 1024 * 1024) {
            const auto count = avio_read(input->Context(), buffer.data(),
                static_cast<int>(buffer.size()));
            if (count <= 0) break;
            total += count;
        }
        Check(total >= 2 * 1024 * 1024, "network UNC sequential read");
    }

    static void Seek(const std::string& path) {
        EnvironmentVariableGuard skipEnv(L"FFF_SEEK_SKIP", L"1");
        for (bool skip : {true, false}) {
            skipEnv.SetValue(skip ? L"1" : L"0");
            FFF3FPConfiguration config{}; config.size = sizeof(config);
            PlayerSession s(config); StopWorker(s);
            Check(avformat_open_input(&s.format_, path.c_str(), nullptr, nullptr) == 0 &&
                avformat_find_stream_info(s.format_, nullptr) >= 0, "seek fixture open");
            s.videoStream_ = av_find_best_stream(s.format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            Check(s.videoStream_ >= 0, "seek fixture video stream");
            auto* codec = avcodec_find_decoder(s.format_->streams[s.videoStream_]->codecpar->codec_id);
            s.videoDecoder_ = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(s.videoDecoder_, s.format_->streams[s.videoStream_]->codecpar);
            // An unopened GPU context deterministically exercises send-packet failure
            // and the real CPU fallback, including its nested seek.
            auto* original = s.videoDecoder_;
            s.snapshot_.decodeMode = FFF3FPDecodeMode::Gpu;
            s.snapshot_.position100ns = 0;
            s.seekTarget100ns_ = 100000000;
            s.appliedSkipRung_ = 1;
            s.DecodeUntilSeekTarget(false);
            Check(s.videoDecoder_ != original && s.snapshot_.decodeMode == FFF3FPDecodeMode::Cpu, "seek executes real GPU-to-CPU replacement");
            Check(s.videoDecoder_->skip_frame == (skip ? AVDISCARD_NONREF : AVDISCARD_DEFAULT), "seek restores live decoder discard policy");
            s.seekTarget100ns_ = 100000000;
            s.DecodeUntilSeekTarget(false);
            Check(s.seekTarget100ns_ < 0, "CPU seek drains EOF without pending target");
            s.DoClose(); // The fixture stopped the real worker before installing this input.
        }
    }
};

namespace FFFPrefetch {
AVFrame* SecurityDecodeStill(const std::string&, std::uint64_t, bool);
std::uint64_t SecurityFrameBytes(const AVFrame*);
struct PrefetchSecurityRegression {
    static void Run(const std::string& root) {
        auto& service = PrefetchService::Instance();
        {
            std::lock_guard lock(service.mutex_);
            service.ReleaseAllLocked();
            const auto budget = service.budgetBytes_;
            service.budgetBytes_ = 100;
            Check(!service.EnforceBudgetLocked(0), "zero-sized frame rejected");
            Check(!service.EnforceBudgetLocked(101) && !service.EnforceBudgetLocked(UINT64_MAX), "oversized and overflowing admission rejected");
            Check(service.EnforceBudgetLocked(100), "exact cache budget accepted");
            PrefetchService::StillEntry entry{}; entry.bytes = 80;
            service.stills_["old"] = entry; service.cachedBytes_ = 80;
            Check(service.EnforceBudgetLocked(30) && service.cachedBytes_ == 0 && service.stills_.empty(), "cache evicts before admission");
            service.budgetBytes_ = budget;
        }
        for (const auto* name : {"small.bmp", "4k.bmp", "8k.bmp", "small.png", "small.jpg", "small.webp", "small.tiff", "small.avif"}) {
            auto* frame = SecurityDecodeStill(root + "/" + name, 256ull * 1024 * 1024, false);
            Check(frame != nullptr, "ordinary still prefetched");
            Check(SecurityFrameBytes(frame) > 0 && SecurityFrameBytes(frame) <= 256ull * 1024 * 1024, "prefetch actual allocation charged");
            av_frame_free(&frame);
        }
        auto* tooLarge = SecurityDecodeStill(root + "/8k.bmp", 16ull * 1024 * 1024, false);
        Check(tooLarge == nullptr, "oversized decode refused before frame allocation");
        auto* cancelled = SecurityDecodeStill(root + "/small.bmp", 256ull * 1024 * 1024, true);
        Check(cancelled == nullptr, "cancelled still decode refused");
        auto* tagged = SecurityDecodeStill(root + "/small.bmp", 256ull * 1024 * 1024, false);
        Check(tagged != nullptr, "side data fixture decoded");
        const auto pixels = SecurityFrameBytes(tagged);
        Check(av_frame_new_side_data(tagged, AV_FRAME_DATA_ICC_PROFILE, 32768) != nullptr, "ICC side data allocated");
        Check(SecurityFrameBytes(tagged) >= pixels + 32768, "ICC allocation included in frame charge");
        {
            std::lock_guard lock(service.mutex_);
            const auto budget = service.budgetBytes_;
            service.budgetBytes_ = pixels + 16384;
            Check(!service.EnforceBudgetLocked(SecurityFrameBytes(tagged)), "oversized side data cannot enter cache");
            service.budgetBytes_ = budget;
            Check(service.EnforceBudgetLocked(SecurityFrameBytes(tagged)), "ordinary ICC frame fits normal budget");
        }
        av_frame_free(&tagged);
    }
};
}

extern "C" __declspec(dllexport) int FFF3FP_RunSecurityRegression(const char* root) {
    IocpTestHookReset hookReset;
    try {
        checks = 0;
        AvifTests();
        AvifDeadlineTests(root);
        PlayerSecurityRegression::Paths(std::string(root) + "/small.bmp");
        PlayerSecurityRegression::OpeningCancellation(std::string(root) + "/small.bmp");
        PlayerSecurityRegression::Tap();
        PlayerSecurityRegression::Tempo();
        PlayerSecurityRegression::Seek(std::string(root) + "/seek.mp4");
        PlayerSecurityRegression::Iocp(std::string(root) + "/small.bmp");
        PlayerSecurityRegression::IocpBudget(std::string(root) + "/seek.mp4");
        PlayerSecurityRegression::IocpTrim(std::string(root) + "/4k.bmp");
        PlayerSecurityRegression::IocpAdaptive(std::string(root) + "/4k.bmp");
        PlayerSecurityRegression::IocpJitterPattern(root);
        PlayerSecurityRegression::IocpAdaptiveShrinkUnderHighP95(root);
        PlayerSecurityRegression::IocpPattern(root);
        PlayerSecurityRegression::IocpHoleLimitSeekRecovery(root);
        PlayerSecurityRegression::IocpStopLateCompletionBudgetRecovery(root);
        PlayerSecurityRegression::IocpUnknownSizeEof(root);
        PlayerSecurityRegression::IocpPermanentError(root);
        PlayerSecurityRegression::IocpEofTrim(root);
        PlayerSecurityRegression::IocpSeekPattern(root);
        PlayerSecurityRegression::SharedInputEventPattern(root);
        if (const auto* unc = std::getenv("FFF_TEST_UNC_PATH"); unc != nullptr && *unc != '\0')
            PlayerSecurityRegression::Network(unc);
        FFFPrefetch::PrefetchSecurityRegression::Run(root);
        std::printf("Native security regression: %d passed\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
