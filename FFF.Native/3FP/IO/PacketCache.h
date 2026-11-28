#pragma once

#include "3FP/IO/IoStatus.h"

extern "C" {
#include <libavcodec/packet.h>
}

#include <cstdint>
#include <memory>
#include <vector>

// P2 packet cache: forward packets plus a keyframe/GOP index. It owns cloned
// packets, never decoder references, and is session-worker affine. Generation
// is the cancellation boundary: a reset with a new generation makes every
// in-flight push from the old generation fail instead of contaminating the new
// seek.
namespace FFFPlayer::Io {

using PacketPtr = std::shared_ptr<AVPacket>;

class PacketCache final {
public:
    explicit PacketCache(std::uint64_t maxBytes = 64ull * 1024ull * 1024ull) noexcept;
    ~PacketCache();
    PacketCache(const PacketCache&) = delete;
    PacketCache& operator=(const PacketCache&) = delete;

    void Reset(std::uint64_t generation) noexcept;
    std::uint64_t Generation() const noexcept { return generation_; }
    void SetCapacity(std::uint64_t maxBytes) noexcept;

    bool Push(const AVPacket& packet, int streamIndex, std::uint64_t generation) noexcept;

    // Nearest keyframe at or before `timestampUs` on `streamIndex`. Returns
    // false when the cache has no such keyframe; callers must then seek the
    // demuxer instead of guessing a decode point.
    bool FindKeyframeBefore(int streamIndex, std::int64_t timestampUs,
        std::int64_t& keyframeUs) const noexcept;

    // Packets from the selected keyframe through `timestampUs` (inclusive),
    // capped by `maxPackets`. A missing keyframe yields an empty vector.
    std::vector<PacketPtr> CopyFromKeyframe(int streamIndex, std::int64_t timestampUs,
        std::uint32_t maxPackets) const;

    // True when a keyframe at/before `timestampUs` and a later packet on the
    // same stream are both retained, i.e. a seek to this point can be served
    // entirely from the cache instead of touching the demuxer.
    bool Covers(int streamIndex, std::int64_t timestampUs) const noexcept;

    // Interleaved packets from the nearest keyframe through the newest cached
    // packet. Audio/subtitle entries keep their original order, which the
    // player needs for a cache-backed seek replay.
    std::vector<PacketPtr> CopyFromKeyframeToLatest(int streamIndex,
        std::int64_t timestampUs, std::uint32_t maxPackets) const;

    std::uint64_t Bytes() const noexcept { return bytes_; }
    std::uint32_t Count() const noexcept { return static_cast<std::uint32_t>(entries_.size()); }
    std::uint32_t KeyframeCount() const noexcept { return keyframeCount_; }

private:
    struct Entry final {
        PacketPtr packet;
        int streamIndex = -1;
        std::int64_t pts = 0;
        bool keyframe = false;
    };
    void EvictToCapacity() noexcept;

    std::uint64_t maxBytes_;
    std::uint64_t bytes_ = 0;
    std::uint64_t generation_ = 1;
    std::uint32_t keyframeCount_ = 0;
    std::vector<Entry> entries_;
};

} // namespace FFFPlayer::Io
