#pragma once

#include "3FP/Api/FFF.Player.AudioTap.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

struct AVFrame;

// Bounded single-producer / single-consumer ring of raw decoded audio frames:
// the ASR (real-time subtitle) data source. Design and ABI:
// docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md section 4.
//
// The producer is exactly one thread -- the session worker that calls
// PlayerWasapiRenderer::Enqueue -- and it never blocks, never allocates in
// steady state, and never takes a lock while the ring has room. The consumer
// is the host's pump thread (via FFF3FP_ReadAudioTap); an internal mutex
// serializes consumers because the HOST may call from more than one thread,
// which the audio path must not care about.
//
// Drop-oldest on overflow: a consumer that stalls loses the oldest blocks and
// sees blocksDropped climb. That is the intended trade -- back-pressure must
// never reach the audio path.
class AsrAudioTap final {
public:
    static constexpr std::uint32_t MaximumChannels = FFF3FP_AUDIO_TAP_MAX_CHANNELS;

    // What the ring stores ahead of every PCM payload. Deliberately NOT the
    // ABI struct: that one carries consumer-side fields (bytesWritten /
    // requiredBytes) and must be free to evolve without touching ring layout.
    struct Header {
        std::uint64_t generation;
        std::int64_t position100ns;
        std::uint32_t frames;
        std::uint32_t sampleRate;
        std::uint32_t channels;
        std::int32_t positions[MaximumChannels];
        std::int32_t sampleFormat;
        std::uint32_t bytes;   // PCM bytes following this header
    };

    enum class ConsumeResult {
        Empty,           // ring had nothing; block metadata zeroed
        Ok,              // one block copied out and removed
        BufferTooSmall,  // a block is there but dst/capacity cannot hold it
    };

    // Not noexcept on purpose: the ring allocation can fail, and the caller
    // (PlayerSession::EnableAudioTap) must be able to turn that into
    // NativeFailure instead of a terminate.
    explicit AsrAudioTap(std::size_t ringCapacityBytes);
    ~AsrAudioTap();

    AsrAudioTap(const AsrAudioTap&) = delete;
    AsrAudioTap& operator=(const AsrAudioTap&) = delete;

    // ---- producer side: session worker thread only ----
    // Copies one decoded frame (header + PCM) into the ring. Frames wider than
    // MaximumChannels, or with an unreadable layout, are skipped and counted in
    // blocksDropped. `position100ns` is the media position the session already
    // computed (AV_NOPTS_VALUE when the frame is untimed).
    void Produce(const AVFrame* frame, std::int64_t position100ns) noexcept;

    // ---- generation stamping: session worker thread only ----
    // Called on open and on every successful seek: drops everything buffered.
    // The kernel guarantees seek invalidation itself rather than trusting the
    // host or the worker to notice a generation change.
    void NoteGeneration(std::uint64_t generation) noexcept;

    // ---- consumer side: host pump thread (FFF3FP_ReadAudioTap) ----
    // Copies at most one block out; metadata lands in `block` in every
    // non-Empty outcome, PCM in `dst` only on Ok.
    ConsumeResult Consume(void* dst, std::size_t capacity,
        FFF3FPAudioTapBlock& block) noexcept;

    // Atomic; safe from any thread.
    std::uint64_t BlocksProduced() const noexcept {
        return blocksProduced_.load(std::memory_order_relaxed);
    }
    std::uint64_t BlocksDropped() const noexcept {
        return blocksDropped_.load(std::memory_order_relaxed);
    }
    std::size_t BufferedBytes() const noexcept;

private:
    std::size_t FreeBytes() const noexcept;
    // Evicts oldest whole blocks until `needed` bytes are free. consumeMutex_
    // must be held: eviction moves the read cursor the consumer also moves.
    void DropOldestLocked(std::size_t needed) noexcept;
    void WriteAt(std::size_t index, const void* src, std::size_t bytes) noexcept;
    void ReadAt(std::size_t index, void* dst, std::size_t bytes) const noexcept;

    std::vector<std::uint8_t> ring_;
    std::size_t capacity_;
    // Byte cursors that only move forward; physical offset is index & mask_
    // (capacity_ is always a power of two). The pair is a lock-free SPSC
    // handoff: the producer only stores writeIdx_, the consumer only readIdx_.
    // The consumer's mutex exists because the host may call Consume from more
    // than one thread; the producer takes it only on the slow path, where
    // evicting the oldest blocks would otherwise race the reader.
    std::atomic<std::size_t> writeIdx_{0};
    std::atomic<std::size_t> readIdx_{0};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> blocksProduced_{0};
    std::atomic<std::uint64_t> blocksDropped_{0};
    mutable std::mutex consumeMutex_;
};
