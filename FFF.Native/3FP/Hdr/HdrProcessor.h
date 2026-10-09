#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Hdr/HdrDynamicMetadata.h"

#include <cstdint>
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
    std::uint32_t dolbyVisionProfile = 0;
    std::uint32_t dolbyVisionLevel = 0;
    bool hasRpu = false;
    bool hasEnhancementLayer = false;
    FFF3FPDolbyVisionEnhancementLayer enhancementLayer =
        FFF3FPDolbyVisionEnhancementLayer::None;
    bool dynamicMetadata = false;
    bool fallback = false;
    bool externalExtensionAvailable = false;
    bool externalExtensionActive = false;
    float sourcePeakNits = 100.0f;
    float targetPeakNits = 1000.0f;
    HdrStaticMetadata staticMetadata;
    HdrDisplayCapabilities display;
    // ST 2094-40 / ST 2094-30 snapshot for this frame. Fixed-size and trivially
    // copyable, so it rides along in the by-value copies of this struct.
    HdrDynamicMetadata dynamic;
};

// Owns HDR stream classification, per-frame metadata extraction, Dolby Vision
// fallback decisions and source/display luminance resolution. AVFrame side data
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
    void SetExtensionAvailability(bool available) noexcept;
    void SetExtensionProcessing(float sourcePeakNits, bool active) noexcept;
    bool IsHdrSource() const noexcept;
    bool RequiresMetadataAwareShader() const noexcept;
    void BuildDxgiHdr10Metadata(DXGI_HDR_METADATA_HDR10& metadata) const noexcept;

    // ---- injected ST 2094-40 metadata (see FFF3FP_SetHdrDynamicMetadata) ----
    // Replaces the injected table. Validated up front: a caller passing a
    // malformed entry gets InvalidArgument and the previous table stays live,
    // rather than a half-applied table silently changing the picture.
    FFFResult SetInjectedMetadata(const FFF3FPHdrDynamicMetadataEntry* entries,
        std::uint32_t count) noexcept;
    void ClearInjectedMetadata() noexcept;
    // True when an injected entry is driving the current frame.
    bool InjectedMetadataActive() const noexcept;

    static const char* FormatName(FFF3FPHdrFormat format) noexcept;
    static const char* ProcessingPathName(FFF3FPHdrProcessingPath path) noexcept;
    static const char* EnhancementLayerName(
        FFF3FPDolbyVisionEnhancementLayer layer) noexcept;
    static std::string CompatibilityNames(std::uint32_t compatibility);
    static FFFResult EvaluateProbe(FFF3FPHdrProcessingProbe& probe) noexcept;

private:
    static float ResolveTargetPeak(float overrideNits,
        const HdrDisplayCapabilities& display) noexcept;
    // Build the frame state an injected entry describes. Returns false when the
    // entry is malformed; the caller then leaves bitstream metadata in charge.
    static bool BuildInjectedState(const FFF3FPHdrDynamicMetadataEntry& entry,
        HdrDynamicMetadata& metadata) noexcept;
    // Select the injected entry in force for frameIndex and apply it to `state`.
    // Split out of ProcessFrame so installing or clearing a table can re-evaluate the
    // *cached* frame: without that, a host that installs while paused or at
    // end-of-stream gets a silent no-op that still returns Success.
    void ApplyInjectionLocked(std::int64_t frameIndex, HdrFrameState& state) noexcept;
    // Re-evaluate the table against the last finished frame. Call with mutex_ held --
    // both callers already hold it, and std::mutex is not recursive.
    void ReapplyInjectedMetadataLocked() noexcept;
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
    std::vector<FFF3FPHdrDynamicMetadataEntry> injected_;
    bool injectedActive_ = false;
};
