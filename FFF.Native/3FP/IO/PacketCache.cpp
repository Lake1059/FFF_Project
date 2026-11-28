#include "pch.h"
#include "3FP/IO/PacketCache.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace FFFPlayer::Io {
namespace {
PacketPtr ClonePacket(const AVPacket& source) noexcept {
    auto* clone = av_packet_clone(&source);
    if (clone == nullptr) return {};
    return PacketPtr(clone, [](AVPacket* value) {
        if (value != nullptr) av_packet_free(&value);
    });
}
} // namespace

PacketCache::PacketCache(const std::uint64_t maxBytes) noexcept
    : maxBytes_(std::max<std::uint64_t>(maxBytes, 1)) {}

PacketCache::~PacketCache() = default;

void PacketCache::Reset(const std::uint64_t generation) noexcept {
    entries_.clear();
    bytes_ = 0;
    keyframeCount_ = 0;
    generation_ = generation == 0 ? 1 : generation;
}

void PacketCache::SetCapacity(const std::uint64_t maxBytes) noexcept {
    maxBytes_ = std::max<std::uint64_t>(maxBytes, 1);
    EvictToCapacity();
}

bool PacketCache::Push(const AVPacket& packet, const int streamIndex,
    const std::uint64_t generation) noexcept {
    if (generation != generation_ || streamIndex < 0 || packet.size < 0) return false;
    const auto size = static_cast<std::uint64_t>(packet.size);
    if (size > maxBytes_) return false;
    auto clone = ClonePacket(packet);
    if (!clone) return false;
    const bool keyframe = (packet.flags & AV_PKT_FLAG_KEY) != 0;
    entries_.push_back(Entry{std::move(clone), streamIndex, packet.pts, keyframe});
    bytes_ += size;
    if (keyframe) ++keyframeCount_;
    EvictToCapacity();
    return true;
}

void PacketCache::EvictToCapacity() noexcept {
    while (bytes_ > maxBytes_ && !entries_.empty()) {
        const auto& front = entries_.front();
        bytes_ -= static_cast<std::uint64_t>(front.packet ? front.packet->size : 0);
        if (front.keyframe && keyframeCount_ > 0) --keyframeCount_;
        entries_.erase(entries_.begin());
    }
}

bool PacketCache::FindKeyframeBefore(const int streamIndex,
    const std::int64_t timestampUs, std::int64_t& keyframeUs) const noexcept {
    bool found = false;
    std::int64_t best = (std::numeric_limits<std::int64_t>::min)();
    for (const auto& entry : entries_) {
        if (entry.streamIndex != streamIndex || !entry.keyframe || entry.pts > timestampUs) continue;
        if (!found || entry.pts > best) {
            best = entry.pts;
            found = true;
        }
    }
    if (found) keyframeUs = best;
    return found;
}

std::vector<PacketPtr> PacketCache::CopyFromKeyframe(const int streamIndex,
    const std::int64_t timestampUs, const std::uint32_t maxPackets) const {
    std::vector<PacketPtr> result;
    if (maxPackets == 0) return result;
    std::int64_t keyframeUs = 0;
    if (!FindKeyframeBefore(streamIndex, timestampUs, keyframeUs)) return result;

    bool collecting = false;
    for (const auto& entry : entries_) {
        if (entry.streamIndex != streamIndex) continue;
        if (!collecting && entry.pts == keyframeUs) collecting = true;
        if (!collecting) continue;
        if (entry.pts > timestampUs) break;
        result.push_back(entry.packet);
        if (result.size() >= maxPackets) break;
    }
    return result;
}

bool PacketCache::Covers(const int streamIndex, const std::int64_t timestampUs) const noexcept {
    std::int64_t keyframeUs = 0;
    if (!FindKeyframeBefore(streamIndex, timestampUs, keyframeUs)) return false;
    for (const auto& entry : entries_) {
        if (entry.streamIndex == streamIndex && entry.pts >= timestampUs) return true;
    }
    return false;
}

std::vector<PacketPtr> PacketCache::CopyFromKeyframeToLatest(const int streamIndex,
    const std::int64_t timestampUs, const std::uint32_t maxPackets) const {
    std::vector<PacketPtr> result;
    if (maxPackets == 0) return result;
    std::int64_t keyframeUs = 0;
    if (!FindKeyframeBefore(streamIndex, timestampUs, keyframeUs)) return result;

    bool collecting = false;
    for (const auto& entry : entries_) {
        if (!collecting) {
            if (entry.streamIndex != streamIndex || entry.pts != keyframeUs || !entry.keyframe)
                continue;
            collecting = true;
        }
        result.push_back(entry.packet);
        if (result.size() >= maxPackets) break;
    }
    return result;
}
} // namespace FFFPlayer::Io
