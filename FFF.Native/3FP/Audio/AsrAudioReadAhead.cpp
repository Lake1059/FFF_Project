#include "pch.h"
#include "3FP/Audio/AsrAudioReadAhead.h"
#include <chrono>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/common.h>
}

namespace {
constexpr AVRational MediaTicks{1, 10'000'000};

std::int64_t MonotonicMilliseconds() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

AsrAudioReadAhead::AsrAudioReadAhead(const std::int64_t lead, std::shared_ptr<AsrAudioTap> live)
    : lead_(lead), live_(std::move(live)) {}

AsrAudioReadAhead::~AsrAudioReadAhead() { Close(); }

void AsrAudioReadAhead::Cancel() noexcept {
    std::lock_guard lock(sourceMutex_);
    cancelled_.store(true);
    live_->Suspend(false);
}

void AsrAudioReadAhead::Update(const std::string* const path, const AVInputFormat* const demuxer,
    const int stream, const std::uint64_t generation, const std::int64_t origin,
    const std::int64_t clock) noexcept {
    clock_.store(clock, std::memory_order_relaxed);
    std::lock_guard lock(sourceMutex_);
    if (cancelled_.load()) return;
    const bool hasPath = path != nullptr && !path->empty();
    if (source_.stream == stream && source_.generation == generation && source_.demuxer == demuxer &&
        source_.origin == origin && (hasPath ? source_.path == *path : source_.path.empty())) return;
    bool usable = false;
    try {
        source_.path = hasPath ? *path : std::string{};
        source_.demuxer = demuxer;
        source_.stream = stream;
        source_.generation = generation;
        source_.origin = origin;
        usable = hasPath;
    } catch (...) {
        source_.path.clear();
    }
    source_.revision = revision_.fetch_add(1) + 1;
    live_->Suspend(usable);
}

int AsrAudioReadAhead::Interrupted(void* const data) noexcept {
    const auto& self = *static_cast<AsrAudioReadAhead*>(data);
    return self.cancelled_.load() || self.revision_.load() != self.active_.revision ||
        MonotonicMilliseconds() >= self.interruptDeadlineMs_;
}

bool AsrAudioReadAhead::Open(const std::int64_t position) noexcept {
    Close();
    interruptDeadlineMs_ = MonotonicMilliseconds() + 1500;
    format_ = avformat_alloc_context();
    if (!format_) return false;
    format_->interrupt_callback = {Interrupted, this};
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const auto result = avformat_open_input(&format_, active_.path.c_str(), active_.demuxer, &options);
    av_dict_free(&options);
    if (result < 0 || active_.stream < 0 || static_cast<unsigned>(active_.stream) >= format_->nb_streams)
        return false;
    const auto* stream = format_->streams[active_.stream];
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;
    const auto* implementation = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!implementation) return false;
    decoder_ = avcodec_alloc_context3(implementation);
    if (!decoder_ || avcodec_parameters_to_context(decoder_, stream->codecpar) < 0) return false;
    decoder_->pkt_timebase = stream->time_base;
    decoder_->thread_count = 1;
    if (avcodec_open2(decoder_, implementation, nullptr) < 0) return false;
    if (position > 2'500'000) {
        // Seek a short distance before the requested sample for decoder preroll.
        const auto absolute = av_sat_add64(position - 2'500'000, active_.origin);
        const auto target = av_rescale_q(absolute, MediaTicks, stream->time_base);
        if (av_seek_frame(format_, active_.stream, target, AVSEEK_FLAG_BACKWARD) < 0) return false;
    }
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    nextPosition_ = minimumPosition_ = position;
    draining_ = eof_ = false;
    return packet_ && frame_;
}

void AsrAudioReadAhead::Close() noexcept {
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&decoder_);
    avformat_close_input(&format_);
}

bool AsrAudioReadAhead::Read(void* const dst, const std::uint32_t capacity,
    FFF3FPAudioTapBlock& block, FFFResult& result) noexcept {
    std::lock_guard readLock(readerMutex_);
    if (cancelled_.load()) return false;
    const auto clock = clock_.load(std::memory_order_relaxed);
    const auto fallBack = [&] {
        Close();
        std::lock_guard sourceLock(sourceMutex_);
        // An in-flight read must not restore the live producer for a newer seek.
        if (active_.revision != revision_.load()) return true;
        failed_ = true;
        live_->Suspend(false);
        return false;
    };
    if (active_.revision != revision_.load()) {
        {
            std::lock_guard sourceLock(sourceMutex_);
            try { active_ = source_; }
            catch (...) { live_->Suspend(false); return false; }
        }
        failed_ = false;
        singleFrame_.NoteGeneration(active_.generation);
        if (active_.path.empty()) { Close(); return false; }
        if (!Open((std::max)(std::int64_t{0}, clock))) return fallBack();
    }
    if (active_.path.empty() || failed_) return false;
    const auto consume = [&] {
        const auto value = singleFrame_.Consume(dst, capacity, block);
        if (value == AsrAudioTap::ConsumeResult::BufferTooSmall) result = FFFResult::BufferTooSmall;
        return value != AsrAudioTap::ConsumeResult::Empty;
    };
    if (consume() || eof_) return true;
    if (nextPosition_ > av_sat_add64(clock, lead_)) return true;
    interruptDeadlineMs_ = MonotonicMilliseconds() + 200;
    const auto* stream = format_->streams[active_.stream];
    for (int budget = 0; budget < 128 && !Interrupted(this); ++budget) {
        const auto receive = avcodec_receive_frame(decoder_, frame_);
        if (receive == 0) {
            const auto pts = frame_->best_effort_timestamp;
            const auto position = pts == AV_NOPTS_VALUE ? nextPosition_ : av_sat_sub64(
                av_rescale_q(pts, stream->time_base, MediaTicks), active_.origin);
            if (frame_->sample_rate > 0) {
                nextPosition_ = av_sat_add64(position, av_rescale_q(frame_->nb_samples,
                    AVRational{1, frame_->sample_rate}, MediaTicks));
                if (nextPosition_ > minimumPosition_ && position >= 0 && !Interrupted(this)) {
                    singleFrame_.Produce(frame_, position);
                    av_frame_unref(frame_);
                    if (consume()) return true;
                }
            }
            av_frame_unref(frame_);
            continue;
        }
        if (receive == AVERROR_EOF) {
            eof_ = true;
            block.generation = active_.generation;
            block.sampleFormat = FFF3FP_AUDIO_TAP_END_OF_INPUT;
            return true;
        }
        if (receive != AVERROR(EAGAIN) || draining_) return fallBack();
        const auto read = av_read_frame(format_, packet_);
        if (read == AVERROR_EOF) {
            draining_ = true;
            if (avcodec_send_packet(decoder_, nullptr) < 0) return fallBack();
            continue;
        }
        if (read < 0) return fallBack();
        int sent = 0;
        if (packet_->stream_index == active_.stream) sent = avcodec_send_packet(decoder_, packet_);
        av_packet_unref(packet_);
        if (sent < 0 && sent != AVERROR_INVALIDDATA) return fallBack();
    }
    return true;
}
