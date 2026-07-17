#pragma once

#include "Api/FFF.Native.Api.h"
#include "Audio/AudioTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct SwrContext;
class AudioTrackEncoder;

class AudioMixer final {
public:
    // 创建固定数量的输入源状态；outputTrack 仅借用且必须比混音器存活更久，增益数组长度即源数量。
    AudioMixer(AudioTrackEncoder* outputTrack, std::vector<float> sourceGains);
    // 释放各输入源的重采样器；不会隐式排空或结束由外部持有的 AAC 输出轨。
    ~AudioMixer();
    // 把一个 WASAPI 包映射到指定源的 48 kHz 绝对样本位置并尝试混音；可由多个端点线程并发调用。
    FFFResult Encode(std::size_t sourceIndex, const std::uint8_t* data,
        std::uint32_t frameCount, std::uint32_t flags,
        std::int64_t targetPresentationSample, const WasapiSampleFormat& inputFormat) noexcept;
    // 在停止阶段把各源剩余区间补静音混完，但不负责 Finish 下游 AudioTrackEncoder。
    FFFResult Finish() noexcept;
    // 返回最近一次重采样、时间线或下游编码错误的线程安全副本。
    std::string LastError() const;
    // 返回指定源最近一次目标位置与缓冲末端的样本误差；索引越界时返回零。
    std::int64_t TimelineErrorSamples(std::size_t sourceIndex) const noexcept;
    // 返回指定源当前平滑重采样补偿的近似 ppm；未补偿或索引越界时返回零。
    std::int32_t CompensationPpm(std::size_t sourceIndex) const noexcept;

private:
    struct SourceState {
        SwrContext* resampler;
        WasapiSampleFormat format;
        std::deque<float> samples;
        std::int64_t baseSample;
        std::int64_t endSample;
        bool initialized;
        bool seenPacket;
        std::int64_t timelineErrorSamples;
        std::int32_t compensationPpm;
    };

    // 首包时创建到 48 kHz stereo float 的重采样器；运行中输入格式变化会明确失败。
    FFFResult EnsureResampler(SourceState& source, const WasapiSampleFormat& inputFormat) noexcept;
    // 混合所有已就绪区间；force 为真时把落后源视作静音并处理到最长源末端。
    FFFResult MixReady(bool force) noexcept;
    // 按绝对样本索引读取一个声道，源尚未覆盖该位置时返回静音零值。
    float ReadSample(const SourceState& source, std::int64_t sampleIndex, int channel) const noexcept;
    // 删除指定绝对位置之前已经完成混音的交错样本，限制每源缓冲持续增长。
    void DiscardBefore(SourceState& source, std::int64_t sampleIndex) noexcept;
    // 把 FFmpeg 错误码转换为带操作名的持久文本，供 LastError 跨边界复制。
    void SetFfmpegError(const char* operation, int error) noexcept;

    AudioTrackEncoder* outputTrack_;
    std::vector<SourceState> sources_;
    std::vector<float> sourceGains_;
    std::int64_t nextMixedSample_;
    mutable std::mutex mutex_;
    std::string lastError_;
};
