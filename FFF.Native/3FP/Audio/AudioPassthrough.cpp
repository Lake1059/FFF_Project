#include "pch.h"
#include "3FP/Audio/AudioPassthrough.h"

#include <cstring>

// The one endpoint-negotiation fallback the kernel performs on its own. This is
// NOT stream handling and not packing: it only swaps the SubFormat GUID a
// rejected format probe would be retried with, from a variant carrier
// onto the generic carrier of the same codec family. The module still decides
// everything about the stream itself (the kernel owns "bytes + device
// negotiation", the module owns "frame + format semantics"); a rejected probe
// is retried once with the generic carrier instead of asking the module to
// re-describe, which the frozen describe() ABI cannot express.
namespace {
struct SubFormatGuid final {
    std::uint32_t a;
    std::uint32_t b;
    std::uint32_t c;
    std::uint32_t d;
};
// SubFormat GUIDs the one-step degrade step may retarget. The GUID values are
// the publicly registered IEC 61937 carriers; the names below are neutral
// ({Data1} only, the family semantics live in the module).
constexpr SubFormatGuid kSubFormatVariant{0x0000010au, 0x00100ceau, 0xaa000080u, 0x719b3800u};
constexpr SubFormatGuid kSubFormatGeneric{0x0000000au, 0x00100ceau, 0xaa000080u, 0x719b3800u};
constexpr SubFormatGuid kSubFormatFamilyB{0x0000010bu, 0x00100ceau, 0xaa000080u, 0x719b3800u};
constexpr SubFormatGuid kSubFormatFamilyBGeneric{0x0000000bu, 0x00100ceau, 0xaa000080u, 0x719b3800u};

constexpr bool SameGuid(const FFF3FPAudioPassthroughFormat& format,
    const SubFormatGuid& guid) noexcept {
    return format.subFormat[0] == guid.a && format.subFormat[1] == guid.b &&
        format.subFormat[2] == guid.c && format.subFormat[3] == guid.d;
}
}  // namespace

AudioPassthroughLane::~AudioPassthroughLane() {
    std::lock_guard lock(mutex_);
    if (tableInstalled_ && table_.destroy != nullptr && table_.context != nullptr)
        table_.destroy(table_.context);
    tableInstalled_ = false;
}

const char* AudioPassthroughLane::ReasonName(const Reason reason) noexcept {
    switch (reason) {
        case Reason::ModuleAbsent: return "module-absent";
        case Reason::ModuleDeclined: return "module-declined";
        case Reason::EndpointUnsupported: return "endpoint-unsupported";
        case Reason::ExclusiveBusy: return "exclusive-busy";
        case Reason::PackError: return "pack-error";
        case Reason::None: break;
    }
    return "none";
}

FFFResult AudioPassthroughLane::InstallTable(const FFF3FPAudioPassthroughApi* api) noexcept {
    std::lock_guard lock(mutex_);
    if (api == nullptr) {
        // Uninstall: ownership of the context returns to us; destroy it now that
        // no worker call can be inside the module (the same lock covers both).
        if (tableInstalled_ && table_.destroy != nullptr && table_.context != nullptr)
            table_.destroy(table_.context);
        table_ = {};
        tableInstalled_ = false;
        return FFFResult::Success;
    }
    if (api->size != sizeof(FFF3FPAudioPassthroughApi) ||
        api->version != FFF3FP_AUDIO_PASSTHROUGH_VERSION || api->context == nullptr ||
        api->describe == nullptr || api->pack == nullptr || api->reset == nullptr)
        return FFFResult::InvalidArgument;
    // Replace-with-replace keeps the old context alive until the new table is
    // in place, then destroys the old one -- the install call itself never
    // half-applies.
    FFF3FPAudioPassthroughApi previous = table_;
    const bool hadPrevious = tableInstalled_;
    table_ = *api;
    tableInstalled_ = true;
    if (hadPrevious && previous.destroy != nullptr && previous.context != nullptr)
        previous.destroy(previous.context);
    return FFFResult::Success;
}

bool AudioPassthroughLane::TableInstalled() const noexcept {
    std::lock_guard lock(mutex_);
    return tableInstalled_;
}

bool AudioPassthroughLane::SetMode(const std::uint32_t mode) noexcept {
    if (mode != FFF3FP_PT_MODE_AUTO && mode != FFF3FP_PT_MODE_FORCE &&
        mode != FFF3FP_PT_MODE_OFF)
        return false;
    std::lock_guard lock(mutex_);
    if (mode_ == mode) return false;
    mode_ = mode;
    return true;
}

std::uint32_t AudioPassthroughLane::Mode() const noexcept {
    std::lock_guard lock(mutex_);
    return mode_;
}

bool AudioPassthroughLane::WantsTrack(const std::uint32_t codecId) noexcept {
    std::lock_guard lock(mutex_);
    return tableInstalled_ && mode_ != FFF3FP_PT_MODE_OFF &&
        codecId != FFF3FP_PT_CODEC_NONE;
}

void AudioPassthroughLane::Arm() noexcept {
    std::lock_guard lock(mutex_);
    trackState_ = TrackState::Armed;
    engagedCodec_ = FFF3FP_PT_CODEC_NONE;
    format_ = {};
    formatValid_ = false;
    displayName_.clear();
    trackFlag1_ = false;
}

void AudioPassthroughLane::Disarm() noexcept {
    std::lock_guard lock(mutex_);
    trackState_ = TrackState::Off;
    engagedCodec_ = FFF3FP_PT_CODEC_NONE;
    format_ = {};
    formatValid_ = false;
    displayName_.clear();
    trackFlag1_ = false;
}

void AudioPassthroughLane::RearmForReopen() noexcept {
    // Keep the module's context (a device rebuild does not invalidate its
    // accumulation state -- the next describe() re-decides the format anyway).
    std::lock_guard lock(mutex_);
    if (trackState_ == TrackState::Engaged) {
        if (table_.reset != nullptr && table_.context != nullptr) table_.reset(table_.context);
        trackState_ = TrackState::Armed;
        engagedCodec_ = FFF3FP_PT_CODEC_NONE;
        format_ = {};
        formatValid_ = false;
        displayName_.clear();
        trackFlag1_ = false;
    }
}

bool AudioPassthroughLane::Armed() const noexcept {
    std::lock_guard lock(mutex_);
    return trackState_ == TrackState::Armed;
}

bool AudioPassthroughLane::Engaged() const noexcept {
    std::lock_guard lock(mutex_);
    return trackState_ == TrackState::Engaged;
}

bool AudioPassthroughLane::Declined() const noexcept {
    std::lock_guard lock(mutex_);
    return trackState_ == TrackState::Declined;
}

bool AudioPassthroughLane::DescribeTrack(
    const FFF3FPAudioPassthroughTrackInfo& info) noexcept {
    std::lock_guard lock(mutex_);
    if (!tableInstalled_ || trackState_ != TrackState::Armed) return false;
    FFF3FPAudioPassthroughFormat decided{};
    decided.size = sizeof(decided);
    decided.version = FFF3FP_AUDIO_PASSTHROUGH_VERSION;
    if (table_.describe(table_.context, &info, &decided) != 0)
        return false;
    format_ = decided;
    formatValid_ = true;
    displayName_ = decided.displayName != nullptr ? decided.displayName : "";
    trackFlag1_ = (decided.flags & FFF3FP_PT_FLAG_1) != 0;
    return true;
}

void AudioPassthroughLane::MarkEngaged(const std::uint32_t codecId) noexcept {
    std::lock_guard lock(mutex_);
    trackState_ = TrackState::Engaged;
    engagedCodec_ = codecId;
}

void AudioPassthroughLane::NoteEndpointSupport(const std::uint32_t codecId) noexcept {
    if (codecId == FFF3FP_PT_CODEC_NONE) return;
    std::lock_guard lock(mutex_);
    for (const auto known : endpointSupportedCodecs_)
        if (known == codecId) return;
    endpointSupportedCodecs_.push_back(codecId);
}

FFF3FPAudioPassthroughFormat AudioPassthroughLane::Format() const noexcept {
    // By value on purpose: the lock guards only the read below. A const
    // reference would escape it, so any third thread calling
    // DegradeCachedFormat() concurrently would turn the caller's next member
    // access into a data race. The struct is a small POD -- the copy is free.
    std::lock_guard lock(mutex_);
    return format_;
}

bool AudioPassthroughLane::DegradeCachedFormat() noexcept {
    std::lock_guard lock(mutex_);
    if (!formatValid_) return false;
    if (SameGuid(format_, kSubFormatVariant)) {
        format_.subFormat[0] = kSubFormatGeneric.a;
        format_.subFormat[1] = kSubFormatGeneric.b;
        format_.subFormat[2] = kSubFormatGeneric.c;
        format_.subFormat[3] = kSubFormatGeneric.d;
        // Module flag 1 marked the variant carrier; the endpoint accepted the
        // generic one instead, so the bit must not stay lit.
        format_.flags &= ~FFF3FP_PT_FLAG_1;
        trackFlag1_ = false;
        // The display name stays whatever the module injected at describe()
        // time; the kernel never authors one.
        return true;
    }
    if (SameGuid(format_, kSubFormatFamilyB)) {
        format_.subFormat[0] = kSubFormatFamilyBGeneric.a;
        format_.subFormat[1] = kSubFormatFamilyBGeneric.b;
        format_.subFormat[2] = kSubFormatFamilyBGeneric.c;
        format_.subFormat[3] = kSubFormatFamilyBGeneric.d;
        return true;
    }
    return false;
}

std::uint32_t AudioPassthroughLane::EngagedCodec() const noexcept {
    std::lock_guard lock(mutex_);
    return engagedCodec_;
}

std::string AudioPassthroughLane::DisplayName() const noexcept {
    std::lock_guard lock(mutex_);
    return displayName_;
}

std::uint32_t AudioPassthroughLane::TrackFlags() const noexcept {
    std::lock_guard lock(mutex_);
    return trackFlag1_ ? FFF3FP_PT_FLAG_1 : 0u;
}

void AudioPassthroughLane::ModuleReset() noexcept {
    std::lock_guard lock(mutex_);
    if (tableInstalled_ && table_.reset != nullptr && table_.context != nullptr)
        table_.reset(table_.context);
}

std::int32_t AudioPassthroughLane::Pack(const std::uint8_t* const frame,
    const std::uint32_t frameSize, const std::int64_t framePts100ns,
    std::uint8_t* const outBurst, const std::uint32_t burstCapacity,
    std::uint32_t* const burstSize, std::int64_t* const burstPts100ns,
    std::int64_t* const burstDuration100ns, Reason* const failReason) noexcept {
    std::lock_guard lock(mutex_);
    if (failReason != nullptr) *failReason = Reason::None;
    if (!tableInstalled_ || trackState_ != TrackState::Engaged) {
        if (failReason != nullptr) *failReason = Reason::PackError;
        return -1;
    }
    const auto result = table_.pack(table_.context, frame, frameSize, framePts100ns,
        outBurst, burstCapacity, burstSize, burstPts100ns, burstDuration100ns);
    if (result < 0 && failReason != nullptr) *failReason = Reason::PackError;
    return result;
}

std::vector<std::uint32_t> AudioPassthroughLane::EndpointSupportedCodecs() const noexcept {
    std::lock_guard lock(mutex_);
    return endpointSupportedCodecs_;
}
