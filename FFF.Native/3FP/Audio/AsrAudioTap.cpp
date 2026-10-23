#include "pch.h"
#include "3FP/Audio/AsrAudioTap.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstring>

AsrAudioTap::AsrAudioTap(const std::size_t ringCapacityBytes) {
    auto capacity = ringCapacityBytes;
    if (capacity < FFF3FP_AUDIO_TAP_MIN_RING_BYTES) capacity = FFF3FP_AUDIO_TAP_MIN_RING_BYTES;
    if (capacity > FFF3FP_AUDIO_TAP_MAX_RING_BYTES) capacity = FFF3FP_AUDIO_TAP_MAX_RING_BYTES;
    // Round up to a power of two so the hot path masks instead of divides.
    std::size_t pow2 = 1;
    while (pow2 < capacity) pow2 <<= 1;
    capacity_ = pow2;
    ring_.assign(capacity_, 0);   // may throw: caller reports NativeFailure
}

AsrAudioTap::~AsrAudioTap() = default;

std::size_t AsrAudioTap::FreeBytes() const noexcept {
    const auto read = readIdx_.load(std::memory_order_acquire);
    const auto written = writeIdx_.load(std::memory_order_acquire);
    return capacity_ - (written - read);
}

void AsrAudioTap::DropOldestLocked(const std::size_t needed) noexcept {
    while (capacity_ - (writeIdx_.load(std::memory_order_relaxed) -
               readIdx_.load(std::memory_order_relaxed)) < needed) {
        const auto read = readIdx_.load(std::memory_order_relaxed);
        Header header{};
        ReadAt(read, &header, sizeof(Header));
        // A stored block is always header + payload, so the total is always
        // positive: this loop cannot spin on a zero-sized block.
        readIdx_.store(read + sizeof(Header) + header.bytes, std::memory_order_release);
        blocksDropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void AsrAudioTap::WriteAt(const std::size_t index, const void* const src,
    const std::size_t bytes) noexcept {
    if (bytes == 0) return;
    const auto mask = capacity_ - 1;
    auto offset = index & mask;
    auto remaining = bytes;
    auto source = static_cast<const std::uint8_t*>(src);
    while (remaining > 0) {
        const auto chunk = std::min(remaining, capacity_ - offset);
        std::memcpy(ring_.data() + offset, source, chunk);
        source += chunk;
        offset = 0;
        remaining -= chunk;
    }
}

void AsrAudioTap::ReadAt(const std::size_t index, void* const dst,
    const std::size_t bytes) const noexcept {
    if (bytes == 0) return;
    const auto mask = capacity_ - 1;
    auto offset = index & mask;
    auto remaining = bytes;
    auto target = static_cast<std::uint8_t*>(dst);
    while (remaining > 0) {
        const auto chunk = std::min(remaining, capacity_ - offset);
        std::memcpy(target, ring_.data() + offset, chunk);
        target += chunk;
        offset = 0;
        remaining -= chunk;
    }
}

void AsrAudioTap::Produce(const AVFrame* const frame,
    const std::int64_t position100ns) noexcept {
    if (frame == nullptr || frame->nb_samples <= 0 ||
        frame->ch_layout.nb_channels <= 0 || capacity_ == 0)
        return;
    const auto channels = static_cast<std::uint32_t>(frame->ch_layout.nb_channels);
    const auto payloadSigned = av_samples_get_buffer_size(nullptr,
        frame->ch_layout.nb_channels, frame->nb_samples,
        static_cast<AVSampleFormat>(frame->format), 1);
    if (payloadSigned <= 0) return;   // unreadable layout: not the tap's problem
    const auto payload = static_cast<std::size_t>(payloadSigned);
    const auto total = sizeof(Header) + payload;
    if (channels > MaximumChannels || total > capacity_) {
        // Too wide for the ABI, or a single frame larger than the whole ring:
        // skip and count. Never block, never resize, never fail the frame.
        blocksDropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (total > FreeBytes()) {
        // Slow path: evict oldest blocks. The consumer's mutex is required
        // here because eviction moves the read cursor the consumer also moves.
        std::lock_guard lock(consumeMutex_);
        DropOldestLocked(total);
    }
    Header header{};
    header.generation = generation_.load(std::memory_order_relaxed);
    header.position100ns = position100ns;
    header.frames = static_cast<std::uint32_t>(frame->nb_samples);
    header.sampleRate = frame->sample_rate > 0
        ? static_cast<std::uint32_t>(frame->sample_rate) : 0u;
    header.channels = channels;
    for (std::uint32_t index = 0; index < MaximumChannels; ++index) {
        header.positions[index] = index < channels
            ? static_cast<std::int32_t>(av_channel_layout_channel_from_index(
                  &frame->ch_layout, static_cast<int>(index)))
            : -1;
    }
    header.sampleFormat = static_cast<std::int32_t>(frame->format);
    header.bytes = static_cast<std::uint32_t>(payload);
    const auto start = writeIdx_.load(std::memory_order_relaxed);
    WriteAt(start, &header, sizeof(Header));
    if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format)) != 0) {
        // Planar: one plane per channel, back to back -- exactly the order
        // av_samples_get_buffer_size sized above.
        const auto bytesPerSample = av_get_bytes_per_sample(
            static_cast<AVSampleFormat>(frame->format));
        if (bytesPerSample > 0) {
            const auto planeBytes = static_cast<std::size_t>(frame->nb_samples) *
                static_cast<std::size_t>(bytesPerSample);
            for (int channel = 0; channel < frame->ch_layout.nb_channels; ++channel) {
                WriteAt(start + sizeof(Header) +
                    static_cast<std::size_t>(channel) * planeBytes,
                    frame->extended_data[channel], planeBytes);
            }
        }
    } else if (payload > 0) {
        WriteAt(start + sizeof(Header), frame->extended_data[0], payload);
    }
    writeIdx_.store(start + total, std::memory_order_release);
    blocksProduced_.fetch_add(1, std::memory_order_relaxed);
}

void AsrAudioTap::NoteGeneration(const std::uint64_t generation) noexcept {
    std::lock_guard lock(consumeMutex_);
    generation_.store(generation, std::memory_order_relaxed);
    // Drop everything buffered: a seek (or a new file) invalidates the
    // timeline, and blocks stamped with the previous generation must not
    // reach a consumer that already re-anchored.
    readIdx_.store(0, std::memory_order_relaxed);
    writeIdx_.store(0, std::memory_order_relaxed);
}

AsrAudioTap::ConsumeResult AsrAudioTap::Consume(void* const dst,
    const std::size_t capacity, FFF3FPAudioTapBlock& block) noexcept {
    std::lock_guard lock(consumeMutex_);
    block.size = sizeof(FFF3FPAudioTapBlock);
    block.version = FFF3FP_AUDIO_TAP_VERSION;
    block.bytesWritten = 0;
    block.requiredBytes = 0;
    const auto written = writeIdx_.load(std::memory_order_acquire);
    const auto read = readIdx_.load(std::memory_order_relaxed);
    if (read == written || capacity_ == 0) return ConsumeResult::Empty;
    Header header{};
    ReadAt(read, &header, sizeof(Header));
    block.generation = header.generation;
    block.position100ns = header.position100ns;
    block.frames = header.frames;
    block.sampleRate = header.sampleRate;
    block.channels = header.channels;
    for (std::uint32_t index = 0; index < MaximumChannels; ++index)
        block.positions[index] = header.positions[index];
    block.sampleFormat = header.sampleFormat;
    block.requiredBytes = header.bytes;
    if (header.bytes > capacity) return ConsumeResult::BufferTooSmall;
    ReadAt(read + sizeof(Header), dst, header.bytes);
    block.bytesWritten = header.bytes;
    readIdx_.store(read + sizeof(Header) + header.bytes, std::memory_order_release);
    return ConsumeResult::Ok;
}

std::size_t AsrAudioTap::BufferedBytes() const noexcept {
    // Read the read cursor first: both only move forward, so this difference
    // can never read as negative the way the other order could.
    const auto read = readIdx_.load(std::memory_order_acquire);
    const auto written = writeIdx_.load(std::memory_order_acquire);
    return written - read;
}
