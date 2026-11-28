#include "pch.h"
#include "3FP/Stream/DashSegmentSource.h"

#include <windows.h>

#include <cstdio>
#include <vector>

namespace FFFPlayer::Stream {
namespace {
constexpr std::size_t MaxSegmentBytes = 64ull * 1024 * 1024;

bool ParseRange(const std::string& text, std::uint64_t& start,
    std::uint64_t& length) noexcept {
    unsigned long long first = 0;
    unsigned long long last = 0;
    char trailing = 0;
    if (std::sscanf(text.c_str(), "%llu-%llu%c", &first, &last, &trailing) != 2)
        return false;
    if (last < first) return false;
    start = first;
    length = last - first + 1;
    return true;
}
} // namespace

DashSegmentSource::DashSegmentSource(DashSegmentPlan plan,
    FFFPlayer::Io::IoOptions options, const std::uint64_t cacheBytes) noexcept
    : plan_(std::move(plan)), options_(options), cache_(cacheBytes) {}

std::unique_ptr<DashSegmentSource> DashSegmentSource::Create(DashSegmentPlan plan,
    FFFPlayer::Io::IoOptions options, const std::uint64_t cacheBytes,
    std::string& error) noexcept {
    if (plan.segments.empty()) {
        error = "The DASH plan has no media segments.";
        return nullptr;
    }
    try {
        return std::unique_ptr<DashSegmentSource>(new DashSegmentSource(
            std::move(plan), options, cacheBytes));
    } catch (...) {
        error = "Could not allocate the DASH segment source.";
        return nullptr;
    }
}

bool DashSegmentSource::FetchUrl(const std::string& url, const std::string& rangeText,
    const std::uint64_t cacheKey, std::string& error) noexcept {
    std::uint64_t rangeStart = 0;
    std::uint64_t rangeLength = 0;
    const bool ranged = !rangeText.empty();
    if (ranged) {
        if (!ParseRange(rangeText, rangeStart, rangeLength)) {
            error = "DASH mediaRange is malformed.";
            return false;
        }
        // Reject before creating a request: a malformed manifest must not be
        // able to make the player download gigabytes just to discover the cap.
        if (rangeLength > MaxSegmentBytes) {
            error = "DASH mediaRange exceeds the segment byte ceiling.";
            return false;
        }
    }
    FFFPlayer::Io::Result result{};
    auto source = FFFPlayer::Io::HttpMediaSource::Create(url, options_, result,
        error);
    if (source == nullptr) return false;
    if (ranged) {
        const auto seek = source->Seek(static_cast<std::int64_t>(rangeStart), options_);
        if (seek.status != FFFPlayer::Io::Status::Ok) {
            error = "DASH mediaRange seek failed.";
            return false;
        }
    }
    std::vector<std::uint8_t> bytes;
    if (ranged) {
        bytes.reserve(static_cast<std::size_t>(rangeLength));
    }
    const auto sourceSize = source->Size();
    if (!ranged && sourceSize > static_cast<std::int64_t>(MaxSegmentBytes)) {
        error = "DASH segment size exceeds the segment byte ceiling.";
        return false;
    }
    std::uint32_t readBytes = 0;
    std::int64_t readPosition = ranged ? static_cast<std::int64_t>(rangeStart) : 0;
    int retries = 0;
    for (;;) {
        std::vector<std::uint8_t> chunk(64 * 1024);
        const auto read = source->Read(chunk.data(),
            static_cast<std::uint32_t>(chunk.size()), readBytes, options_);
        if (read.status == FFFPlayer::Io::Status::Ok) {
            if (readBytes == 0) {
                error = "DASH segment read made no progress.";
                return false;
            }
            if (readBytes > MaxSegmentBytes - bytes.size()) {
                error = "DASH segment grew beyond the segment byte ceiling.";
                return false;
            }
            bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + readBytes);
            readPosition += static_cast<std::int64_t>(readBytes);
            if (ranged && bytes.size() >= rangeLength) break;
            if (!ranged && sourceSize >= 0 &&
                bytes.size() >= static_cast<std::size_t>(sourceSize)) break;
            continue;
        }
        if (read.status == FFFPlayer::Io::Status::Eof) break;
        if (read.status == FFFPlayer::Io::Status::Retryable ||
            read.status == FFFPlayer::Io::Status::WouldBlock) {
            if (++retries > 4) {
                error = "DASH segment read did not make progress.";
                return false;
            }
            // Only a fresh Range request can recover a dead connection. The
            // reader keeps its old request handle otherwise, so retrying Read
            // alone would just fail against the same socket.
            const auto seek = source->Seek(readPosition, options_);
            if (seek.status != FFFPlayer::Io::Status::Ok) {
                error = "DASH segment retry seek failed.";
                return false;
            }
            Sleep(static_cast<DWORD>(100 * retries));
            continue;
        }
        error = "DASH segment read failed.";
        return false;
    }
    if (ranged && bytes.size() < rangeLength) {
        error = "DASH segment ended before the declared mediaRange.";
        return false;
    }
    if (!cache_.Put(cacheKey, bytes)) {
        error = "DASH segment exceeds the segment cache capacity.";
        return false;
    }
    return true;
}

bool DashSegmentSource::FetchInitialization(std::string& error) noexcept {
    if (cache_.Get(0)) return true;
    if (plan_.initializationUrl.empty()) return true;
    return FetchUrl(plan_.initializationUrl, {}, 0, error);
}

bool DashSegmentSource::FetchSegment(const std::size_t index,
    std::string& error) noexcept {
    if (index >= plan_.segments.size()) {
        error = "DASH segment index is outside the plan.";
        return false;
    }
    if (cache_.Get(index + 1)) return true;
    return FetchUrl(plan_.segments[index].url, plan_.segments[index].mediaRange,
        index + 1, error);
}

bool DashSegmentSource::Prefetch(const std::size_t count,
    std::string& error) noexcept {
    if (!FetchInitialization(error)) return false;
    const auto wanted = std::min(count, plan_.segments.size());
    for (std::size_t i = 0; i < wanted; ++i) {
        if (!FetchSegment(i, error)) return false;
    }
    return true;
}

} // namespace FFFPlayer::Stream
