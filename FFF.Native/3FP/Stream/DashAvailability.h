#pragma once

#include "3FP/Stream/DashManifest.h"

#include <cstdint>

// P4 availability gate for dynamic MPDs. A segment is admitted only after its
// wall-clock availability time and before the time-shift buffer falls off the
// live edge. Static MPDs are always available. Timestamps are UTC epoch seconds;
// this keeps the gate deterministic and independent of the host's timezone.
namespace FFFPlayer::Stream {

class DashAvailability final {
public:
    static std::int64_t ParseAvailabilityStart(const DashManifest& manifest) noexcept;
    static bool IsAvailable(const DashManifest& manifest, std::size_t periodIndex,
        double segmentStartSeconds, double segmentDurationSeconds,
        std::int64_t nowEpochSeconds) noexcept;
    static bool RefreshDue(const DashManifest& manifest, std::int64_t lastRefreshEpochSeconds,
        std::int64_t nowEpochSeconds) noexcept;
};

} // namespace FFFPlayer::Stream
