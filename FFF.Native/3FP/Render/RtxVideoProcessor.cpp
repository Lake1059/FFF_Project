#include "pch.h"
#include "3FP/Render/RtxVideoProcessor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <sstream>
#include <vector>

#ifndef FFF_ENABLE_RTX_VIDEO
#define FFF_ENABLE_RTX_VIDEO 0
#endif

#ifndef FFF_RTX_VIDEO_APPLICATION_ID
#define FFF_RTX_VIDEO_APPLICATION_ID 0ull
#endif

#if FFF_ENABLE_RTX_VIDEO
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_truehdr.h>
#include <nvsdk_ngx_defs_vsr.h>
#include <nvsdk_ngx_helpers_truehdr.h>
#include <nvsdk_ngx_helpers_vsr.h>
#endif

namespace {

std::string HResultText(const long result) {
    std::ostringstream output;
    output << "0x" << std::hex << static_cast<std::uint32_t>(result);
    return output.str();
}

#if FFF_ENABLE_RTX_VIDEO
void RtxModuleMarker() noexcept {}

// NGX calls are not thread-safe. Keep the D3D11 multithread protection paired
// even when a future evaluation path gains an additional early return.
struct MultithreadScope final {
    ID3D10Multithread* value = nullptr;

    explicit MultithreadScope(ID3D10Multithread* multithread) noexcept
        : value(multithread) {
        if (value != nullptr) value->Enter();
    }

    ~MultithreadScope() {
        if (value != nullptr) value->Leave();
    }

    MultithreadScope(const MultithreadScope&) = delete;
    MultithreadScope& operator=(const MultithreadScope&) = delete;
};

std::wstring ModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&RtxModuleMarker), &module))
        return {};
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(module, path.data(),
            static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length + 1 < path.size()) {
            path.resize(length);
            return std::filesystem::path(path).parent_path().wstring();
        }
        path.resize(path.size() * 2);
    }
}

std::wstring NgxDataDirectory() {
    wchar_t localAppData[MAX_PATH]{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA",
        localAppData, ARRAYSIZE(localAppData));
    const auto root = length > 0 && length < ARRAYSIZE(localAppData)
        ? std::filesystem::path(localAppData)
        : std::filesystem::temp_directory_path();
    const auto directory = root / L"3F Project" / L"RTX Video SDK";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory.wstring();
}

std::string SdkResultText(const long result) {
    const auto* text = GetNGXResultAsString(static_cast<NVSDK_NGX_Result>(result));
    if (text == nullptr) return HResultText(result);
    const auto length = static_cast<int>(wcslen(text));
    if (length <= 0) return HResultText(result);
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text, length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return HResultText(result);
    std::string output(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length,
            output.data(), bytes, nullptr, nullptr) != bytes)
        return HResultText(result);
    return output;
}
#endif

} // namespace

struct RtxVideoProcessor::SdkState {
#if FFF_ENABLE_RTX_VIDEO
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* vsrFeature = nullptr;
    NVSDK_NGX_Handle* trueHdrFeature = nullptr;
    ID3D11Buffer* scratchBuffer = nullptr;
    std::size_t scratchSize = 0;
    ID3D10Multithread* multithread = nullptr;
    bool ngxInitialized = false;
#endif
};

RtxVideoProcessor::RtxVideoProcessor() noexcept
    : device_(nullptr), context_(nullptr), resultView_(nullptr),
      resultTexture_(nullptr), middleTexture_(nullptr), resultWidth_(0),
      resultHeight_(0), resultFormat_(DXGI_FORMAT_UNKNOWN),
      state_(RtxVideoState::NotInitialized), path_(RtxVideoPath::None),
      initialized_(false), vsrAvailable_(false), trueHdrAvailable_(false),
      evaluateCount_(0), lastEvaluate100ns_(0), failedDevice_(nullptr),
      sdk_(nullptr) {
    try {
        sdk_ = new SdkState();
    } catch (...) {
        sdk_ = nullptr;
        state_ = RtxVideoState::Unavailable;
    }
}

RtxVideoProcessor::~RtxVideoProcessor() {
    ReleaseDeviceObjects();
    delete sdk_;
    sdk_ = nullptr;
}

void RtxVideoProcessor::SetError(std::string message) noexcept {
    try {
        std::lock_guard lock(statusMutex_);
        lastError_ = std::move(message);
    } catch (...) {
    }
}

void RtxVideoProcessor::SetSdkError(const char* operation, const long result) noexcept {
#if FFF_ENABLE_RTX_VIDEO
    try {
        std::ostringstream output;
        output << (operation == nullptr ? "RTX Video SDK operation failed" : operation)
               << " (" << SdkResultText(result) << ").";
        SetError(output.str());
    } catch (...) {
        SetError("RTX Video SDK operation failed.");
    }
#else
    (void)operation;
    (void)result;
    SetError("RTX Video SDK support is disabled in this build.");
#endif
}

void RtxVideoProcessor::SetDecisionReason(std::string message) noexcept {
    SetError(std::move(message));
}

void RtxVideoProcessor::ClearError() noexcept {
    try {
        std::lock_guard lock(statusMutex_);
        lastError_.clear();
    } catch (...) {
    }
}

FFFResult RtxVideoProcessor::EnsureInitialized(ID3D11Device* device,
    ID3D11DeviceContext* context, const bool needVsr,
    const bool needTrueHdr) noexcept {
#if !FFF_ENABLE_RTX_VIDEO
    (void)device;
    (void)context;
    (void)needVsr;
    (void)needTrueHdr;
    state_ = RtxVideoState::Unavailable;
    SetError("RTX Video SDK support is disabled in this build.");
    return FFFResult::NotSupported;
#else
    if (device == nullptr || context == nullptr || sdk_ == nullptr)
        return FFFResult::InvalidState;
    if (state_ == RtxVideoState::Unavailable && failedDevice_ == device)
        return FFFResult::NotSupported;
    if (failedDevice_ != nullptr && failedDevice_ != device)
        failedDevice_ = nullptr;
    if (initialized_ && device_ == device && context_ == context &&
        (!needVsr || sdk_->vsrFeature != nullptr) &&
        (!needTrueHdr || sdk_->trueHdrFeature != nullptr)) return FFFResult::Success;
    if (initialized_) ReleaseDeviceObjects();

    device_ = device;
    // Keep this marker if any initialization step below fails. A successful
    // initialization clears it after all requested features are created.
    failedDevice_ = device;
    context_ = context;
    device_->AddRef();
    context_->AddRef();
    context_->QueryInterface(IID_PPV_ARGS(&sdk_->multithread));
    if (sdk_->multithread != nullptr) sdk_->multithread->SetMultithreadProtected(TRUE);
    if (sdk_->multithread != nullptr) sdk_->multithread->Enter();

    const auto moduleDirectory = ModuleDirectory();
    std::vector<const wchar_t*> searchPaths;
    if (!moduleDirectory.empty()) searchPaths.push_back(moduleDirectory.c_str());
    NVSDK_NGX_FeatureCommonInfo featureInfo{};
    featureInfo.PathListInfo.Path = searchPaths.empty() ? nullptr : searchPaths.data();
    featureInfo.PathListInfo.Length = static_cast<unsigned int>(searchPaths.size());
    const auto dataDirectory = NgxDataDirectory();
#if FFF_RTX_VIDEO_APPLICATION_ID != 0
    const auto result = NVSDK_NGX_D3D11_Init(FFF_RTX_VIDEO_APPLICATION_ID,
        dataDirectory.c_str(), device_, &featureInfo);
    constexpr const char* initOperation = "NVSDK_NGX_D3D11_Init";
#else
    // The ProjectID entry point is useful for SDK integrations registered as a
    // custom engine, but it is not a way to bypass NVIDIA's application
    // registration. Some SDK/driver combinations still reject it unless the
    // project has been provisioned by NVIDIA; a release build should pass the
    // assigned Application ID through FFF_RTX_VIDEO_APPLICATION_ID.
    const auto result = NVSDK_NGX_D3D11_Init_with_ProjectID(
        "3FP", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1", dataDirectory.c_str(),
        device_, &featureInfo);
    constexpr const char* initOperation = "NVSDK_NGX_D3D11_Init_with_ProjectID";
#endif
    if (NVSDK_NGX_FAILED(result)) {
        if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
        SetSdkError(initOperation, result);
        ReleaseDeviceObjects();
        state_ = RtxVideoState::Unavailable;
        return FFFResult::NotSupported;
    }
    sdk_->ngxInitialized = true;

    auto capabilityResult = NVSDK_NGX_D3D11_GetCapabilityParameters(&sdk_->parameters);
    if (NVSDK_NGX_FAILED(capabilityResult) || sdk_->parameters == nullptr) {
        if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
        SetSdkError("NVSDK_NGX_D3D11_GetCapabilityParameters", capabilityResult);
        ReleaseDeviceObjects();
        state_ = RtxVideoState::Unavailable;
        return FFFResult::NotSupported;
    }

    int available = 0;
    auto vsrResult = sdk_->parameters->Get(NVSDK_NGX_Parameter_VSR_Available, &available);
    vsrAvailable_ = NVSDK_NGX_SUCCEED(vsrResult) && available != 0;
    available = 0;
    auto trueHdrResult = sdk_->parameters->Get(
        NVSDK_NGX_Parameter_TrueHDR_Available, &available);
    trueHdrAvailable_ = NVSDK_NGX_SUCCEED(trueHdrResult) && available != 0;
    std::size_t scratchSize = 0;
    if (needVsr && vsrAvailable_) {
        size_t vsrScratchSize = 0;
        const auto scratchResult = NVSDK_NGX_D3D11_GetScratchBufferSize(
            NVSDK_NGX_Feature_VSR, sdk_->parameters, &vsrScratchSize);
        if (NVSDK_NGX_FAILED(scratchResult)) {
            if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
            SetSdkError("NVSDK_NGX_D3D11_GetScratchBufferSize(VSR)",
                scratchResult);
            ReleaseDeviceObjects();
            state_ = RtxVideoState::Unavailable;
            return FFFResult::NotSupported;
        }
        scratchSize = std::max(scratchSize, vsrScratchSize);
    }
    if (needTrueHdr && trueHdrAvailable_) {
        size_t trueHdrScratchSize = 0;
        const auto scratchResult = NVSDK_NGX_D3D11_GetScratchBufferSize(
            NVSDK_NGX_Feature_TrueHDR, sdk_->parameters, &trueHdrScratchSize);
        if (NVSDK_NGX_FAILED(scratchResult)) {
            if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
            SetSdkError("NVSDK_NGX_D3D11_GetScratchBufferSize(TrueHDR)",
                scratchResult);
            ReleaseDeviceObjects();
            state_ = RtxVideoState::Unavailable;
            return FFFResult::NotSupported;
        }
        scratchSize = std::max(scratchSize, trueHdrScratchSize);
    }
    if (scratchSize > 0) {
        if (scratchSize > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
            if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
            SetError("RTX Video SDK requested an oversized scratch buffer.");
            ReleaseDeviceObjects();
            state_ = RtxVideoState::Unavailable;
            return FFFResult::NotSupported;
        }
        D3D11_BUFFER_DESC scratchDescription{};
        scratchDescription.ByteWidth = static_cast<UINT>(scratchSize);
        scratchDescription.Usage = D3D11_USAGE_DEFAULT;
        scratchDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        scratchDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        if (FAILED(device_->CreateBuffer(&scratchDescription, nullptr,
                &sdk_->scratchBuffer))) {
            if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
            SetError("Could not create the RTX Video SDK scratch buffer.");
            ReleaseDeviceObjects();
            state_ = RtxVideoState::Unavailable;
            return FFFResult::DeviceFailure;
        }
        sdk_->scratchSize = scratchSize;
        NVSDK_NGX_Parameter_SetD3d11Resource(sdk_->parameters,
            NVSDK_NGX_Parameter_Scratch, sdk_->scratchBuffer);
        NVSDK_NGX_Parameter_SetUI(sdk_->parameters,
            NVSDK_NGX_Parameter_Scratch_SizeInBytes, static_cast<UINT>(scratchSize));
    }
    if (needVsr && vsrAvailable_) {
        NVSDK_NGX_Feature_Create_Params createParams{};
        const auto createResult = NGX_D3D11_CREATE_VSR_EXT(context_,
            &sdk_->vsrFeature, sdk_->parameters, &createParams);
        if (NVSDK_NGX_FAILED(createResult)) {
            if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
            SetSdkError("NGX_D3D11_CREATE_VSR_EXT", createResult);
            ReleaseDeviceObjects();
            state_ = RtxVideoState::Unavailable;
            return FFFResult::NotSupported;
        }
    }
    if (needTrueHdr && trueHdrAvailable_) {
        NVSDK_NGX_Feature_Create_Params createParams{};
        const auto createResult = NGX_D3D11_CREATE_TRUEHDR_EXT(context_,
            &sdk_->trueHdrFeature, sdk_->parameters, &createParams);
        if (NVSDK_NGX_FAILED(createResult)) {
            if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
            SetSdkError("NGX_D3D11_CREATE_TRUEHDR_EXT", createResult);
            ReleaseDeviceObjects();
            state_ = RtxVideoState::Unavailable;
            return FFFResult::NotSupported;
        }
    }
    if ((needVsr && !vsrAvailable_) || (needTrueHdr && !trueHdrAvailable_)) {
        if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
        SetError(needVsr && !vsrAvailable_
            ? "RTX Video Super Resolution is unavailable."
            : "RTX Video HDR is unavailable.");
        ReleaseDeviceObjects();
        state_ = RtxVideoState::Unavailable;
        return FFFResult::NotSupported;
    }
    initialized_ = true;
    failedDevice_ = nullptr;
    if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
    state_ = vsrAvailable_ || trueHdrAvailable_
        ? RtxVideoState::Ready : RtxVideoState::Unavailable;
    return FFFResult::Success;
#endif
}

FFFResult RtxVideoProcessor::EnsureOutput(const std::uint32_t width,
    const std::uint32_t height, const DXGI_FORMAT format) noexcept {
    if (device_ == nullptr || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN)
        return FFFResult::InvalidArgument;
    if (resultTexture_ != nullptr && resultView_ != nullptr &&
        resultWidth_ == width && resultHeight_ == height && resultFormat_ == format)
        return FFFResult::Success;
    ReleaseResult();
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(device_->CreateTexture2D(&description, nullptr, &resultTexture_)) ||
        FAILED(device_->CreateShaderResourceView(resultTexture_, nullptr, &resultView_))) {
        ReleaseResult();
        SetError("Could not create the RTX result texture.");
        return FFFResult::DeviceFailure;
    }
    resultWidth_ = width;
    resultHeight_ = height;
    resultFormat_ = format;
    return FFFResult::Success;
}

void RtxVideoProcessor::ReleaseResult() noexcept {
    if (resultView_ != nullptr) {
        resultView_->Release();
        resultView_ = nullptr;
    }
    if (resultTexture_ != nullptr) {
        resultTexture_->Release();
        resultTexture_ = nullptr;
    }
    if (middleTexture_ != nullptr) {
        middleTexture_->Release();
        middleTexture_ = nullptr;
    }
    resultWidth_ = resultHeight_ = 0;
    resultFormat_ = DXGI_FORMAT_UNKNOWN;
    path_ = RtxVideoPath::None;
}

FFFResult RtxVideoProcessor::Evaluate(ID3D11Device* device,
    ID3D11DeviceContext* context, ID3D11Texture2D* input,
    const std::uint32_t inputWidth, const std::uint32_t inputHeight,
    const DXGI_FORMAT inputFormat, const std::uint32_t outputWidth,
    const std::uint32_t outputHeight, const DXGI_FORMAT outputFormat,
    const bool runVsr, const bool runTrueHdr,
    const std::uint32_t maxLuminance) noexcept {
    if (input == nullptr || inputWidth == 0 || inputHeight == 0 ||
        outputWidth == 0 || outputHeight == 0 || inputFormat == DXGI_FORMAT_UNKNOWN ||
        outputFormat == DXGI_FORMAT_UNKNOWN || (!runVsr && !runTrueHdr))
        return FFFResult::InvalidArgument;

    const auto initialized = EnsureInitialized(device, context, runVsr, runTrueHdr);
    if (initialized != FFFResult::Success) return initialized;
#if !FFF_ENABLE_RTX_VIDEO
    return FFFResult::NotSupported;
#else
    if ((runVsr && !vsrAvailable_) || (runTrueHdr && !trueHdrAvailable_)) {
        state_ = RtxVideoState::Unavailable;
        SetError(runVsr && !vsrAvailable_
            ? "RTX Video Super Resolution is unavailable."
            : "RTX Video HDR is unavailable.");
        return FFFResult::NotSupported;
    }

    const auto processedWidth = runVsr ? outputWidth : inputWidth;
    const auto processedHeight = runVsr ? outputHeight : inputHeight;
    const auto expectedPath = runVsr && runTrueHdr ? RtxVideoPath::VsrThenTrueHdr :
        (runVsr ? RtxVideoPath::Vsr : RtxVideoPath::TrueHdr);
    const auto outputResult = EnsureOutput(processedWidth, processedHeight, outputFormat);
    if (outputResult != FFFResult::Success) {
        state_ = RtxVideoState::FailedForKey;
        return outputResult;
    }
    if (runVsr && runTrueHdr) {
        if (middleTexture_ != nullptr) {
            D3D11_TEXTURE2D_DESC middleDescription{};
            middleTexture_->GetDesc(&middleDescription);
            if (middleDescription.Width != processedWidth ||
                middleDescription.Height != processedHeight ||
                middleDescription.Format != inputFormat) {
                middleTexture_->Release();
                middleTexture_ = nullptr;
            }
        }
        if (middleTexture_ == nullptr) {
            D3D11_TEXTURE2D_DESC middleDescription{};
            middleDescription.Width = processedWidth;
            middleDescription.Height = processedHeight;
            middleDescription.MipLevels = middleDescription.ArraySize = 1;
            middleDescription.Format = inputFormat;
            middleDescription.SampleDesc.Count = 1;
            middleDescription.Usage = D3D11_USAGE_DEFAULT;
            middleDescription.BindFlags = D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(device_->CreateTexture2D(&middleDescription, nullptr,
                    &middleTexture_))) {
                state_ = RtxVideoState::FailedForKey;
                SetError("Could not create the RTX intermediate texture.");
                return FFFResult::DeviceFailure;
            }
        }
    }

    const auto evaluateStart = std::chrono::steady_clock::now();
    const MultithreadScope multithreadScope(sdk_->multithread);
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Success;
    if (runVsr) {
        NVSDK_NGX_D3D11_VSR_Eval_Params parameters{};
        parameters.pInput = input;
        parameters.pOutput = runTrueHdr ? middleTexture_ : resultTexture_;
        parameters.InputSubrectBase = {0, 0};
        parameters.InputSubrectSize = {inputWidth, inputHeight};
        parameters.OutputSubrectBase = {0, 0};
        parameters.OutputSubrectSize = {processedWidth, processedHeight};
        parameters.QualityLevel = NVSDK_NGX_VSR_Quality_Low;
        result = NGX_D3D11_EVALUATE_VSR_EXT(context_, sdk_->vsrFeature,
            sdk_->parameters, &parameters);
    }
    if (NVSDK_NGX_SUCCEED(result) && runTrueHdr) {
        NVSDK_NGX_D3D11_TRUEHDR_Eval_Params parameters{};
        parameters.pInput = runVsr ? middleTexture_ : input;
        parameters.pOutput = resultTexture_;
        parameters.InputSubrectTL = {0, 0};
        parameters.InputSubrectBR = {processedWidth, processedHeight};
        parameters.OutputSubrectTL = {0, 0};
        parameters.OutputSubrectBR = {processedWidth, processedHeight};
        parameters.Contrast = 100;
        parameters.Saturation = 100;
        parameters.MiddleGray = 50;
        parameters.MaxLuminance = std::clamp(maxLuminance, 400u, 2000u);
        result = NGX_D3D11_EVALUATE_TRUEHDR_EXT(context_, sdk_->trueHdrFeature,
            sdk_->parameters, &parameters);
    }
    if (NVSDK_NGX_FAILED(result)) {
        state_ = RtxVideoState::FailedForKey;
        SetSdkError(runVsr && runTrueHdr ? "RTX VSR/TrueHDR Evaluate" :
            (runVsr ? "RTX VSR Evaluate" : "RTX TrueHDR Evaluate"), result);
        return FFFResult::NotSupported;
    }

    path_ = expectedPath;
    state_ = RtxVideoState::Ready;
    ClearError();
    ++evaluateCount_;
    lastEvaluate100ns_ = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - evaluateStart).count() / 100);
    return FFFResult::Success;
#endif
}

void RtxVideoProcessor::Invalidate(const bool releaseResources) noexcept {
    // Invalidation is normally a frame-boundary operation. Keep the NGX output
    // and intermediate allocations alive so D3D11 can reuse them on the next
    // frame; releasing them here defers destruction until the GPU catches up
    // and causes unbounded VRAM growth during playback.
    if (releaseResources) ReleaseResult();
    path_ = RtxVideoPath::None;
    if (initialized_) state_ = RtxVideoState::Ready;
}

void RtxVideoProcessor::ReleaseScratch() noexcept {
#if FFF_ENABLE_RTX_VIDEO
    if (sdk_ == nullptr) return;
    if (sdk_->scratchBuffer != nullptr) {
        sdk_->scratchBuffer->Release();
        sdk_->scratchBuffer = nullptr;
    }
    sdk_->scratchSize = 0;
#endif
}

void RtxVideoProcessor::ReleaseFeatures() noexcept {
#if FFF_ENABLE_RTX_VIDEO
    if (sdk_ == nullptr) return;
    if (sdk_->multithread != nullptr) sdk_->multithread->Enter();
    if (sdk_->vsrFeature != nullptr) {
        NVSDK_NGX_D3D11_ReleaseFeature(sdk_->vsrFeature);
        sdk_->vsrFeature = nullptr;
    }
    if (sdk_->trueHdrFeature != nullptr) {
        NVSDK_NGX_D3D11_ReleaseFeature(sdk_->trueHdrFeature);
        sdk_->trueHdrFeature = nullptr;
    }
    ReleaseScratch();
    if (sdk_->ngxInitialized && device_ != nullptr)
        NVSDK_NGX_D3D11_Shutdown1(device_);
    if (sdk_->parameters != nullptr) {
        NVSDK_NGX_D3D11_DestroyParameters(sdk_->parameters);
        sdk_->parameters = nullptr;
    }
    sdk_->ngxInitialized = false;
    if (sdk_->multithread != nullptr) sdk_->multithread->Leave();
#endif
}

void RtxVideoProcessor::ReleaseDeviceObjects() noexcept {
    ReleaseResult();
    if (sdk_ != nullptr) ReleaseFeatures();
#if FFF_ENABLE_RTX_VIDEO
    if (sdk_ != nullptr && sdk_->multithread != nullptr) {
        sdk_->multithread->Release();
        sdk_->multithread = nullptr;
    }
#endif
    if (context_ != nullptr) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_ != nullptr) {
        device_->Release();
        device_ = nullptr;
    }
    initialized_ = false;
    vsrAvailable_ = false;
    trueHdrAvailable_ = false;
    state_ = RtxVideoState::NotInitialized;
}

bool RtxVideoProcessor::HasResult() const noexcept {
    return resultTexture_ != nullptr && resultView_ != nullptr &&
        resultWidth_ != 0 && resultHeight_ != 0;
}

ID3D11ShaderResourceView* RtxVideoProcessor::ResultView() const noexcept {
    return resultView_;
}

std::uint32_t RtxVideoProcessor::ResultWidth() const noexcept {
    return resultWidth_;
}

std::uint32_t RtxVideoProcessor::ResultHeight() const noexcept {
    return resultHeight_;
}

DXGI_FORMAT RtxVideoProcessor::ResultFormat() const noexcept {
    return resultFormat_;
}

RtxVideoStatus RtxVideoProcessor::Status() const {
    RtxVideoStatus status{};
    status.state = state_;
    status.path = path_;
    status.initialized = initialized_;
    status.vsrAvailable = vsrAvailable_;
    status.trueHdrAvailable = trueHdrAvailable_;
    status.resultWidth = resultWidth_;
    status.resultHeight = resultHeight_;
    status.resultFormat = resultFormat_;
    status.evaluateCount = evaluateCount_;
    status.lastEvaluate100ns = lastEvaluate100ns_;
    {
        std::lock_guard lock(statusMutex_);
        status.lastError = lastError_;
    }
    return status;
}
