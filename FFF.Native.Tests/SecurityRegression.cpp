#include "pch.h"
#include "3FP/Core/AvifIlocShim.h"
#include "3FP/Core/PlayerSession.h"
#include "3FP/Audio/AsrAudioTap.h"
#include "3FP/Prefetch/PrefetchService.h"
#include "Shared/Ffmpeg/IocpMediaReader.h"
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
        reader.reset();
        CloseHandle(file);
    }

    static void Seek(const std::string& path) {
        for (bool skip : {true, false}) {
            SetEnvironmentVariableW(L"FFF_SEEK_SKIP", skip ? L"1" : L"0");
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
        SetEnvironmentVariableW(L"FFF_SEEK_SKIP", nullptr);
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
    try {
        checks = 0;
        AvifTests();
        PlayerSecurityRegression::Paths(std::string(root) + "/small.bmp");
        PlayerSecurityRegression::Tap();
        PlayerSecurityRegression::Tempo();
        PlayerSecurityRegression::Seek(std::string(root) + "/seek.mp4");
        PlayerSecurityRegression::Iocp(std::string(root) + "/small.bmp");
        FFFPrefetch::PrefetchSecurityRegression::Run(root);
        std::printf("Native security regression: %d passed\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
