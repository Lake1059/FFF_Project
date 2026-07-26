#include "pch.h"
#include "Audio/AudioTrackEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

// 构造未绑定容器的 AAC 轨道编码器。所有 FFmpeg 指针置空，使 Initialize 任一步失败后都能
// 由 ReleaseResources 按幂等方式清理；构造本身不打开编码器或分配 FIFO。
AudioTrackEncoder::AudioTrackEncoder() noexcept
    : codecContext_(nullptr), formatContext_(nullptr), stream_(nullptr), resampler_(nullptr), fifo_(nullptr),
      packet_(nullptr), convertedBuffer_(nullptr), convertedCapacity_(0),
      inputFormat_{}, nextPresentationSample_(0),
      inputFormatInitialized_(false), initialized_(false), finished_(false), gain_(1.0F),
      timelineErrorSamples_(0), compensationPpm_(0) {
}

// 释放重采样器、FIFO 和 AAC codec context。AVStream 归 AVFormatContext 所有，本类只保存借用
// 指针且绝不单独释放；正常关闭前应先调用 Finish 送出尾部不足一帧的样本。
AudioTrackEncoder::~AudioTrackEncoder() {
    ReleaseResources();
}

// 在尚未写 Matroska header 的 AVFormatContext 中创建音频轨道。formatContext 与 packetWriter
// 均由 VideoMuxer 持有并覆盖本对象生命周期。
FFFResult AudioTrackEncoder::Initialize(AVFormatContext* formatContext,
    std::function<FFFResult(const AVPacket*)> packetWriter,
    const float gain, const std::string& encoderName,
    const std::uint32_t sampleRate, const std::uint32_t channelCount,
    const std::int64_t bitRate, const std::uint32_t mode) noexcept {
    if (initialized_ || formatContext == nullptr || !packetWriter || gain < 0.0F || gain > 8.0F ||
        encoderName.empty() || sampleRate == 0 || channelCount == 0 || channelCount > 8 || bitRate < 0)
        return FFFResult::InvalidArgument;
    gain_ = gain;
    packetWriter_ = std::move(packetWriter);
    const AVCodec* codec = avcodec_find_encoder_by_name(encoderName.c_str());
    if (codec == nullptr) {
        lastError_ = "The FFmpeg build does not contain the selected audio encoder.";
        return FFFResult::NotSupported;
    }
    codecContext_ = avcodec_alloc_context3(codec);
    if (codecContext_ == nullptr) {
        lastError_ = "Could not allocate the AAC encoder context.";
        return FFFResult::FfmpegFailure;
    }
    codecContext_->sample_rate = static_cast<int>(sampleRate);
    formatContext_ = formatContext;
    const void* supportedFormats = nullptr;
    int supportedFormatCount = 0;
    auto result = avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
        &supportedFormats, &supportedFormatCount);
    if (result < 0 || supportedFormats == nullptr || supportedFormatCount <= 0) {
        SetFfmpegError("avcodec_get_supported_config(AAC sample formats)", result);
        ReleaseResources();
        return FFFResult::FfmpegFailure;
    }
    codecContext_->sample_fmt = static_cast<const AVSampleFormat*>(supportedFormats)[0];
    codecContext_->bit_rate = bitRate;
    codecContext_->time_base = { 1, static_cast<int>(sampleRate) };
    codecContext_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_channel_layout_default(&codecContext_->ch_layout, static_cast<int>(channelCount));
    AVDictionary* encoderOptions = nullptr;
    if (mode == 1) av_dict_set(&encoderOptions, "aac_coder", "nmr", 0);
    else if (mode == 2) av_dict_set(&encoderOptions, "vbr", "5", 0);
    result = avcodec_open2(codecContext_, codec, &encoderOptions);
    if (result >= 0 && av_dict_count(encoderOptions) > 0) {
        av_dict_free(&encoderOptions);
        lastError_ = "The selected audio encoder rejected its quality options.";
        ReleaseResources();
        return FFFResult::NotSupported;
    }
    av_dict_free(&encoderOptions);
    if (result < 0) {
        SetFfmpegError("avcodec_open2(audio)", result);
        ReleaseResources();
        return FFFResult::FfmpegFailure;
    }
    stream_ = avformat_new_stream(formatContext, nullptr);
    if (stream_ == nullptr) {
        lastError_ = "Could not create the Matroska audio stream.";
        ReleaseResources();
        return FFFResult::FfmpegFailure;
    }
    stream_->time_base = codecContext_->time_base;
    result = avcodec_parameters_from_context(stream_->codecpar, codecContext_);
    if (result < 0) {
        SetFfmpegError("avcodec_parameters_from_context(AAC)", result);
        ReleaseResources();
        return FFFResult::FfmpegFailure;
    }
    stream_->codecpar->codec_tag = 0;
    fifo_ = av_audio_fifo_alloc(codecContext_->sample_fmt, codecContext_->ch_layout.nb_channels, 1);
    if (fifo_ == nullptr) {
        lastError_ = "Could not allocate the AAC audio FIFO.";
        ReleaseResources();
        return FFFResult::FfmpegFailure;
    }
    packet_ = av_packet_alloc();
    if (packet_ == nullptr) {
        lastError_ = "Could not allocate the reusable AAC packet.";
        ReleaseResources();
        return FFFResult::FfmpegFailure;
    }
    initialized_ = true;
    return FFFResult::Success;
}

// 将一个 WASAPI 交错样本包重采样到 AAC 工作格式并写入 FIFO。qpcPosition100ns 是 GetBuffer
// 返回的首样本系统 QPC 位置；它决定与会话 T0 的目标样本索引。大缺口补静音，小偏差通过
// swr_set_compensation 在约一秒内平滑修正，不按回调到达时间生成 PTS。
FFFResult AudioTrackEncoder::Encode(const std::uint8_t* data, const std::uint32_t frameCount,
    const std::uint32_t flags, const std::int64_t targetPresentationSample,
    const WasapiSampleFormat& inputFormat) noexcept {
    if (!initialized_ || finished_) return FFFResult::InvalidState;
    if (frameCount == 0) return FFFResult::Success;
    const auto resamplerResult = EnsureResampler(inputFormat);
    if (resamplerResult != FFFResult::Success) return resamplerResult;

    const auto queuedEnd = nextPresentationSample_ + av_audio_fifo_size(fifo_);
    const auto timelineError = targetPresentationSample - queuedEnd;
    timelineErrorSamples_.store(timelineError);
    compensationPpm_.store(0);
    const auto outputSampleRate = codecContext_->sample_rate;
    if (timelineError > outputSampleRate / 10) {
        const auto silenceResult = AppendSilence(static_cast<std::int32_t>(std::min<std::int64_t>(timelineError, outputSampleRate)));
        if (silenceResult != FFFResult::Success) return silenceResult;
    } else if (std::abs(timelineError) > 2) {
        const auto compensation = static_cast<int>(std::clamp<std::int64_t>(timelineError, -10, 10));
        swr_set_compensation(resampler_, compensation, outputSampleRate);
        compensationPpm_.store(static_cast<std::int32_t>(compensation * 1'000'000LL / outputSampleRate));
    }

    const auto outputCapacity = static_cast<int>(av_rescale_rnd(
        swr_get_delay(resampler_, inputFormat.sampleRate) + frameCount,
        outputSampleRate, inputFormat.sampleRate, AV_ROUND_UP));
    auto result = 0;
    if (outputCapacity > convertedCapacity_) {
        std::uint8_t** replacement = nullptr;
        result = av_samples_alloc_array_and_samples(&replacement, nullptr,
            codecContext_->ch_layout.nb_channels, outputCapacity, codecContext_->sample_fmt, 0);
        if (result < 0) {
            SetFfmpegError("av_samples_alloc_array_and_samples", result);
            return FFFResult::FfmpegFailure;
        }
        if (convertedBuffer_ != nullptr) {
            av_freep(&convertedBuffer_[0]);
            av_freep(&convertedBuffer_);
        }
        convertedBuffer_ = replacement;
        convertedCapacity_ = outputCapacity;
    }
    const std::uint8_t* inputPointer = data;
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
        silenceBuffer_.resize(static_cast<std::size_t>(frameCount) * inputFormat.blockAlign);
        std::fill(silenceBuffer_.begin(), silenceBuffer_.end(), 0);
        inputPointer = silenceBuffer_.data();
    }
    const std::uint8_t* inputPlanes[] = { inputPointer };
    const auto convertedSamples = swr_convert(resampler_, convertedBuffer_, outputCapacity,
        inputPlanes, static_cast<int>(frameCount));
    if (convertedSamples < 0) {
        SetFfmpegError("swr_convert", convertedSamples);
        return FFFResult::FfmpegFailure;
    }
    ApplyGain(convertedBuffer_, convertedSamples);
    if (av_audio_fifo_realloc(fifo_, av_audio_fifo_size(fifo_) + convertedSamples) < 0 ||
        av_audio_fifo_write(fifo_, reinterpret_cast<void**>(convertedBuffer_), convertedSamples) < convertedSamples) {
        lastError_ = "Could not append converted samples to the AAC FIFO.";
        return FFFResult::FfmpegFailure;
    }
    return EncodeAvailableFrames(false);
}

// 对重采样后的 AAC 工作样本应用线性增益。静音直接调用 FFmpeg 的格式感知清零函数；常见
// float/planar-float 路径逐样本缩放并限幅，避免混音前后的增益产生整数溢出或削波绕回。
void AudioTrackEncoder::ApplyGain(std::uint8_t** samples, const std::int32_t sampleCount) const noexcept {
    if (samples == nullptr || sampleCount <= 0 || gain_ == 1.0F) return;
    const auto channels = codecContext_->ch_layout.nb_channels;
    if (gain_ == 0.0F) {
        av_samples_set_silence(samples, 0, sampleCount, channels, codecContext_->sample_fmt);
        return;
    }
    const bool planar = av_sample_fmt_is_planar(codecContext_->sample_fmt) != 0;
    const auto packedFormat = av_get_packed_sample_fmt(codecContext_->sample_fmt);
    if (packedFormat == AV_SAMPLE_FMT_FLT) {
        for (int channel = 0; channel < (planar ? channels : 1); ++channel) {
            auto* values = reinterpret_cast<float*>(samples[channel]);
            const auto count = sampleCount * (planar ? 1 : channels);
            for (int index = 0; index < count; ++index)
                values[index] = std::clamp(values[index] * gain_, -1.0F, 1.0F);
        }
    } else if (packedFormat == AV_SAMPLE_FMT_DBL) {
        for (int channel = 0; channel < (planar ? channels : 1); ++channel) {
            auto* values = reinterpret_cast<double*>(samples[channel]);
            const auto count = sampleCount * (planar ? 1 : channels);
            for (int index = 0; index < count; ++index)
                values[index] = std::clamp(values[index] * static_cast<double>(gain_), -1.0, 1.0);
        }
    }
}

// 将 FIFO 尾部用精确静音补齐到 AAC frame_size，排空 codec 延迟 packet 并进入终态。方法幂等；
// trailer 由所有音视频编码器都 Finish 后的 VideoMuxer 统一写入。
FFFResult AudioTrackEncoder::Finish() noexcept {
    if (finished_) return FFFResult::Success;
    if (!initialized_) return FFFResult::InvalidState;
    auto result = EncodeAvailableFrames(true);
    if (result != FFFResult::Success) return result;
    const auto sent = avcodec_send_frame(codecContext_, nullptr);
    if (sent < 0 && sent != AVERROR_EOF) {
        SetFfmpegError("avcodec_send_frame(AAC drain)", sent);
        return FFFResult::FfmpegFailure;
    }
    result = DrainPackets();
    if (result == FFFResult::Success) finished_ = true;
    return result;
}

// 返回最后一次 AAC、重采样或 FIFO 失败消息的值副本。调用方在停止采集线程后读取，因此当前
// 不需要内部锁；运行中错误由上层会话在同一回调返回路径立即复制。
std::string AudioTrackEncoder::LastError() const {
    return lastError_;
}

// 原子读取最近一次包级时间线误差；统计线程无需阻塞音频重采样线程。
std::int64_t AudioTrackEncoder::TimelineErrorSamples() const noexcept {
    return timelineErrorSamples_.load();
}

// 原子读取最近一次实际设置到 SwrContext 的补偿 ppm 近似值。
std::int32_t AudioTrackEncoder::CompensationPpm() const noexcept {
    return compensationPpm_.load();
}

// 首包到达时按 WASAPI mix format 创建 SwrContext；后续包若格式发生变化则明确失败并要求会话
// 重建设备，避免在同一轨道中悄悄改变声道或采样格式。仅支持 Windows 常见 float32/PCM16/32。
FFFResult AudioTrackEncoder::EnsureResampler(const WasapiSampleFormat& inputFormat) noexcept {
    if (inputFormatInitialized_) {
        if (inputFormat_.sampleRate == inputFormat.sampleRate &&
            inputFormat_.channelCount == inputFormat.channelCount &&
            inputFormat_.bitsPerSample == inputFormat.bitsPerSample &&
            inputFormat_.validBitsPerSample == inputFormat.validBitsPerSample &&
            inputFormat_.blockAlign == inputFormat.blockAlign &&
            inputFormat_.channelMask == inputFormat.channelMask &&
            inputFormat_.floatingPoint == inputFormat.floatingPoint)
            return FFFResult::Success;
        swr_free(&resampler_);
        inputFormatInitialized_ = false;
    }
    AVSampleFormat inputSampleFormat = AV_SAMPLE_FMT_NONE;
    if (inputFormat.floatingPoint && inputFormat.bitsPerSample == 32) inputSampleFormat = AV_SAMPLE_FMT_FLT;
    else if (!inputFormat.floatingPoint && inputFormat.bitsPerSample == 16) inputSampleFormat = AV_SAMPLE_FMT_S16;
    else if (!inputFormat.floatingPoint && inputFormat.bitsPerSample == 32) inputSampleFormat = AV_SAMPLE_FMT_S32;
    if (inputSampleFormat == AV_SAMPLE_FMT_NONE || inputFormat.channelCount == 0 || inputFormat.sampleRate == 0) {
        lastError_ = "The WASAPI mix format is not supported by the audio resampler.";
        return FFFResult::NotSupported;
    }
    AVChannelLayout inputLayout{};
    if (inputFormat.channelMask != 0)
        av_channel_layout_from_mask(&inputLayout, inputFormat.channelMask);
    else
        av_channel_layout_default(&inputLayout, inputFormat.channelCount);
    auto result = swr_alloc_set_opts2(&resampler_, &codecContext_->ch_layout,
        codecContext_->sample_fmt, codecContext_->sample_rate, &inputLayout, inputSampleFormat,
        inputFormat.sampleRate, 0, nullptr);
    av_channel_layout_uninit(&inputLayout);
    if (result >= 0 && resampler_ != nullptr) result = swr_init(resampler_);
    if (result < 0 || resampler_ == nullptr) {
        SetFfmpegError("swr_alloc_set_opts2/swr_init", result);
        return FFFResult::FfmpegFailure;
    }
    inputFormat_ = inputFormat;
    inputFormatInitialized_ = true;
    return FFFResult::Success;
}

// 向 FIFO 追加指定数量的输出格式静音，用于 T0 后真实存在的大音频缺口。分配的 planar buffer
// 在写入后立即释放；sampleCount 非正时无操作成功。
FFFResult AudioTrackEncoder::AppendSilence(const std::int32_t sampleCount) noexcept {
    if (sampleCount <= 0) return FFFResult::Success;
    std::uint8_t** silence = nullptr;
    auto result = av_samples_alloc_array_and_samples(&silence, nullptr,
        codecContext_->ch_layout.nb_channels, sampleCount, codecContext_->sample_fmt, 0);
    if (result < 0) {
        SetFfmpegError("av_samples_alloc_array_and_samples(silence)", result);
        return FFFResult::FfmpegFailure;
    }
    av_samples_set_silence(silence, 0, sampleCount, codecContext_->ch_layout.nb_channels,
        codecContext_->sample_fmt);
    result = av_audio_fifo_realloc(fifo_, av_audio_fifo_size(fifo_) + sampleCount);
    if (result >= 0) result = av_audio_fifo_write(fifo_, reinterpret_cast<void**>(silence), sampleCount);
    av_freep(&silence[0]);
    av_freep(&silence);
    if (result < sampleCount) {
        lastError_ = "Could not append timeline silence to the AAC FIFO.";
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

// 只要 FIFO 达到 AAC 固定 frame_size 就持续编码；flushPartialFrame 为真时将最后不足一帧的
// 样本交给 SendFrame 补静音。每帧 PTS 只由累计输出样本数生成。
FFFResult AudioTrackEncoder::EncodeAvailableFrames(const bool flushPartialFrame) noexcept {
    if (codecContext_->frame_size <= 0) {
        const auto available = av_audio_fifo_size(fifo_);
        if (available <= 0) return FFFResult::Success;
        return SendFrame(available, false);
    }
    const auto frameSize = codecContext_->frame_size;
    while (av_audio_fifo_size(fifo_) >= frameSize) {
        const auto result = SendFrame(frameSize, false);
        if (result != FFFResult::Success) return result;
    }
    if (flushPartialFrame && av_audio_fifo_size(fifo_) > 0) return SendFrame(frameSize, true);
    return FFFResult::Success;
}

// 从 FIFO 读取 sampleCount 个样本到可写 AVFrame；尾部不足时先写已有样本，再把剩余区间置静音。
// frame->pts 使用 1/48000 时间基的累计样本数，send 成功后立即收取可用 packet。
FFFResult AudioTrackEncoder::SendFrame(const std::int32_t sampleCount, const bool padWithSilence) noexcept {
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) {
        lastError_ = "Could not allocate an AAC frame.";
        return FFFResult::FfmpegFailure;
    }
    frame->nb_samples = sampleCount;
    frame->format = codecContext_->sample_fmt;
    frame->sample_rate = codecContext_->sample_rate;
    av_channel_layout_copy(&frame->ch_layout, &codecContext_->ch_layout);
    auto result = av_frame_get_buffer(frame, 0);
    if (result < 0) {
        SetFfmpegError("av_frame_get_buffer(AAC)", result);
        av_frame_free(&frame);
        return FFFResult::FfmpegFailure;
    }
    const auto available = std::min(sampleCount, av_audio_fifo_size(fifo_));
    av_audio_fifo_read(fifo_, reinterpret_cast<void**>(frame->data), available);
    if (padWithSilence && available < sampleCount) {
        av_samples_set_silence(frame->data, available, sampleCount - available,
            codecContext_->ch_layout.nb_channels, codecContext_->sample_fmt);
    }
    frame->pts = nextPresentationSample_;
    nextPresentationSample_ += sampleCount;
    result = avcodec_send_frame(codecContext_, frame);
    av_frame_free(&frame);
    if (result < 0) {
        SetFfmpegError("avcodec_send_frame(AAC)", result);
        return FFFResult::FfmpegFailure;
    }
    return DrainPackets();
}

// 接收 AAC packet、转换到音频 stream time_base，并交给异步 Matroska packetWriter。回调会克隆
// packet，因此本方法可立即释放原对象，系统声和麦克风轨也不会被文件 IO 阻塞。
FFFResult AudioTrackEncoder::DrainPackets() noexcept {
    if (packet_ == nullptr) return FFFResult::InvalidState;
    while (true) {
        const auto result = avcodec_receive_packet(codecContext_, packet_);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            return FFFResult::Success;
        }
        if (result < 0) {
            SetFfmpegError("avcodec_receive_packet(AAC)", result);
            return FFFResult::FfmpegFailure;
        }
        av_packet_rescale_ts(packet_, codecContext_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;
        const auto writeResult = packetWriter_(packet_);
        av_packet_unref(packet_);
        if (writeResult != FFFResult::Success) {
            lastError_ = "The asynchronous Matroska writer rejected an AAC packet.";
            return writeResult;
        }
    }
}

// 保存带操作名和数值码的 FFmpeg 错误文本。固定栈缓冲区避免跨模块内存所有权问题。
void AudioTrackEncoder::SetFfmpegError(const char* operation, const int error) noexcept {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error, buffer, sizeof(buffer)) == 0)
        lastError_ = std::string(operation) + ": " + buffer + " (" + std::to_string(error) + ")";
    else lastError_ = std::string(operation) + ": FFmpeg error " + std::to_string(error);
}

// 释放 FIFO、SwrContext 和 AAC codec context，并清空所有借用指针。该函数不释放 AVStream，
// 因为它由外层 AVFormatContext 统一管理。
void AudioTrackEncoder::ReleaseResources() noexcept {
    if (packet_ != nullptr) av_packet_free(&packet_);
    if (convertedBuffer_ != nullptr) {
        av_freep(&convertedBuffer_[0]);
        av_freep(&convertedBuffer_);
    }
    convertedCapacity_ = 0;
    silenceBuffer_.clear();
    if (fifo_ != nullptr) av_audio_fifo_free(fifo_);
    fifo_ = nullptr;
    if (resampler_ != nullptr) swr_free(&resampler_);
    if (codecContext_ != nullptr) avcodec_free_context(&codecContext_);
    stream_ = nullptr;
    formatContext_ = nullptr;
    packetWriter_ = {};
    initialized_ = false;
}
