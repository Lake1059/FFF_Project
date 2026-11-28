#pragma once

extern "C" {
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
}

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// P2 Input thread: the only thread that calls av_read_frame on a session's
// AVFormatContext. The controller/session worker keeps owning decode and
// presentation, so a blocking local disk or a 30 s network read cannot freeze
// Pause/Seek/Close command handling.
//
// Contract:
// - Start() installs an AVIO interrupt callback and starts one reader.
// - Stop() is synchronous: it signals, wakes the reader, joins, and clears the
//   queue. The owner must call Stop() before av_seek_frame, avformat_close_input,
//   or any other operation on the same AVFormatContext.
// - TryPop() moves the front packet into the caller's AVPacket. The caller owns
//   it and must unref it.
// - The queue is bounded by bytes. A full queue stops reading, which is the
//   forward packet-cache admission limit; it never grows without a budget.
namespace FFFPlayer::Io {

class InputPacketPump final {
public:
    using Interrupt = std::function<int()>;

    InputPacketPump() noexcept = default;
    ~InputPacketPump();
    InputPacketPump(const InputPacketPump&) = delete;
    InputPacketPump& operator=(const InputPacketPump&) = delete;

    bool Start(AVFormatContext* format, std::size_t maxBytes, Interrupt externalInterrupt) noexcept;
    void Stop() noexcept;

    // P0 VBR: the compressed-byte window is a live target, not a fixed 12 MiB.
    // Growing wakes the reader immediately; shrinking takes effect as the queue
    // drains naturally and never discards already-demuxed packets.
    void SetMaxBytes(std::size_t maxBytes) noexcept;

    // Moves one packet out. False means the queue is empty; inspect EndOfStream()
    // and ErrorCode() to distinguish "wait" from "finished" and "failed".
    bool TryPop(AVPacket& packet) noexcept;

    bool Running() const noexcept { return running_.load(std::memory_order_acquire); }
    bool EndOfStream() const noexcept { return endOfStream_.load(std::memory_order_acquire); }
    int ErrorCode() const noexcept { return errorCode_.load(std::memory_order_acquire); }
    std::uint64_t PacketsRead() const noexcept { return packetsRead_.load(std::memory_order_relaxed); }
    std::uint64_t BytesRead() const noexcept { return bytesRead_.load(std::memory_order_relaxed); }
    // Average/peak compressed-byte rate observed by this pump. The peak is a
    // conservative one-second bucket, used when a segment-level p95 is not
    // available (for example a raw HTTP byte stream).
    std::uint64_t MeasuredBps() const noexcept { return measuredBps_.load(std::memory_order_relaxed); }
    std::uint64_t PeakBps() const noexcept { return peakBps_.load(std::memory_order_relaxed); }
    std::uint64_t QueuedBytes() const noexcept;
    std::size_t QueuedPackets() const noexcept;
    std::size_t MaxBytes() const noexcept { return maxBytes_.load(std::memory_order_relaxed); }

private:
    static int InterruptCallback(void* opaque) noexcept;
    void Run() noexcept;
    void ClearQueue() noexcept;

    AVFormatContext* format_ = nullptr;
    AVIOInterruptCB previousInterrupt_{};
    Interrupt externalInterrupt_;
    mutable std::mutex mutex_;
    std::condition_variable dataCondition_;
    std::condition_variable spaceCondition_;
    std::deque<AVPacket*> queue_;
    std::size_t queuedBytes_ = 0;
    std::atomic<std::size_t> maxBytes_{0};
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> endOfStream_{false};
    std::atomic<int> errorCode_{0};
    std::atomic<std::uint64_t> packetsRead_{0};
    std::atomic<std::uint64_t> bytesRead_{0};
    std::atomic<std::uint64_t> measuredBps_{0};
    std::atomic<std::uint64_t> peakBps_{0};
    std::uint64_t startTickMs_ = 0;
    std::uint64_t windowStartTickMs_ = 0;
    std::uint64_t windowBytes_ = 0;
};

} // namespace FFFPlayer::Io
