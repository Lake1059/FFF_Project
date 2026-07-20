#pragma once

#include "Api/FFF.Native.Api.h"
#include "Timeline/QpcTimeline.h"

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class VideoMuxer;
class WasapiCapture;
struct WasapiCaptureStatistics;

struct ID3D11Device;
struct ID3D11Texture2D;

class RecorderSession final {
public:
    // 复制带版本配置并持有 D3D11 Device 引用；传入字符串在构造返回后不再借用。
    explicit RecorderSession(const FFFSessionConfiguration& configuration);
    // 释放设备及剩余资源；不会替代 Stop 写正常 Matroska trailer。
    ~RecorderSession();
    // 初始化编码、容器和音频端点并进入 Running；失败原因保存在 LastError。
    FFFResult Start() noexcept;
    // 在 Running 状态提交同设备 D3D11 纹理；纹理只在调用期间借用，QPC 映射为视频 PTS。
    FFFResult Submit(ID3D11Texture2D* texture, std::uint32_t textureArrayIndex,
        std::int64_t qpcTimestamp, bool repeatedFrame) noexcept;
    // 汇入托管捕获层在进入编码队列前丢弃的源帧计数，不修改媒体时间线。
    FFFResult ReportDroppedFrames(std::uint32_t frameCount) noexcept;
    // 把托管捕获事件写入统一 JSON 数组并触发诊断回调；两个 UTF-8 字符串仅在调用期间借用。
    FFFResult ReportDiagnosticEvent(const char* eventName, const char* message) noexcept;
    // 记录共享媒体时间线暂停起点并拒绝后续帧；时间戳单位为原始 QPC tick。
    FFFResult Pause(std::int64_t qpcTimestamp) noexcept;
    // 结束暂停并把该区间从后续音视频 PTS 中扣除；时间戳必须不早于暂停起点。
    FFFResult Resume(std::int64_t qpcTimestamp) noexcept;
    FFFResult Split(const char* outputPathUtf8) noexcept;
    FFFResult SwitchSystemAudioEndpoint(const char* endpointIdUtf8) noexcept;
    // 停止采集、排空音视频编码器和异步写队列并写正常 trailer；重复停止保持成功。
    FFFResult Stop() noexcept;
    // 紧急丢弃待写包并释放资源，允许输出文件缺少正常 trailer。
    FFFResult Abort() noexcept;
    // 返回原子计数、音频漂移和写入延迟的一致快照；调用方必须提供受支持的结构版本。
    FFFResult GetStatistics(FFFSessionStatistics& statistics) const noexcept;
    // 返回最近一次会话、设备、编码或文件错误的线程安全 UTF-8 副本。
    std::string LastError() const;

private:
    // 保存稳定结果码及可读错误文本；即使字符串分配失败也不允许异常越过 DLL 边界。
    void SetError(FFFResult result, std::string message) noexcept;
    // 写入带通用 qpc 的完整 JSON 数组事件并调用托管回调；detail 是不含外层花括号的字段片段。
    void WriteDiagnostic(const char* eventName, const std::string& detail = {}) noexcept;
    // 在音频线程停止后复制设备统计及编码补偿值，使会话清理后仍可读取最终快照。
    void CollectAudioStatistics() noexcept;
    // 用设备报告的 100 ns QPC 首末位置计算累计音频采样时长误差，结果单位为微秒。
    static std::int64_t CalculateAudioDriftMicroseconds(const WasapiCaptureStatistics& statistics) noexcept;
    FFFResult CreateAudioCapture(const std::string& endpointId, bool loopback, std::size_t trackIndex,
        std::unique_ptr<WasapiCapture>& capture) noexcept;

    mutable std::mutex mutex_;
    ID3D11Device* device_;
    std::string outputPath_;
    std::string encoderName_;
    std::uint32_t width_;
    std::uint32_t height_;
    std::uint32_t frameRateNumerator_;
    std::uint32_t frameRateDenominator_;
    std::int64_t bitRate_;
    std::uint32_t gopSize_;
    std::uint32_t bFrameCount_;
    bool tenBit_;
    bool hdr10_;
    std::string systemAudioEndpointId_;
    std::string microphoneEndpointId_;
    bool keepSeparateAudioTracks_;
    bool timelineStarted_;
    std::int64_t segmentVideoOffset_;
    std::int64_t segmentAudioOffset_;
    std::uint32_t inputTextureFormat_;
    std::uint32_t chromaSampling_;
    std::uint32_t rateControl_;
    std::int32_t quality_;
    std::int64_t maximumBitRate_;
    std::uint32_t lookaheadFrames_;
    std::string preset_;
    std::string profile_;
    std::string sceneOptimization_;
    std::uint32_t multipass_;
    std::uint32_t colorRange_;
    float systemAudioGain_;
    float microphoneGain_;
    std::string audioEncoderName_;
    std::uint32_t audioSampleRate_;
    std::uint32_t audioChannelCount_;
    std::int64_t audioBitRate_;
    std::uint32_t audioMode_;
    bool followDefaultSystemAudioDevice_;
    std::uint32_t qualityMode_;
    std::string customVideoParameters_;
    std::string diagnosticLogPath_;
    std::string captureBackend_;
    std::string sourceDescription_;
    std::string sourceFormat_;
    FFFDiagnosticCallback diagnosticCallback_;
    void* diagnosticCallbackContext_;
    std::atomic<FFFSessionState> state_;
    std::atomic<std::uint64_t> submittedFrames_;
    std::atomic<std::uint64_t> droppedFrames_;
    std::atomic<std::uint64_t> repeatedFrames_;
    std::atomic<std::int64_t> lastVideoQpc_;
    std::atomic<std::int32_t> lastErrorCode_;
    std::atomic<std::uint64_t> lastEncodeMicroseconds_;
    std::atomic<std::uint64_t> peakEncodeMicroseconds_;
    bool trailerWritten_;
    std::uint64_t completedVideoBytes_;
    std::uint64_t completedAudioBytes_;
    std::vector<WasapiCaptureStatistics> completedAudioStatistics_;
    std::vector<std::int64_t> completedAudioTimelineErrors_;
    std::vector<std::int32_t> completedAudioCompensationPpm_;
    std::ofstream diagnosticLog_;
    mutable std::mutex diagnosticMutex_;
    bool diagnosticFirstEntry_;
    std::string lastError_;
    QpcTimeline timeline_;
    std::unique_ptr<VideoMuxer> videoMuxer_;
    std::vector<std::unique_ptr<WasapiCapture>> audioCaptures_;
};
