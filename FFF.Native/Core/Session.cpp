#include "pch.h"
#include "Core/Session.h"
#include "Audio/WasapiCapture.h"
#include "Ffmpeg/VideoMuxer.h"

#include <filesystem>

extern "C" {
#include <libavutil/avutil.h>
}

namespace {
// 对诊断日志中的 UTF-8 文本执行最小且完整的 JSON 字符转义。日志只记录配置和错误，不记录
// 帧或音频内容；非 ASCII 字节保持原样，便于直接阅读中文路径和设备名称。
std::string EscapeDiagnosticJson(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) >= 0x20) result += character;
            break;
        }
    }
    return result;
}

// 把 Windows UTF-16 设备名称严格转换成 UTF-8，仅供诊断 JSON 使用；转换失败返回空文本，
// 不影响录制主流程，也不依赖系统活动代码页。
std::string DiagnosticUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

// 查询实际 D3D11 Device 所属适配器的名称、PCI 标识、LUID 和驱动版本。任何 DXGI 查询失败
// 都只减少诊断字段，不会用默认显卡信息冒充当前编码设备。
std::string DescribeGraphicsDevice(ID3D11Device* device) {
    if (device == nullptr) return "\"gpu\":\"unknown\"";
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter))) return "\"gpu\":\"unknown\"";
    DXGI_ADAPTER_DESC description{};
    adapter->GetDesc(&description);
    LARGE_INTEGER driverVersion{};
    const auto driverResult = adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &driverVersion);
    const auto version = static_cast<std::uint64_t>(driverVersion.QuadPart);
    const auto driverText = SUCCEEDED(driverResult) ?
        std::to_string((version >> 48) & 0xffff) + "." + std::to_string((version >> 32) & 0xffff) +
        "." + std::to_string((version >> 16) & 0xffff) + "." + std::to_string(version & 0xffff) : "unknown";
    return "\"gpu\":\"" + EscapeDiagnosticJson(DiagnosticUtf8(description.Description)) +
        "\",\"vendorId\":" + std::to_string(description.VendorId) +
        ",\"deviceId\":" + std::to_string(description.DeviceId) +
        ",\"adapterLuid\":\"" + std::to_string(description.AdapterLuid.HighPart) + ":" +
        std::to_string(description.AdapterLuid.LowPart) + "\",\"driverVersion\":\"" + driverText + "\"";
}

// 通过 ntdll 的 RtlGetVersion 取得未受应用 manifest 影响的 Windows 主/次/Build 版本。函数
// 动态解析且失败时返回 unknown，避免为一个诊断字段增加新的静态 DLL 依赖。
std::string DescribeWindowsVersion() {
    using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto function = module == nullptr ? nullptr : reinterpret_cast<RtlGetVersionFunction>(
        GetProcAddress(module, "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (function == nullptr || function(&version) != 0) return "unknown";
    return std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) +
        "." + std::to_string(version.dwBuildNumber);
}
}

// 复制全部由调用方持有的配置，并对 D3D11 Device 执行 AddRef，使托管包装器可在创建会话后
// 释放自己的引用。构造结束后，所有字符串都由本对象持有，不再指向托管内存。
RecorderSession::RecorderSession(const FFFSessionConfiguration& configuration)
    : device_(static_cast<ID3D11Device*>(configuration.d3d11Device)),
      outputPath_(configuration.outputPathUtf8 == nullptr ? "" : configuration.outputPathUtf8),
      encoderName_(configuration.encoderNameUtf8 == nullptr ? "" : configuration.encoderNameUtf8),
      width_(configuration.width), height_(configuration.height),
      frameRateNumerator_(configuration.frameRateNumerator),
      frameRateDenominator_(configuration.frameRateDenominator),
      bitRate_(configuration.bitRate), gopSize_(configuration.gopSize),
      bFrameCount_(configuration.bFrameCount), tenBit_(configuration.tenBit != 0),
      hdr10_(configuration.hdr10 != 0),
      systemAudioEndpointId_(configuration.systemAudioEndpointIdUtf8 == nullptr ? "" : configuration.systemAudioEndpointIdUtf8),
      microphoneEndpointId_(configuration.microphoneEndpointIdUtf8 == nullptr ? "" : configuration.microphoneEndpointIdUtf8),
      keepSeparateAudioTracks_(configuration.keepSeparateAudioTracks != 0),
      timelineStarted_(false), segmentVideoOffset_(0), segmentAudioOffset_(0),
      inputTextureFormat_(configuration.inputTextureFormat),
      chromaSampling_(configuration.chromaSampling),
      rateControl_(configuration.rateControl), quality_(configuration.quality),
      maximumBitRate_(configuration.maximumBitRate), lookaheadFrames_(configuration.lookaheadFrames),
      preset_(configuration.presetUtf8 == nullptr ? "" : configuration.presetUtf8),
      profile_(configuration.profileUtf8 == nullptr ? "" : configuration.profileUtf8),
      sceneOptimization_(configuration.sceneOptimizationUtf8 == nullptr ? "" : configuration.sceneOptimizationUtf8),
      multipass_(configuration.multipass), colorRange_(configuration.colorRange),
      systemAudioGain_(configuration.muteSystemAudio != 0 ? 0.0F : configuration.systemAudioGain),
      microphoneGain_(configuration.muteMicrophone != 0 ? 0.0F : configuration.microphoneGain),
      audioEncoderName_(configuration.audioEncoderNameUtf8 == nullptr ? "aac" : configuration.audioEncoderNameUtf8),
      audioSampleRate_(configuration.audioSampleRate == 0 ? 48'000U : configuration.audioSampleRate),
      audioChannelCount_(configuration.audioChannelCount == 0 ? 2U : configuration.audioChannelCount),
      audioBitRate_(configuration.audioBitRate < 0 ? 0 : configuration.audioBitRate),
      audioMode_(configuration.audioMode),
      followDefaultSystemAudioDevice_(configuration.followDefaultSystemAudioDevice != 0),
      qualityMode_(configuration.qualityMode),
      customVideoParameters_(configuration.customVideoParametersUtf8 == nullptr ? "" : configuration.customVideoParametersUtf8),
      diagnosticLogPath_(configuration.diagnosticLogPathUtf8 == nullptr ? "" : configuration.diagnosticLogPathUtf8),
      captureBackend_(configuration.captureBackendUtf8 == nullptr ? "" : configuration.captureBackendUtf8),
      sourceDescription_(configuration.sourceDescriptionUtf8 == nullptr ? "" : configuration.sourceDescriptionUtf8),
      sourceFormat_(configuration.sourceFormatUtf8 == nullptr ? "" : configuration.sourceFormatUtf8),
      diagnosticCallback_(configuration.diagnosticCallback),
      diagnosticCallbackContext_(configuration.diagnosticCallbackContext),
      state_(FFFSessionState::Created), submittedFrames_(0), droppedFrames_(0), repeatedFrames_(0),
      lastVideoQpc_(0), lastErrorCode_(0), lastEncodeMicroseconds_(0), peakEncodeMicroseconds_(0),
      trailerWritten_(false), completedVideoBytes_(0), completedAudioBytes_(0), diagnosticFirstEntry_(true),
      videoMuxer_(std::make_unique<VideoMuxer>()) {
    if (device_ != nullptr) {
        device_->AddRef();
    }
}

// 释放原生设备引用。析构函数不负责写文件尾；调用方销毁句柄前必须先用 Stop 正常关闭，
// 或用 Abort 明确执行紧急关闭。
RecorderSession::~RecorderSession() {
    for (auto& capture : audioCaptures_) capture->Stop();
    audioCaptures_.clear();
    if (videoMuxer_ != nullptr) {
        videoMuxer_->Abort();
    }
    if (device_ != nullptr) {
        device_->Release();
        device_ = nullptr;
    }
}

// 将配置完整的会话切换为 Running。此处再次验证配置，确保编码器初始化失败时捕获线程尚未产帧。
FFFResult RecorderSession::Start() noexcept {
    std::scoped_lock lock(mutex_);
    if (state_.load() != FFFSessionState::Created) {
        return FFFResult::InvalidState;
    }
    if (device_ == nullptr || outputPath_.empty() || encoderName_.empty() || width_ == 0 || height_ == 0 ||
        frameRateNumerator_ == 0 || frameRateDenominator_ == 0) {
        SetError(FFFResult::InvalidArgument, "The recording configuration is incomplete.");
        return FFFResult::InvalidArgument;
    }
    if (!diagnosticLogPath_.empty()) {
        try {
            const std::u8string utf8Path(reinterpret_cast<const char8_t*>(diagnosticLogPath_.data()),
                reinterpret_cast<const char8_t*>(diagnosticLogPath_.data() + diagnosticLogPath_.size()));
            diagnosticLog_.open(std::filesystem::path(utf8Path), std::ios::out | std::ios::trunc);
            if (diagnosticLog_) diagnosticLog_ << "[\n";
        } catch (...) {
            diagnosticLog_.setstate(std::ios::failbit);
        }
        if (!diagnosticLog_) {
            SetError(FFFResult::NativeFailure, "Could not create the recording diagnostic log.");
            state_.store(FFFSessionState::Failed);
            return FFFResult::NativeFailure;
        }
    }
    WriteDiagnostic("start", "\"windows\":\"" + DescribeWindowsVersion() +
        "\",\"ffmpeg\":\"" + EscapeDiagnosticJson(av_version_info()) + "\"," + DescribeGraphicsDevice(device_) +
        ",\"captureBackend\":\"" + EscapeDiagnosticJson(captureBackend_) +
        "\",\"sourceDescription\":\"" + EscapeDiagnosticJson(sourceDescription_) +
        "\",\"sourceFormat\":\"" + EscapeDiagnosticJson(sourceFormat_) +
        "\",\"encoder\":\"" + EscapeDiagnosticJson(encoderName_) +
        "\",\"width\":" + std::to_string(width_) + ",\"height\":" + std::to_string(height_) +
        ",\"fpsNumerator\":" + std::to_string(frameRateNumerator_) +
        ",\"fpsDenominator\":" + std::to_string(frameRateDenominator_) +
        ",\"bitRate\":" + std::to_string(bitRate_) +
        ",\"maximumBitRate\":" + std::to_string(maximumBitRate_) +
        ",\"rateControl\":" + std::to_string(rateControl_) +
        ",\"quality\":" + std::to_string(quality_) +
        ",\"gopSize\":" + std::to_string(gopSize_) +
        ",\"bFrames\":" + std::to_string(bFrameCount_) +
        ",\"lookahead\":" + std::to_string(lookaheadFrames_) +
        ",\"preset\":\"" + EscapeDiagnosticJson(preset_) +
        "\",\"profile\":\"" + EscapeDiagnosticJson(profile_) +
        "\",\"sceneOptimization\":\"" + EscapeDiagnosticJson(sceneOptimization_) +
        "\",\"audioEncoder\":\"" + EscapeDiagnosticJson(audioEncoderName_) +
        "\",\"audioSampleRate\":" + std::to_string(audioSampleRate_) +
        ",\"audioChannels\":" + std::to_string(audioChannelCount_) +
        ",\"multipass\":" + std::to_string(multipass_) +
        ",\"chromaSampling\":" + std::to_string(chromaSampling_) +
        ",\"colorRange\":" + std::to_string(colorRange_) +
        ",\"tenBit\":" + (tenBit_ ? "true" : "false") +
        ",\"hdr10\":" + (hdr10_ ? "true" : "false"));
    const bool mixAudioSources = !keepSeparateAudioTracks_ &&
        !systemAudioEndpointId_.empty() && !microphoneEndpointId_.empty();
    std::vector<float> audioSourceGains;
    if (!systemAudioEndpointId_.empty()) audioSourceGains.push_back(systemAudioGain_);
    if (!microphoneEndpointId_.empty()) audioSourceGains.push_back(microphoneGain_);
    const auto initialized = videoMuxer_->Initialize(device_, outputPath_, encoderName_, width_, height_,
        frameRateNumerator_, frameRateDenominator_, bitRate_, gopSize_, bFrameCount_, tenBit_, hdr10_,
        mixAudioSources, inputTextureFormat_, chromaSampling_,
        rateControl_, qualityMode_, customVideoParameters_, quality_, maximumBitRate_, lookaheadFrames_, preset_, profile_, sceneOptimization_,
        multipass_, colorRange_, audioSourceGains, audioEncoderName_, audioSampleRate_,
        audioChannelCount_, audioBitRate_, audioMode_);
    if (initialized != FFFResult::Success) {
        SetError(initialized, videoMuxer_->LastError());
        state_.store(FFFSessionState::Failed);
        WriteDiagnostic("encoder_initialization_failed", "\"message\":\"" +
            EscapeDiagnosticJson(videoMuxer_->LastError()) + "\"");
        return initialized;
    }
    state_.store(FFFSessionState::Running);
    std::size_t trackIndex = 0;
    auto audioResult = FFFResult::Success;
    if (!systemAudioEndpointId_.empty()) {
        std::unique_ptr<WasapiCapture> capture;
        audioResult = CreateAudioCapture(systemAudioEndpointId_, true, trackIndex++, capture);
        if (audioResult == FFFResult::Success) audioCaptures_.push_back(std::move(capture));
    }
    if (audioResult == FFFResult::Success && !microphoneEndpointId_.empty()) {
        std::unique_ptr<WasapiCapture> capture;
        audioResult = CreateAudioCapture(microphoneEndpointId_, false, trackIndex, capture);
        if (audioResult == FFFResult::Success) audioCaptures_.push_back(std::move(capture));
    }
    if (audioResult != FFFResult::Success) {
        CollectAudioStatistics();
        for (auto& capture : audioCaptures_) capture->Stop();
        audioCaptures_.clear();
        videoMuxer_->Abort();
        state_.store(FFFSessionState::Failed);
        return audioResult;
    }
    return FFFResult::Success;
}

// 接收一张由调用方持有的 D3D11 纹理，验证原始 QPC 时间戳严格递增，并同步完成 GPU 到
// FFmpeg surface 的复制和硬件编码提交；已编码 packet 随后由独立文件线程异步写入。
FFFResult RecorderSession::Submit(ID3D11Texture2D* texture, const std::uint32_t textureArrayIndex,
    const std::int64_t qpcTimestamp, const bool repeatedFrame) noexcept {
    std::scoped_lock lock(mutex_);
    static_cast<void>(textureArrayIndex);
    if (state_.load() != FFFSessionState::Running) {
        return FFFResult::InvalidState;
    }
    if (texture == nullptr || qpcTimestamp <= 0) {
        return FFFResult::InvalidArgument;
    }
    const auto previous = lastVideoQpc_.load();
    if (previous != 0 && qpcTimestamp <= previous) {
        SetError(FFFResult::InvalidArgument, "Video timestamps must increase monotonically.");
        return FFFResult::InvalidArgument;
    }
    if (!timelineStarted_) {
        timeline_.Reset(qpcTimestamp);
        timelineStarted_ = true;
    }
    const auto presentationTimestamp = std::max<std::int64_t>(0,
        timeline_.ToMediaTicks(qpcTimestamp, frameRateNumerator_) / frameRateDenominator_ - segmentVideoOffset_);
    LARGE_INTEGER encodeStarted{};
    QueryPerformanceCounter(&encodeStarted);
    const auto encoded = videoMuxer_->Encode(texture, textureArrayIndex, presentationTimestamp);
    LARGE_INTEGER encodeFinished{};
    QueryPerformanceCounter(&encodeFinished);
    const auto microseconds = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
        (encodeFinished.QuadPart - encodeStarted.QuadPart) * 1'000'000LL / timeline_.Frequency()));
    lastEncodeMicroseconds_.store(microseconds);
    auto peak = peakEncodeMicroseconds_.load();
    while (peak < microseconds && !peakEncodeMicroseconds_.compare_exchange_weak(peak, microseconds)) {}
    if (encoded != FFFResult::Success) {
        SetError(encoded, videoMuxer_->LastError());
        state_.store(FFFSessionState::Failed);
        WriteDiagnostic("video_failed", "\"message\":\"" + EscapeDiagnosticJson(videoMuxer_->LastError()) + "\"");
        return encoded;
    }
    lastVideoQpc_.store(qpcTimestamp);
    submittedFrames_.fetch_add(1);
    if (repeatedFrame) repeatedFrames_.fetch_add(1);
    return FFFResult::Success;
}

// 累加托管捕获/CFR 层在进入编码器前主动丢弃的源帧。此计数不改变媒体时间线，也不把编码
// 失败误算为调度丢帧；零数量是合法空操作，便于调用方批量汇报。
FFFResult RecorderSession::ReportDroppedFrames(const std::uint32_t frameCount) noexcept {
    const auto state = state_.load();
    if (state != FFFSessionState::Running && state != FFFSessionState::Paused) return FFFResult::InvalidState;
    droppedFrames_.fetch_add(frameCount);
    return FFFResult::Success;
}

// 接收托管 WGC/DXGI 层产生的结构化事件，并通过与 Native 错误相同的 JSON 数组/回调通道上报。
// 字符串只在调用期间借用；message 会经过 JSON 转义，调用方无需构造 JSON 也不会破坏日志格式。
FFFResult RecorderSession::ReportDiagnosticEvent(const char* eventName, const char* message) noexcept {
    if (eventName == nullptr || *eventName == '\0') return FFFResult::InvalidArgument;
    try {
        const std::string text = message == nullptr ? "" : message;
        WriteDiagnostic(eventName, "\"message\":\"" + EscapeDiagnosticJson(text) + "\"");
        return FFFResult::Success;
    } catch (...) {
        return FFFResult::NativeFailure;
    }
}

// 在指定时间戳暂停共享 QPC 时间线，并拒绝重复或倒退的状态转换。Paused 状态下不接收视频帧。
FFFResult RecorderSession::Pause(const std::int64_t qpcTimestamp) noexcept {
    std::scoped_lock lock(mutex_);
    if (state_.load() != FFFSessionState::Running || !timeline_.Pause(qpcTimestamp)) {
        return FFFResult::InvalidState;
    }
    state_.store(FFFSessionState::Paused);
    WriteDiagnostic("pause", "\"mediaQpc\":" + std::to_string(qpcTimestamp));
    return FFFResult::Success;
}

// 在同一时间线上恢复所有媒体，并从后续 PTS 扣除精确暂停时长。音频和视频必须共用同一恢复时间戳。
FFFResult RecorderSession::Resume(const std::int64_t qpcTimestamp) noexcept {
    std::scoped_lock lock(mutex_);
    if (state_.load() != FFFSessionState::Paused || !timeline_.Resume(qpcTimestamp)) {
        return FFFResult::InvalidState;
    }
    state_.store(FFFSessionState::Running);
    WriteDiagnostic("resume", "\"mediaQpc\":" + std::to_string(qpcTimestamp));
    return FFFResult::Success;
}

// 正常结束当前 Matroska 后，在同一捕获会话中建立新的编码/封装器。调用期间持有会话锁，
// 因此音频回调和视频提交会在文件边界处短暂等待，而不会向已经关闭的 muxer 写入。
FFFResult RecorderSession::Split(const char* outputPathUtf8) noexcept {
    if (outputPathUtf8 == nullptr || *outputPathUtf8 == '\0') return FFFResult::InvalidArgument;
    std::scoped_lock lock(mutex_);
    if (state_.load() != FFFSessionState::Running) return FFFResult::InvalidState;

    const auto finished = videoMuxer_->Finish();
    if (finished != FFFResult::Success) {
        SetError(finished, videoMuxer_->LastError());
        state_.store(FFFSessionState::Failed);
        return finished;
    }
    completedVideoBytes_ += videoMuxer_->VideoBytes();
    completedAudioBytes_ += videoMuxer_->AudioBytes();

    const bool mixAudioSources = !keepSeparateAudioTracks_ &&
        !systemAudioEndpointId_.empty() && !microphoneEndpointId_.empty();
    std::vector<float> audioSourceGains;
    if (!systemAudioEndpointId_.empty()) audioSourceGains.push_back(systemAudioGain_);
    if (!microphoneEndpointId_.empty()) audioSourceGains.push_back(microphoneGain_);

    auto nextMuxer = std::make_unique<VideoMuxer>();
    const auto initialized = nextMuxer->Initialize(device_, outputPathUtf8, encoderName_, width_, height_,
        frameRateNumerator_, frameRateDenominator_, bitRate_, gopSize_, bFrameCount_, tenBit_, hdr10_,
        mixAudioSources, inputTextureFormat_, chromaSampling_,
        rateControl_, qualityMode_, customVideoParameters_, quality_, maximumBitRate_, lookaheadFrames_, preset_, profile_, sceneOptimization_,
        multipass_, colorRange_, audioSourceGains, audioEncoderName_, audioSampleRate_,
        audioChannelCount_, audioBitRate_, audioMode_);
    if (initialized != FFFResult::Success) {
        SetError(initialized, nextMuxer->LastError());
        state_.store(FFFSessionState::Failed);
        return initialized;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    segmentVideoOffset_ = timelineStarted_ ?
        timeline_.ToMediaTicks(now.QuadPart, frameRateNumerator_) / frameRateDenominator_ : 0;
    segmentAudioOffset_ = timelineStarted_ ? timeline_.ToMediaTicks(now.QuadPart, audioSampleRate_) : 0;
    videoMuxer_ = std::move(nextMuxer);
    outputPath_ = outputPathUtf8;
    WriteDiagnostic("split", "\"outputPath\":\"" + EscapeDiagnosticJson(outputPath_) + "\"");
    return FFFResult::Success;
}

FFFResult RecorderSession::SwitchSystemAudioEndpoint(const char* endpointIdUtf8) noexcept {
    if (endpointIdUtf8 == nullptr || *endpointIdUtf8 == '\0') return FFFResult::InvalidArgument;
    std::unique_ptr<WasapiCapture> previousCapture;
    {
        std::scoped_lock lock(mutex_);
        const auto state = state_.load();
        if ((state != FFFSessionState::Running && state != FFFSessionState::Paused) ||
            systemAudioEndpointId_.empty() || audioCaptures_.empty()) return FFFResult::InvalidState;
        if (systemAudioEndpointId_ == endpointIdUtf8) return FFFResult::Success;
        previousCapture = std::move(audioCaptures_.front());
        audioCaptures_.erase(audioCaptures_.begin());
    }
    previousCapture->Stop();

    std::unique_ptr<WasapiCapture> replacement;
    const auto started = CreateAudioCapture(endpointIdUtf8, true, 0, replacement);
    if (started != FFFResult::Success) {
        if (previousCapture->Start() == FFFResult::Success) {
            std::scoped_lock lock(mutex_);
            audioCaptures_.insert(audioCaptures_.begin(), std::move(previousCapture));
        }
        return started;
    }
    {
        std::scoped_lock lock(mutex_);
        systemAudioEndpointId_ = endpointIdUtf8;
        audioCaptures_.insert(audioCaptures_.begin(), std::move(replacement));
        completedAudioStatistics_.clear();
        completedAudioTimelineErrors_.clear();
        completedAudioCompensationPpm_.clear();
        WriteDiagnostic("system_audio_endpoint_switched", "\"endpoint\":\"" +
            EscapeDiagnosticJson(systemAudioEndpointId_) + "\"");
    }
    return FFFResult::Success;
}

FFFResult RecorderSession::CreateAudioCapture(const std::string& endpointId, const bool loopback,
    const std::size_t trackIndex, std::unique_ptr<WasapiCapture>& capture) noexcept {
    if (endpointId.empty()) return FFFResult::InvalidArgument;
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        endpointId.c_str(), -1, nullptr, 0);
    if (required <= 1) return FFFResult::InvalidArgument;
    std::wstring wideId(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, endpointId.c_str(), -1,
        wideId.data(), required);
    wideId.resize(static_cast<std::size_t>(required - 1));
    auto packetCallback = [this, trackIndex](const std::uint8_t* data, const std::uint32_t frameCount,
        const std::uint32_t flags, const std::uint64_t qpcPosition100ns,
        const WasapiSampleFormat& format) -> FFFResult {
        std::scoped_lock callbackLock(mutex_);
        if (state_.load() != FFFSessionState::Running || !timelineStarted_) return FFFResult::Success;
        const auto frequency = timeline_.Frequency();
        const auto wholeSeconds = qpcPosition100ns / 10'000'000ULL;
        const auto remaining100ns = qpcPosition100ns % 10'000'000ULL;
        const auto rawQpc = static_cast<std::int64_t>(wholeSeconds * frequency +
            (remaining100ns * frequency) / 10'000'000ULL);
        const auto targetSample = std::max<std::int64_t>(0,
            timeline_.ToMediaTicks(rawQpc, audioSampleRate_) - segmentAudioOffset_);
        const auto encoded = videoMuxer_->EncodeAudio(trackIndex, data, frameCount, flags,
            targetSample, format);
        if (encoded != FFFResult::Success) {
            SetError(encoded, videoMuxer_->LastError());
            state_.store(FFFSessionState::Failed);
            WriteDiagnostic("audio_failed", "\"message\":\"" +
                EscapeDiagnosticJson(videoMuxer_->LastError()) + "\"");
        }
        return encoded;
    };
    auto failureCallback = [this, trackIndex](const std::string& message) {
        std::scoped_lock callbackLock(mutex_);
        const auto state = state_.load();
        if (state != FFFSessionState::Running && state != FFFSessionState::Paused) return;
        WriteDiagnostic("audio_device_failed", "\"message\":\"" +
            EscapeDiagnosticJson(message) + "\"");
        if (trackIndex == 0 && followDefaultSystemAudioDevice_) return;
        SetError(FFFResult::DeviceFailure, message);
        state_.store(FFFSessionState::Failed);
    };
    try {
        capture = std::make_unique<WasapiCapture>(std::move(wideId), loopback,
            std::move(packetCallback), std::move(failureCallback));
    } catch (...) {
        return FFFResult::NativeFailure;
    }
    const auto started = capture->Start();
    if (started != FFFResult::Success) SetError(started, capture->LastError());
    return started;
}

// 执行正常终止转换。成功停止后重复调用仍返回成功；此过程排空音频 FIFO、音视频编码器和异步
// packet 队列，最后写入 Matroska trailer。
FFFResult RecorderSession::Stop() noexcept {
    {
        std::scoped_lock lock(mutex_);
        const auto state = state_.load();
        if (state == FFFSessionState::Stopped) return FFFResult::Success;
        if (state != FFFSessionState::Running && state != FFFSessionState::Paused &&
            state != FFFSessionState::Failed) return FFFResult::InvalidState;
        state_.store(FFFSessionState::Stopping);
    }
    for (auto& capture : audioCaptures_) capture->Stop();
    CollectAudioStatistics();
    audioCaptures_.clear();
    std::scoped_lock lock(mutex_);
    const auto finished = videoMuxer_->Finish();
    if (finished != FFFResult::Success) {
        SetError(finished, videoMuxer_->LastError());
        state_.store(FFFSessionState::Failed);
        return finished;
    }
    state_.store(FFFSessionState::Stopped);
    trailerWritten_ = true;
    WriteDiagnostic("stop", "\"submittedFrames\":" + std::to_string(submittedFrames_.load()) +
        ",\"repeatedFrames\":" + std::to_string(repeatedFrames_.load()) +
        ",\"peakEncodeMicroseconds\":" + std::to_string(peakEncodeMicroseconds_.load()) +
        ",\"trailerWritten\":true");
    if (diagnosticLog_.is_open()) {
        diagnosticLog_ << "\n]\n";
        diagnosticLog_.close();
    }
    return FFFResult::Success;
}

// 将会话标记为强制中止。此路径刻意跳过正常文件尾语义，并保持幂等，异常清理无需先检查状态。
FFFResult RecorderSession::Abort() noexcept {
    {
        std::scoped_lock lock(mutex_);
        state_.store(FFFSessionState::Aborted);
    }
    for (auto& capture : audioCaptures_) capture->Stop();
    CollectAudioStatistics();
    audioCaptures_.clear();
    std::scoped_lock lock(mutex_);
    videoMuxer_->Abort();
    WriteDiagnostic("abort", "\"trailerWritten\":false");
    if (diagnosticLog_.is_open()) {
        diagnosticLog_ << "\n]\n";
        diagnosticLog_.close();
    }
    return FFFResult::Success;
}

// 把一致的诊断快照复制到调用方内存。调用方负责初始化 size 和 version；本方法不返回原生存储指针。
FFFResult RecorderSession::GetStatistics(FFFSessionStatistics& statistics) const noexcept {
    if (statistics.size < sizeof(FFFSessionStatistics) || statistics.version != 1) {
        return FFFResult::InvalidArgument;
    }
    std::scoped_lock lock(mutex_);
    statistics.state = state_.load();
    statistics.submittedFrames = submittedFrames_.load();
    statistics.droppedFrames = droppedFrames_.load();
    statistics.repeatedFrames = repeatedFrames_.load();
    statistics.lastVideoQpc = lastVideoQpc_.load();
    statistics.pauseDurationQpc = timeline_.PausedTicks();
    statistics.lastErrorCode = lastErrorCode_.load();
    statistics.queueDepth = videoMuxer_->QueueDepth();
    statistics.lastEncodeMicroseconds = lastEncodeMicroseconds_.load();
    statistics.peakEncodeMicroseconds = peakEncodeMicroseconds_.load();
    std::vector<WasapiCaptureStatistics> audioStatistics = completedAudioStatistics_;
    if (audioStatistics.empty()) {
        audioStatistics.reserve(audioCaptures_.size());
        for (const auto& capture : audioCaptures_) audioStatistics.push_back(capture->Statistics());
    }
    WasapiCaptureStatistics system{};
    WasapiCaptureStatistics microphone{};
    std::size_t audioIndex = 0;
    if (!systemAudioEndpointId_.empty() && audioIndex < audioStatistics.size()) system = audioStatistics[audioIndex++];
    if (!microphoneEndpointId_.empty() && audioIndex < audioStatistics.size()) microphone = audioStatistics[audioIndex];
    statistics.systemAudioDiscontinuities = system.discontinuityCount;
    statistics.microphoneDiscontinuities = microphone.discontinuityCount;
    statistics.systemAudioTimestampErrors = system.timestampErrorCount;
    statistics.microphoneTimestampErrors = microphone.timestampErrorCount;
    statistics.systemAudioDriftMicroseconds = CalculateAudioDriftMicroseconds(system);
    statistics.microphoneDriftMicroseconds = CalculateAudioDriftMicroseconds(microphone);
    statistics.trailerWritten = trailerWritten_ ? 1U : 0U;
    statistics.reserved2 = 0;
    statistics.peakQueueDepth = videoMuxer_->PeakQueueDepth();
    statistics.reserved3 = 0;
    statistics.lastWriteMicroseconds = videoMuxer_->LastWriteMicroseconds();
    statistics.peakWriteMicroseconds = videoMuxer_->PeakWriteMicroseconds();
    const auto audioSourceCount = static_cast<std::size_t>(!systemAudioEndpointId_.empty()) +
        static_cast<std::size_t>(!microphoneEndpointId_.empty());
    std::vector<std::int64_t> timelineErrors = completedAudioTimelineErrors_;
    std::vector<std::int32_t> compensationPpm = completedAudioCompensationPpm_;
    if (timelineErrors.empty()) {
        timelineErrors.reserve(audioSourceCount);
        compensationPpm.reserve(audioSourceCount);
        for (std::size_t index = 0; index < audioSourceCount; ++index) {
            timelineErrors.push_back(videoMuxer_->AudioTimelineErrorSamples(index));
            compensationPpm.push_back(videoMuxer_->AudioCompensationPpm(index));
        }
    }
    audioIndex = 0;
    if (!systemAudioEndpointId_.empty()) {
        statistics.systemAudioTimelineErrorMicroseconds =
            timelineErrors[audioIndex] * 1'000'000LL / audioSampleRate_;
        statistics.systemAudioCompensationPpm = compensationPpm[audioIndex++];
    } else {
        statistics.systemAudioTimelineErrorMicroseconds = 0;
        statistics.systemAudioCompensationPpm = 0;
    }
    if (!microphoneEndpointId_.empty()) {
        statistics.microphoneTimelineErrorMicroseconds =
            timelineErrors[audioIndex] * 1'000'000LL / audioSampleRate_;
        statistics.microphoneCompensationPpm = compensationPpm[audioIndex];
    } else {
        statistics.microphoneTimelineErrorMicroseconds = 0;
    statistics.microphoneCompensationPpm = 0;
    }
    statistics.videoBytes = completedVideoBytes_ + videoMuxer_->VideoBytes();
    statistics.audioBytes = completedAudioBytes_ + videoMuxer_->AudioBytes();
    statistics.audioChannelCount = system.channelCount;
    statistics.audioChannelMask = system.channelMask;
    statistics.systemAudioPeak = system.peakLevel;
    statistics.microphonePeak = microphone.peakLevel;
    return FFFResult::Success;
}

// 返回最新 UTF-8 可读错误的副本。复制期间持有互斥锁，避免并发失败回调使字符串存储失效。
std::string RecorderSession::LastError() const {
    std::scoped_lock lock(mutex_);
    return lastError_;
}

// 保存一次结构化失败：数值错误码使用原子变量，变长消息受会话互斥锁保护。即使赋值消息时发生
// 内存分配失败，本方法也不会让异常越过 DLL 边界。
void RecorderSession::SetError(const FFFResult result, std::string message) noexcept {
    lastErrorCode_.store(static_cast<std::int32_t>(result));
    try {
        lastError_ = std::move(message);
    } catch (...) {
        lastError_ = "Native error message allocation failed.";
    }
}

// 向 JSON 数组追加一条诊断事件。每条记录包含原始 QPC，detail 只能是已经转义的 JSON 字段片段；
// 写入失败不会递归覆盖主录制错误，避免日志磁盘故障掩盖编码器的首要失败原因。
void RecorderSession::WriteDiagnostic(const char* eventName, const std::string& detail) noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    try {
        if (diagnosticLog_.is_open()) {
            std::scoped_lock lock(diagnosticMutex_);
            if (!diagnosticFirstEntry_) diagnosticLog_ << ",\n";
            diagnosticFirstEntry_ = false;
            diagnosticLog_ << "  {\"event\":\"" << eventName << "\",\"qpc\":" << now.QuadPart;
            if (!detail.empty()) diagnosticLog_ << ',' << detail;
            diagnosticLog_ << "}\n";
            diagnosticLog_.flush();
        }
    } catch (...) {
    }
    if (diagnosticCallback_ != nullptr) {
        try {
            const auto callbackDetail = detail.empty() ? std::string("{}") : "{" + detail + "}";
            diagnosticCallback_(diagnosticCallbackContext_, eventName, callbackDetail.c_str());
        } catch (...) {}
    }
}

// 在停止或中止并清空采集器之前保存每个端点的最终包级统计。重复调用只在仍有活动采集器时
// 更新快照，确保 Stop 后的统计查询仍能读取 discontinuity、timestamp error 和漂移。
void RecorderSession::CollectAudioStatistics() noexcept {
    if (audioCaptures_.empty()) return;
    completedAudioStatistics_.clear();
    completedAudioStatistics_.reserve(audioCaptures_.size());
    for (const auto& capture : audioCaptures_) completedAudioStatistics_.push_back(capture->Statistics());
    completedAudioTimelineErrors_.clear();
    completedAudioCompensationPpm_.clear();
    completedAudioTimelineErrors_.reserve(audioCaptures_.size());
    completedAudioCompensationPpm_.reserve(audioCaptures_.size());
    for (std::size_t index = 0; index < audioCaptures_.size(); ++index) {
        completedAudioTimelineErrors_.push_back(videoMuxer_->AudioTimelineErrorSamples(index));
        completedAudioCompensationPpm_.push_back(videoMuxer_->AudioCompensationPpm(index));
    }
}

// 用端点 device sample position 与 WASAPI 包 QPC position 的总跨度估算时钟漂移。返回微秒；
// 包不足、采样率未知或计数倒退时返回零，避免把无效首包统计解释成真实漂移。
std::int64_t RecorderSession::CalculateAudioDriftMicroseconds(
    const WasapiCaptureStatistics& statistics) noexcept {
    if (statistics.packetCount < 2 || statistics.sampleRate == 0 ||
        statistics.lastDevicePosition < statistics.firstDevicePosition ||
        statistics.lastQpc100ns < statistics.firstQpc100ns) return 0;
    const auto deviceFrames = statistics.lastDevicePosition - statistics.firstDevicePosition;
    const auto deviceDuration100ns = static_cast<std::int64_t>(deviceFrames * 10'000'000ULL /
        statistics.sampleRate);
    const auto qpcDuration100ns = static_cast<std::int64_t>(statistics.lastQpc100ns -
        statistics.firstQpc100ns);
    return (qpcDuration100ns - deviceDuration100ns) / 10;
}
