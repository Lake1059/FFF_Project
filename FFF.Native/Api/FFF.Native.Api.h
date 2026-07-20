#pragma once

#include <cstdint>

#ifdef FFFNATIVE_EXPORTS
#define FFF_API extern "C" __declspec(dllexport)
#else
#define FFF_API extern "C" __declspec(dllimport)
#endif

// C ABI 导出名刻意使用 ASCII，以避免 PE 导出表、GetProcAddress 和 P/Invoke 对 Unicode 名称的
// 编码差异。这些名称只是私有互操作细节；面向调用方的 VB.NET public API 使用中文方法名和参数名。

enum class FFFResult : std::int32_t {
    Success = 0,
    InvalidArgument = -1,
    InvalidState = -2,
    BufferTooSmall = -3,
    NativeFailure = -4,
    FfmpegFailure = -5,
    DeviceFailure = -6,
    NotSupported = -7,
};

enum class FFFSessionState : std::uint32_t {
    Created = 0,
    Running = 1,
    Paused = 2,
    Stopping = 3,
    Stopped = 4,
    Failed = 5,
    Aborted = 6,
};

struct FFFVersionedHeader {
    std::uint32_t size;
    std::uint32_t version;
};

using FFFDiagnosticCallback = void(__cdecl*)(void* context, const char* eventNameUtf8,
    const char* detailJsonUtf8);

struct FFFSessionConfiguration {
    std::uint32_t size;
    std::uint32_t version;
    void* d3d11Device;
    const char* outputPathUtf8;
    const char* encoderNameUtf8;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t frameRateNumerator;
    std::uint32_t frameRateDenominator;
    std::int64_t bitRate;
    std::uint32_t gopSize;
    std::uint32_t bFrameCount;
    std::uint32_t tenBit;
    std::uint32_t hdr10;
    const char* systemAudioEndpointIdUtf8;
    const char* microphoneEndpointIdUtf8;
    std::uint32_t keepSeparateAudioTracks;
    std::uint32_t inputTextureFormat;
    std::uint32_t chromaSampling;
    std::uint32_t rateControl;
    std::int32_t quality;
    std::int64_t maximumBitRate;
    std::uint32_t lookaheadFrames;
    const char* presetUtf8;
    const char* profileUtf8;
    std::uint32_t multipass;
    std::uint32_t colorRange;
    float systemAudioGain;
    float microphoneGain;
    std::uint32_t muteSystemAudio;
    std::uint32_t muteMicrophone;
    const char* diagnosticLogPathUtf8;
    const char* captureBackendUtf8;
    const char* sourceDescriptionUtf8;
    const char* sourceFormatUtf8;
    FFFDiagnosticCallback diagnosticCallback;
    void* diagnosticCallbackContext;
    const char* audioEncoderNameUtf8;
    std::uint32_t audioSampleRate;
    std::uint32_t audioChannelCount;
    std::int64_t audioBitRate;
    std::uint32_t audioMode;
    const char* sceneOptimizationUtf8;
    std::uint32_t followDefaultSystemAudioDevice;
    std::uint32_t qualityMode;
    const char* customVideoParametersUtf8;
};

struct FFFSessionStatistics {
    std::uint32_t size;
    std::uint32_t version;
    FFFSessionState state;
    std::uint32_t reserved;
    std::uint64_t submittedFrames;
    std::uint64_t droppedFrames;
    std::uint64_t repeatedFrames;
    std::int64_t lastVideoQpc;
    std::int64_t pauseDurationQpc;
    std::int32_t lastErrorCode;
    std::uint32_t queueDepth;
    std::uint64_t lastEncodeMicroseconds;
    std::uint64_t peakEncodeMicroseconds;
    std::uint64_t systemAudioDiscontinuities;
    std::uint64_t microphoneDiscontinuities;
    std::uint64_t systemAudioTimestampErrors;
    std::uint64_t microphoneTimestampErrors;
    std::int64_t systemAudioDriftMicroseconds;
    std::int64_t microphoneDriftMicroseconds;
    std::uint32_t trailerWritten;
    std::uint32_t reserved2;
    std::uint32_t peakQueueDepth;
    std::uint32_t reserved3;
    std::uint64_t lastWriteMicroseconds;
    std::uint64_t peakWriteMicroseconds;
    std::int64_t systemAudioTimelineErrorMicroseconds;
    std::int64_t microphoneTimelineErrorMicroseconds;
    std::int32_t systemAudioCompensationPpm;
    std::int32_t microphoneCompensationPpm;
    std::uint64_t videoBytes;
    std::uint64_t audioBytes;
    std::uint32_t audioChannelCount;
    std::uint32_t audioChannelMask;
    float systemAudioPeak;
    float microphonePeak;
};

using FFFSessionHandle = void*;

// 返回稳定 C ABI 主版本；不分配内存且可在任何会话创建前调用。
FFF_API std::uint32_t FFF_GetApiVersion() noexcept;
// 执行 QPC 与 FFmpeg major 自检，并用两次调用容量协议返回 UTF-8 JSON。
FFF_API FFFResult FFF_RunSelfTest(char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// 返回实际加载的 FFmpeg 版本与 ABI 信息；outputUtf8 由调用方分配和释放。
FFF_API FFFResult FFF_GetRuntimeInfo(char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// 枚举活动播放和采集端点并返回 UTF-8 JSON 数组，不保留任何端点 COM 指针。
FFF_API FFFResult FFF_EnumerateAudioEndpoints(char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// 对一个端点执行限定时长的事件驱动采集自检；loopback 非零时端点必须属于播放设备。
FFF_API FFFResult FFF_TestAudioEndpoint(const char* endpointIdUtf8, std::uint32_t loopback,
    std::uint32_t durationMilliseconds, char* outputUtf8, std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept;
// 使用保守 NV12 软件配置真实打开指定编码器，返回支持状态及 FFmpeg 原因 JSON。
FFF_API FFFResult FFF_ProbeEncoder(const char* encoderNameUtf8, std::uint32_t width, std::uint32_t height,
    std::uint32_t frameRateNumerator, std::uint32_t frameRateDenominator,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// 用配置中的实际 D3D11 Device、格式和选项初始化硬件编码路径；探测过程只写 Windows NUL。
FFF_API FFFResult FFF_ProbeD3D11Encoder(const FFFSessionConfiguration* configuration,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// 验证并复制版本化配置，成功时返回必须由 FFF_DestroySession 销毁的不透明会话句柄。
FFF_API FFFResult FFF_CreateSession(const FFFSessionConfiguration* configuration, FFFSessionHandle* session) noexcept;
// 初始化会话的编码、音频、容器和诊断资源，并把状态从 Created 转为 Running。
FFF_API FFFResult FFF_StartSession(FFFSessionHandle session) noexcept;
// 提交同设备 D3D11 纹理和原始 QPC；纹理在返回后不再被 Native 持有。
FFF_API FFFResult FFF_SubmitVideoTexture(FFFSessionHandle session, void* d3d11Texture,
    std::uint32_t textureArrayIndex, std::int64_t qpcTimestamp, std::uint32_t submissionFlags) noexcept;
// 汇入托管捕获层主动丢弃的源帧数量，仅更新统计而不推进编码时间线。
FFF_API FFFResult FFF_ReportDroppedVideoFrames(FFFSessionHandle session, std::uint32_t frameCount) noexcept;
// 把托管捕获事件写入统一 JSON 数组/回调通道；UTF-8 字符串只在调用期间借用。
FFF_API FFFResult FFF_ReportDiagnosticEvent(FFFSessionHandle session, const char* eventNameUtf8,
    const char* messageUtf8) noexcept;
// 以原始 QPC 记录暂停起点；Paused 状态拒绝继续提交视频帧。
FFF_API FFFResult FFF_PauseSession(FFFSessionHandle session, std::int64_t qpcTimestamp) noexcept;
// 结束暂停并从后续媒体 PTS 扣除暂停区间；时间戳不得早于暂停起点。
FFF_API FFFResult FFF_ResumeSession(FFFSessionHandle session, std::int64_t qpcTimestamp) noexcept;
// 在保持捕获器和会话状态的情况下结束当前 Matroska，并从零时间戳开始写入新文件。
FFF_API FFFResult FFF_SplitSession(FFFSessionHandle session, const char* outputPathUtf8) noexcept;
// 在同一媒体时间线和 Matroska 音轨内重建系统音频回环捕获器，用于默认播放设备变化。
FFF_API FFFResult FFF_SwitchSystemAudioEndpoint(FFFSessionHandle session,
    const char* endpointIdUtf8) noexcept;
// 正常停止采集、排空编码与写队列并写 Matroska trailer；重复停止保持幂等。
FFF_API FFFResult FFF_StopSession(FFFSessionHandle session) noexcept;
// 紧急丢弃待写数据并释放资源，允许输出缺少正常 trailer。
FFF_API FFFResult FFF_AbortSession(FFFSessionHandle session) noexcept;
// 按 size/version 填充会话统计快照，调用方继续拥有 statistics 内存。
FFF_API FFFResult FFF_GetSessionStatistics(FFFSessionHandle session, FFFSessionStatistics* statistics) noexcept;
// 复制会话最近错误的 UTF-8 文本；空缓冲区用于查询包含 NUL 的所需容量。
FFF_API FFFResult FFF_GetLastError(FFFSessionHandle session, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// 销毁不透明 C++ 会话对象；空句柄允许，正常文件尾必须在此前由 Stop 写入。
FFF_API void FFF_DestroySession(FFFSessionHandle session) noexcept;
