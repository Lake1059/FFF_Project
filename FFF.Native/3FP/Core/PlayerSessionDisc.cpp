#include "pch.h"
#include "3FP/Core/PlayerSession.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <algorithm>

std::string PlayerSession::DiscStatus() const {
    std::lock_guard lock(snapshotMutex_); return discStatus_;
}
FFFResult PlayerSession::DiscNavigate(int command, int value, int y) noexcept {
    if (command < 0 || command > static_cast<int>(DiscInput::Command::Subtitle)) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    if (state != FFF3FPState::Playing && state != FFF3FPState::Ready && state != FFF3FPState::Paused)
        return FFFResult::InvalidState;
    Enqueue([this, command, value, y] {
        const auto current = state_.load();
        if (current != FFF3FPState::Playing && current != FFF3FPState::Ready && current != FFF3FPState::Paused) return;
        if (!disc_) { ReportError(FFFResult::InvalidState, "No disc is open.", "disc-navigation"); return; }
        if (command == static_cast<int>(DiscInput::Command::Subtitle)) {
            disc_->ClearSubtitle();
        }
        if (!disc_->Navigate(static_cast<DiscInput::Command>(command), value, y)) return;
        if (disc_->RestartRequired() && !ReopenDiscDemux()) return;
        PublishDisc();
    });
    return FFFResult::Success;
}
void PlayerSession::PublishDisc() {
    if (!disc_) return;
    const int selectedAudio = disc_->AudioStream(format_);
    if (selectedAudio >= 0 && selectedAudio != audioStream_ && !disc_->RestartRequired()) {
        AVCodecContext* replacement = nullptr;
        if (OpenDecoder(format_, selectedAudio, false, &replacement) == FFFResult::Success) {
            for (auto*& packet : pendingAudioPackets_) av_packet_free(&packet);
            pendingAudioPackets_.clear(); pendingAudioPacketBytes_ = 0;
            avcodec_free_context(&audioDecoder_); audioDecoder_ = replacement;
            audioStream_ = selectedAudio; snapshot_.selectedAudioStream = audioStream_;
            RebuildMediaInfo();
        }
    }
    { std::lock_guard lock(snapshotMutex_); discStatus_ = state_.load() == FFF3FPState::Failed ? "{}" : disc_->StatusJson(); }
    if (disc_->Duration() > 0) snapshot_.duration100ns = disc_->Duration();
    videoRenderer_.SetDiscAspect(disc_->Aspect());
    const auto& graphics = disc_->Graphics();
    if (graphics.sequence == discGraphicsSequence_) return;
    TimedTextRenderLayer layer;
    layer.canvasWidth = graphics.width; layer.canvasHeight = graphics.height;
    if (!graphics.pixels.empty() && graphics.width > 0 && graphics.height > 0) {
        TimedTextRenderCommand command;
        command.type = FFF3FPTimedTextCommandType::Bitmap;
        command.bitmap = graphics.pixels;
        command.bitmapWidth = graphics.width; command.bitmapHeight = graphics.height;
        command.width = static_cast<float>(graphics.width); command.height = static_cast<float>(graphics.height);
        command.bitmapStride = graphics.width * 4;
        layer.commands.push_back(std::move(command));
    }
    videoRenderer_.SetTimedTextLayer(std::move(layer), TimedTextLayerSlot::Disc);
    discGraphicsSequence_ = graphics.sequence;
}
bool PlayerSession::ReopenDiscDemux() {
    if (!disc_) return false;
    const bool playing = state_.load() == FFF3FPState::Playing;
    if (audioRenderer_) { audioRenderer_->Stop(); audioRenderer_.reset(); }
    ClearVideoQueue();
    avcodec_free_context(&videoDecoder_); avcodec_free_context(&audioDecoder_);
    disc_->CloseDemux(&format_);
    disc_->AcknowledgeRestart();
    if (!disc_->OpenDemux(&format_)) {
        Fail(FFFResult::FfmpegFailure, disc_->Error(), "disc-demux"); return false;
    }
    videoStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioStream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, videoStream_, nullptr, 0);
    if (videoStream_ >= 0) {
        auto result = decodeMode_ == FFF3FPDecodeMode::Gpu ?
            OpenHardwareVideoDecoder(format_, videoStream_, &videoDecoder_) :
            OpenDecoder(format_, videoStream_, true, &videoDecoder_);
        snapshot_.decodeMode = decodeMode_;
        if (result != FFFResult::Success) {
            result = OpenDecoder(format_, videoStream_, true, &videoDecoder_, -1, nullptr, false);
            snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
        }
        if (result != FFFResult::Success) { Fail(result, "Could not decode disc video."); return false; }
    }
    if (audioStream_ >= 0 && OpenDecoder(format_, audioStream_, false, &audioDecoder_) != FFFResult::Success)
        audioStream_ = -1;
    if (videoStream_ < 0 && audioStream_ < 0) { Fail(FFFResult::NotSupported, "No audio/video at the current disc position."); return false; }
    videoRenderer_.ConfigureHdrStream(videoStream_ >= 0 ? format_->streams[videoStream_]->codecpar : nullptr);
    if (audioStream_ >= 0) {
        std::string error;
        if (RecreateAudioRenderer(audioEndpointId_, audioExclusive_, true, error) != FFFResult::Success) {
            Fail(FFFResult::DeviceFailure, error, "disc-audio"); return false;
        }
    }
    discPositionOffset_ = disc_->Menu() ? 0 : disc_->Position();
    snapshot_.position100ns = discPositionOffset_;
    snapshot_.duration100ns = disc_->Duration();
    snapshot_.selectedVideoStream = videoStream_; snapshot_.selectedAudioStream = audioStream_;
    snapshot_.videoWidth = videoDecoder_ ? videoDecoder_->width : 0;
    snapshot_.videoHeight = videoDecoder_ ? videoDecoder_->height : 0;
    snapshot_.frameIndex = -1; snapshot_.framePts = AV_NOPTS_VALUE;
    ++snapshot_.timelineGeneration;
    framePtsIndex_.clear(); framePtsIndexBase_ = 0;
    lastQueuedVideoPts_ = AV_NOPTS_VALUE;
    nextUntimedVideoPosition100ns_ = discPositionOffset_;
    seekTarget100ns_ = seekTargetFrame_ = -1;
    lastVideoFrameDuration100ns_ = 0;
    draining_ = demuxEnded_ = audioDecoderDrained_ = externalAudioDrained_ = audioClockFinished_ = discDrained_ = false;
    ResetClock(discPositionOffset_);
    if (audioRenderer_) audioRenderer_->Reset(discPositionOffset_);
    ArmAudioUntilVideoFrame();
    if (playing) playbackPreroll_ = true;
    RebuildMediaInfo(); PublishSnapshot(); PublishDisc();
    return true;
}
bool PlayerSession::HoldDisc() {
    if (!disc_) return false;
    if (disc_->Still()) {
        PublishDisc();
        const auto redraw = videoRenderer_.Redraw();
        if (redraw != FFFResult::Success && redraw != FFFResult::InvalidState) {
            Fail(redraw, videoRenderer_.LastError(), "disc-still-redraw");
            return true;
        }
    }
    if (disc_->Still() && !videoFrameQueue_.empty()) {
        // A finite disc still is a presentation boundary. Keep the decoded
        // frame visible while the navigation VM waits; do not reopen the
        // demuxer or let the next VOBU replace it early.
        PumpVideoPresentation();
        if (videoRenderer_.HasPendingVideoPresentation()) return true;
    }
    if (!pendingAudioPackets_.empty()) {
        if (audioRenderer_ && audioRenderer_->Buffered100ns() > 1200000) { PumpVideoPresentation(); Sleep(1); return true; }
        auto* packet = pendingAudioPackets_.front(); pendingAudioPackets_.pop_front();
        pendingAudioPacketBytes_ -= static_cast<size_t>(std::max(0, packet->size));
        DecodePacket(audioDecoder_, packet, false, format_); av_packet_free(&packet); return true;
    }
    if (!pendingVideoPackets_.empty()) {
        if (VideoQueueSaturated()) { PumpVideoPresentation(); Sleep(1); return true; }
        auto* packet = pendingVideoPackets_.front(); pendingVideoPackets_.pop_front();
        pendingVideoPacketBytes_ -= static_cast<size_t>(std::max(0, packet->size));
        DecodePacket(videoDecoder_, packet, true, format_); av_packet_free(&packet); return true;
    }
    if (!draining_) {
        draining_ = true;
        if (videoDecoder_) DecodePacket(videoDecoder_, nullptr, true, format_);
        if (audioDecoder_) DecodePacket(audioDecoder_, nullptr, false, format_);
        audioDecoderDrained_ = true;
        if (audioRenderer_) audioRenderer_->Finish();
    }
    if (disc_->Still() && videoFrameQueue_.empty() && videoDecoder_) {
        // MPEG-2 still cells can hold delayed reference frames behind the first
        // drain call. Give the decoder a bounded second drain before the VM
        // timer is allowed to release the still.
        for (int attempt = 0; attempt < 4 && videoFrameQueue_.empty(); ++attempt)
            DecodePacket(videoDecoder_, nullptr, true, format_);
    }
    if (!videoFrameQueue_.empty()) { PumpVideoPresentation(); TryCompletePlaybackPreroll(); return true; }
    if (audioRenderer_ && audioRenderer_->Buffered100ns() > 0) {
        TryReleaseAudioAfterVideoPresentation(); TryCompletePlaybackPreroll(); Sleep(2); return true;
    }
    TryCompletePlaybackPreroll();
    DrainInternalAudio();
    UpdateDrainedAudioClock();
    PublishDisc();
    if (!disc_->PollHold(true)) { Sleep(10); return true; }
    if (disc_->RestartRequired()) return ReopenDiscDemux();
    if (format_->pb) { format_->pb->eof_reached = 0; format_->pb->error = 0; }
    avformat_flush(format_);
    if (videoDecoder_) avcodec_flush_buffers(videoDecoder_);
    if (audioDecoder_) avcodec_flush_buffers(audioDecoder_);
    audioDecoderDrained_ = false;
    draining_ = discDrained_ = false;
    return true;
}
