#include "pch.h"
#include "3FP/IO/InputPacketPump.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include <libavutil/error.h>
}

namespace FFFPlayer::Io {
namespace {
constexpr std::size_t kDefaultQueueBytes = 32ull * 1024ull * 1024ull;

std::uint64_t NowTickMs() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::uint64_t BytesToBps(const std::uint64_t bytes, const std::uint64_t elapsedMs) noexcept {
    if (elapsedMs == 0) return 0;
    return bytes * 8000ull / elapsedMs;
}
}

InputPacketPump::~InputPacketPump() {
    Stop();
}

bool InputPacketPump::Start(AVFormatContext* format, const std::size_t maxBytes,
    Interrupt externalInterrupt) noexcept {
    if (format == nullptr) return false;
    Stop();
    format_ = format;
    previousInterrupt_ = format->interrupt_callback;
    externalInterrupt_ = std::move(externalInterrupt);
    maxBytes_.store(std::max<std::size_t>(maxBytes, 1024), std::memory_order_relaxed);
    stopRequested_.store(false, std::memory_order_release);
    endOfStream_.store(false, std::memory_order_release);
    errorCode_.store(0, std::memory_order_release);
    packetsRead_.store(0, std::memory_order_relaxed);
    bytesRead_.store(0, std::memory_order_relaxed);
    measuredBps_.store(0, std::memory_order_relaxed);
    peakBps_.store(0, std::memory_order_relaxed);
    startTickMs_ = NowTickMs();
    windowStartTickMs_ = startTickMs_;
    windowBytes_ = 0;
    {
        std::lock_guard lock(mutex_);
        queuedBytes_ = 0;
    }
    format->interrupt_callback = AVIOInterruptCB{&InputPacketPump::InterruptCallback, this};
    running_.store(true, std::memory_order_release);
    try {
        thread_ = std::thread(&InputPacketPump::Run, this);
    } catch (...) {
        running_.store(false, std::memory_order_release);
        format->interrupt_callback = previousInterrupt_;
        format_ = nullptr;
        return false;
    }
    return true;
}

void InputPacketPump::Stop() noexcept {
    if (!running_.load(std::memory_order_acquire) && !thread_.joinable()) {
        ClearQueue();
        return;
    }
    stopRequested_.store(true, std::memory_order_release);
    dataCondition_.notify_all();
    spaceCondition_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
    ClearQueue();
    if (format_ != nullptr) {
        // Only restore when this pump still owns the callback; another opener may
        // have replaced the context's callback in the meantime.
        if (format_->interrupt_callback.opaque == this)
            format_->interrupt_callback = previousInterrupt_;
        format_ = nullptr;
    }
    externalInterrupt_ = {};
}

void InputPacketPump::SetMaxBytes(const std::size_t maxBytes) noexcept {
    const auto bounded = std::max<std::size_t>(maxBytes, 1024);
    maxBytes_.store(bounded, std::memory_order_relaxed);
    std::lock_guard lock(mutex_);
    spaceCondition_.notify_all();
}

bool InputPacketPump::TryPop(AVPacket& packet) noexcept {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) return false;
    auto* front = queue_.front();
    queue_.pop_front();
    if (front != nullptr) {
        queuedBytes_ -= static_cast<std::size_t>(std::max(front->size, 0));
        av_packet_move_ref(&packet, front);
        av_packet_free(&front);
    }
    spaceCondition_.notify_one();
    return true;
}

std::uint64_t InputPacketPump::QueuedBytes() const noexcept {
    std::lock_guard lock(mutex_);
    return static_cast<std::uint64_t>(queuedBytes_);
}

std::size_t InputPacketPump::QueuedPackets() const noexcept {
    std::lock_guard lock(mutex_);
    return queue_.size();
}

int InputPacketPump::InterruptCallback(void* opaque) noexcept {
    auto* self = static_cast<InputPacketPump*>(opaque);
    if (self == nullptr) return 0;
    if (self->stopRequested_.load(std::memory_order_acquire)) return 1;
    if (self->externalInterrupt_ && self->externalInterrupt_() != 0) return 1;
    return 0;
}

void InputPacketPump::Run() noexcept {
    const bool trace = std::getenv("FFF_IO_TRACE") != nullptr;
    auto* packet = av_packet_alloc();
    if (trace) {
        std::fprintf(stderr, "[io-pump] start format=%s pb=%p eof=%d pos=%lld nb=%d\n",
            format_->iformat && format_->iformat->name ? format_->iformat->name : "?",
            static_cast<void*>(format_->pb),
            format_->pb ? format_->pb->eof_reached : -1,
            format_->pb ? static_cast<long long>(format_->pb->pos) : -1LL,
            format_->nb_streams);
    }
    if (packet == nullptr) {
        errorCode_.store(AVERROR(ENOMEM), std::memory_order_release);
        endOfStream_.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        dataCondition_.notify_all();
        return;
    }
    while (!stopRequested_.load(std::memory_order_acquire)) {
        {
            std::unique_lock lock(mutex_);
            spaceCondition_.wait(lock, [this] {
                return stopRequested_.load(std::memory_order_acquire) ||
                    queuedBytes_ < maxBytes_.load(std::memory_order_relaxed);
            });
        }
        if (stopRequested_.load(std::memory_order_acquire)) break;
        const auto result = av_read_frame(format_, packet);
        if (stopRequested_.load(std::memory_order_acquire)) break;
        if (result < 0) {
            if (result == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (result != AVERROR_EOF) errorCode_.store(result, std::memory_order_release);
            endOfStream_.store(true, std::memory_order_release);
            if (trace) std::fprintf(stderr, "[io-pump] end result=%d packets=%llu\n",
                result, static_cast<unsigned long long>(packetsRead_.load()));
            break;
        }
        // A demuxer is allowed to hand back a packet whose data aliases its
        // internal/AVIO buffer. The packet is consumed on the session worker,
        // after the reader has advanced, so make the ownership explicit before
        // it leaves this thread.
        if (av_packet_make_refcounted(packet) < 0) {
            errorCode_.store(AVERROR(ENOMEM), std::memory_order_release);
            endOfStream_.store(true, std::memory_order_release);
            break;
        }
        const auto size = static_cast<std::size_t>(std::max(packet->size, 0));
        const int streamIndex = packet->stream_index;
        // The demuxer may return a packet that aliases an AVIO/demuxer buffer.
        // The consumer runs on another thread after this reader advances, so a
        // deep copy is the only lifetime that is independent of the next read.
        auto* queued = av_packet_alloc();
        if (queued == nullptr || av_new_packet(queued, static_cast<int>(size)) < 0 ||
            av_packet_copy_props(queued, packet) < 0) {
            if (queued != nullptr) av_packet_free(&queued);
            errorCode_.store(AVERROR(ENOMEM), std::memory_order_release);
            endOfStream_.store(true, std::memory_order_release);
            break;
        }
        if (size > 0) std::memcpy(queued->data, packet->data, size);
        queued->stream_index = packet->stream_index;
        av_packet_unref(packet);
        {
            std::lock_guard lock(mutex_);
            if (stopRequested_.load(std::memory_order_acquire)) {
                av_packet_free(&queued);
                break;
            }
            queue_.push_back(queued);
            queued = nullptr;
            queuedBytes_ += size;
            packetsRead_.fetch_add(1, std::memory_order_relaxed);
            bytesRead_.fetch_add(size, std::memory_order_relaxed);
            windowBytes_ += size;
            const auto now = NowTickMs();
            const auto elapsed = now >= windowStartTickMs_ ? now - windowStartTickMs_ : 0;
            if (elapsed >= 1000) {
                const auto bps = BytesToBps(windowBytes_, elapsed);
                measuredBps_.store(bps, std::memory_order_relaxed);
                auto peak = peakBps_.load(std::memory_order_relaxed);
                while (bps > peak &&
                    !peakBps_.compare_exchange_weak(peak, bps,
                        std::memory_order_relaxed, std::memory_order_relaxed)) {
                }
                windowBytes_ = 0;
                windowStartTickMs_ = now;
            }
            if (trace && packetsRead_.load(std::memory_order_relaxed) <= 8)
                std::fprintf(stderr, "[io-pump] packet stream=%d size=%zu key=%d pts=%lld dts=%lld queued=%zu\n",
                    queue_.back()->stream_index, static_cast<std::size_t>(queue_.back()->size),
                    (queue_.back()->flags & AV_PKT_FLAG_KEY) != 0,
                    static_cast<long long>(queue_.back()->pts),
                    static_cast<long long>(queue_.back()->dts), queue_.size());
        }
        dataCondition_.notify_all();
    }
    if (packet != nullptr) av_packet_free(&packet);
    {
        const auto now = NowTickMs();
        if (now > startTickMs_) {
            measuredBps_.store(BytesToBps(
                bytesRead_.load(std::memory_order_relaxed), now - startTickMs_),
                std::memory_order_relaxed);
        }
    }
    running_.store(false, std::memory_order_release);
    dataCondition_.notify_all();
    spaceCondition_.notify_all();
}

void InputPacketPump::ClearQueue() noexcept {
    std::deque<AVPacket*> pending;
    {
        std::lock_guard lock(mutex_);
        pending.swap(queue_);
        queuedBytes_ = 0;
    }
    for (auto* packet : pending) {
        if (packet != nullptr) av_packet_free(&packet);
    }
    dataCondition_.notify_all();
    spaceCondition_.notify_all();
}

} // namespace FFFPlayer::Io
