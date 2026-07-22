#pragma once

#include "Api/FFF.Native.Api.h"
#include "Audio/AudioTypes.h"

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>

struct AVBufferRef;
struct AVBSFContext;
struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVPacket;
struct ID3D11Device;
struct ID3D11Device3;
struct ID3D11DeviceContext;
struct ID3D11ComputeShader;
struct ID3D11Texture2D;
struct ID3D11VideoDevice;
struct ID3D11VideoContext;
struct ID3D11VideoProcessorEnumerator;
struct ID3D11VideoProcessor;
class AudioTrackEncoder;
class AudioMixer;

// These two values are resolved once from the explicit encoder strategy table.  Every
// allocation and per-frame branch below uses them instead of independently guessing from
// the encoder name, bit depth, and chroma sampling.
enum class VideoEncoderBackend : std::uint8_t {
    None,
    Software,
    Nvenc,
    Qsv,
    Amf
};

enum class RgbToYuvPath : std::uint8_t {
    None,
    SoftwarePlanar,
    D3D11VideoProcessor420,
    D3D11ComputeShader420
};

enum class YuvBitPacking : std::uint8_t {
    EightBit,
    TenBitLsb,
    TenBitMsb
};

class SynchronizedErrorText final {
public:
    void operator=(std::string value) noexcept {
        std::scoped_lock lock(mutex_);
        try {
            value_ = std::move(value);
        } catch (...) {
            try { value_ = "Native error message allocation failed."; }
            catch (...) { value_.clear(); }
        }
    }

    void operator=(const char* value) noexcept { operator=(std::string(value)); }

    void operator+=(std::string suffix) noexcept {
        std::scoped_lock lock(mutex_);
        try { value_ += suffix; }
        catch (...) {}
    }

    bool empty() const noexcept {
        std::scoped_lock lock(mutex_);
        return value_.empty();
    }

    std::string Copy() const {
        std::scoped_lock lock(mutex_);
        return value_;
    }

private:
    mutable std::mutex mutex_;
    std::string value_;
};

class VideoMuxer final {
public:
    // 构造空封装器；不打开文件、设备或 FFmpeg 上下文。
    VideoMuxer() noexcept;
    // 异常清理只释放资源；正常 trailer 必须由 Finish 写入。
    ~VideoMuxer();
    // 在同一 D3D11 Device 上建立硬件帧池、视频/音频轨、Matroska header 和异步写线程；
    // 所有字符串及音频数组在返回前复制，失败时可读取 LastError 并由本对象统一清理半成品资源。
    FFFResult Initialize(ID3D11Device* device, const std::string& outputPath,
        const std::string& encoderName, std::uint32_t width, std::uint32_t height,
        std::uint32_t frameRateNumerator, std::uint32_t frameRateDenominator,
        std::int64_t bitRate, std::uint32_t gopSize, std::uint32_t bFrameCount,
        bool tenBit, bool hdr10, bool mixAudioSources, std::uint32_t inputTextureFormat,
        std::uint32_t chromaSampling, std::uint32_t rateControl,
        std::uint32_t qualityMode, const std::string& customVideoParameters,
        std::int32_t quality,
        std::int64_t maximumBitRate, std::uint32_t lookaheadFrames, const std::string& preset,
        const std::string& profile, const std::string& sceneOptimization,
        std::uint32_t multipass, std::uint32_t colorRange,
        const std::vector<float>& audioSourceGains, const std::string& audioEncoderName,
        std::uint32_t audioSampleRate, std::uint32_t audioChannelCount,
        std::int64_t audioBitRate, std::uint32_t audioMode) noexcept;
    // 把同设备纹理复制到 FFmpeg hardware frame，并提交准确视频 PTS。
    FFFResult Encode(ID3D11Texture2D* sourceTexture, std::uint32_t sourceArrayIndex,
        std::int64_t presentationTimestamp) noexcept;
    // 排空全部编码器和异步写队列，写 trailer 后释放资源。
    FFFResult Finish() noexcept;
    // 把一包 WASAPI 数据送入指定独立轨或混音源的目标采样率时间线。
    FFFResult EncodeAudio(std::size_t trackIndex, const std::uint8_t* data,
        std::uint32_t frameCount, std::uint32_t flags, std::int64_t targetPresentationSample,
        const WasapiSampleFormat& inputFormat) noexcept;
    // 丢弃待写 packet 并释放所有资源，允许输出缺少正常 trailer。
    void Abort() noexcept;
    // 返回最近一次 FFmpeg、D3D11、队列或文件错误。
    std::string LastError() const;
    // 返回当前异步 packet 队列深度的原子快照，不阻塞写入线程。
    std::uint32_t QueueDepth() const noexcept;
    // 返回本会话观察到的最大 packet 队列深度，用于判断磁盘反压。
    std::uint32_t PeakQueueDepth() const noexcept;
    // 返回最近一个 packet 的实际文件写入耗时，单位为微秒。
    std::uint64_t LastWriteMicroseconds() const noexcept;
    // 返回会话内单个 packet 文件写入耗时峰值，单位为微秒。
    std::uint64_t PeakWriteMicroseconds() const noexcept;
    std::uint64_t VideoBytes() const noexcept;
    std::uint64_t AudioBytes() const noexcept;
    // 返回指定音频源最近一次目标位置与编码时间线的样本误差；索引越界时返回零。
    std::int64_t AudioTimelineErrorSamples(std::size_t sourceIndex) const noexcept;
    // 返回指定音频源当前平滑重采样补偿的近似 ppm；索引越界时返回零。
    std::int32_t AudioCompensationPpm(std::size_t sourceIndex) const noexcept;

private:
    // 建立同设备 D3D11 Video Processor，仅负责 SDR BT.709 packed RGB 到 NV12/P010。
    FFFResult InitializeVideoProcessor(std::uint32_t frameRateNumerator,
        std::uint32_t frameRateDenominator) noexcept;
    // 建立明确指定 BT.709/BT.2020 完整范围矩阵的 DirectCompute RGB 到 YUV 转换器。
    FFFResult InitializeRgbToYuvConverter(bool tenBit, bool hdr10,
        std::uint32_t chromaSampling, bool softwareYuv, bool leftChroma) noexcept;
    // 把一张调用方 RGB 纹理转换到 FFmpeg D3D11 NV12/P010 surface。
    FFFResult ConvertTextureToEncoderSurface(ID3D11Texture2D* sourceTexture, std::uint32_t sourceArrayIndex,
        ID3D11Texture2D* destinationTexture, std::uint32_t destinationArrayIndex) noexcept;
    FFFResult ConvertTextureToEncoderSurfaceWithShader(ID3D11Texture2D* sourceTexture,
        std::uint32_t sourceArrayIndex, ID3D11Texture2D* destinationTexture,
        std::uint32_t destinationArrayIndex) noexcept;
    FFFResult EncodeSoftwareYuv(ID3D11Texture2D* sourceTexture,
        std::uint32_t sourceArrayIndex, std::int64_t presentationTimestamp) noexcept;
    // 持续接收视频编码 packet、重标时间基并交给异步写队列，直到 EAGAIN 或 EOF。
    FFFResult DrainPackets() noexcept;
    // 克隆调用方 packet 并放入有界异步队列；队列或写线程失败时返回明确错误。
    FFFResult EnqueuePacket(const AVPacket* packet) noexcept;
    // 创建唯一文件写线程并复位队列统计；仅能在 Matroska header 成功后调用一次。
    FFFResult StartWriter() noexcept;
    // 请求写线程排空或丢弃剩余 packet 并等待退出；可在失败清理路径重复调用。
    void StopWriter(bool discardPending) noexcept;
    // 从队列取 packet 执行 av_interleaved_write_frame，并记录队列深度和磁盘延迟。
    void WriterThread() noexcept;
    // 把 FFmpeg 操作名、可读文本和数值错误码保存到 LastError 所用存储。
    void SetFfmpegError(const char* operation, int error) noexcept;
    // 按 writeTrailer 决定是否补写正常文件尾，然后释放队列、音频、编码器、硬件和容器资源。
    void ReleaseResources(bool writeTrailer) noexcept;

    ID3D11DeviceContext* immediateContext_;
    ID3D11Device* d3d11Device_;
    ID3D11Device3* d3d11Device3_;
    ID3D11ComputeShader* rgbToYuvShader_;
    AVBufferRef* hardwareDevice_;
    AVBufferRef* hardwareFrames_;
    AVBufferRef* encoderHardwareDevice_;
    AVBufferRef* encoderFrames_;
    AVBSFContext* videoBitstreamFilter_;
    AVCodecContext* codecContext_;
    AVFormatContext* formatContext_;
    AVStream* videoStream_;
    std::uint32_t width_;
    std::uint32_t height_;
    std::uint32_t inputDxgiFormat_;
    VideoEncoderBackend encoderBackend_;
    RgbToYuvPath rgbToYuvPath_;
    YuvBitPacking yuvBitPacking_;
    bool tenBit_;
    std::uint32_t chromaSampling_;
    ID3D11VideoDevice* videoDevice_;
    ID3D11VideoContext* videoContext_;
    ID3D11VideoProcessorEnumerator* videoProcessorEnumerator_;
    ID3D11VideoProcessor* videoProcessor_;
    ID3D11Texture2D* planarYuvGpuTextures_[3]{};
    ID3D11Texture2D* planarYuvStagingTextures_[3]{};
    bool initialized_;
    bool headerWritten_;
    bool finished_;
    mutable std::mutex packetMutex_;
    std::condition_variable packetCondition_;
    std::deque<AVPacket*> packetQueue_;
    std::thread writerThread_;
    bool writerStopRequested_;
    bool writerFailed_;
    std::atomic<std::uint32_t> queueDepth_;
    std::atomic<std::uint32_t> peakQueueDepth_;
    std::atomic<std::uint64_t> lastWriteMicroseconds_;
    std::atomic<std::uint64_t> peakWriteMicroseconds_;
    std::atomic<std::uint64_t> videoBytes_;
    std::atomic<std::uint64_t> audioBytes_;
    std::vector<std::unique_ptr<AudioTrackEncoder>> audioTracks_;
    std::unique_ptr<AudioMixer> audioMixer_;
    SynchronizedErrorText lastError_;
};
