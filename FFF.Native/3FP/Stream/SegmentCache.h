#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// P3 shared segment cache. HLS and DASH both feed it; it is keyed by a caller
// supplied key (HLS sequence / DASH segment number) so the two source adapters
// cannot accidentally share a byte range from different timelines.
namespace FFFPlayer::Stream {

class SegmentCache final {
public:
    using Bytes = std::shared_ptr<const std::vector<std::uint8_t>>;

    explicit SegmentCache(const std::uint64_t maxBytes = 128ull * 1024 * 1024)
        : maxBytes_(maxBytes == 0 ? 1 : maxBytes) {}

    void SetCapacity(const std::uint64_t maxBytes) noexcept {
        std::lock_guard lock(mutex_);
        maxBytes_ = maxBytes == 0 ? 1 : maxBytes;
        Evict();
    }

    bool Put(const std::uint64_t key, const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() > maxBytes_) return false;
        std::lock_guard lock(mutex_);
        auto value = std::make_shared<const std::vector<std::uint8_t>>(bytes);
        const auto existing = entries_.find(key);
        if (existing != entries_.end()) {
            used_ -= existing->second.size;
            existing->second = Entry{value, static_cast<std::uint64_t>(bytes.size())};
            used_ += bytes.size();
            Evict();
            return true;
        }
        entries_.emplace(key, Entry{value, static_cast<std::uint64_t>(bytes.size())});
        order_.push_back(key);
        used_ += bytes.size();
        Evict();
        return true;
    }

    Bytes Get(const std::uint64_t key) const {
        std::lock_guard lock(mutex_);
        const auto it = entries_.find(key);
        return it == entries_.end() ? Bytes{} : it->second.bytes;
    }

    void Clear() noexcept {
        std::lock_guard lock(mutex_);
        entries_.clear();
        order_.clear();
        used_ = 0;
    }

    std::uint64_t BytesUsed() const noexcept {
        std::lock_guard lock(mutex_);
        return used_;
    }

private:
    struct Entry final {
        Bytes bytes;
        std::uint64_t size = 0;
    };

    void Evict() noexcept {
        while (used_ > maxBytes_ && !order_.empty()) {
            const auto key = order_.front();
            order_.pop_front();
            const auto it = entries_.find(key);
            if (it == entries_.end()) continue;
            used_ -= it->second.size;
            entries_.erase(it);
        }
    }

    mutable std::mutex mutex_;
    std::uint64_t maxBytes_;
    std::uint64_t used_ = 0;
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::deque<std::uint64_t> order_;
};

} // namespace FFFPlayer::Stream
