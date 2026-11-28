#include "pch.h"
#include "3FP/Stream/HlsLiveWindow.h"

#include <algorithm>

namespace FFFPlayer::Stream {

std::uint32_t HlsLiveWindow::Update(const HlsPlaylist& playlist,
    const std::uint64_t nowMs) noexcept {
    if (!playlist.valid || playlist.master || playlist.segments.empty()) return 0;
    const auto first = playlist.segments.front().sequence;
    const auto last = playlist.segments.back().sequence;
    endList_ = playlist.endList;
    targetDurationSeconds_ = playlist.targetDurationSeconds;
    lastRefreshMs_ = nowMs;
    std::uint32_t added = 0;
    if (segments_.empty()) {
        nextSequence_ = first;
        for (const auto& segment : playlist.segments) {
            if (segment.sequence < nextSequence_) continue;
            segments_.push_back(segment);
            ++added;
        }
    } else if (first > nextSequence_ + 1) {
        // The server skipped past our cursor; honour the discontinuity instead
        // of inventing the missing sequence numbers.
        segments_.clear();
        nextSequence_ = first;
        for (const auto& segment : playlist.segments) {
            segments_.push_back(segment);
            ++added;
        }
    } else {
        const auto lastRetained = segments_.empty() ? 0 : segments_.back().sequence;
        for (const auto& segment : playlist.segments) {
            if (segment.sequence <= lastRetained || segment.sequence < nextSequence_) continue;
            segments_.push_back(segment);
            ++added;
        }
    }
    while (segments_.size() > retainedSegments_) {
        segments_.erase(segments_.begin());
    }
    if (!segments_.empty() && nextSequence_ < segments_.front().sequence)
        nextSequence_ = segments_.front().sequence;
    (void)last;
    return added;
}

bool HlsLiveWindow::RefreshDue(const std::uint64_t nowMs) const noexcept {
    if (endList_ || targetDurationSeconds_ == 0) return false;
    if (lastRefreshMs_ == 0) return true;
    const auto interval = std::max<std::uint64_t>(
        static_cast<std::uint64_t>(targetDurationSeconds_) * 500ull, 500ull);
    return nowMs >= lastRefreshMs_ + interval;
}

std::optional<HlsSegment> HlsLiveWindow::NextSegment() const noexcept {
    for (const auto& segment : segments_) {
        if (segment.sequence == nextSequence_) return segment;
        if (segment.sequence > nextSequence_) break;
    }
    return std::nullopt;
}

void HlsLiveWindow::Advance() noexcept {
    if (NextSegment().has_value()) ++nextSequence_;
}

void HlsLiveWindow::Reset() noexcept {
    segments_.clear();
    nextSequence_ = 0;
    lastRefreshMs_ = 0;
    targetDurationSeconds_ = 0;
    endList_ = false;
}

bool HlsLiveWindow::FindSegmentForTime(const double seconds,
    std::size_t& index) const noexcept {
    double cursor = 0;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const auto duration = segments_[i].durationSeconds;
        if (duration <= 0) continue;
        if (seconds >= cursor && seconds < cursor + duration) {
            index = i;
            return true;
        }
        cursor += duration;
    }
    return false;
}

HlsPrefetchPlan HlsLiveWindow::PlanPrefetch(const std::uint32_t rttMs,
    const std::uint64_t goodputBps, const std::uint64_t nowMs,
    const std::uint32_t targetDurationSeconds) const noexcept {
    HlsPrefetchPlan plan;
    plan.refreshDue = RefreshDue(nowMs);
    const auto target = targetDurationSeconds != 0
        ? targetDurationSeconds : targetDurationSeconds_;
    if (target == 0 || endList_) return plan;
    const auto leadMs = static_cast<std::uint64_t>(target) * 1000ull;
    const auto byRtt = std::max<std::uint64_t>(1,
        leadMs / std::max<std::uint64_t>(rttMs, 50ull));
    plan.segmentCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(byRtt, 6ull));
    const auto bytesPerSegment = goodputBps == 0 ? 0
        : goodputBps * static_cast<std::uint64_t>(target) / 8ull;
    plan.estimatedBytes = bytesPerSegment * plan.segmentCount;
    return plan;
}

int HlsLiveWindow::SelectVariant(const std::vector<HlsVariant>& variants,
    const std::uint64_t goodputBps, const std::uint64_t currentBandwidth) noexcept {
    if (variants.empty()) return -1;
    // Up only when measured goodput leaves 1.3x headroom over the next rung.
    // The old 0.8 budget let the selector sit on a rung it could not sustain
    // through a burst, which is exactly the D8/D9 oscillation case.
    const auto budget = goodputBps == 0 ? 0 : goodputBps * 10 / 13;
    int best = -1;
    std::uint64_t bestBandwidth = 0;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto bandwidth = variants[i].bandwidth;
        if (budget != 0 && bandwidth > budget) continue;
        if (best < 0 || bandwidth > bestBandwidth) {
            best = static_cast<int>(i);
            bestBandwidth = bandwidth;
        }
    }
    if (best < 0) {
        // Nothing fits the measured goodput: stay on the lowest advertised rung
        // rather than selecting an impossible one.
        best = 0;
        for (std::size_t i = 1; i < variants.size(); ++i) {
            if (variants[i].bandwidth < variants[best].bandwidth) best = static_cast<int>(i);
        }
        return best;
    }
    if (currentBandwidth != 0) {
        for (std::size_t i = 0; i < variants.size(); ++i) {
            if (variants[i].bandwidth == currentBandwidth &&
                currentBandwidth <= budget &&
                currentBandwidth * 13ull >= bestBandwidth * 10ull) {
                return static_cast<int>(i);
            }
        }
    }
    return best;
}

int HlsLiveWindow::SelectVariantForBuffer(const std::vector<HlsVariant>& variants,
    const std::uint64_t goodputBps, const std::uint64_t currentBandwidth,
    const std::uint64_t cacheBytes, const std::uint64_t resumeBytes,
    const std::uint64_t lowBytes, const bool segmentTimeout) noexcept {
    if (variants.empty()) return -1;
    int current = -1;
    for (std::size_t i = 0; i < variants.size(); ++i) {
        if (variants[i].bandwidth == currentBandwidth) current = static_cast<int>(i);
    }
    if (segmentTimeout || (lowBytes != 0 && cacheBytes < lowBytes)) {
        if (current > 0) return current - 1;
        return 0;
    }
    if (current >= 0 && resumeBytes != 0 && cacheBytes < resumeBytes) return current;
    return SelectVariant(variants, goodputBps, currentBandwidth);
}

} // namespace FFFPlayer::Stream
