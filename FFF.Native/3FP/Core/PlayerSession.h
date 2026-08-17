#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Audio/WasapiRenderer.h"
#include "3FP/Render/VideoRenderer.h"
#include "Shared/Ffmpeg/SharedFileInput.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct AVCodecContext;
struct AVCodec;
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
    FFFResult DiscardAudioOutput() noexcept;
    FFFResult Stop() noexcept;
    FFFResult Close() noexcept;
    FFFResult Seek(std::int64_t position100ns) noexcept;
    FFFResult SeekKeyframe(std::int64_t position100ns) noexcept;
    FFFResult SeekFrame(std::int64_t frameIndex) noexcept;
    FFFResult StepFrame(std::int32_t direction) noexcept;
    FFFResult StepKeyframe(std::int32_t direction) noexcept;
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
    FFFResult SetAudioExclusiveMode(bool exclusive) noexcept;
    FFFResult SetVolume(float volume, bool muted) noexcept;
    FFFResult SetTimedTextLayer(const FFF3FPTimedTextLayer& layer) noexcept;
    FFFResult GetSnapshot(FFF3FPSnapshot& snapshot) const noexcept;
    FFFResult ReadVideoPixel(FFF3FPVideoPixelProbe& probe) noexcept;
    FFFResult GetAudioPeakLevels(FFF3FPAudioPeakLevels& levels) const noexcept;
    FFFResult GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept;
    FFFResult GetDanmakuStatus(FFF3FPTimedTextStatus& status) noexcept;
    FFFResult GetLyricsStatus(FFF3FPTimedTextStatus& status) noexcept;
    std::string MediaInfo() const;
    std::string LastError() const;

private:
    using Command = std::function<void()>;
    enum class StepOperation {
        Frame,
        Keyframe
    };
    void Enqueue(Command command) noexcept;
    void NotifyAudioRestart() noexcept;
    void NotifyVideoRecovery() noexcept;
    FFFResult ScheduleStep(StepOperation operation, std::int32_t direction) noexcept;
    void ProcessStep() noexcept;
    void DoStepFrame(std::int32_t direction);
    void DoStepKeyframe(std::int32_t direction);
    void Worker() noexcept;
    void PumpPlayback() noexcept;
    void PumpExternalAudio() noexcept;
    bool ShouldDelayAudioUntilVideoFrame() const noexcept;
    void ArmAudioUntilVideoFrame() noexcept;
    void TryReleaseAudioAfterVideoPresentation() noexcept;
    void ReleaseAudioWithoutVideo() noexcept;
    void ApplyAudioPlaybackPause(bool playing) noexcept;
    bool PresentAudioBoundary() noexcept;
    void DoOpen(std::string pathUtf8) noexcept;
    void DoClose(FFF3FPState finalState = FFF3FPState::Closed,
        bool preserveVideoOutput = false) noexcept;
    void DoSeek(std::int64_t position100ns, std::int64_t targetFrame = -1,
        bool exact = true) noexcept;
    void DecodeUntilSeekTarget() noexcept;
    void DoSelectStream(std::int32_t streamIndex, bool video) noexcept;
    void DoLoadExternalAudio(std::string pathUtf8, std::int32_t streamIndex,
        std::int64_t offset100ns) noexcept;
    FFFResult RecreateAudioRenderer(const std::wstring& endpointId, bool exclusive,
        bool paused, std::string& error) noexcept;
    void SuspendAudioRenderer(bool releaseExclusive) noexcept;
    FFFResult ResumeAudioRenderer() noexcept;
    bool RecoverAudioDevice() noexcept;
    bool RecoverVideoDevice() noexcept;
    FFFResult OpenFormat(const std::string& pathUtf8, AVFormatContext** format,
        std::unique_ptr<SharedFileInput>& io, std::string& error) noexcept;
    void CloseFormat(AVFormatContext** format,
        std::unique_ptr<SharedFileInput>& io) noexcept;
    FFFResult OpenDecoder(AVFormatContext* format, std::int32_t streamIndex, bool video,
        AVCodecContext** decoder, std::int32_t hardwareDeviceType = -1,
        std::int32_t* hardwarePixelFormat = nullptr, bool useConfiguredHardware = true,
        const AVCodec* codecOverride = nullptr, std::string* failureReason = nullptr) noexcept;
    FFFResult OpenHardwareVideoDecoder(AVFormatContext* format, std::int32_t streamIndex,
        AVCodecContext** decoder, std::string* failureReason = nullptr) noexcept;
    FFFResult FallbackToSoftwareVideoDecoder(const char* reason) noexcept;
    FFFResult DecodeInitialStillImage() noexcept;
    FFFResult LoadCoverArt() noexcept;
    FFFResult ProbeHardwareVideo(AVFormatContext* format, AVCodecContext* decoder,
        std::int32_t streamIndex, std::int32_t hardwarePixelFormat) noexcept;
    FFFResult DecodePacket(AVCodecContext* decoder, AVPacket* packet, bool video,
        AVFormatContext* owner) noexcept;
    bool PumpVideoPresentation() noexcept;
    void QueueVideoFrame(AVFrame* frame) noexcept;
    void ClearVideoQueue() noexcept;
    void ClearPendingPackets() noexcept;
    void NormalizeVideoFrameTimestamp(AVFrame* frame) noexcept;
    std::int64_t VideoFramePosition(const AVFrame* frame) const noexcept;
    void PresentVideoFrame(AVFrame* frame, AVFormatContext* owner) noexcept;
    void QueueAudioFrame(AVFrame* frame, AVFormatContext* owner, std::int32_t streamIndex) noexcept;
    bool HandleInternalAudioDecodeFailure(FFFResult result, std::string message) noexcept;
    void DisableFailedInternalAudio(FFFResult result, std::string message) noexcept;
    void UpdateAudioDiagnostics() noexcept;
    void TrackPacketBitRate(const AVPacket* packet, AVFormatContext* owner) noexcept;
    void UpdateBitRateForPosition(std::int64_t position100ns) noexcept;
    void ResetBitRateTracking() noexcept;
    void FlushAtEnd() noexcept;
    void PublishSnapshot() noexcept;
    void SetState(FFF3FPState state, const char* operation = nullptr) noexcept;
    void ReportError(FFFResult result, std::string message, const char* operation = nullptr) noexcept;
    void Fail(FFFResult result, std::string message, const char* operation = nullptr) noexcept;
    void Emit(FFF3FPEvent eventType, const std::string& detailJson) const noexcept;
    void RebuildMediaInfo() noexcept;
    std::int64_t ClockPosition() const noexcept;
    void PublishPlaybackClock(std::int64_t position100ns,
        std::int64_t limit100ns) const noexcept;
    void ResetClock(std::int64_t position100ns) noexcept;
    static bool NormalizeLocalPath(const char* pathUtf8, std::string& normalized,
        std::string& error) noexcept;

    FFF3FPDecodeMode decodeMode_;
    FFF3FPEventCallback callback_;
    void* callbackContext_;
    mutable std::mutex mutex_;
    mutable std::mutex snapshotMutex_;
    mutable std::mutex errorMutex_;
    mutable std::mutex timedTextContentMutex_;
    std::condition_variable commandCondition_;
    std::deque<Command> commands_;
    bool stepScheduled_;
    bool stepRepeatRequested_;
    StepOperation pendingStepOperation_;
    std::int32_t pendingStepDirection_;
    std::thread worker_;
    std::atomic<bool> terminate_;
    AVFormatContext* format_;
    std::unique_ptr<SharedFileInput> formatIo_;
    // These objects belong exclusively to the session worker.  FFmpeg permits
    // reuse after av_packet_unref/av_frame_unref, avoiding per-packet heap churn
    // on both the audio and video decode paths.
    AVPacket* playbackPacket_;
    AVPacket* externalAudioPacket_;
    AVFrame* videoDecodeFrame_;
    AVFrame* videoTransferFrame_;
    AVFrame* audioDecodeFrame_;
    AVFrame* externalAudioDecodeFrame_;
    AVCodecContext* videoDecoder_;
    AVCodecContext* audioDecoder_;
    std::int32_t videoStream_;
    std::int32_t audioStream_;
    std::int32_t coverArtStream_;
    AVFrame* coverArtFrame_;
    AVFrame* stillImageFrame_;
    AVFormatContext* externalFormat_;
    std::unique_ptr<SharedFileInput> externalFormatIo_;
    AVCodecContext* externalAudioDecoder_;
    std::int32_t externalAudioStream_;
    std::int64_t externalAudioOffset100ns_;
    std::string externalAudioPath_;
    PlayerAudioRuntimeState audioRuntimeState_;
    std::unique_ptr<PlayerWasapiRenderer> audioRenderer_;
    PlayerVideoRenderer videoRenderer_;
    std::wstring audioEndpointId_;
    bool audioExclusive_;
    float volume_;
    bool muted_;
    FFF3FPSnapshot snapshot_;
    FFF3FPSnapshot publishedSnapshot_;
    std::string mediaInfoJson_;
    std::string lastError_;
    std::atomic<std::int64_t> clockOriginPosition100ns_;
    std::atomic<std::int64_t> clockOriginQpc_;
    mutable std::atomic<std::int64_t> playbackPosition100ns_;
    mutable std::atomic<std::int64_t> playbackClockSampleQpc_;
    mutable std::atomic<std::int64_t> playbackClockLimit100ns_;
    mutable std::atomic<std::uint64_t> playbackClockSequence_;
    std::atomic<FFF3FPState> state_;
    std::int64_t qpcFrequency_;
    std::int64_t seekTarget100ns_;
    std::int64_t seekTargetFrame_;
    bool keyframeSeekPending_;
    std::int64_t lastVideoFrameDuration100ns_;
    std::int64_t nextUntimedVideoPosition100ns_;
    std::deque<std::int64_t> framePtsIndex_;
    std::int64_t framePtsIndexBase_;
    bool rebuildingFrameIndex_;
    bool audioBlockedUntilVideoFrame_;
    std::uint64_t audioUnblockVideoGeneration_;
    bool audioResumePendingAfterVideoFrame_;
    std::deque<AVFrame*> videoFrameQueue_;
    std::vector<AVFrame*> videoFramePool_;
    std::deque<AVPacket*> pendingVideoPackets_;
    std::size_t pendingVideoPacketBytes_;
    std::deque<AVPacket*> pendingAudioPackets_;
    std::size_t pendingAudioPacketBytes_;
    struct BitRateBucket {
        std::int64_t secondIndex;
        std::uint64_t bytes;
    };
    std::deque<BitRateBucket> videoBitRateBuckets_;
    std::deque<BitRateBucket> audioBitRateBuckets_;
    std::int64_t publishedBitRateSecond_;
    bool draining_;
    bool staticImage_;
    bool hardwareFallbackPending_;
    std::string pendingHardwareFallbackReason_;
    bool internalAudioFailurePending_;
    FFFResult internalAudioFailureResult_;
    std::uint32_t internalAudioDecodeErrorCount_;
    // Stable danmaku content is interned by contentId+UTF-8 hash. Position-only
    // layers then share immutable strings instead of allocating 100 wstrings at
    // every 60 Hz submission.
    std::unordered_map<std::uint64_t, std::shared_ptr<const TimedTextRenderCommand::TextContent>> timedTextContentCache_;
};
