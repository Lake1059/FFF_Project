#pragma once

#include "3FP/Api/FFF.Player.AudioTap.h"
#include "3FP/Audio/AsrAudioTap.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVInputFormat;
struct AVPacket;

// The tap pump owns the independent demuxer/decoder. Playback only publishes
// the source and clock, so this work never competes for its packet queue.
class AsrAudioReadAhead final {
public:
    AsrAudioReadAhead(std::int64_t lead, std::shared_ptr<AsrAudioTap> live);
    ~AsrAudioReadAhead();
    void Update(const std::string* path, const AVInputFormat* demuxer, int stream,
        std::uint64_t generation, std::int64_t origin, std::int64_t clock) noexcept;
    void Cancel() noexcept;
    bool Read(void* dst, std::uint32_t capacity, FFF3FPAudioTapBlock& block,
        FFFResult& result) noexcept;

private:
    struct Source {
        std::string path;
        const AVInputFormat* demuxer = nullptr;
        int stream = -1;
        std::uint64_t generation = 0;
        std::int64_t origin = 0;
        std::uint64_t revision = 0;
    };
    bool Open(std::int64_t position) noexcept;
    void Close() noexcept;
    static int Interrupted(void* data) noexcept;

    const std::int64_t lead_;
    const std::shared_ptr<AsrAudioTap> live_;
    AsrAudioTap singleFrame_{FFF3FP_AUDIO_TAP_MIN_RING_BYTES};
    std::atomic<bool> cancelled_{false};
    std::atomic<std::uint64_t> revision_{0};
    std::atomic<std::int64_t> clock_{0};
    std::mutex sourceMutex_;
    Source source_;
    std::mutex readerMutex_;
    Source active_;
    AVFormatContext* format_ = nullptr;
    AVCodecContext* decoder_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    std::int64_t nextPosition_ = 0;
    std::int64_t minimumPosition_ = 0;
    std::int64_t interruptDeadlineMs_ = 0;
    bool failed_ = false;
    bool draining_ = false;
    bool eof_ = false;
};
