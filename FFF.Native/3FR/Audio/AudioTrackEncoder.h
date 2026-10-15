#pragma once

#include "Api/FFF.Native.Api.h"
#include "Audio/AudioTypes.h"

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct AVAudioFifo;
struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVPacket;
struct SwrContext;

class AudioTrackEncoder final {
public:
    // 构造未初始化的轨道对象；不分配 FFmpeg 上下文，也不打开输出文件。
    AudioTrackEncoder() noexcept;
    // 幂等释放 FIFO、重采样器和编码器；正常关闭前仍应显式调用 Finish。
    ~AudioTrackEncoder();
    // 在给定容器中建立配置指定的音频轨；容器与 packetWriter 的生命周期由 VideoMuxer 保证。
    FFFResult Initialize(AVFormatContext* formatContext,
        std::function<FFFResult(const AVPacket*)> packetWriter,
        float gain, const std::string& encoderName,
        std::uint32_t sampleRate, std::uint32_t channelCount,
        std::int64_t bitRate, std::uint32_t mode) noexcept;
    // 把一个 WASAPI 包重采样并放入绝对样本时间线；data 只在调用期间借用，静音包可传空指针。
    FFFResult Encode(const std::uint8_t* data, std::uint32_t frameCount, std::uint32_t flags,
        std::int64_t targetPresentationSample, const WasapiSampleFormat& inputFormat) noexcept;
    // 补齐最后不足一帧的 FIFO、送出编码器尾包并保持 stream 由外层容器管理。
    FFFResult Finish() noexcept;
    // 返回最近一次格式、重采样、编码器或 packetWriter 错误的值副本。
    std::string LastError() const;
    // 返回最近音频包目标位置与 FIFO 末端之间的样本误差，正值表示需要追赶。
    std::int64_t TimelineErrorSamples() const noexcept;
    // 返回最近一次平滑重采样补偿的近似 ppm，零表示当前没有小偏差补偿。
    std::int32_t CompensationPpm() const noexcept;

private:
    // 首包时建立输入格式到编码器采样格式的 SwrContext；设备切换后按新 mix format 重建。
    FFFResult EnsureResampler(const WasapiSampleFormat& inputFormat) noexcept;
    // 对 FIFO 中的编码器采样应用限幅增益；samples 各平面仅在本次调用中可写。
    void ApplyGain(std::uint8_t** samples, std::int32_t sampleCount) const noexcept;
    // 向 FIFO 追加输出格式静音，用于真实时间线缺口而非暂停区间。
    FFFResult AppendSilence(std::int32_t sampleCount) noexcept;
    // 编码全部完整帧；flushPartialFrame 为真时将尾部不足帧补静音后提交。
    FFFResult EncodeAvailableFrames(bool flushPartialFrame) noexcept;
    // 从 FIFO 构造一个带累计样本 PTS 的 AVFrame，并在发送后立即收取 packet。
    FFFResult SendFrame(std::int32_t sampleCount, bool padWithSilence) noexcept;
    // 持续接收编码 packet、换算时间基并转交异步写入回调，直到 EAGAIN 或 EOF。
    FFFResult DrainPackets() noexcept;
    // 保存 FFmpeg 操作名、可读文本和数值错误码，不把 FFmpeg 内存交给调用方。
    void SetFfmpegError(const char* operation, int error) noexcept;
    // 释放本类拥有的 FFmpeg 资源并清空借用指针；不单独释放容器拥有的 AVStream。
    void ReleaseResources() noexcept;

    AVCodecContext* codecContext_;
    AVFormatContext* formatContext_;
    AVStream* stream_;
    SwrContext* resampler_;
    AVAudioFifo* fifo_;
    AVPacket* packet_;
    std::uint8_t** convertedBuffer_;
    std::int32_t convertedCapacity_;
    std::vector<std::uint8_t> silenceBuffer_;
    std::function<FFFResult(const AVPacket*)> packetWriter_;
    WasapiSampleFormat inputFormat_;
    std::int64_t nextPresentationSample_;
    bool inputFormatInitialized_;
    bool initialized_;
    bool finished_;
    float gain_;
    std::atomic<std::int64_t> timelineErrorSamples_;
    std::atomic<std::int32_t> compensationPpm_;
    std::string lastError_;
};
