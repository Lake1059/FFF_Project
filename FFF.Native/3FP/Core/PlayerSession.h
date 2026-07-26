#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Audio/WasapiRenderer.h"
#include "3FP/Render/VideoRenderer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;

class PlayerSession final {
public:
    explicit PlayerSession(const FFF3FPConfiguration& configuration);
    ~PlayerSession();

    FFFResult Open(const char* localPathUtf8) noexcept;
    FFFResult Play() noexcept;
    FFFResult Pause() noexcept;
    FFFResult Stop() noexcept;
    FFFResult Close() noexcept;
    FFFResult Seek(std::int64_t position100ns) noexcept;
    FFFResult SeekFrame(std::int64_t frameIndex) noexcept;
    FFFResult StepFrame(std::int32_t direction) noexcept;
    FFFResult SelectVideoStream(std::int32_t streamIndex) noexcept;
    FFFResult SelectAudioStream(std::int32_t streamIndex) noexcept;
    FFFResult LoadExternalAudio(const char* localPathUtf8, std::int32_t streamIndex,
        std::int64_t offset100ns) noexcept;
    FFFResult ClearExternalAudio() noexcept;
    FFFResult SetExternalAudioOffset(std::int64_t offset100ns) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits) noexcept;
    FFFResult SetOutputWindow(void* outputWindow) noexcept;
    FFFResult SetAudioEndpoint(const char* endpointIdUtf8) noexcept;
    FFFResult SetVolume(float volume, bool muted) noexcept;
    FFFResult GetSnapshot(FFF3FPSnapshot& snapshot) const noexcept;
    std::string MediaInfo() const;
    std::string LastError() const;

private:
    using Command = std::function<void()>;
    void Enqueue(Command command) noexcept;
    void Worker() noexcept;
    void PumpPlayback() noexcept;
    void PumpExternalAudio() noexcept;
    void DoOpen(std::string pathUtf8) noexcept;
    void DoClose(FFF3FPState finalState = FFF3FPState::Closed) noexcept;
    void DoSeek(std::int64_t position100ns, std::int64_t targetFrame = -1) noexcept;
    void DecodeUntilSeekTarget() noexcept;
    void DoSelectStream(std::int32_t streamIndex, bool video) noexcept;
    void DoLoadExternalAudio(std::string pathUtf8, std::int32_t streamIndex,
        std::int64_t offset100ns) noexcept;
    FFFResult OpenFormat(const std::string& pathUtf8, AVFormatContext** format,
        std::string& error) noexcept;
    FFFResult OpenDecoder(AVFormatContext* format, std::int32_t streamIndex, bool video,
        AVCodecContext** decoder, std::int32_t hardwareDeviceType = -1,
        std::int32_t* hardwarePixelFormat = nullptr, bool useConfiguredHardware = true) noexcept;
    FFFResult OpenHardwareVideoDecoder(AVFormatContext* format, std::int32_t streamIndex,
        AVCodecContext** decoder) noexcept;
    FFFResult LoadCoverArt() noexcept;
    FFFResult ProbeHardwareVideo(AVFormatContext* format, AVCodecContext* decoder,
        std::int32_t streamIndex, std::int32_t hardwarePixelFormat) noexcept;
    FFFResult DecodePacket(AVCodecContext* decoder, AVPacket* packet, bool video,
        AVFormatContext* owner) noexcept;
    void PresentVideoFrame(AVFrame* frame, AVFormatContext* owner) noexcept;
    void QueueAudioFrame(AVFrame* frame, AVFormatContext* owner, std::int32_t streamIndex) noexcept;
    void FlushAtEnd() noexcept;
    void PublishSnapshot() noexcept;
    void SetState(FFF3FPState state, const char* operation = nullptr) noexcept;
    void ReportError(FFFResult result, std::string message, const char* operation = nullptr) noexcept;
    void Fail(FFFResult result, std::string message, const char* operation = nullptr) noexcept;
    void Emit(FFF3FPEvent eventType, const std::string& detailJson) const noexcept;
    void RebuildMediaInfo() noexcept;
    std::int64_t ClockPosition() const noexcept;
    void ResetClock(std::int64_t position100ns) noexcept;
    static bool NormalizeLocalPath(const char* pathUtf8, std::string& normalized,
        std::string& error) noexcept;

    FFF3FPDecodeMode decodeMode_;
    FFF3FPEventCallback callback_;
    void* callbackContext_;
    mutable std::mutex mutex_;
    mutable std::mutex snapshotMutex_;
    mutable std::mutex errorMutex_;
    std::condition_variable commandCondition_;
    std::deque<Command> commands_;
    std::thread worker_;
    std::atomic<bool> terminate_;
    AVFormatContext* format_;
    AVCodecContext* videoDecoder_;
    AVCodecContext* audioDecoder_;
    std::int32_t videoStream_;
    std::int32_t audioStream_;
    std::int32_t coverArtStream_;
    AVFrame* coverArtFrame_;
    AVFormatContext* externalFormat_;
    AVCodecContext* externalAudioDecoder_;
    std::int32_t externalAudioStream_;
    std::int64_t externalAudioOffset100ns_;
    std::string externalAudioPath_;
    std::unique_ptr<PlayerWasapiRenderer> audioRenderer_;
    PlayerVideoRenderer videoRenderer_;
    std::wstring audioEndpointId_;
    float volume_;
    bool muted_;
    FFF3FPSnapshot snapshot_;
    FFF3FPSnapshot publishedSnapshot_;
    std::string mediaInfoJson_;
    std::string lastError_;
    std::atomic<std::int64_t> clockOriginPosition100ns_;
    std::atomic<std::int64_t> clockOriginQpc_;
    mutable std::atomic<std::int64_t> playbackPosition100ns_;
    std::atomic<FFF3FPState> state_;
    std::int64_t qpcFrequency_;
    std::int64_t seekTarget100ns_;
    std::int64_t seekTargetFrame_;
    std::int64_t lastVideoFrameDuration100ns_;
    std::vector<std::int64_t> framePtsIndex_;
    AVFrame* displayedFrame_;
    bool draining_;
};
