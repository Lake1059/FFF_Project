#include "pch.h"
#include "3FP/Audio/IamfAudioDecoder.h"

#include <algorithm>
#include <array>
#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

#if FFF_WITH_LIBIAMF
#include "IAMF_decoder.h"
#endif

namespace {
// libiamf consumes the bitstream in fixed-size blocks and expects the caller to
// slide unconsumed bytes forward; this matches its own reference tool
// (libiamf/code/test/tools/iamfdec/src/test_iamfdec.c). The exact size is not
// semantically important, only that it is large enough for one access unit.
constexpr std::size_t PreferredBlockSize = 960 * 6 * 2 * 16;

// Map our sound-system index onto the library enum. Kept explicit rather than a
// static_cast: the two orders are only coincidentally equal today.
#if FFF_WITH_LIBIAMF
IAMF_SoundSystem ToSoundSystem(const int index) {
    switch (index) {
        case 0: return SOUND_SYSTEM_A;
        case 1: return SOUND_SYSTEM_B;
        case 2: return SOUND_SYSTEM_C;
        case 3: return SOUND_SYSTEM_D;
        case 4: return SOUND_SYSTEM_E;
        case 5: return SOUND_SYSTEM_F;
        case 6: return SOUND_SYSTEM_G;
        case 7: return SOUND_SYSTEM_H;
        case 8: return SOUND_SYSTEM_I;
        case 9: return SOUND_SYSTEM_J;
        case 10: return SOUND_SYSTEM_EXT_712;
        case 11: return SOUND_SYSTEM_EXT_312;
        case 12: return SOUND_SYSTEM_MONO;
        case 13: return SOUND_SYSTEM_EXT_916;
        case 14: return SOUND_SYSTEM_EXT_7154;
        default: return SOUND_SYSTEM_INVALID;
    }
}
#endif

// Channel count per sound system. IAMF_StreamInfo does NOT report the output
// channel count -- only max_frame_size -- so this is the authoritative table.
// The values were measured by decoding live_7_1_4.iamf with libiamf's own
// iamfdec.exe for every `-s` value and reading the WAV header; they are not
// derived from the "Upper+Middle+Bottom" notation, which would predict 22 for
// system H where the library actually emits 24.
int ChannelsForSoundSystem(const int index) {
    switch (index) {
        case 0: return 2;
        case 1: return 6;
        case 2: return 8;
        case 3: return 10;
        case 4: return 11;
        case 5: return 12;
        case 6: return 14;
        case 7: return 24;
        case 8: return 8;
        case 9: return 12;
        case 10: return 10;
        case 11: return 6;
        case 12: return 1;
        case 13: return 16;
        case 14: return 16;
        default: return 0;
    }
}

// Sound system whose channel count best matches a wanted number of channels.
// Used to translate a channel target into the enumeration libiamf wants. Ties
// keep the smaller system so a 12-channel request does not silently become 24.
int SoundSystemForChannels(const int wanted) {
    int best = 0;
    int bestChannels = ChannelsForSoundSystem(0);
    for (int index = 1; index <= 14; ++index) {
        const auto channels = ChannelsForSoundSystem(index);
        if (channels == 0) continue;
        if (std::abs(channels - wanted) < std::abs(bestChannels - wanted)) {
            best = index;
            bestChannels = channels;
        }
    }
    return best;
}

// libiamf's channel order is NOT the order FFmpeg and Windows use for the same
// layout, so the interleaved PCM has to be permuted before it is handed on.
//
// Ground truth is the bed-order probe in
// an out-of-tree fixture set rendered from Atmos material, which muxed fixtures
// whose tracks are pairwise decorrelated (max |corr| 0.0141) and compared each
// rendered channel against the source identity. It resolves to the same swap for
// every layout that has both side and back speakers:
//
//   7.1.4 (12ch)  out[k] <- src[0,1,2,3,6,7,4,5,8,9,10,11]
//   7.1.2 (10ch)  out[k] <- src[0,1,2,3,6,7,4,5,8,9]
//   7.1   ( 8ch)  out[k] <- src[0,1,2,3,6,7,4,5]
//
// i.e. slots 4..7 hold (back L, back R, side L, side R) in libiamf's output and
// (side L, side R, back L, back R) in the consumer's, so 4<->6 and 5<->7 have to
// be exchanged. Getting this wrong silences nothing -- it puts side content on
// the back speakers and back content on the side speakers, which is invisible on
// a stereo device and obvious on a real 7.1.4 setup.
//
// Only layouts that actually carry both pairs are affected. Mono, stereo, 5.1,
// 5.1.4, HOA and the extended systems keep their order, which is what the
// untouched slots in the mappings above already show.
//
// Note the distinction that makes this easy to get wrong: the container's declared
// layer order and libiamf's decoder output are NOT the same thing. `ffprobe` on
// p_A.mp4 reports the layer as FL+FR+SL+SR+BL+BR+TFL+TFR+TBL+TBR+FC+LFE, which would
// suggest a much larger permutation, but that describes how the IAMF layer is written,
// not what the decoder hands back -- and FFmpeg itself renders that file as plain
// stereo, so its metadata is no guide to the decoded order. The mapping here is anchored
// to the decoder output (-s9 SOURCE) that this wrapper actually receives.
constexpr bool NeedsSideBackSwap(const int channels) noexcept {
    return channels == 8 || channels == 10 || channels == 12;
}

// Rearranges interleaved PCM from libiamf order into the order the declared
// layout implies. No-op for layouts without a side/back pair.
void ReorderFromIamfToChannelLayout(std::uint8_t* const interleaved,
    const std::size_t sampleCount, const int channels, const std::size_t bytesPerSample) noexcept {
    if (!NeedsSideBackSwap(channels)) return;
    const auto frameBytes = static_cast<std::size_t>(channels) * bytesPerSample;
    std::array<std::uint8_t, 64> scratch{};
    for (std::size_t sample = 0; sample < sampleCount; ++sample) {
        auto* const base = interleaved + sample * frameBytes;
        for (int pair = 0; pair < 2; ++pair) {
            auto* const a = base + static_cast<std::size_t>(4 + pair) * bytesPerSample;
            auto* const b = base + static_cast<std::size_t>(6 + pair) * bytesPerSample;
            std::memcpy(scratch.data(), a, bytesPerSample);
            std::memcpy(a, b, bytesPerSample);
            std::memcpy(b, scratch.data(), bytesPerSample);
        }
    }
}

// Layout to render when the caller expresses no preference.
//
// IAMF mix presentations are authored against a target speaker layout, and the
// fixtures here overwhelmingly target 7.1.4. Rendering that and letting the
// WASAPI resampler fold down to whatever the device has keeps full quality on a
// multichannel system and loses nothing on stereo, whereas asking libiamf for
// stereo up front would throw the spatial content away irrecoverably.
constexpr int DefaultSoundSystemIndex = 5;   // F (3+7+0) = 12 channels = 7.1.4

} // namespace

IamfAudioDecoder::IamfAudioDecoder() = default;

IamfAudioDecoder::~IamfAudioDecoder() {
    Close();
}

bool IamfAudioDecoder::Available() noexcept {
#if FFF_WITH_LIBIAMF
    return true;
#else
    return false;
#endif
}

// Channels the content declares, from libiamf's per-element detail. The bed element is the
// one whose channel count describes the sound field; a scene/HOA element reports the
// channels its objects render into, so the largest is taken and a bed+scene mix reports the
// bed. Returns 0 when libiamf offers no element detail, which callers treat as "unknown"
// rather than "silent".
//
// ⚠ Read this on **every** configure path. It was originally read only in Open(), the
// in-memory entry point, while real files go through RestartFile() -- so the value stayed 0
// for every file and the UI had nothing to show.
int IamfAudioDecoder::ContentChannelsFromElements(
    const void* elements, const std::uint32_t count) noexcept {
#if FFF_WITH_LIBIAMF
    if (elements == nullptr || count == 0) return 0;
    const auto* list = static_cast<const iamf_audio_element_info_t*>(elements);
    int channels = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (list[index].num_channels > static_cast<std::uint32_t>(channels))
            channels = static_cast<int>(list[index].num_channels);
    }
    return channels;
#else
    (void)elements;
    (void)count;
    return 0;
#endif
}

void IamfAudioDecoder::Close() noexcept {
    if (frame_ != nullptr) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
#if FFF_WITH_LIBIAMF
    if (handle_ != nullptr) {
        IAMF_decoder_close(static_cast<IAMF_DecoderHandle>(handle_));
        handle_ = nullptr;
    }
#endif
    // The streaming file handle is owned here too, and RestartFile calls Close()
    // before reopening -- leaving it open would leak a handle on every seek.
    if (file_ != nullptr && file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
    used_ = 0;
    filled_ = 0;
    fileEnd_ = false;
    endOfStream_ = false;
    configured_ = false;
}

bool IamfAudioDecoder::Open(const std::uint8_t* data, const std::size_t size,
    const int soundSystem, std::string& error) {
#if !FFF_WITH_LIBIAMF
    (void)data; (void)size; (void)soundSystem;
    error = "This build has no IAMF decoder (libiamf was not linked).";
    return false;
#else
    Close();
    if (data == nullptr || size == 0) {
        error = "The IAMF stream is empty.";
        return false;
    }
    handle_ = IAMF_decoder_open();
    if (handle_ == nullptr) {
        error = "libiamf could not create a decoder.";
        return false;
    }
    auto* decoder = static_cast<IAMF_DecoderHandle>(handle_);

    // Output layout is the *rendering target*, and libiamf renders nothing until
    // one is chosen -- without this it fails with "Failed to activate renderer".
    // -1 means "pick the best the content offers", i.e. the layout the mix
    // presentation itself declares. Downmixing to the actual device happens later
    // in the WASAPI resampler, which already handles any channel count, so asking
    // for the full layout loses nothing on a stereo device and gains everything
    // on a multichannel one.
    int requested = soundSystem;
    if (requested < 0) requested = DefaultSoundSystemIndex;
    soundSystem_ = requested;
    {
        const auto layoutResult = IAMF_decoder_output_layout_set_sound_system(
            decoder, ToSoundSystem(requested));
        if (layoutResult != IAMF_OK) {
            error = "libiamf rejected the requested output layout.";
            Close();
            return false;
        }
    }

    // Configuration takes the descriptor OBUs. IAMF_ERR_BUFFER_TOO_SMALL means
    // "more descriptor bytes needed", which is a normal intermediate state, but
    // rsize == 0 at the same time means no progress and is terminal.
    std::size_t used = 0;
    bool configured = false;
    while (used < size) {
        const auto chunk = std::min<std::size_t>(PreferredBlockSize, size - used);
        std::uint32_t consumed = 0;
        const auto result = IAMF_decoder_configure(decoder, data + used,
            static_cast<std::uint32_t>(chunk), &consumed);
        if (result == IAMF_OK) {
            configured = true;
            used += consumed;
            break;
        }
        if (result != IAMF_ERR_BUFFER_TOO_SMALL || consumed == 0) break;
        used += consumed;
    }
    if (!configured) {
        error = "libiamf could not configure from this IAMF stream "
                "(unsupported carriage or layout).";
        Close();
        return false;
    }
    configured_ = true;

    // libiamf reports the frame geometry; the channel count comes from the
    // layout we asked for. Sizing the buffer from the library's max_frame_size
    // is what keeps a 24-channel target from overrunning a stereo-sized buffer.
    if (const auto* info = IAMF_decoder_get_stream_info(decoder); info != nullptr) {
        sampleRate_ = static_cast<int>(info->iamf_stream_info.sampling_rate);
        const auto maxFrame = static_cast<std::size_t>(info->max_frame_size);
        if (soundSystem >= 0) {
            channels_ = ChannelsForSoundSystem(soundSystem);
        } else {
            // No layout requested: libiamf defaults to sound system A (stereo).
            channels_ = 2;
            soundSystem_ = 0;
        }
        // The layout the *content* declares, as opposed to the render target we asked
        // for. These differ whenever the content is smaller: measured with libiamf's own
        // iamfdec, a 7.1 file rendered at -s5 (7.1.4) still comes out 12 channels, with
        // the four speakers the content does not have filled with **digital silence**
        // (peak -inf on exactly those channels; 7.1.2 leaves 2 silent, 7.1.4 leaves
        // none). So requesting the larger layout is safe -- nothing is fabricated -- but
        // reporting it as if it were the content is misleading: a 7.1 track would show
        // as "7.1.4", and on real hardware four speakers would simply stay quiet.
        //
        // audio_elements carries the per-element truth; see ContentChannelsFromStreamInfo.
        contentChannels_ = ContentChannelsFromElements(info->iamf_stream_info.audio_elements,
            info->iamf_stream_info.audio_element_count);
        if (channels_ <= 0 || sampleRate_ <= 0 || maxFrame == 0) {
            error = "libiamf reported an unusable output geometry.";
            Close();
            return false;
        }
        // 16-bit keeps the handoff small and is what the WASAPI resampler wants
        // anyway; asking for more would only add a conversion.
        bitDepth_ = 16;
        pcm_.assign(maxFrame * static_cast<std::size_t>(channels_) * 2u, 0);
    } else {
        error = "libiamf could not report stream information.";
        Close();
        return false;
    }

    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
        error = "Could not allocate an audio frame.";
        Close();
        return false;
    }
    return true;
#endif
}

bool IamfAudioDecoder::Receive(const bool draining,
    const std::function<void(AVFrame*)>& sink, std::string& error) {
#if !FFF_WITH_LIBIAMF
    (void)draining; (void)sink; (void)error;
    return false;
#else
    if (!configured_ || handle_ == nullptr) {
        error = "The IAMF decoder is not configured.";
        return false;
    }
    // A null payload drains the delay line: libiamf synthesises its tail frames
    // that way, so this is the flush path.
    std::uint32_t consumed = 0;
    const auto produced = IAMF_decoder_decode(static_cast<IAMF_DecoderHandle>(handle_),
        nullptr, 0, &consumed, pcm_.data());
    if (produced < 0) {
        error = "libiamf failed while draining the decoder.";
        return false;
    }
    (void)draining;
    return Deliver(produced, AV_NOPTS_VALUE, sink, error);
#endif
}

bool IamfAudioDecoder::Deliver(const int producedSamples, const std::int64_t pts100ns,
    const std::function<void(AVFrame*)>& sink, std::string& error) {
    if (producedSamples <= 0 || sink == nullptr) return true;
    const auto bytesPerSample = static_cast<std::size_t>(bitDepth_ / 8);
    const auto rowBytes = static_cast<std::size_t>(producedSamples) *
        static_cast<std::size_t>(channels_) * bytesPerSample;
    if (rowBytes > pcm_.size()) {
        // Defensive: the library must not exceed the max_frame_size it declared.
        error = "libiamf produced more samples than its declared maximum.";
        return false;
    }
    // Wrap interleaved S16 PCM in an AVFrame so the existing renderer path keeps
    // working unchanged (peak metering, seek anchoring, WASAPI downmix).
    av_frame_unref(frame_);
    frame_->format = AV_SAMPLE_FMT_S16;
    frame_->sample_rate = sampleRate_;
    frame_->nb_samples = producedSamples;
    frame_->pts = pts100ns;
    av_channel_layout_default(&frame_->ch_layout, channels_);
    if (av_frame_get_buffer(frame_, 0) < 0) {
        error = "Could not allocate PCM storage for the decoded IAMF frame.";
        return false;
    }
    std::memcpy(frame_->data[0], pcm_.data(), rowBytes);
    // libiamf emits its own channel order; the layout just declared is the
    // consumer's. Exchange the side/back pairs so the labels are truthful.
    ReorderFromIamfToChannelLayout(static_cast<std::uint8_t*>(frame_->data[0]),
        static_cast<std::size_t>(producedSamples), channels_, bytesPerSample);
    sink(frame_);
    av_frame_unref(frame_);
    return true;
}

bool IamfAudioDecoder::Decode(const std::uint8_t* data, const std::size_t size,
    const std::int64_t position100ns, const std::function<void(AVFrame*)>& sink,
    std::string& error) {
#if !FFF_WITH_LIBIAMF
    (void)data; (void)size; (void)position100ns; (void)sink; (void)error;
    return false;
#else
    if (!configured_ || handle_ == nullptr) {
        error = "The IAMF decoder is not configured.";
        return false;
    }
    std::uint32_t consumed = 0;
    const auto produced = IAMF_decoder_decode(static_cast<IAMF_DecoderHandle>(handle_),
        data, static_cast<std::int32_t>(size), &consumed, pcm_.data());
    if (produced < 0) {
        error = "libiamf failed to decode an IAMF access unit.";
        return false;
    }
    return Deliver(produced, position100ns, sink, error);
#endif
}

bool IamfAudioDecoder::Flush(const std::function<void(AVFrame*)>& sink,
    std::string& error) {
    return Receive(true, sink, error);
}

// ── 流式文件解码（IAMF 专用）──────────────────────────────────────────────
//
// 为什么不能走"FFmpeg 出 packet -> 我们喂 decode"：ffmpeg 的 iamf demuxer 把
// 一个音频元素拆成 N 个子流，**每个子流一个 packet**；而 libiamf 的 decode 期望
// **一个完整访问单元**（所有子流的 OBU 在一起）。喂子流 packet 会让它报
// "Substream ID N already has an audio frame OBU" 并拒绝。
// 所以这里自己按块读原始 OBU 流来驱动解码器——这也正是参考工具的做法。
//
// 增量而非一次性：语料里最大件 328 MB，全量解码既费内存也无法 seek。

bool IamfAudioDecoder::OpenFile(const std::string& pathUtf8, std::string& error) {
#if !FFF_WITH_LIBIAMF
    (void)pathUtf8;
    error = "This build has no IAMF decoder (libiamf was not linked).";
    return false;
#else
    filePath_ = pathUtf8;
    // A fresh open anchors at the start of the file.
    seekBase100ns_ = 0;
    return RestartFile(error);
#endif
}

bool IamfAudioDecoder::RestartFileAt(const std::int64_t position100ns, std::string& error) {
    seekBase100ns_ = std::max<std::int64_t>(0, position100ns);
    return RestartFile(error);
}

bool IamfAudioDecoder::RestartFile(std::string& error) {
#if !FFF_WITH_LIBIAMF
    error = "This build has no IAMF decoder (libiamf was not linked).";
    return false;
#else
    // 完全重建解码器：libiamf 没有随机访问入口，seek 只能重新开始。
    Close();
    if (filePath_.empty()) { error = "No IAMF file is open."; return false; }
    const auto wide = std::wstring(filePath_.begin(), filePath_.end());
    file_ = CreateFileW(wide.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        error = "Could not open the IAMF file for reading.";
        return false;
    }
    handle_ = IAMF_decoder_open();
    if (handle_ == nullptr) {
        CloseHandle(file_); file_ = INVALID_HANDLE_VALUE;
        error = "libiamf could not create a decoder.";
        return false;
    }
    const auto requested = soundSystem_ >= 0 ? soundSystem_ : DefaultSoundSystemIndex;
    if (IAMF_decoder_output_layout_set_sound_system(
            static_cast<IAMF_DecoderHandle>(handle_), ToSoundSystem(requested)) != IAMF_OK) {
        error = "libiamf rejected the requested output layout.";
        CloseHandle(file_); file_ = INVALID_HANDLE_VALUE;
        IAMF_decoder_close(static_cast<IAMF_DecoderHandle>(handle_));
        handle_ = nullptr;
        return false;
    }
    soundSystem_ = requested;
    channels_ = ChannelsForSoundSystem(requested);
    block_.assign(PreferredBlockSize * 2, 0);
    used_ = 0;
    filled_ = 0;
    fileEnd_ = false;
    configured_ = false;
    endOfStream_ = false;
    // libiamf has no random access, so a "seek" is a restart from byte 0 and it numbers
    // the frames it produces from zero again. Reporting those raw would be wrong twice
    // over: the timestamps would name the wrong point in the file, and PlayerSession's
    // timestamp filter (which drops audio before seekTarget100ns_) would discard every
    // frame after a forward seek, leaving the track silent until it caught up.
    //
    // seekBase100ns_ therefore holds the file position that the restarted stream's zero
    // corresponds to. It is 0 for a normal open; RestartFileAt sets it to the seek
    // target, so absolute position = seekBase100ns_ + decoded-so-far. That keeps the
    // labels truthful and makes the filter drop exactly the pre-target part.
    nextPosition100ns_ = seekBase100ns_;
    stalledRounds_ = 0;
    sampleRate_ = 0;
    bitDepth_ = 16;
    pcm_.clear();
    if (frame_ == nullptr) frame_ = av_frame_alloc();
    if (frame_ == nullptr) { error = "Could not allocate an audio frame."; return false; }
    return true;
#endif
}

bool IamfAudioDecoder::PumpFile(const std::size_t maxFrames,
    std::vector<std::pair<AVFrame*, std::int64_t>>& out, bool& atEnd,
    std::string& error) {
#if !FFF_WITH_LIBIAMF
    (void)maxFrames; (void)out; (void)atEnd;
    error = "This build has no IAMF decoder (libiamf was not linked).";
    return false;
#else
    atEnd = false;
    if (handle_ == nullptr || file_ == INVALID_HANDLE_VALUE) {
        error = "The IAMF file decoder is not open.";
        return false;
    }
    auto* decoder = static_cast<IAMF_DecoderHandle>(handle_);

    while (out.size() < maxFrames) {
        // Two counters, deliberately distinct: `filled_` is how many bytes of the
        // buffer hold data, `used_` is how far into them libiamf has consumed.
        // Conflating them (as an earlier revision did) makes the slice handed to
        // decode() permanently empty, so it answers "0 produced, 0 consumed"
        // forever and the loop spins -- measured as a hung player.
        //
        // Compact first. Without moving the unconsumed tail to the front, a full
        // buffer holding a partial tail can never be refilled and the pump stalls
        // in exactly the same way (measured: available stayed at 24404 forever).
        if (used_ == filled_) { used_ = 0; filled_ = 0; }
        else if (used_ > 0) {
            std::memmove(block_.data(), block_.data() + used_, filled_ - used_);
            filled_ -= used_;
            used_ = 0;
        }
        if (!fileEnd_ && filled_ < block_.size()) {
            DWORD read = 0;
            const auto want = static_cast<DWORD>(block_.size() - filled_);
            if (ReadFile(file_, block_.data() + filled_, want, &read, nullptr) && read > 0)
                filled_ += read;
            else
                fileEnd_ = true;
        }
        const std::size_t available = filled_ - used_;

        // ⚠ 终止守卫：文件读完且缓冲里再无可用数据时必须退出。
        //   少了它，decode/configure 会持续返回"消费 0 字节"，循环永不收敛
        //   —— 实测表现为探针挂死（第一版就是这么挂的）。
        if (fileEnd_ && available == 0) {
            while (out.size() < maxFrames) {
                std::uint32_t drainConsumed = 0;
                const auto tail = IAMF_decoder_decode(decoder, nullptr, 0,
                    &drainConsumed, pcm_.data());
                if (tail <= 0) { endOfStream_ = true; break; }
                if (!AppendFrame(tail, nextPosition100ns_, out, error)) return false;
                if (sampleRate_ > 0)
                    nextPosition100ns_ += av_rescale(tail, 10'000'000, sampleRate_);
            }
            atEnd = endOfStream_;
            return true;
        }

        if (!configured_) {
            std::uint32_t consumed = 0;
            const auto result = IAMF_decoder_configure(decoder, block_.data(),
                static_cast<std::uint32_t>(available), &consumed);
            if (result == IAMF_OK) {
                configured_ = true;
                std::size_t maxFrame = 0;
                if (const auto* cfgInfo = IAMF_decoder_get_stream_info(decoder); cfgInfo != nullptr) {
                    sampleRate_ = static_cast<int>(cfgInfo->iamf_stream_info.sampling_rate);
                    maxFrame = static_cast<std::size_t>(cfgInfo->max_frame_size);
                    if (channels_ > 0 && maxFrame > 0)
                        pcm_.assign(maxFrame * static_cast<std::size_t>(channels_) * 2u, 0);
                    // Content layout, not the render target. This read used to live only in
                    // Open(), which is the in-memory path -- file playback goes through
                    // here, so the field stayed 0 for every real file. Read it on both.
                    contentChannels_ = ContentChannelsFromElements(cfgInfo->iamf_stream_info.audio_elements,
                        cfgInfo->iamf_stream_info.audio_element_count);
                }
                if (pcm_.empty()) {
                    error = "libiamf reported an unusable output geometry.";
                    return false;
                }
            } else if (result != IAMF_ERR_BUFFER_TOO_SMALL || consumed == 0) {
                error = "libiamf could not configure from this IAMF stream.";
                return false;
            }
            Consume(consumed);
            // No progress and no more file to read would spin forever; treat it
            // as a malformed stream rather than hanging the player.
            if (consumed == 0 && fileEnd_) {
                error = "libiamf could not configure from this IAMF stream.";
                return false;
            }
            continue;
        }

        // 解码阶段：一次调用会吃掉若干个完整访问单元
        std::uint32_t consumed = 0;
        const auto produced = IAMF_decoder_decode(decoder,
            available > 0 ? block_.data() + used_ : nullptr,
            available > 0 ? static_cast<std::int32_t>(available) : 0,
            &consumed, pcm_.data());
        // Stall guard. libiamf can accept a stream at configure time and then never
        // produce anything from it -- measured on conv_bedonly.iamf, where configure
        // succeeds after six retries and decode then answers "0 produced, 0
        // consumed" indefinitely. Without this the player would spin here forever;
        // reporting an error instead hands the track to the FFmpeg fallback.
        if (produced <= 0 && consumed == 0) {
            if (++stalledRounds_ > 8) {
                error = "libiamf stalled while decoding this IAMF stream "
                        "(unsupported carriage or layout).";
                return false;
            }
        } else {
            stalledRounds_ = 0;
        }
        if (produced > 0) {
            if (!AppendFrame(produced, nextPosition100ns_, out, error)) return false;
            if (sampleRate_ > 0)
                nextPosition100ns_ += av_rescale(produced, 10'000'000, sampleRate_);
        } else if (produced < 0 && !fileEnd_) {
            error = "libiamf failed to decode an IAMF access unit.";
            return false;
        }
        Consume(consumed);

        // Nothing left to consume from a fully-read file means the guard at the top
        // of the loop takes over and drains the delay line.
    }
    atEnd = endOfStream_;
    return true;
#endif
}

void IamfAudioDecoder::Consume(const std::uint32_t bytes) {
    if (bytes == 0) return;
    // Advance the read cursor only; the bytes stay put. The loop sizes the next
    // slice as filled_ - used_, and once the cursor reaches the end of what has
    // been read the buffer is reclaimed so the next read starts clean.
    used_ += (std::min)(static_cast<std::size_t>(bytes), filled_ - used_);
    if (used_ >= filled_) { used_ = 0; filled_ = 0; }
}

bool IamfAudioDecoder::AppendFrame(const int samples, const std::int64_t position100ns,
    std::vector<std::pair<AVFrame*, std::int64_t>>& out, std::string& error) {
    const auto bytesPerSample = static_cast<std::size_t>(bitDepth_ / 8);
    const auto rowBytes = static_cast<std::size_t>(samples) *
        static_cast<std::size_t>(channels_) * bytesPerSample;
    if (rowBytes > pcm_.size()) {
        error = "libiamf produced more samples than its declared maximum.";
        return false;
    }
    auto* copy = av_frame_alloc();
    if (copy == nullptr) { error = "Could not allocate an audio frame."; return false; }
    copy->format = AV_SAMPLE_FMT_S16;
    copy->sample_rate = sampleRate_;
    copy->nb_samples = samples;
    copy->pts = position100ns;
    av_channel_layout_default(&copy->ch_layout, channels_);
    if (av_frame_get_buffer(copy, 0) < 0) {
        av_frame_free(&copy);
        error = "Could not allocate PCM storage for a decoded IAMF frame.";
        return false;
    }
    std::memcpy(copy->data[0], pcm_.data(), rowBytes);
    // Same side/back exchange as the streaming path: this second handoff exists
    // for the file/batch reader and must not diverge from it.
    ReorderFromIamfToChannelLayout(static_cast<std::uint8_t*>(copy->data[0]),
        static_cast<std::size_t>(samples), channels_, bytesPerSample);
    out.emplace_back(copy, position100ns);
    return true;
}
