// Passthrough lane state (the kernel side of the neutral seam).
//
// This class owns the *bookkeeping* of the seam and nothing else: the module's
// function table, the host preference, the per-track state machine, the cached
// format decision and the endpoint capability notes. It holds no FFmpeg types
// and no WASAPI types -- PlayerSession does the wiring (demuxer interception,
// decoder release, renderer switch, events), exactly as the IAMF bypass does,
// and the module does all packing and codec semantics. The kernel never
// interprets an encoded byte here either: the one GUID fallback step below is
// endpoint *negotiation* (choosing which SubFormat to ask the driver for), not
// stream handling, and it only maps a variant carrier onto the generic carrier
// of the same codec family the module itself already decided.
//
// Threading. InstallTable/SetMode run on the API thread; every track call runs
// on the session worker. One mutex guards the table and the mutable state, so
// a table swap can never land inside a module callback.

#pragma once

#include "3FP/Api/FFF.Player.AudioPassthrough.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class AudioPassthroughLane final {
public:
    // Why a track left (or never entered) the passthrough route. module-absent is
    // deliberately NOT emitted as a fallback event by the session -- a machine
    // without the module must look exactly like today's decode path -- but the
    // reason is still carried here so the remaining cases share one vocabulary.
    enum class Reason {
        None,
        ModuleAbsent,
        ModuleDeclined,
        EndpointUnsupported,
        ExclusiveBusy,
        PackError,
    };
    static const char* ReasonName(Reason reason) noexcept;

    AudioPassthroughLane() = default;
    ~AudioPassthroughLane();
    AudioPassthroughLane(const AudioPassthroughLane&) = delete;
    AudioPassthroughLane& operator=(const AudioPassthroughLane&) = delete;

    // ---- table + preference (API thread) ------------------------------------
    // Installs (api != nullptr) or clears (api == nullptr) the module's table.
    // A null clear calls destroy on the previously installed context. Fails on a
    // bad size/version or a table whose mandatory entries are missing, leaving
    // the previous table untouched (same "a bad call cannot half-apply" rule as
    // SetAudioRenderPlug).
    FFFResult InstallTable(const FFF3FPAudioPassthroughApi* api) noexcept;
    bool TableInstalled() const noexcept;
    // FFF3FP_PT_MODE_*. Takes effect at the next track open; the active track is
    // not re-opened mid-flight (contract header). Returns true when it changed.
    bool SetMode(std::uint32_t mode) noexcept;
    std::uint32_t Mode() const noexcept;

    // ---- per-track state machine (session worker) ---------------------------
    // True when the lane would even consider this track: table installed, mode
    // != OFF, codec in the accepted set. Guards every caller-side shortcut.
    bool WantsTrack(std::uint32_t codecId) noexcept;
    // Track bound, describe pending (the first complete frame triggers it).
    void Arm() noexcept;
    // Track closed / replaced without an event: back to Off.
    void Disarm() noexcept;
    // Engaged -> Armed after a renderer/device rebuild: the endpoint may have
    // changed, so the next packet re-describes and re-attempts the whole open.
    void RearmForReopen() noexcept;
    bool Armed() const noexcept;
    bool Engaged() const noexcept;
    bool Declined() const noexcept;

    // Runs the module's describe() once for this track and caches the decision.
    // firstFrame may be null when no complete frame is in hand yet; a module is
    // free to decline that (the session then falls back).
    bool DescribeTrack(const FFF3FPAudioPassthroughTrackInfo& info) noexcept;
    // Marks the decision accepted: the track is now owned by the lane.
    void MarkEngaged(std::uint32_t codecId) noexcept;
    // Endpoint accepted the decided format on a real device: remembered so
    // RebuildMediaInfo can report the session's measured capability set.
    void NoteEndpointSupport(std::uint32_t codecId) noexcept;

    // The cached decision (valid only while describe succeeded). One-step GUID
    // degrade for the endpoint negotiation retry: variant carrier -> generic
    // carrier of the same family. Returns false when nothing to degrade to.
    // Returns by value: the mutex guards the read, and a reference would outlive
    // it (Format may race DegradeCachedFormat from other threads).
    FFF3FPAudioPassthroughFormat Format() const noexcept;
    bool DegradeCachedFormat() noexcept;

    // Track facts for events / snapshot / media info. TrackFlags() reports the
    // module's cached decision bits verbatim; the kernel never interprets them.
    std::uint32_t EngagedCodec() const noexcept;
    std::string DisplayName() const noexcept;
    std::uint32_t TrackFlags() const noexcept;

    // Module reset (seek / track switch): clears the module's accumulation.
    void ModuleReset() noexcept;

    // Per-frame seam call. Returns the module's verdict: 0 with a burst (or an
    // empty one while the module accumulates), negative on a hard error with
    // *failReason = PackError.
    std::int32_t Pack(const std::uint8_t* frame, std::uint32_t frameSize,
        std::int64_t framePts100ns, std::uint8_t* outBurst, std::uint32_t burstCapacity,
        std::uint32_t* burstSize, std::int64_t* burstPts100ns,
        std::int64_t* burstDuration100ns, Reason* failReason) noexcept;

    // Session-measured endpoint capability set (codecs that actually engaged on
    // a device this session), for RebuildMediaInfo's "supported" list.
    std::vector<std::uint32_t> EndpointSupportedCodecs() const noexcept;

private:
    mutable std::mutex mutex_;
    FFF3FPAudioPassthroughApi table_{};
    bool tableInstalled_ = false;
    std::uint32_t mode_ = FFF3FP_PT_MODE_AUTO;

    enum class TrackState { Off, Armed, Engaged, Declined };
    TrackState trackState_ = TrackState::Off;
    std::uint32_t engagedCodec_ = FFF3FP_PT_CODEC_NONE;
    FFF3FPAudioPassthroughFormat format_{};
    bool formatValid_ = false;
    std::string displayName_;
    bool trackFlag1_ = false;
    std::vector<std::uint32_t> endpointSupportedCodecs_;
};
