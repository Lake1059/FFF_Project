#pragma once

#include "3FP/Api/FFF.Player.Api.h"
#include "3FP/Api/FFF.Player.AudioRenderPlug.h"
#include "3FP/Audio/WasapiRenderer.h"
#include "3FP/Core/AvifTileGridComposer.h"
#include "3FP/Core/PlacementPolicy.h"
#include "3FP/Core/PlaybackPerformancePolicy.h"
// IAMF needs the AOM reference decoder: FFmpeg splits an IAMF element into
// dependent substreams and never mixes them (see IamfAudioDecoder.h).
#include "3FP/Audio/IamfAudioDecoder.h"
#include "3FP/Render/VideoRenderer.h"
#include "Shared/Ffmpeg/SharedFileInput.h"
#include "3FP/Disc/DiscInput.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct AVCodecContext;
struct AVBSFContext;
struct AVCodec;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;
struct AVFilterGraph;
struct AVFilterContext;

class PlayerSession final {
public:
    explicit PlayerSession(const FFF3FPConfiguration& configuration);
    ~PlayerSession();

    FFFResult Open(const char* localPathUtf8) noexcept;
    FFFResult ConfigurePlaybackPerformance(std::int32_t decodeAdapterIndex,
        std::uint32_t videoBufferPolicy, std::uint32_t audioBufferPolicy) noexcept;
    FFFResult GetImageInfo(FFF3FPImageInfo& info) const noexcept;

    // Wide-gamut carrier: P3 sources are converted to BT.2020 once, because the
    // shader only knows Rec.709 and Rec.2020, and an SDR swap chain cannot hold
    // P3 at all. Still images only — video keeps its existing path.
    bool NeedsPrimariesCarrier(const AVFrame* frame) const noexcept;
    FFFResult ConvertPrimariesToBt2020(const AVFrame* input, AVFrame** output) noexcept;
    void ReleasePrimariesCarrierFilter() noexcept;
    FFFResult DiscNavigate(int command, int value, int y) noexcept;
    std::string DiscStatus() const;
    FFFResult CopySdrFrame(void* pixels, std::uint32_t capacity, std::uint32_t& width,
        std::uint32_t& height, bool discOnly) noexcept { return videoRenderer_.CopySdrFrame(pixels, capacity, width, height, discOnly); }
    /// Off-screen frame readback for screenshots (see FFF3FP_CopyFrame).
    FFFResult CopyFrame(void* pixels, std::uint32_t capacity, std::uint32_t& width,
        std::uint32_t& height, std::uint32_t layout, std::uint32_t format) noexcept
        { return videoRenderer_.CopyFrame(pixels, capacity, width, height, layout, format); }
    std::uint32_t LastCopyFrameBitDepth() const noexcept
        { return videoRenderer_.FinalReadbackBitDepth(); }
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
    // Audio delay for the *embedded* audio track (the external-track counterpart is
    // SetExternalAudioOffset). Positive = audio plays later than the video. Applied to
    // the presentation position computed in QueueAudioFrame, so a positive value opens
    // a gap the renderer's own silence-insertion path bridges. Session-level preference:
    // DoClose does not clear it, so it is expected to survive Close/Open.
    FFFResult SetAudioDelay(std::int64_t delay100ns) noexcept;
    FFFResult SetColorMode(FFF3FPColorMode mode, float sdrPeakNits,
        float hdrPeakNits, float paperWhiteNits, bool forceHdrOutput) noexcept;
    FFFResult SetPresentConfig(bool enableTearing) noexcept;
    // Cadence read-back (PlayerApi exports FFF3FP_GetPresentPacingStatus). Non-queueing:
    // the renderer publishes these from atomics. The flip-rate bound itself is automatic
    // (min of the declared panel rate and the per-media frame-rate ceiling the session
    // publishes at every open / media switch) and has no setter.
    FFFResult GetPresentPacingStatus(FFF3FPPresentPacingStatus& status) noexcept;
    FFFResult SetOutputWindow(void* outputWindow) noexcept;
    FFFResult SetInteractiveMove(bool enabled) noexcept;
    FFFResult SetViewTransform(float zoom, float panX, float panY) noexcept;
    FFFResult PanView(float deltaX, float deltaY) noexcept;
    FFFResult ImageZoom(float& zoom, float& minimum, float& maximum) const noexcept;
    // Cursor-anchored zoom step: see PlayerVideoRenderer::ZoomViewAt.
    FFFResult ZoomViewAt(float factor, float anchorX, float anchorY,
        float* resultingZoom = nullptr) noexcept;
    void ViewTransform(float& zoom, float& panX, float& panY) const noexcept;
    // View rotation in quarter turns clockwise (0..3). Independent of
    // SetViewTransform so existing callers keep working unchanged.
    FFFResult SetViewRotation(std::uint32_t quarterTurnsClockwise) noexcept;
    std::uint32_t ViewRotation() const noexcept;
    // Cap the fit box at the source's native size (see PlayerVideoRenderer).
    FFFResult SetFitLimitToNative(bool enable) noexcept;
    FFFResult Set360View(bool enabled, float yaw, float pitch, float fovY) noexcept;
    FFFResult SetAudioEndpoint(const char* endpointIdUtf8) noexcept;
    FFFResult SetAudioExclusiveMode(bool exclusive) noexcept;
    FFFResult SetPlaybackRate(double rate) noexcept;
    FFFResult SetAudioEffects(bool enabled, const float* gains, float wave) noexcept;
    FFFResult SetScalingMode(std::uint32_t mode) noexcept { const auto r = videoRenderer_.SetScalingMode(mode); if (r == FFFResult::Success) (void)Redraw(); return r; }
    FFFResult SetVolume(float volume, bool muted) noexcept;
    // External bed render plug (3FP/Api/FFF.Player.AudioRenderPlug.h). Install,
    // replace or clear; the session owns the context from the call that installs it
    // and is the only code that calls plug->destroy. See PlayerApi.cpp for the
    // export's contract, and the audioPlug_ members for why the renderer only ever
    // borrows the pointer.
    FFFResult SetAudioRenderPlug(const FFF3FPAudioRenderPlug* plug) noexcept;
    // Read-back for FFF3FP_GetAudioRenderPlugState: whether a plug is installed, its
    // UTF-8 name, and the seam's rendered/declined frame counters. The counters come
    // from the shared runtime state, not from the renderer, because the renderer may
    // be replaced at any moment by a device switch.
    void GetAudioRenderPlugState(std::uint32_t& active, std::string& name,
        std::uint64_t& framesRendered, std::uint64_t& framesDeclined) const noexcept;
    FFFResult SetTimedTextLayer(const FFF3FPTimedTextLayer& layer) noexcept;
    // RTX Video Super Resolution opt-in switch (default Off).
    FFFResult SetVideoSuperResolution(FFF3FPVideoSuperResolution mode) noexcept;
    // Host-declared flag: this session is driving a frame-exact comparison
    // surface that compares several streams side by side. While set, enabling VSR is
    // refused, because the AI model makes presented pixels an unreproducible
    // function of the decoded frame. Clearing it never disables VSR by itself --
    // the host decides, this only gates the transition and reports state.
    FFFResult SetComparisonMode(bool active) noexcept;
    FFFResult GetVideoSuperResolutionStatus(
        FFF3FPVideoSuperResolutionStatus& status) const noexcept;
    // NVIDIA automatic HDR (RTX Video HDR / TrueHDR) opt-in, default Off.
    // Same ownership split as VSR: the session gates the comparison-mode
    // transition and the renderer owns the device-facing decision.
    FFFResult SetVideoAutoHdr(FFF3FPVideoAutoHdr mode) noexcept;
    FFF3FPVideoAutoHdr RequestedVideoAutoHdr() const noexcept;
    FFFResult GetVideoAutoHdrStatus(
        FFF3FPVideoAutoHdrStatus& status) const noexcept;
    FFFResult GetSnapshot(FFF3FPSnapshot& snapshot) const noexcept;
    // HDR presentation diagnostics (see FFF3FPHdrSignalDiagnostics in
    // FFF.Player.Api.h): which ST 2094 mechanism actually ran for the frames drawn
    // so far, and whether the per-frame display signaling reached DXGI.
    FFFResult GetHdrSignalDiagnostics(
        FFF3FPHdrSignalDiagnostics& diagnostics) const noexcept;
    FFFResult ReadVideoPixel(FFF3FPVideoPixelProbe& probe) noexcept;
    // Batch pixel readback
    FFFResult ReadVideoPixelRegion(std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height, float* dst,
        std::uint32_t dstFloatCount, std::uint32_t* outputBitDepth) noexcept;
    FFFResult GetAudioPeakLevels(FFF3FPAudioPeakLevels& levels) const noexcept;
    FFFResult GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept;
    FFFResult GetDanmakuStatus(FFF3FPTimedTextStatus& status) noexcept;
    FFFResult GetLyricsStatus(FFF3FPTimedTextStatus& status) noexcept;
    // Render-target diagnostics
    FFFResult GetRenderTargetInfo(FFF3FPRenderTargetInfo& info) noexcept;
    // Re-present the last cached frame (host calls it after a child HWND resize).
    FFFResult Redraw() noexcept;
    // ST 2094-40 injection, new with this change.
    // See FFF3FP_SetHdrDynamicMetadata in FFF.Player.Api.h for the units and
    // matching rules.
    FFFResult SetInjectedHdrMetadata(const FFF3FPHdrDynamicMetadataEntry* entries,
        std::uint32_t count) noexcept;
    FFFResult ClearInjectedHdrMetadata() noexcept;
    std::uint32_t HdrMetadataSource() const noexcept;
    // See FFF3FP_SetDynamicMetadata in FFF.Player.Api.h: the generic carrier
    // over the same table, kind-tagged packets.
    FFFResult SetDynamicMetadata(const FFF3FPDynamicMetadataPacket* packets,
        std::uint32_t count) noexcept;
    FFFResult ClearDynamicMetadata(std::uint32_t kind) noexcept;
    // Neutral frame side-data ring (see FFF3FP_CopyFrameSideDataAfter in the
    // API header): hands back the untouched bytes a presented frame carried,
    // keyed by the presentation index the injection rule uses. The session
    // neither parses them nor knows what produced them.
    FFFResult CopyFrameSideDataAfter(std::uint32_t sideDataType,
        std::int64_t fromFrameIndex, void* buffer, std::uint32_t capacity,
        std::uint32_t& writtenSize, std::int64_t& foundFrameIndex) noexcept;
    // Neutral stream-level side data (see FFF3FP_CopyStreamSideData in the API
    // header): the first-seen blob of a type from the current video stream's
    // coded side data, bytes untouched. A separate entry point because the
    // stream-level type enumeration is a different one from the per-frame
    // enumeration above -- the two collide on raw numbers.
    FFFResult CopyStreamSideData(std::uint32_t sideDataType, void* buffer,
        std::uint32_t capacity, std::uint32_t& writtenSize) noexcept;
    // Neutral auxiliary video layer (see FFF3FP_SetAuxiliaryComposition in the
    // API header): records one per-frame composition request. Additive entry
    // point; a host that never calls it leaves every existing path untouched.
    FFFResult SetAuxiliaryComposition(const FFF3FPAuxiliaryComposition& composition) noexcept;
    std::string MediaInfo() const;
    std::string LastError() const;

private:
    void ResetTempo(std::int64_t position) noexcept;
    FFFResult TempoAudio(AVFrame* frame, std::int64_t position, std::int64_t timestampResolution = 0) noexcept;
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
    bool VideoQueueSaturated() const noexcept;
    // Decoded pictures the decoder is holding right now, charged against the same
    // memory budget as the queue. Non-zero only for an animated image whose decoder
    // actually ended up running frame threads.
    std::size_t VideoQueuePipelineReservation() const noexcept;
    // VideoFrameQueueLimit for this session, i.e. the queue depth the 128 MB budget
    // allows once the pipeline reservation above is subtracted.
    std::size_t VideoQueueLimit() const noexcept;
    void TryCompletePlaybackPreroll() noexcept;
    void UpdateDrainedAudioClock() noexcept;
    void DrainInternalAudio() noexcept;
    void PumpExternalAudio() noexcept;
    bool ShouldDelayAudioUntilVideoFrame() const noexcept;
    void ArmAudioUntilVideoFrame() noexcept;
    void TryReleaseAudioAfterVideoPresentation() noexcept;
    void ReleaseAudioWithoutVideo() noexcept;
    void ApplyAudioPlaybackPause(bool playing) noexcept;
    bool PresentAudioBoundary() noexcept;
    void DoOpen(std::string pathUtf8) noexcept;
    bool ReopenDiscDemux();
    void PublishDisc();
    bool HoldDisc();
    void DoClose(FFF3FPState finalState = FFF3FPState::Closed,
        bool preserveVideoOutput = false) noexcept;
    FFFResult DoSeek(std::int64_t position100ns, std::int64_t targetFrame = -1,
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
    // A second demuxer for the trial decode, opened under the same IO policy as
    // OpenFormat (protocol allow/deny lists, the same file-or-repaired-memory
    // input, the same demuxer gate) and additionally wired to the session's
    // terminate flag so a slow read can be abandoned instead of joined. It skips
    // avformat_find_stream_info: the trial needs only the stream table, which the
    // header already carries.
    FFFResult OpenProbeFormat(const std::string& pathUtf8, AVFormatContext** format,
        std::unique_ptr<SharedFileInput>& io, std::string& error) noexcept;
    static int ProbeInterrupt(void* opaque) noexcept;
    void CloseFormat(AVFormatContext** format,
        std::unique_ptr<SharedFileInput>& io) noexcept;
    FFFResult OpenDecoder(AVFormatContext* format, std::int32_t streamIndex, bool video,
        AVCodecContext** decoder, std::int32_t hardwareDeviceType = -1,
        std::int32_t* hardwarePixelFormat = nullptr, bool useConfiguredHardware = true,
        const AVCodec* codecOverride = nullptr, std::string* failureReason = nullptr) noexcept;
    FFFResult OpenHardwareVideoDecoder(AVFormatContext* format, std::int32_t streamIndex,
        AVCodecContext** decoder, std::string* failureReason = nullptr) noexcept;
    // The one hardware gate: feed the stream's first packets through a decoder that
    // has just been opened and see whether a frame comes out. Asking the GPU is the
    // only question that is exact for every backend and every device -- a format or
    // capability table has to be maintained per hwaccel and per GPU generation, and
    // being wrong in the "unsupported" direction silently disables a path that works.
    bool HardwareDecoderProducesFrame(AVFormatContext* format, std::int32_t streamIndex,
        AVCodecContext* decoder, std::string* failureReason = nullptr) noexcept;
    // Asking the GPU once per stream is cheap but not free: it costs a decoder open
    // plus a reopened demux for the trial packets. A stream the GPU has already
    // refused is refused again on every retry, every track switch and every device
    // rebuild, so the answer is remembered for the life of the session and the
    // hardware attempt is skipped the next time.
    //
    // Only a refusal that says something about the *stream* is remembered -- no
    // hardware pixel format for this codec, or the trial decode being rejected. A
    // failure that says something about the *machine* is not: a decoder that could
    // not be opened may simply have run out of concurrent decode sessions, which is
    // exactly what a multi-view player does, and remembering that would disable a
    // route that works as soon as another view closes.
    bool HardwareRouteKnownUnsupported(const AVStream* stream) const noexcept;
    void RememberUnsupportedHardwareRoute(const AVStream* stream) noexcept;
    FFFResult FallbackToSoftwareVideoDecoder(const char* reason) noexcept;
    // Maximum workers the shared thread budget allows: the CPU count minus the player's
    // own threads (worker, audio renderer, present thread) minus one core of scheduler
    // headroom, so total player threads stay below the CPU count.
    std::uint32_t DecoderThreadBudget() const noexcept;
    // Chooses the software decoder's worker count. Fixed and budget-bounded by default;
    // with adaptiveDecoderThreads enabled it starts at the floor (4 by default) and
    // climbs a 4/8/16/24/32 ladder while measured load says the decoder is starving --
    // never past the budget, and never crossing the preferred domain boundary
    // (efficiency class / one L3 domain) in a single step.
    std::uint32_t SelectSoftwareDecoderThreads(std::uint32_t width,
        std::uint32_t height) const noexcept;
    // Evaluates measured decode load and, when the decoder is the constraint, reopens
    // it at the next rung. Never descends.
    void EvaluateDecoderLoad() noexcept;
    FFFResult CompleteHardwareFallback(const char* failureMessage) noexcept;
    // Presents the first frame while the session stays stopped. Used for a still
    // picture and for an animated one, both of which must show a frame without
    // playback running; rewinds afterwards so play starts from the beginning.
    FFFResult DecodeInitialFrame() noexcept;
    // A tiled still image (grid AVIF/HEIC) has no pre-assembled picture anywhere in
    // the file: it is one stream per tile plus a grid group saying where the tiles
    // go. This decodes them and presents the canvas they make, and it can run again
    // after a device change because the tile packets were read at open.
    FFFResult ComposeTileGridFrame() noexcept;
    // Reads each tile's packets once, in draw order's stream set, so the canvas can
    // be rebuilt without a demuxer rewind (a still image has no timeline to seek to).
    void ReadTileGridPackets() noexcept;
    void ResetTileGrid() noexcept;
    /// Applies a display matrix found on the newest decoded frame. Needed because
    /// still images expose EXIF orientation only as frame side data.
    void ApplyRotationFromDecodedFrame() noexcept;
    // Fills `info` from the session state. Only ever runs on the worker thread
    // (called at the end of DoOpen) because it reads decoder-owned frames; the
    // public GetImageInfo hands out the published copy instead.
    void BuildImageInfo(FFF3FPImageInfo& info) const noexcept;
    FFFResult LoadCoverArt() noexcept;
    FFFResult DecodePacket(AVCodecContext* decoder, AVPacket* packet, bool video,
        AVFormatContext* owner) noexcept;
    FFFResult QueueSoftwareVideoDecode(AVPacket* packet) noexcept;
    void SoftwareVideoDecodeWorker(AVCodecContext* decoder) noexcept;
    bool SoftwareVideoDecodePending() noexcept;
    bool SoftwareVideoInputFull() noexcept;
    bool CollectSoftwareVideoFrame() noexcept;
    void StopSoftwareVideoDecode() noexcept;
    bool PumpVideoPresentation() noexcept;
    void QueueVideoFrame(AVFrame* frame) noexcept;
    void ClearVideoQueue() noexcept;
    // Copies the side data of one about-to-be-presented frame into the ring.
    void CacheFrameSideData(const AVFrame* frame, std::int64_t frameIndex) noexcept;
    void ClearSideDataRing() noexcept;
    // Captures / drops the stream-level coded side data of a bound video
    // stream (see CopyStreamSideData). Capture replaces the cache, so a newly
    // selected track can never be answered with the previous track's blobs.
    void CacheStreamSideDataFromStream(const struct AVStream* stream) noexcept;
    void ClearStreamSideData() noexcept;
    void ClearPendingPackets() noexcept;
    static void ClearPacketQueue(std::deque<AVPacket*>& queue, std::size_t& bytes) noexcept;
    void DecodePendingPacket(bool video) noexcept;
    void NormalizeVideoFrameTimestamp(AVFrame* frame) noexcept;
    std::int64_t VideoFramePosition(const AVFrame* frame) const noexcept;
    void PresentVideoFrame(AVFrame* frame, AVFormatContext* owner) noexcept;
    // --- optional auxiliary video layer -------------------------------------
    // One host request, keyed by the presentation index it belongs to.
    struct AuxCompositionRequest {
        std::int64_t frameIndex;
        std::uint32_t enabled;
        std::uint32_t residualCenter;
        std::int32_t residualShift;
        std::uint32_t maxValue;
    };
    // Opens the second video stream (software decode; the base layer keeps
    // whatever route it chose) when the host first asks for a composition.
    FFFResult OpenAuxiliaryDecoder() noexcept;
    // Flushes the decoder and drops every queued frame; used by seek and EOF.
    void FlushAuxiliaryLayer() noexcept;
    // Flushes, frees the decoder and clears the parameter ring; used by close
    // and by a video-track switch.
    void CloseAuxiliaryLayer() noexcept;
    // Feeds one demuxed packet of auxStream_ through the auxiliary decoder and
    // queues every frame it produces. Never touches the base-layer pipeline.
    void DecodeAuxiliaryPacket(AVPacket* packet) noexcept;
    // Returns the auxiliary frame whose PTS is closest to the base frame's,
    // within one base-frame period; nullptr when nothing is close enough. The
    // caller owns the returned frame and frees it after presentation.
    AVFrame* TakeAuxiliaryFrame(std::int64_t basePts, AVFormatContext* owner) noexcept;
    // Same selection rule the kind-3 table uses: the last request whose
    // frameIndex is <= the presented index, or the first entry when the index
    // precedes all of them. Returns false when no request has ever arrived.
    bool SelectAuxiliaryComposition(std::int64_t frameIndex,
        AuxCompositionRequest& out) const noexcept;
    void ResetCadenceSpread() noexcept;
    // Adaptive presentation pacing: engage or release the renderer's pace floor from
    // the late branch of PresentVideoFrame. Worker-thread-only; the floor itself is
    // an atomic in the renderer, which the presenter schedules against.
    void EngagePresentationPace(double floorHz, double contentHz,
        std::int64_t lag100ns, const char* via) noexcept;
    void ReleasePresentationPace(const char* reason) noexcept;
    // Settles the rolling engage window once it holds PaceWindowFrames arrivals.
    // Returns true when the pacer engaged on it, in which case the caller must
    // show the picture rather than discard it; the window restarts on every
    // outcome, exactly like the pool rung and upload ladders restart theirs.
    bool SettlePaceWindow(std::int64_t lag100ns, std::int64_t clock100ns,
        double contentHz) noexcept;
    // Drops a half-filled rolling window: its rate estimate is only meaningful
    // across a contiguous run of arrivals.
    void ResetPaceWindow() noexcept;
    // nullptr = 还没测够，调用方必须退回容器那两个字段而不是蒙一个"constant"。
    const char* MeasuredFrameRateMode() const noexcept;
    const char* FrameRateModeLabel(const struct AVStream* stream, AVRational guessed,
        AVRational nominal) const noexcept;
    void QueueAudioFrame(AVFrame* frame, AVFormatContext* owner, std::int32_t streamIndex) noexcept;
    void UpdateInputAudioPeakLevels(const AVFrame* frame) noexcept;
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
    // Per-media frame-rate ceiling (Hz) for the renderer's flip-rate bound, computed
    // from the stream's container frame-rate fields at every open / media switch /
    // media-info rebuild. 0 = no usable bound; the renderer keeps the panel half only.
    double MediaFrameRateCeilingHz(const struct AVStream* stream) const noexcept;
    std::int64_t TimelineOrigin100ns(const AVFormatContext* owner) const noexcept;
    std::int64_t StreamTimestampPosition100ns(const AVFormatContext* owner,
        std::int32_t streamIndex, std::int64_t timestamp) const noexcept;
    bool ShouldGateAudioAtVideoStart() const noexcept;
    std::int64_t ClockPosition() const noexcept;
    void PublishPlaybackClock(std::int64_t position100ns,
        std::int64_t limit100ns) const noexcept;
    void ResetClock(std::int64_t position100ns) noexcept;
    static bool NormalizeLocalPath(const char* pathUtf8, std::string& normalized,
        std::string& error) noexcept;

    // The route the host asked for, normalized in the constructor: Unspecified
    // ("no preference") becomes Gpu, which is the GPU-first default -- the CPU is
    // what a hardware refusal falls back to, never what is asked for up front.
    FFF3FPDecodeMode decodeMode_;
    std::int32_t decodeAdapterIndex_ = -1;
    PlaybackPerformancePolicy performance_;
    // See SetComparisonMode(). Plain bool: only ever touched on the host's
    // calling thread, never read from the render thread.
    bool comparisonModeActive_ = false;
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
    std::thread softwareVideoWorker_;
    std::mutex softwareVideoMutex_;
    std::mutex softwareVideoCodecMutex_;
    std::condition_variable softwareVideoCondition_;
    std::deque<AVPacket*> softwareVideoPackets_;
    std::deque<AVFrame*> softwareVideoOutputs_;
    unsigned softwareVideoOutputLimit_ = 1;
    bool softwareVideoBusy_ = false;
    bool softwareVideoStopping_ = false;
    int softwareVideoResult_ = 0;
    AVFormatContext* format_;
    std::unique_ptr<DiscInput> disc_;
    std::atomic<bool> discCancel_{false};
    // Caller-thread disc checks use this atomic mirror, never the worker-owned disc_.
    std::atomic<bool> discOpened_{false};
    std::string discStatus_ = "{}";
    std::uint64_t discGraphicsSequence_ = 0;
    std::int64_t discPositionOffset_ = 0;
    bool discDrained_ = false;
    unsigned discInvalidPackets_ = 0;
    std::unique_ptr<SharedFileInput> formatIo_;
    // These objects belong exclusively to the session worker.  FFmpeg permits
    // reuse after av_packet_unref/av_frame_unref, avoiding per-packet heap churn
    // on both the audio and video decode paths.
    AVPacket* playbackPacket_;
    // A still image's one packet, kept so a hardware decode that is rejected only at
    // decode time can be retried on the CPU decoder: image2/*_pipe demuxers have no
    // timeline to seek back to, so the packet would otherwise be gone.
    AVPacket* stillImagePacket_;
    AVPacket* externalAudioPacket_;
    AVFrame* videoDecodeFrame_;
    AVFrame* videoTransferFrame_;
    AVFrame* audioDecodeFrame_;
    AVFrame* externalAudioDecodeFrame_;
    AVCodecContext* videoDecoder_;
    AVCodecContext* audioDecoder_;
    // Set when the audio track is IAMF and the AOM reference decoder took over
    // from FFmpeg. Null for every other codec, which keeps the ordinary path
    // byte-for-byte unchanged.
    std::unique_ptr<IamfAudioDecoder> iamfDecoder_;
    // Sound system requested for IAMF output (-1 = decoder default).
    int iamfSoundSystem_ = -1;
    // True once the reference decoder has produced at least one frame, so a
    // stream that ends silently can be distinguished from one that never worked.
    bool iamfProducedAnyAudio_ = false;
    // Path of the media currently open. Kept because the IAMF descriptor OBUs
    // must be re-read from the file after the demuxer has consumed them.
    std::string mediaPath_;
    // 临时诊断开关（默认关）：排查 IAMF 泵是否在跑。
    /// Opens the AOM reference decoder when the track is IAMF. False means the
    /// caller must use the ordinary FFmpeg decoder.
    bool TryOpenIamfAudioDecoder() noexcept;
    /// Decodes one IAMF access unit through the reference decoder.
    FFFResult DecodeIamfPacket(AVPacket* packet) noexcept;
    /// Pulls decoded IAMF frames from the streaming decoder into the renderer.
    void PumpIamfAudio() noexcept;
    /// Abandons the IAMF decoder and re-opens the track through FFmpeg.
    void FallBackFromIamf(const std::string& reason) noexcept;
    void ReleaseIamfDecoder() noexcept;
    std::int32_t videoStream_;
    // Software-decoder thread ladder. Driven by measured load, not by resolution: a
    // resolution says nothing about whether this machine can keep up with this codec.
    // It starts at the floor and only ever climbs -- one step per starving window,
    // within the shared thread budget and without jumping past the preferred domain
    // boundary (see SelectSoftwareDecoderThreads for why a ladder that also descends
    // was tried and removed).
    bool adaptiveThreadsEnabled_ = false;
    std::uint32_t threadLadderMin_ = 4;            // floor / first rung (soft-decode minimum)
    std::uint32_t threadLadderMax_ = 32;
    std::uint32_t decoderThreads_ = 0;             // current rung; 0 = not yet chosen
    std::uint64_t ladderWindowDecoded_ = 0;        // counters when the window armed
    std::uint64_t ladderWindowPresented_ = 0;
    std::int64_t ladderWindowStart100ns_ = 0;
    // Pump-tick evidence for the window being measured: how many playback ticks ran,
    // how many found the decode queue saturated (either gate), and the split the pool
    // verdict needs -- ticks gated by the queue limit itself, and ticks with the queue
    // below its minimum. Diagnostics for the trace line and the pool climb gate.
    std::uint64_t pumpIterations_ = 0;
    std::uint64_t pumpSaturatedIterations_ = 0;
    std::uint64_t pumpQueueBoundIterations_ = 0;
    std::uint64_t pumpStarvedIterations_ = 0;
    // Counter baselines captured when the ladder window armed, so a window reads
    // deltas rather than session totals (the same discipline as decoded/presented).
    std::uint64_t ladderWindowUpload100ns_ = 0;
    std::uint64_t ladderWindowConvert100ns_ = 0;
    std::uint64_t ladderWindowDropped_ = 0;
    // Current rung of the software look-ahead pool ladder: indexes SoftwarePoolBudgets
    // / SoftwarePoolFrameCaps (128 MB / 4 frames at the floor, 1024 MB / 16 at the top).
    // Climbs one rung per window the pool gate finds pool-starved; like the thread
    // ladder it never descends, and it resets per media (see DoClose).
    std::uint8_t softwarePoolRung_ = 0;
    // Frames dropped by ClearVideoQueue (seek, rung reopen, fallback, close). They have
    // no drop counter of their own; the trace line reports them.
    std::uint64_t clearedVideoFrames_ = 0;
    // Adaptive CPU placement (P/E classes or per-CCD L3 domains). Escalation order is
    // spill -> more decoder threads -> priority, so the cheap action comes first.
    PlacementPolicy placement_;
    std::uint32_t workerThreadId_ = 0;
    std::uint32_t placementGoodWindows_ = 0;
    // Reopen back-off: a rung costs a seek, so require sustained starvation first and
    // then a settling period before judging the new rung. The deadline is wall-clock:
    // the pump loop calls the evaluator thousands of times per second, so an
    // iteration-count guard (the earlier design) expired in milliseconds.
    std::int64_t ladderSettlingUntil100ns_ = 0;
    std::int32_t audioStream_;
    std::int32_t coverArtStream_;
    AVFrame* coverArtFrame_;
    AVFrame* stillImageFrame_;
    // Clockwise quarter turns the source asks to be displayed with (0..3).
    // Filled at open time from the container's rotation metadata / display
    // matrix, for images and video alike. 0 means "no rotation requested".
    std::uint32_t sourceRotation_ = 0;
    // Primaries-carrier filter graph (P3 -> BT.2020), built on demand for a
    // single still image and kept until the next open.
    AVFilterGraph* gamutGraph_ = nullptr;
    AVFilterContext* gamutSource_ = nullptr;
    AVFilterContext* gamutSink_ = nullptr;
    int gamutSourceWidth_ = 0;
    int gamutSourceHeight_ = 0;
    int gamutSourceFormat_ = -1;
    AVFormatContext* externalFormat_;
    std::unique_ptr<SharedFileInput> externalFormatIo_;
    AVCodecContext* externalAudioDecoder_;
    std::int32_t externalAudioStream_;
    std::int64_t externalAudioOffset100ns_;
    // Kept next to the offset it mirrors rather than at the end of the block:
    // PlayerSession never crosses the DLL boundary (hosts hold an opaque handle), so
    // the append-at-the-tail rule that FFF3FPSnapshot follows does not apply here,
    // and the constructor's mem-init list has to stay in declaration order.
    std::int64_t audioDelay100ns_;
    std::string externalAudioPath_;
    PlayerAudioRuntimeState audioRuntimeState_;
    std::unique_ptr<PlayerWasapiRenderer> audioRenderer_;
    std::atomic<double> playbackRate_{1.0};
    AVFilterGraph* tempoGraph_ = nullptr;
    AVFilterContext* tempoInput_ = nullptr;
    AVFilterContext* tempoOutput_ = nullptr;
    // Atomics because the playback-clock readers (ClockPosition / GetSnapshot /
    // UpdateDrainedAudioClock) run on other threads than the command-queue worker
    // that owns all writes; relaxed is enough (independent scalars, no ordering
    // requirements beyond eventual visibility).
    std::atomic<std::int64_t> tempoSamples_{0};
    std::atomic<std::int64_t> tempoOrigin_{0};
    bool equalizerEnabled_ = false;
    float equalizerGains_[10]{};
    float waveVolume_ = 1;
    // The bed render plug the host installed. Copied, because the caller may free
    // its own struct the moment the call returns; `context` is the kernel's from
    // then on and only this class releases it (see SetAudioRenderPlug / ~PlayerSession).
    // A renderer therefore only borrows the pointer, which is what makes a device
    // restart safe: RecreateAudioRenderer installs the same context into the
    // successor instead of inheriting a renderer instance's ownership of it.
    mutable std::mutex audioPlugMutex_;
    FFF3FPAudioRenderPlug audioPlug_{};
    bool audioPlugInstalled_ = false;
    std::string audioPlugName_;
    // Contexts handed over by a replaced or cleared plug, waiting for a worker pass
    // to release them (a release must not race a render() call in flight). Draining a
    // list instead of holding one "previous" pointer matters: the destructor drops
    // queued commands, and whatever is still parked here at that point is released
    // there rather than leaked.
    std::vector<FFF3FPAudioRenderPlug> audioPlugRetired_;
    PlayerVideoRenderer videoRenderer_;
    std::wstring audioEndpointId_;
    bool audioExclusive_;
    float volume_;
    bool muted_;
    FFF3FPSnapshot snapshot_;
    FFF3FPSnapshot publishedSnapshot_;
    // Image details for GetImageInfo, built once on the worker thread at the end
    // of DoOpen and handed out under snapshotMutex_ - the decoder-owned frames
    // the build reads must not be touched from the caller's thread.
    bool hasPublishedImageInfo_ = false;
    FFF3FPImageInfo publishedImageInfo_{};
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
    // 帧率模式的实测依据：呈现出来的逐帧间距散布。容器那两个字段单独不够定"恒定/可变"——
    // 实测真 VFR 的 .ts 在会话里 av_guess_frame_rate() 与 r_frame_rate 同为 20/1，比较它们
    // 永远说"恒定"。只有"逐帧测出来/逐帧声明"那两档算证据（队列里下一帧的 PTS 差、
    // frame->duration）；退回整片容器率的那些帧不喂这里，那是猜测不是观测。
    std::int64_t cadenceSpreadMin100ns_ = 0;
    std::int64_t cadenceSpreadMax100ns_ = 0;
    std::int32_t cadenceSpreadSamples_ = 0;
    // 本媒体已经观测到的最快速率（Hz），与散布同生命周期（ResetCadenceSpread 清）。
    // 上限 = max(容器字段的初值, 这个数)：容器的 r_frame_rate 在 MPEG-TS 上会偏低。
    double mediaObservedPeakHz_ = 0.0;
    bool cadenceModeRefreshPending_ = false;
    // 已经发布进 media info JSON 的那一版判词（指向上面两个字符串常量之一，或 nullptr＝还没发布）。
    // 判词是"到目前为止测到的散布"：一个前 2 秒恒速、后面才变速的文件，在第 12 帧时读
    // constant 是对的，但它必须在散布真的变宽时重新发布——一次性的话就永远停在早期的
    // constant 上（实测过这个形状：12b 的 40fps 段要到第 ~40 帧才进散布）。
    const char* publishedFrameRateMode_ = nullptr;
    static constexpr std::int32_t MinimumCadenceModeSamples = 12;
    // --- Adaptive presentation pacing (see the late branch of PresentVideoFrame) ---
    // Worker-thread-only. When the pipeline falls behind the media clock the classic
    // answer is to discard late pictures; the pacer instead holds every picture at a
    // slower cadence (the renderer's pace floor), which a variable-refresh panel
    // follows as a lower refresh rate. The trade is bounded by the drift cap: past it
    // the discard path resumes so A/V lag cannot grow without limit. Only the
    // "pipeline fell behind" bucket engages -- a frame the panel itself cannot scan
    // out is not something a slower cadence can fix.
    bool paceEngaged_ = false;
    double paceHz_ = 0.0;                      // floor currently in force (0 = off)
    std::uint32_t paceLateStreak_ = 0;         // consecutive pipeline-late pictures
    std::uint32_t paceOverDeliverStreak_ = 0;  // consecutive late pops that found a newer picture queued
    std::uint64_t pacePacedFrames_ = 0;        // pictures shown by the pacer since engage (trace)
    std::int64_t paceStreakClock100ns_ = 0;    // streak anchors for the rate estimate
    std::int64_t paceStreakLag100ns_ = 0;
    // Rolling engage window. The streak above needs 12 consecutive pipeline-late
    // pictures, but a pool gate that pins the queue at a couple of frames makes
    // late pictures alternate with on-time ones (measured on the 8K stream: the
    // longest late run was 4 of 1194 arrivals), so that deficit never builds a
    // streak at all. The window counts every arrival -- late or on-time -- and
    // settles every PaceWindowFrames of them, which is the only evidence an
    // oscillating deficit ever produces. The sustainable rate it paces at is the
    // arrival rate itself (while the pacer is off, every decoded picture becomes
    // exactly one arrival), which is phase-independent where the streak's
    // lag-growth estimate is not.
    std::uint32_t paceWindowFrames_ = 0;        // arrivals counted in the window
    std::uint32_t paceWindowLate_ = 0;          // late arrivals inside it
    std::int64_t paceWindowStart100ns_ = 0;     // clock at the window's first arrival
    std::int64_t paceWindowLateLagSum100ns_ = 0; // sum of late lags; the mean is the drift-cap test
    std::size_t readTraceCount_ = 0;   // 仅 FFF_SEEK_TRACE=1 时用，给定位失败排查读位置
    // Frames discarded because the source delivers faster than the panel can scan out, kept
    // apart from snapshot_.droppedVideoFrames (which means "the pipeline fell behind the
    // media clock"). Read from the API thread, written on the worker thread.
    std::atomic<std::uint64_t> displayRateDiscardedFrames_{ 0 };
    std::int64_t nextUntimedVideoPosition100ns_;
    std::deque<std::int64_t> framePtsIndex_;
    std::int64_t framePtsIndexBase_;
    bool rebuildingFrameIndex_;
    bool audioBlockedUntilVideoFrame_;
    bool playbackPreroll_;
    std::uint64_t audioUnblockVideoGeneration_;
    bool audioResumePendingAfterVideoFrame_;
    std::deque<AVFrame*> videoFrameQueue_;
    std::vector<AVFrame*> videoFramePool_;
    // --- Optional auxiliary video layer (see FFF3FP_SetAuxiliaryComposition) ---
    // A second video stream decoded alongside the base layer and paired by PTS.
    // Every member below is inert until the host asks for a composition with
    // enabled != 0: a host that never calls the entry point pays nothing and the
    // single-stream path is byte-for-byte what it was. The worker thread owns the
    // decoder and both frame queues; auxMutex_ is the only hand-off for the
    // parameter ring, which the API thread writes.
    std::int32_t auxStream_ = -1;
    AVCodecContext* auxDecoder_ = nullptr;
    AVFrame* auxDecodeFrame_ = nullptr;
    std::deque<std::pair<std::int64_t, AVFrame*>> auxFrameQueue_;  // aux-stream PTS -> owned frame
    std::deque<AuxCompositionRequest> auxCompositionRing_;
    mutable std::mutex auxMutex_;
    std::atomic<bool> auxLayerRequested_{ false };
    bool auxOpenAttempted_ = false;
    // --- Frame side-data ring (see CopyFrameSideDataAfter) ---
    // Written on the worker thread as frames are presented, read on the API
    // thread; sideDataMutex_ is the only synchronisation between them. Entries
    // are dropped at every queue flush (seek, decoder reopen, device recovery,
    // close), because the presentation index restarts there -- an old entry
    // would then be keyed to a frame that no longer means what it said.
    struct SideDataBlob {
        // Side-data type value, opaque here. The namespace depends on which
        // list holds the blob: the ring carries AVFrameSideDataType values,
        // streamSideData_ carries AVPacketSideDataType values. Different
        // enums that collide on raw numbers -- which is why
        // CopyFrameSideDataAfter and CopyStreamSideData are separate entry
        // points.
        std::uint32_t type;
        std::vector<std::uint8_t> bytes;
    };
    struct CachedSideDataFrame {
        std::int64_t frameIndex;
        std::vector<SideDataBlob> blobs;
    };
    std::deque<CachedSideDataFrame> sideDataRing_;
    std::size_t sideDataRingBytes_ = 0;   // blob payload only
    // --- Stream-level side data (see CopyStreamSideData) ---
    // The coded side data the container declared for the current video stream,
    // captured at stream binding on the command thread and read on the API
    // thread; sideDataMutex_ guards both. Unlike the ring above it survives
    // seeks -- the declaration happens once and a seek does not re-emit it --
    // and is cleared only when the stream is destroyed or rebound.
    std::vector<SideDataBlob> streamSideData_;
    mutable std::mutex sideDataMutex_;
    std::deque<AVPacket*> pendingVideoPackets_;
    std::size_t pendingVideoPacketBytes_;
    std::deque<AVPacket*> pendingAudioPackets_;
    std::size_t pendingAudioPacketBytes_;
    // Audio buffer target, in 100 ns units. Per file round: latched to at least
    // the duration WASAPI actually granted at renderer creation, then raised one
    // step per observed underrun batch. Rise-only within the round -- nothing
    // steps it down -- and reset to the default by DoOpen and by an audio-track
    // change. Everything that touches it runs on the session worker thread.
    static constexpr std::int64_t kAudioTargetBufferStep100ns = 300'000;
    std::int64_t audioTargetBuffer100ns_ = performance_.AudioInitial();
    std::atomic<bool> interactiveMove_{false};
    // Last-seen values of the renderer's warning counters. Any growth since the
    // previous pump buys one step of target headroom -- underruns are the hard
    // signal, timestamp jitter / discontinuities / inserted silence / dropped
    // overlap are the timeline-unsteady signals the pool must also open for.
    std::uint64_t seenAudioUnderruns_ = 0;
    std::uint64_t seenAudioJitterFrames_ = 0;
    std::uint64_t seenAudioDiscontinuities_ = 0;
    std::uint64_t seenAudioInsertedSilence_ = 0;
    std::uint64_t seenAudioDroppedOverlap_ = 0;
    struct BitRateBucket {
        std::int64_t secondIndex;
        std::uint64_t bytes;
    };
    std::deque<BitRateBucket> videoBitRateBuckets_;
    std::deque<BitRateBucket> audioBitRateBuckets_;
    std::int64_t publishedBitRateSecond_;
    bool draining_;
    bool demuxEnded_;
    bool audioDecoderDrained_;
    bool externalAudioDrained_;
    bool audioClockFinished_;
    bool staticImage_;
    // A tiled still image: several tile streams that only become the picture once
    // they are placed on the grid's canvas. Set at open from the tile grid group.
    bool tileGrid_;
    std::int32_t tileGridWidth_;
    std::int32_t tileGridHeight_;
    std::uint8_t tileGridBackground_[4];
    std::vector<AvifTileGridComposer::Tile> tileGridTiles_;
    std::vector<AvifTileGridComposer::TileSource> tileGridSources_;
    // A picture that moves: either a loop-aware image demuxer (GIF/APNG/WebP) or
    // an image sequence carried by the mov demuxer (AVIF/HEIC). Such a file is
    // not a still image, but it is still a picture: it must show its first frame
    // while stopped and it must report the animated flag.
    bool animatedImage_;
    // Last [queue] trace values, so an animation logs its depth accounting once per
    // distinct combination instead of once per pump. Worker thread only.
    mutable std::int64_t tracedQueuePipelineFrames_ = -1;
    mutable std::int64_t tracedQueueLimit_ = -1;
    // APNG and AVIF/HEIC rewind per pass; reset this policy when reusing a session.
    bool sessionWrapLoop_;
    // Consecutive failed wrap-around seeks. A seek that is refused while the
    // demuxer is still mid read-ahead must not turn a looping picture into a
    // failed one; the next pump retries, so only a persistent failure escapes.
    std::uint32_t sessionWrapFailures_;
    bool hardwareFallbackPending_;
    std::string pendingHardwareFallbackReason_;
    // Streams whose hardware route has already been refused this session, keyed by
    // codec + profile + pixel format + frame size. A handful of entries at most, so a
    // linear scan beats a hash table here.
    std::vector<std::uint64_t> unsupportedHardwareRoutes_;
    bool internalAudioFailurePending_;
    FFFResult internalAudioFailureResult_;
    std::uint32_t internalAudioDecodeErrorCount_;
    // Stable danmaku content is interned by contentId+UTF-8 hash. Position-only
    // layers then share immutable strings instead of allocating 100 wstrings at
    // every 60 Hz submission.
    std::unordered_map<std::uint64_t, std::shared_ptr<const TimedTextRenderCommand::TextContent>> timedTextContentCache_;
};
