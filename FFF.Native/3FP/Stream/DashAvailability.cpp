#include "pch.h"
#include "3FP/Stream/DashAvailability.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace FFFPlayer::Stream {
namespace {
bool ParseIsoUtc(const std::string& text, std::int64_t& epochSeconds) noexcept {
    if (text.empty()) return false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    double second = 0;
    const int fields = std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%lf",
        &year, &month, &day, &hour, &minute, &second);
    if (fields != 6 || year < 1970 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second >= 61)
        return false;
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = static_cast<int>(std::floor(second));
    const auto result = _mkgmtime(&value);
    if (result < 0) return false;
    epochSeconds = static_cast<std::int64_t>(result);
    return true;
}
} // namespace

std::int64_t DashAvailability::ParseAvailabilityStart(
    const DashManifest& manifest) noexcept {
    std::int64_t epoch = 0;
    if (!ParseIsoUtc(manifest.availabilityStartTime, epoch)) return -1;
    return epoch;
}

bool DashAvailability::IsAvailable(const DashManifest& manifest,
    const std::size_t periodIndex, const double segmentStartSeconds,
    const double segmentDurationSeconds, const std::int64_t nowEpochSeconds) noexcept {
    if (!manifest.dynamic) return true;
    const auto availabilityStart = ParseAvailabilityStart(manifest);
    if (availabilityStart < 0) return true; // unknown wall clock: fail open
    const auto periodStart = periodIndex < manifest.periods.size()
        ? manifest.periods[periodIndex].startSeconds : 0.0;
    const auto wallStart = static_cast<double>(availabilityStart) +
        periodStart + segmentStartSeconds;
    if (wallStart > static_cast<double>(nowEpochSeconds) + 1.0) return false;
    if (manifest.timeShiftBufferDepthSeconds > 0) {
        const auto oldest = static_cast<double>(nowEpochSeconds) -
            manifest.timeShiftBufferDepthSeconds;
        if (wallStart + segmentDurationSeconds < oldest) return false;
    }
    return true;
}

bool DashAvailability::RefreshDue(const DashManifest& manifest,
    const std::int64_t lastRefreshEpochSeconds,
    const std::int64_t nowEpochSeconds) noexcept {
    if (!manifest.dynamic || manifest.minimumUpdatePeriodSeconds <= 0) return false;
    if (lastRefreshEpochSeconds <= 0) return true;
    return static_cast<double>(nowEpochSeconds - lastRefreshEpochSeconds) >=
        manifest.minimumUpdatePeriodSeconds;
}

} // namespace FFFPlayer::Stream
