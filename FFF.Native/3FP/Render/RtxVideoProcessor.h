#pragma once

#include "3FP/Api/FFF.Player.Api.h"

#include <d3d11.h>

#include <cstdint>
#include <mutex>
#include <string>

enum class RtxVideoPath : std::uint32_t {
    None = 0,
    Vsr = 1,
    TrueHdr = 2,
    VsrThenTrueHdr = 3,
};

enum class RtxVideoState : std::uint32_t {
    Disabled = 0,
    NotInitialized = 1,
    Ready = 2,
    Unavailable = 3,
    FailedForKey = 4,
    DeviceLost = 5,
};

struct RtxVideoStatus {
    RtxVideoState state = RtxVideoState::NotInitialized;
    RtxVideoPath path = RtxVideoPath::None;
    bool initialized = false;
    bool vsrAvailable = false;
    bool trueHdrAvailable = false;
    std::uint32_t resultWidth = 0;
    std::uint32_t resultHeight = 0;
    DXGI_FORMAT resultFormat = DXGI_FORMAT_UNKNOWN;
    std::uint64_t evaluateCount = 0;
    std::uint64_t lastEvaluate100ns = 0;
    std::string lastError;
};

class RtxVideoProcessor final {
public:
    RtxVideoProcessor() noexcept;
    ~RtxVideoProcessor();

    RtxVideoProcessor(const RtxVideoProcessor&) = delete;
    RtxVideoProcessor& operator=(const RtxVideoProcessor&) = delete;

    // Evaluate one immutable RGB frame. The caller owns input and serializes
    // this operation with the renderer's D3D11 device lock.
    FFFResult Evaluate(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11Texture2D* input, std::uint32_t inputWidth, std::uint32_t inputHeight,
        DXGI_FORMAT inputFormat, std::uint32_t outputWidth, std::uint32_t outputHeight,
        DXGI_FORMAT outputFormat, bool runVsr, bool runTrueHdr,
        std::uint32_t maxLuminance) noexcept;

    // Mark the current result stale. Frame invalidation keeps reusable GPU
    // allocations alive; media/device teardown can request full release.
    void Invalidate(bool releaseResources = false) noexcept;
    void ReleaseDeviceObjects() noexcept;

    bool HasResult() const noexcept;
    ID3D11ShaderResourceView* ResultView() const noexcept;
    std::uint32_t ResultWidth() const noexcept;
    std::uint32_t ResultHeight() const noexcept;
    DXGI_FORMAT ResultFormat() const noexcept;
    RtxVideoStatus Status() const;
    void SetDecisionReason(std::string message) noexcept;
    void ClearError() noexcept;

private:
    FFFResult EnsureInitialized(ID3D11Device* device,
        ID3D11DeviceContext* context, bool needVsr, bool needTrueHdr) noexcept;
    FFFResult EnsureOutput(std::uint32_t width, std::uint32_t height,
        DXGI_FORMAT format) noexcept;
    void ReleaseFeatures() noexcept;
    void ReleaseResult() noexcept;
    void ReleaseScratch() noexcept;
    void SetError(std::string message) noexcept;
    void SetSdkError(const char* operation, long result) noexcept;

    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    ID3D11ShaderResourceView* resultView_;
    ID3D11Texture2D* resultTexture_;
    ID3D11Texture2D* middleTexture_;
    std::uint32_t resultWidth_;
    std::uint32_t resultHeight_;
    DXGI_FORMAT resultFormat_;
    RtxVideoState state_;
    RtxVideoPath path_;
    bool initialized_;
    bool vsrAvailable_;
    bool trueHdrAvailable_;
    std::uint64_t evaluateCount_;
    std::uint64_t lastEvaluate100ns_;
    // A failed SDK initialization is sticky for the current D3D device. The
    // renderer submits many frames per second; retrying the same invalid
    // application-id/driver combination on every frame can churn NGX state.
    ID3D11Device* failedDevice_;
    mutable std::mutex statusMutex_;
    std::string lastError_;

    struct SdkState;
    SdkState* sdk_;
};
