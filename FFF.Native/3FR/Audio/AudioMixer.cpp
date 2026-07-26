#include "pch.h"
#include "Audio/AudioMixer.h"
#include "Audio/AudioTrackEncoder.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// 创建包含 sourceCount 个独立输入时钟/重采样状态的混音器。outputTrack 由 VideoMuxer 持有且
// 生命周期更长；本类只借用它。每个源的 deque 保存 48 kHz stereo 交错 float，并以绝对样本索引定位。
AudioMixer::AudioMixer(AudioTrackEncoder* outputTrack, std::vector<float> sourceGains)
    : outputTrack_(outputTrack), sources_(sourceGains.size()), sourceGains_(std::move(sourceGains)),
      nextMixedSample_(0) {
    for (auto& source : sources_) source = { nullptr, {}, {}, 0, 0, false, false, 0, 0 };
}

// 释放每个输入源的 SwrContext。已混合的 AAC FIFO 归 outputTrack 管理；析构不隐式 Finish，
// 正常停止必须由 VideoMuxer 先调用本类 Finish 再排空 AAC 编码器。
AudioMixer::~AudioMixer() {
    for (auto& source : sources_) if (source.resampler != nullptr) swr_free(&source.resampler);
}

// 把一个端点包重采样为 48 kHz stereo interleaved float，并按 targetPresentationSample 放入该源
// 的绝对时间线。重叠包丢掉已覆盖前缀，正向缺口填零；随后输出所有已具备数据或超过 100 ms
// 等待窗口的混音区间。方法用 mutex 串行化两个 WASAPI 线程。
FFFResult AudioMixer::Encode(const std::size_t sourceIndex, const std::uint8_t* data,
    const std::uint32_t frameCount, const std::uint32_t flags,
    const std::int64_t targetPresentationSample, const WasapiSampleFormat& inputFormat) noexcept {
    std::scoped_lock lock(mutex_);
    if (sourceIndex >= sources_.size() || outputTrack_ == nullptr || frameCount == 0)
        return sourceIndex >= sources_.size() ? FFFResult::InvalidArgument : FFFResult::Success;
    auto& source = sources_[sourceIndex];
    const auto ensured = EnsureResampler(source, inputFormat);
    if (ensured != FFFResult::Success) return ensured;
    const auto error = targetPresentationSample - source.endSample;
    source.timelineErrorSamples = error;
    source.compensationPpm = 0;
    if (source.seenPacket && std::abs(error) > 2 && std::abs(error) <= 4'800) {
        const auto compensation = static_cast<int>(std::clamp<std::int64_t>(error, -10, 10));
        swr_set_compensation(source.resampler, compensation, 48'000);
        source.compensationPpm = static_cast<std::int32_t>(compensation * 1'000'000LL / 48'000LL);
    }

    const auto capacity = static_cast<int>(av_rescale_rnd(
        swr_get_delay(source.resampler, inputFormat.sampleRate) + frameCount,
        48'000, inputFormat.sampleRate, AV_ROUND_UP));
    convertedBuffer_.resize(static_cast<std::size_t>(capacity) * 2);
    std::uint8_t* outputPlanes[] = { reinterpret_cast<std::uint8_t*>(convertedBuffer_.data()) };
    const std::uint8_t* input = data;
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || input == nullptr) {
        silenceBuffer_.resize(static_cast<std::size_t>(frameCount) * inputFormat.blockAlign);
        std::fill(silenceBuffer_.begin(), silenceBuffer_.end(), 0);
        input = silenceBuffer_.data();
    }
    const std::uint8_t* inputPlanes[] = { input };
    const auto convertedCount = swr_convert(source.resampler, outputPlanes, capacity,
        inputPlanes, static_cast<int>(frameCount));
    if (convertedCount < 0) {
        SetFfmpegError("swr_convert(mixer)", convertedCount);
        return FFFResult::FfmpegFailure;
    }
    const auto convertedSize = static_cast<std::size_t>(convertedCount) * 2;
    std::int64_t appendAt = targetPresentationSample;
    std::size_t inputOffset = 0;
    if (!source.seenPacket) {
        source.baseSample = appendAt;
        source.endSample = appendAt;
        source.seenPacket = true;
    }
    if (appendAt > source.endSample) {
        const auto gap = std::min<std::int64_t>(appendAt - source.endSample, 48'000);
        source.samples.insert(source.samples.end(), static_cast<std::size_t>(gap) * 2, 0.0F);
        source.endSample += gap;
    } else if (appendAt < source.endSample) {
        const auto overlap = std::min<std::int64_t>(source.endSample - appendAt, convertedCount);
        inputOffset = static_cast<std::size_t>(overlap) * 2;
        appendAt += overlap;
    }
    if (appendAt < source.endSample) return MixReady(false);
    source.samples.insert(source.samples.end(), convertedBuffer_.begin() + inputOffset,
        convertedBuffer_.begin() + convertedSize);
    source.endSample += static_cast<std::int64_t>((convertedSize - inputOffset) / 2);
    return MixReady(false);
}

// 在停止时把所有源已到达的剩余区间混完；较短或从未出包的端点按静音处理。该方法不 Finish
// outputTrack，外层仍需在所有混音样本入 FIFO 后统一排空 AAC 编码器。
FFFResult AudioMixer::Finish() noexcept {
    std::scoped_lock lock(mutex_);
    return MixReady(true);
}

// 返回最近一次输入格式、重采样或下游 AAC 错误的值副本。调用方在 Encode/Finish 返回失败后读取。
std::string AudioMixer::LastError() const {
    std::scoped_lock lock(mutex_);
    return lastError_;
}

// 返回指定混音输入源最近一次包级绝对样本位置误差；索引越界时返回零。
std::int64_t AudioMixer::TimelineErrorSamples(const std::size_t sourceIndex) const noexcept {
    std::scoped_lock lock(mutex_);
    return sourceIndex < sources_.size() ? sources_[sourceIndex].timelineErrorSamples : 0;
}

// 返回指定混音源实际设置的平滑补偿 ppm；索引越界或未补偿时返回零。
std::int32_t AudioMixer::CompensationPpm(const std::size_t sourceIndex) const noexcept {
    std::scoped_lock lock(mutex_);
    return sourceIndex < sources_.size() ? sources_[sourceIndex].compensationPpm : 0;
}

// 为一个输入源创建到 48 kHz stereo float interleaved 的 SwrContext。运行中格式改变会明确失败，
// 防止端点切换或驱动重配置悄悄破坏时间线。
FFFResult AudioMixer::EnsureResampler(SourceState& source, const WasapiSampleFormat& inputFormat) noexcept {
    if (source.initialized) {
        const auto& old = source.format;
        if (old.sampleRate != inputFormat.sampleRate || old.channelCount != inputFormat.channelCount ||
            old.bitsPerSample != inputFormat.bitsPerSample || old.floatingPoint != inputFormat.floatingPoint) {
            lastError_ = "An audio source format changed while the mixer was running.";
            return FFFResult::DeviceFailure;
        }
        return FFFResult::Success;
    }
    AVSampleFormat sampleFormat = AV_SAMPLE_FMT_NONE;
    if (inputFormat.floatingPoint && inputFormat.bitsPerSample == 32) sampleFormat = AV_SAMPLE_FMT_FLT;
    else if (!inputFormat.floatingPoint && inputFormat.bitsPerSample == 16) sampleFormat = AV_SAMPLE_FMT_S16;
    else if (!inputFormat.floatingPoint && inputFormat.bitsPerSample == 32) sampleFormat = AV_SAMPLE_FMT_S32;
    if (sampleFormat == AV_SAMPLE_FMT_NONE) {
        lastError_ = "The mixer does not support this WASAPI sample format.";
        return FFFResult::NotSupported;
    }
    AVChannelLayout inputLayout{};
    AVChannelLayout outputLayout{};
    av_channel_layout_default(&inputLayout, inputFormat.channelCount);
    av_channel_layout_default(&outputLayout, 2);
    auto result = swr_alloc_set_opts2(&source.resampler, &outputLayout, AV_SAMPLE_FMT_FLT, 48'000,
        &inputLayout, sampleFormat, inputFormat.sampleRate, 0, nullptr);
    av_channel_layout_uninit(&inputLayout);
    av_channel_layout_uninit(&outputLayout);
    if (result >= 0) result = swr_init(source.resampler);
    if (result < 0) {
        SetFfmpegError("swr_init(mixer)", result);
        return FFFResult::FfmpegFailure;
    }
    source.format = inputFormat;
    source.initialized = true;
    return FFFResult::Success;
}

// 计算可以提交给 AAC 的区间：两源都有数据时立即混到较短源末端；任一源落后超过 100 ms 时，
// 超时部分把缺失源视为静音。force=true 时直接混到最长源末端。每次最多构造 1024 个样本。
FFFResult AudioMixer::MixReady(const bool force) noexcept {
    std::int64_t maximumEnd = 0;
    std::int64_t minimumEnd = INT64_MAX;
    for (const auto& source : sources_) {
        maximumEnd = std::max(maximumEnd, source.endSample);
        minimumEnd = std::min(minimumEnd, source.seenPacket ? source.endSample : 0LL);
    }
    if (minimumEnd == INT64_MAX) minimumEnd = 0;
    const auto timeoutReady = std::max(0LL, maximumEnd - 4'800LL);
    const auto readyEnd = force ? maximumEnd : std::max(minimumEnd, timeoutReady);
    static const WasapiSampleFormat mixedFormat{ 48'000, 2, 32, 32, 8, true };
    while (nextMixedSample_ < readyEnd) {
        const auto count = static_cast<int>(std::min<std::int64_t>(1024, readyEnd - nextMixedSample_));
        mixBuffer_.resize(static_cast<std::size_t>(count) * 2);
        std::fill(mixBuffer_.begin(), mixBuffer_.end(), 0.0F);
        for (int sample = 0; sample < count; ++sample) {
            for (int channel = 0; channel < 2; ++channel) {
                float value = 0.0F;
                for (std::size_t sourceIndex = 0; sourceIndex < sources_.size(); ++sourceIndex)
                    value += ReadSample(sources_[sourceIndex], nextMixedSample_ + sample, channel) *
                        sourceGains_[sourceIndex];
                mixBuffer_[static_cast<std::size_t>(sample) * 2 + channel] = std::clamp(value, -1.0F, 1.0F);
            }
        }
        const auto encoded = outputTrack_->Encode(reinterpret_cast<const std::uint8_t*>(mixBuffer_.data()),
            count, 0, nextMixedSample_, mixedFormat);
        if (encoded != FFFResult::Success) {
            lastError_ = outputTrack_->LastError();
            return encoded;
        }
        nextMixedSample_ += count;
        for (auto& source : sources_) DiscardBefore(source, nextMixedSample_);
    }
    return FFFResult::Success;
}

// 读取某源某绝对样本的一个声道；尚未开始、已经丢弃或尚未到达的区间返回静音。
float AudioMixer::ReadSample(const SourceState& source, const std::int64_t sampleIndex,
    const int channel) const noexcept {
    if (!source.seenPacket || sampleIndex < source.baseSample || sampleIndex >= source.endSample) return 0.0F;
    const auto offset = static_cast<std::size_t>(sampleIndex - source.baseSample) * 2 + channel;
    return offset < source.samples.size() ? source.samples[offset] : 0.0F;
}

// 丢弃 sampleIndex 之前已完成混音的交错样本，保持每源缓冲上限约为等待窗口加一个输入包。
void AudioMixer::DiscardBefore(SourceState& source, const std::int64_t sampleIndex) noexcept {
    if (!source.seenPacket || sampleIndex <= source.baseSample) return;
    const auto discard = std::min<std::int64_t>(sampleIndex - source.baseSample,
        static_cast<std::int64_t>(source.samples.size() / 2));
    for (std::int64_t index = 0; index < discard * 2; ++index) source.samples.pop_front();
    source.baseSample += discard;
}

// 保存 FFmpeg 重采样错误及数值码，不让异常或 FFmpeg 内存越过类边界。
void AudioMixer::SetFfmpegError(const char* operation, const int error) noexcept {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error, buffer, sizeof(buffer)) == 0)
        lastError_ = std::string(operation) + ": " + buffer + " (" + std::to_string(error) + ")";
    else lastError_ = std::string(operation) + ": FFmpeg error " + std::to_string(error);
}
