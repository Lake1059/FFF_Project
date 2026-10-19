#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Hdr/HdrDynamicMetadata.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AVCodecParameters;
struct AVFrame;
struct DXGI_HDR_METADATA_HDR10;

struct HdrDisplayCapabilities {
    bool supported = false;
    float minimumNits = 0.0f;
    float maximumNits = 0.0f;
    float maximumFullFrameNits = 0.0f;
    // Windows' "SDR content brightness": the luminance DWM maps an ordinary SDR
    // window's white to while HDR is active (AdvancedColorInfo::SdrWhiteLevelInNits).
    // 0 = not reported (older Windows, or Advanced Color off). An SDR source that
    // is presented on the scRGB chain must be anchored here to land on the same
    // luminance the classic SDR chain would have produced.
    float sdrWhiteLevelNits = 0.0f;
};

struct HdrStaticMetadata {
    bool hasPrimaries = false;
    bool hasLuminance = false;
    bool hasContentLight = false;
    float redX = 0.708f;
    float redY = 0.292f;
    float greenX = 0.170f;
    float greenY = 0.797f;
    float blueX = 0.131f;
    float blueY = 0.046f;
    float whiteX = 0.3127f;
    float whiteY = 0.3290f;
    float minimumLuminanceNits = 0.0f;
    float maximumMasteringLuminanceNits = 0.0f;
    float maximumContentLightLevelNits = 0.0f;
    float maximumFrameAverageLightLevelNits = 0.0f;
};

struct HdrFrameState {
    FFF3FPHdrFormat format = FFF3FPHdrFormat::Sdr;
    std::uint32_t compatibility = 0;
    FFF3FPHdrProcessingPath processingPath = FFF3FPHdrProcessingPath::None;
    bool dynamicMetadata = false;
    bool fallback = false;
    float sourcePeakNits = 100.0f;
    float targetPeakNits = 1000.0f;
    HdrStaticMetadata staticMetadata;
    HdrDisplayCapabilities display;
    // ST 2094-40 / ST 2094-30 snapshot for this frame. Fixed-size and trivially
    // copyable, so it rides along in the by-value copies of this struct.
    HdrDynamicMetadata dynamic;
};

// Owns HDR stream classification, per-frame metadata extraction and
// source/display luminance resolution. AVFrame side data
// never escapes ProcessFrame, keeping cached renderer state aligned with its frame.
class HdrProcessor final {
public:
    void ConfigureStream(const AVCodecParameters* parameters) noexcept;
    // frameIndex must be the value the session publishes as FFF3FPSnapshot::frameIndex:
    // that is the key an injected entry's frameIndex is compared against. Passing the
    // packet pts instead resolved every sparse scene list to its last entry, because at
    // a 1/15360 time base and 60 fps frame 4 already carries pts 1024.
    static constexpr std::int64_t kUnknownFrameIndex = -1;
    HdrFrameState ProcessFrame(const AVFrame* frame, float targetPeakOverrideNits,
        float paperWhiteNits, std::int64_t frameIndex = kUnknownFrameIndex) noexcept;
    void SetDisplayCapabilities(const HdrDisplayCapabilities& display) noexcept;
    void SetTargetPeakOverride(float targetPeakOverrideNits) noexcept;
    void Reset() noexcept;

    HdrFrameState State() const noexcept;
    bool IsHdrSource() const noexcept;
    bool RequiresMetadataAwareShader() const noexcept;
    void BuildDxgiHdr10Metadata(DXGI_HDR_METADATA_HDR10& metadata) const noexcept;
    // Static HDR10 signalling block for a frame the NVIDIA TrueHDR video-
    // processor pass produced: BT.2020 primaries, PQ, 1000-nit peak (the
    // extension's output contract, same max_luma mpv uses). The stream's own
    // static metadata is deliberately not consulted -- the samples are the
    // driver's generated HDR, not the stream's.
    void BuildTrueHdrMetadata(DXGI_HDR_METADATA_HDR10& metadata) const noexcept;

    // ---- injected dynamic metadata (ST 2094-40 entry point and the generic
    // FFF3FP_SetDynamicMetadata carrier; one shared table, see the header
    // contract for the matching/rejection semantics both channels follow) ----
    // Replaces the injected table. Validated up front: a caller passing a
    // malformed entry gets InvalidArgument and the previous table stays live,
    // rather than a half-applied table silently changing the picture.
    FFFResult SetInjectedMetadata(const FFF3FPHdrDynamicMetadataEntry* entries,
        std::uint32_t count) noexcept;
    // Generic-carrier flavour of the same table: kind-tagged packets, whole-call
    // validation (unknown kind -> NotSupported, previous table stays live).
    FFFResult SetInjectedMetadataPackets(const FFF3FPDynamicMetadataPacket* packets,
        std::uint32_t count) noexcept;
    // kind 0 clears every kind; any other kind clears only that kind's entries
    // and re-evaluates the rest against the frame on screen.
    void ClearInjectedMetadata(std::uint32_t kind = 0) noexcept;
    // True when an injected entry is driving the current frame.
    bool InjectedMetadataActive() const noexcept;
    // The colour-transform payload in force for the current frame, if the chosen
    // entry is kind 3 (ExternalColorTransform). Returns false when no such entry
    // is active; on true, `out` holds a shared reference to the table the
    // renderer uploads. Kept out of HdrFrameState so the ~421 KB payload is
    // never copied by the by-value frame-state passes.
    bool ActiveColorTransform(
        std::shared_ptr<const FFF3FPColorTransformPayload>& out) const noexcept;

    static const char* FormatName(FFF3FPHdrFormat format) noexcept;
    static const char* ProcessingPathName(FFF3FPHdrProcessingPath path) noexcept;
    static std::string CompatibilityNames(std::uint32_t compatibility);
    static FFFResult EvaluateProbe(FFF3FPHdrProcessingProbe& probe) noexcept;

private:
    // One installed table row. Kind-tagged so the generic carrier and the
    // dedicated ST 2094-40 entry point share one sorted table; only the payload
    // matching `kind` is meaningful.
    struct InjectedEntry {
        HdrMetadataKind kind = HdrMetadataKind::Hdr10Plus;
        std::int64_t frameIndex = 0;
        FFF3FPHdrDynamicMetadataEntry hdr10Plus{};
        FFF3FPHdrVividMetadataPayload vivid{};
        // kind == ExternalColorTransform only. Heap-allocated behind a shared_ptr
        // because the payload is ~421 KB and installing a table must not copy it
        // per entry; the renderer holds a shared reference for the frame it draws.
        std::shared_ptr<FFF3FPColorTransformPayload> colorTransform;
    };
    static float ResolveTargetPeak(float overrideNits,
        const HdrDisplayCapabilities& display) noexcept;
    // Build the frame state an injected entry describes. Returns false when the
    // entry is malformed; the caller then leaves bitstream metadata in charge.
    static bool BuildInjectedState(const FFF3FPHdrDynamicMetadataEntry& entry,
        HdrDynamicMetadata& metadata) noexcept;
    // Vivid flavour: peak-only windows (same reading as bitstream Vivid), no
    // curve interpretation. Returns false on malformed payloads.
    static bool BuildInjectedStateVivid(const FFF3FPHdrVividMetadataPayload& payload,
        HdrDynamicMetadata& metadata) noexcept;
    // Select the injected entry in force for frameIndex and apply it to `state`.
    // Split out of ProcessFrame so installing or clearing a table can re-evaluate the
    // *cached* frame: without that, a host that installs while paused or at
    // end-of-stream gets a silent no-op that still returns Success.
    void ApplyInjectionLocked(std::int64_t frameIndex, HdrFrameState& state) noexcept;
    // Re-evaluate the table against the last finished frame. Call with mutex_ held --
    // both callers already hold it, and std::mutex is not recursive.
    void ReapplyInjectedMetadataLocked() noexcept;
    // Sort, install and re-evaluate. Shared tail of both setter flavours; call
    // before taking mutex_ (it takes it itself).
    FFFResult InstallInjectedTable(std::vector<InjectedEntry>&& validated) noexcept;
    std::int64_t lastLookupIndex_ = kUnknownFrameIndex;
    // The frame state as the bitstream alone produced it, kept so clearing a table
    // while paused returns the picture to that frame instead of leaving the injected
    // curve in place until the next frame happens to arrive.
    HdrFrameState lastUninjectedState_;
    bool haveUninjectedState_ = false;

    mutable std::mutex mutex_;
    float targetPeakOverrideNits_ = 0.0f;
    HdrFrameState streamState_;
    HdrFrameState frameState_;
    // Injected table, kept sorted by frameIndex. Bounded so a host cannot grow
    // it without limit; scene lists are naturally small.
    std::vector<InjectedEntry> injected_;
    bool injectedActive_ = false;
    // The kind-3 payload in force for the current frame, or null. Mirrors what
    // ApplyInjectionLocked chose, so the getter needs no second table walk.
    std::shared_ptr<const FFF3FPColorTransformPayload> activeTransform_;
};
