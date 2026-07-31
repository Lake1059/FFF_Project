#pragma once

#include "3FP/Api/FFF.Player.Api.h"

#include <cstdint>
#include <mutex>
#include <string>

struct AVCodecParameters;
struct AVFrame;
struct DXGI_HDR_METADATA_HDR10;

struct HdrDisplayCapabilities {
    bool supported = false;
    float minimumNits = 0.0f;
    float maximumNits = 0.0f;
    float maximumFullFrameNits = 0.0f;
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
    float sourcePeakNits = 100.0f;
    float targetPeakNits = 1000.0f;
    HdrDisplayCapabilities display;
};

// Owns HDR stream classification, per-frame metadata extraction, Dolby Vision
// fallback decisions and source/display luminance resolution. AVFrame side data
// never escapes ProcessFrame, keeping cached renderer state aligned with its frame.
class HdrProcessor final {
public:
    void ConfigureStream(const AVCodecParameters* parameters) noexcept;
    HdrFrameState ProcessFrame(const AVFrame* frame, float targetPeakOverrideNits,
        float paperWhiteNits) noexcept;
    void SetDisplayCapabilities(const HdrDisplayCapabilities& display) noexcept;
    void SetTargetPeakOverride(float targetPeakOverrideNits) noexcept;
    void Reset() noexcept;

    HdrFrameState State() const noexcept;
    bool IsHdrSource() const noexcept;
    bool RequiresMetadataAwareShader() const noexcept;
    void BuildDxgiHdr10Metadata(DXGI_HDR_METADATA_HDR10& metadata) const noexcept;

    static const char* FormatName(FFF3FPHdrFormat format) noexcept;
    static const char* ProcessingPathName(FFF3FPHdrProcessingPath path) noexcept;
    static const char* EnhancementLayerName(
        FFF3FPDolbyVisionEnhancementLayer layer) noexcept;
    static std::string CompatibilityNames(std::uint32_t compatibility);
    static FFFResult EvaluateProbe(FFF3FPHdrProcessingProbe& probe) noexcept;

private:
    static float ResolveTargetPeak(float overrideNits,
        const HdrDisplayCapabilities& display) noexcept;

    mutable std::mutex mutex_;
    float targetPeakOverrideNits_ = 0.0f;
    HdrFrameState streamState_;
    HdrFrameState frameState_;
};
