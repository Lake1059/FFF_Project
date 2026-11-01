#include "pch.h"
#include "3FP/Core/AvifIlocShim.h"
#include "3FP/Core/MediaText.h"
#include "3FP/Core/PlayerSession.h"
#include "3FP/Core/DecodeDeviceSelection.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavcodec/codec_desc.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/spherical.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
// IAMF stream groups carry the real channel layout; the audio element and mix
// presentation types are only declared here.
#include <libavutil/iamf.h>
}

#include <filesystem>
#include <iomanip>
#include <limits>
#include <cmath>
#include <chrono>

namespace {
using PlayerMediaText::FfmpegError;
using PlayerMediaText::AppendDictionaryJson;
using PlayerMediaText::EscapeJson;
using PlayerMediaText::CodecTagName;
constexpr std::int64_t TicksPerSecond = 10'000'000;
constexpr std::size_t MaxQueuedVideoFrames = 8;
constexpr std::size_t MinimumQueuedVideoFrames = 3;
constexpr std::size_t MinimumMemoryBoundVideoFrames = 2;
constexpr std::size_t DecodedVideoQueueBudgetBytes = 128 * 1024 * 1024;
// The software decoding look-ahead pool, staged like the thread ladder. A rung is
// only a wider admission bound -- it never pre-allocates -- so the floor is pure
// declared headroom: 128 MB already holds the four-frame look-ahead for anything up
// to 4K 10-bit 4:2:0, and small-frame content saves the declaration outright. Rung 3
// reproduces the fixed pair the upstream patch shipped (512 MB / 8 frames); the steps
// above it keep the same cadence for streams that keep starving (768 / 1024 MB).
constexpr std::size_t SoftwarePoolBudgets[] = { 128ULL * 1024 * 1024,
                                                256ULL * 1024 * 1024,
                                                384ULL * 1024 * 1024,
                                                512ULL * 1024 * 1024,
                                                768ULL * 1024 * 1024,
                                                1024ULL * 1024 * 1024 };
constexpr std::size_t SoftwarePoolFrameCaps[] = { 4, 4, 6, 8, 12, 16 };
// The pool's opening rung must fit at least this many frames: the saturation gate
// closes one frame below the queue limit, so a two-frame queue gates the demux feed
// almost continuously while the stream is still filling its pipeline (see the
// opening-rung block in OpenDecoder).
constexpr std::size_t MinimumPoolLookAheadFrames = 4;
constexpr std::size_t SoftwarePoolRungCount =
    sizeof(SoftwarePoolBudgets) / sizeof(SoftwarePoolBudgets[0]);
// Queue depth a pool rung actually admits for a frame of this size: the budget
// divided by the frame, clamped to the rung's frame cap. The in-flight subtraction
// VideoQueueLimit applies on top is constant across rungs for one media, so this
// relative depth is what the climb gate compares.
std::size_t SoftwarePoolRungDepth(std::size_t rung,
    std::size_t bytesPerFrame, const PlaybackPerformancePolicy& policy) noexcept {
    const auto cap = policy.VideoFrames(SoftwarePoolFrameCaps[rung]);
    if (bytesPerFrame == 0) return cap;
    const auto byBudget = policy.VideoBudget(SoftwarePoolBudgets[rung]) / bytesPerFrame;
    return byBudget < 2 ? std::size_t{2} : (std::min)(byBudget, cap);
}
// The pool climbs only when the pool itself starved the pipeline. A window that
// misses the source rate is equally produced by a decode-limited or a
// presentation-limited stream, and a deeper cushion helps neither of those (the
// thread ladder and the placement path already cover them). The verdict below
// separates the three from the evidence the window already collects, and only a
// pool-starved stream earns the next rung. Shares are of pump ticks in the window.
constexpr double PoolBoundRatioFloor = 0.80;      // ticks that found the queue at its admission bound
constexpr double PoolStarvedRatioCeiling = 0.20;  // ticks with the queue below its minimum
constexpr double PoolUploadSaturateShare = 0.80;  // upload cost / frame period that marks presentation saturation
constexpr double PoolUploadHeadroomShare = 0.50;  // below this the upload path still has room to spare
// Frame-delay tiers for libdav1d, keyed on the same per-frame byte estimate that
// sizes the decode pool (no pixel-format enumeration): dav1d's automatic
// ceil(sqrt(workers)) frame contexts under-serve any large frame, so at or above
// HeavyDecoderFrameBytes the delay follows the worker count; at or above
// CappedDecoderFrameBytes it is bounded so the resident pipeline stays finite on
// the largest frames (an 8K context is ~100 MB). See the dav1d block in OpenDecoder.
constexpr std::size_t HeavyDecoderFrameBytes = 32 * 1024 * 1024;
constexpr std::size_t CappedDecoderFrameBytes = 64 * 1024 * 1024;
constexpr int MaximumDecoderFrameDelay = 16;
// Adaptive presentation pacing. Discarding late pictures keeps A/V in sync but
// judders; presenting every picture at a slower cadence is smooth, and on a
// variable-refresh panel the panel simply follows the slower cadence (G-SYNC /
// FreeSync) instead of dropping the surplus. The trade is bounded: a picture may
// fall behind the audio clock by at most the drift cap, past which the classic
// discard behaviour resumes and burns the lag back down. Only the "pipeline fell
// behind" bucket engages -- a frame the panel itself cannot scan out is not
// something a slower cadence can fix.
constexpr std::uint32_t PaceEngageLateFrames = 12;      // consecutive pipeline-late pictures
constexpr std::uint32_t PaceOverDeliverFrames = 16;     // consecutive late pops that found a newer picture queued
constexpr std::int64_t PaceDriftCap100ns = 2'500'000;   // 250 ms of A/V lag bounds the smooth trade
constexpr double PaceMinimumHz = 10.0;                  // never hold a picture longer than this
constexpr double PaceDeficitFraction = 0.95;            // only a measured deficit is worth pacing
constexpr std::int64_t PaceEstimateMinSpan100ns = 1'000'000; // 100 ms of streak for a rate estimate
constexpr std::uint32_t PaceWindowFrames = 32;              // rolling window for oscillating deficits
constexpr double PaceWindowLateShare = 0.40;                // late share inside it that counts as a deficit
constexpr std::uint32_t MaximumSoftwareDecoderThreads = 8;
// Thread budget for the software decoder. Its workers share the machine with the
// threads the player itself runs -- the worker (demux, decode drive, present pacing),
// the audio renderer and the video present thread -- plus one core of scheduler
// headroom; the decoder may never claim so much that those push the total to the CPU
// count. See DecoderThreadBudget.
constexpr std::uint32_t ReservedPlaybackThreads = 3;     // worker + audio + present
constexpr std::uint32_t SchedulerHeadroomThreads = 1;    // one core left for the OS
// A hardware decoder is driven from the calling thread and never runs a worker pool,
// so its decode part is exactly one thread.
constexpr std::uint32_t HardwareDecoderThreads = 1;
// Buffered100ns includes both application PCM and samples already submitted to
// WASAPI.  Keep the complete audible queue short so seek, stream/volume changes
// and the information overlay reflect a low-latency local playback pipeline.
// The target itself (audioTargetBuffer100ns_) is a per-round adaptive member --
// see PlayerSession.h; this file deliberately keeps no copy of the default.
constexpr std::size_t MaximumIndexedVideoFrames = 32'768;
constexpr std::size_t MaximumPendingVideoPackets = 64;
constexpr std::size_t MaximumPendingVideoPacketBytes = 16 * 1024 * 1024;
constexpr std::size_t MaximumPendingAudioPackets = 512;
constexpr std::size_t MaximumPendingAudioPacketBytes = 8 * 1024 * 1024;
// Frame side-data ring (CopyFrameSideDataAfter). Sized from measurement, not
// guesswork -- see 3fp/tools/sidedata_size_probe.cpp, compiled against the
// bundled FFmpeg headers: an AV_FRAME_DATA_DOVI_METADATA blob is a
// self-relative structure of 48 B of offsets + 20 B external-metadata header + 5144 B data
// mapping + 196 B colour metadata + 76 B per display-management extension
// (~5.5-7.5 KB in practice), AV_FRAME_DATA_DYNAMIC_HDR_PLUS is 11304 B,
// AV_FRAME_DATA_DYNAMIC_HDR_VIVID 1372 B, AV_FRAME_DATA_FILM_GRAIN_PARAMS
// 10848 B. Everything a colour analyser can use fits under 16 KiB; ICC
// profiles and motion vectors are far larger and are no use here, so they are
// skipped rather than inflating the budget.
constexpr std::size_t MaximumCachedSideDataBlobBytes = 16 * 1024;
constexpr std::size_t MaximumSideDataRingFrames = 120;        // ~2 s at 60 fps
constexpr std::size_t MaximumSideDataRingBytes = 1024 * 1024; // hard stop
// Optional auxiliary video layer. The frame queue only needs to cover decoder
// reorder plus PTS jitter between the two tracks; the request ring mirrors the
// side-data ring's horizon so a host polling at the same rate cannot outrun it.
constexpr std::size_t MaximumAuxiliaryQueuedFrames = 12;
constexpr std::size_t MaximumAuxiliaryCompositionRequests = 256;
// Stream-level side-data cache (CopyStreamSideData). A container configuration
// record is a one-shot, stream-level constant, so the bounds are much smaller
// than the ring's: there is nothing per-frame to accumulate, just the handful
// of records a demuxer attaches to codecpar at binding time. The per-blob
// bound reuses MaximumCachedSideDataBlobBytes: the payloads on this path are
// of the same kind, and anything larger is of no use to a colour analyser.
constexpr std::size_t MaximumStreamSideDataBlobs = 32;
constexpr std::size_t MaximumStreamSideDataBytes = 64 * 1024;

// Monotonic tick in 100 ns units, for the decoder thread ladder's timing. Uses
// steady_clock rather than av_gettime_relative because PlayerSession does not include
// libavutil/time.h, and a monotonic source is what the ladder wants anyway.
std::int64_t CurrentSteadyTicks() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 100;
}

std::size_t EstimateDecodedFrameBytes(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0) return 0;
    auto format = static_cast<AVPixelFormat>(frame->format);
    if (frame->hw_frames_ctx != nullptr) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        if (frames != nullptr) format = frames->sw_format;
    }
    const auto size = av_image_get_buffer_size(format, frame->width, frame->height, 1);
    if (size > 0) return static_cast<std::size_t>(size);
    const auto pixels = static_cast<std::uint64_t>(frame->width) * frame->height;
    return static_cast<std::size_t>(std::min<std::uint64_t>(
        pixels * 4u, std::numeric_limits<std::size_t>::max()));
}

// Single-DLL A/B gate for the animated queue accounting below: setting
// FFF_ANIM_QUEUE_RESERVE=0 restores the old budget split without a rebuild, so a
// timing batch can interleave the two behaviours inside one binary.
bool AnimQueueReserveEnabled() noexcept {
    static const auto enabled = [] {
        wchar_t value[8] = {};
        if (GetEnvironmentVariableW(L"FFF_ANIM_QUEUE_RESERVE", value,
            static_cast<DWORD>(sizeof(value) / sizeof(value[0]))) == 0) return true;
        return value[0] != L'0';
    }();
    return enabled;
}

// `pipelineFrames` is how many decoded pictures the decoder itself is holding
// before it can hand one out. A frame-threaded decoder keeps up to thread_count
// of them in flight, and those are charged to the same budget as the queue, so
// the queue may only be sized with what is left over. Passing 0 reproduces the
// older accounting, which counted the whole 128 MB to the queue and therefore
// under-counted the real peak by exactly the depth of the pipeline.
std::size_t VideoFrameQueueLimit(const std::deque<AVFrame*>& queue,
    const std::size_t pipelineFrames, const bool softwareVideo,
    const std::size_t softwarePoolBudgetBytes,
    const std::size_t softwarePoolFrameCap,
    const PlaybackPerformancePolicy& policy) noexcept {
    if (queue.empty()) return policy.VideoFrames(MaxQueuedVideoFrames);
    const auto bytesPerFrame = EstimateDecodedFrameBytes(queue.back());
    if (bytesPerFrame == 0) return policy.VideoFrames(MaxQueuedVideoFrames);
    // Software decoding needs look-ahead while the session feeds audio and uploads; its
    // budget and frame cap are the pool ladder's current rung (SoftwarePoolBudgets).
    // The hardware and animated-image paths keep the fixed 128 MB / 8 frames split.
    const auto software = softwareVideo && queue.back()->hw_frames_ctx == nullptr;
    const auto budget = policy.VideoBudget(software ? softwarePoolBudgetBytes : DecodedVideoQueueBudgetBytes);
    const auto cap = policy.VideoFrames(software ? softwarePoolFrameCap : MaxQueuedVideoFrames);
    const auto memoryBound = static_cast<std::size_t>(budget / bytesPerFrame);
    const auto queueBound = memoryBound > pipelineFrames ? memoryBound - pipelineFrames : 0;
    return std::clamp(queueBound, MinimumMemoryBoundVideoFrames, cap);
}

// Classification comes from the STREAM declaration (container tags + coded side
// data, re-resolved on display/target changes but never per frame) -- the info
// surfaces read file data, so a frame without HDR10+ SEI must not pull the
// reported caliber back while the next frame pushes it forward again. Only the
// ST 2094-40 diagnostics blocks stay per-frame: they are deliberately live
// readouts of the metadata currently on screen.
void ApplyHdrState(FFF3FPSnapshot& snapshot, const HdrFrameState& stream,
    const HdrFrameState& frame) noexcept {
    const auto toUnsigned = [](const float value, const float scale = 1.0f) {
        if (!std::isfinite(value) || value <= 0.0f) return 0u;
        return static_cast<std::uint32_t>(std::lround(std::min(
            static_cast<double>(value) * scale,
            static_cast<double>(std::numeric_limits<std::uint32_t>::max()))));
    };
    snapshot.hdrFormat = stream.format;
    snapshot.compatibleHdrFormats = stream.compatibility;
    snapshot.hdrProcessingPath = stream.processingPath;
    snapshot.hdrDpProfile = 0;  // deprecated: no longer classified
    snapshot.hdrDpLevel = 0;  // deprecated: no longer classified
    snapshot.hasHDRdpRpu = 0;  // deprecated: no longer classified
    snapshot.dynamicHdrMetadataActive = stream.dynamicMetadata ? 1u : 0u;
    snapshot.hdrFallbackActive = stream.fallback ? 1u : 0u;
    snapshot.displayMinLuminanceMilliNits = toUnsigned(stream.display.minimumNits, 1000.0f);
    snapshot.displayPeakNits = toUnsigned(stream.display.maximumNits);
    snapshot.displayFullFramePeakNits = toUnsigned(stream.display.maximumFullFrameNits);
    snapshot.effectiveTargetPeakNits = toUnsigned(stream.targetPeakNits);
    snapshot.isHdrSource = stream.format == FFF3FPHdrFormat::Sdr ? 0u : 1u;
    snapshot.dynamicMetadataWindows = frame.dynamic.windowCount;
    snapshot.dynamicMetadataDegrade =
        static_cast<std::uint32_t>(frame.dynamic.degrade);
    snapshot.dynamicMetadataSerial = frame.dynamic.serial;
    snapshot.dynamicMetadataHeldFrames = frame.dynamic.heldFrames;
    snapshot.dynamicMetadataTargetedNits =
        toUnsigned(frame.dynamic.targetedDisplayNits);
}

bool ReadLeb128(const std::uint8_t* data, const std::size_t size,
    std::size_t& offset, std::uint64_t& value) noexcept {
    value = 0;
    for (unsigned shift = 0; shift < 56 && offset < size; shift += 7) {
        const auto current = data[offset++];
        value |= static_cast<std::uint64_t>(current & 0x7f) << shift;
        if ((current & 0x80) == 0) return true;
    }
    return false;
}

void FilterAv1HardwareTimecodeMetadata(const AVCodecContext* decoder,
    AVPacket* packet) noexcept {
    if (decoder == nullptr || packet == nullptr || packet->data == nullptr || packet->size <= 0 ||
        decoder->codec_id != AV_CODEC_ID_AV1 || decoder->hw_device_ctx == nullptr) return;

    auto* source = packet->data;
    const auto sourceSize = static_cast<std::size_t>(packet->size);
    const auto readObu = [&source, sourceSize](std::size_t& offset, std::size_t& obuStart,
        std::size_t& obuEnd, bool& remove) noexcept {
        obuStart = offset;
        const auto header = source[offset++];
        if ((header & 0x81) != 0) return false;
        const auto type = static_cast<unsigned>((header >> 3) & 0x0f);
        const auto hasExtension = (header & 0x04) != 0;
        const auto hasSize = (header & 0x02) != 0;
        if (hasExtension) {
            if (offset >= sourceSize) return false;
            ++offset;
        }
        if (!hasSize) return false;
        std::uint64_t payloadSize = 0;
        if (!ReadLeb128(source, sourceSize, offset, payloadSize) ||
            payloadSize > sourceSize - offset) return false;
        const auto payloadStart = offset;
        obuEnd = offset + static_cast<std::size_t>(payloadSize);
        remove = false;
        if (type == 5 && payloadSize > 0) {
            std::size_t metadataOffset = payloadStart;
            std::uint64_t metadataType = 0;
            // NVIDIA's AV1 encoder can emit metadata_type=5 timecode OBUs that
            // libdav1d tolerates but FFmpeg's hardware parsers reject. Timecode
            // is not needed for presentation; retain every other metadata type,
            // including HDR CLL/MDCV and ITU-T T.35 dynamic metadata.
            remove = ReadLeb128(source, obuEnd, metadataOffset, metadataType) && metadataType == 5;
        }
        offset = obuEnd;
        return true;
    };

    std::size_t read = 0;
    std::size_t filteredSize = 0;
    bool removedTimecode = false;
    while (read < sourceSize) {
        std::size_t obuStart = 0;
        std::size_t obuEnd = 0;
        bool remove = false;
        if (!readObu(read, obuStart, obuEnd, remove)) return;
        if (remove) removedTimecode = true;
        else filteredSize += obuEnd - obuStart;
    }
    if (!removedTimecode || filteredSize == 0 || filteredSize > static_cast<std::size_t>(INT_MAX)) return;
    if (av_packet_make_writable(packet) < 0) return;
    source = packet->data;
    read = 0;
    std::size_t write = 0;
    while (read < sourceSize) {
        std::size_t obuStart = 0;
        std::size_t obuEnd = 0;
        bool remove = false;
        if (!readObu(read, obuStart, obuEnd, remove)) return;
        if (remove) continue;
        const auto obuSize = obuEnd - obuStart;
        std::memmove(packet->data + write, source + obuStart, obuSize);
        write += obuSize;
    }
    av_shrink_packet(packet, static_cast<int>(filteredSize));
}

std::uint64_t TimedTextContentKey(const std::uint64_t contentId, const char* text,
    const char* fontFamily) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](const char* value) noexcept {
        for (auto* current = reinterpret_cast<const unsigned char*>(value); *current != 0; ++current) {
            hash ^= *current;
            hash *= 1099511628211ull;
        }
        hash ^= 0xffu;
        hash *= 1099511628211ull;
    };
    for (std::size_t index = 0; index < sizeof(contentId); ++index) {
        hash ^= reinterpret_cast<const std::uint8_t*>(&contentId)[index];
        hash *= 1099511628211ull;
    }
    append(text); append(fontFamily);
    return hash == 0 ? 1 : hash;
}

std::string ChannelLayoutName(const AVChannelLayout& layout) {
    char text[256]{};
    return av_channel_layout_describe(&layout, text, sizeof(text)) >= 0 ? text : std::string{};
}

std::string ToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), length, nullptr, nullptr);
    result.resize(length - 1); return result;
}

std::wstring FromUtf8(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), length);
    result.resize(length - 1); return result;
}

bool FromUtf8Strict(const char* value, std::wstring& output) noexcept {
    try {
        if (value == nullptr) { output.clear(); return true; }
        const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
        if (length <= 0) return false;
        std::wstring converted(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
            converted.data(), length) <= 0) return false;
        converted.resize(static_cast<std::size_t>(length - 1));
        output = std::move(converted);
        return true;
    } catch (...) { return false; }
}

AVPixelFormat SelectHardwareFormat(AVCodecContext* context, const AVPixelFormat* formats) {
    const auto expected = static_cast<AVPixelFormat>(reinterpret_cast<std::intptr_t>(context->opaque));
    for (auto format = formats; *format != AV_PIX_FMT_NONE; ++format)
        if (*format == expected) return *format;
    return AV_PIX_FMT_NONE;
}

// The predictive D3D11 capability probe is OFF by default. It asks the driver
// whether a decoder configuration exists, which is exact for what it asks, but it
// is a prediction about one backend only and the authoritative answer is the trial
// decode (see HardwareDecoderProducesFrame). Keep it behind a flag for A/B work.
bool HardwareCapabilityProbeEnabled() noexcept {
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"FFF3FP_HWCAP_PROBE", value, 8) == 0) return false;
    return value[0] == L'1' && value[1] == L'\0';
}

// The trial decode is the gate, so it is on by default. FFF3FP_HWCAP_TRIAL=0
// restores "open the decoder and hope", which is what an A/B measurement needs.
bool HardwareTrialDecodeEnabled() noexcept {
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"FFF3FP_HWCAP_TRIAL", value, 8) == 0) return true;
    return !(value[0] == L'0' && value[1] == L'\0');
}

bool HardwareCapabilityTrace() noexcept {
    return GetEnvironmentVariableW(L"FFF3FP_HWCAP_TRACE", nullptr, 0) != 0;
}

// Remembering a refused hardware route skips the second attempt. On by default
// because the memory only covers answers the GPU itself gave; FFF3FP_HWCAP_MEMORY=0
// restores "ask again every time" for A/B work.
bool HardwareRouteMemoryEnabled() noexcept {
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"FFF3FP_HWCAP_MEMORY", value, 8) == 0) return true;
    return !(value[0] == L'0' && value[1] == L'\0');
}

// Stills ask D3D11VA first because its surfaces are sampled as-is; this decides
// whether CUDA joins the list as a refusal fallback. Default on: the measured cost of
// the CUDA route for a still (a frame transfer, and not bit-exact -- see
// OpenHardwareVideoDecoder) is only paid after D3D11VA has already refused, when the
// alternative is the CPU decoder. FFF3FP_HWCAP_STILL_CUDA=0 restores the D3D11VA-only
// list for A/B work.
bool StillCudaFallbackEnabled() noexcept {
    wchar_t value[8]{};
    if (GetEnvironmentVariableW(L"FFF3FP_HWCAP_STILL_CUDA", value, 8) == 0) return true;
    return !(value[0] == L'0' && value[1] == L'\0');
}

// What the hardware route is capable of depends on the codec, the profile, the pixel
// format and the frame size, and on nothing else in the stream description. Two files
// that agree on those four agree on the answer, which is what makes a key out of them
// safe to reuse.
std::uint64_t HardwareRouteKey(const AVCodecParameters* parameters) noexcept {
    if (parameters == nullptr) return 0;
    const auto mix = [](const std::uint64_t value, const std::uint64_t seed) noexcept {
        return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    };
    auto key = mix(static_cast<std::uint32_t>(parameters->codec_id), 0ull);
    key = mix(static_cast<std::uint32_t>(parameters->profile), key);
    key = mix(static_cast<std::uint32_t>(parameters->format), key);
    key = mix(static_cast<std::uint32_t>(parameters->width), key);
    return mix(static_cast<std::uint32_t>(parameters->height), key);
}

AVPixelFormat FindHardwareFormat(const AVCodec* codec, const AVHWDeviceType deviceType) noexcept {
    if (codec == nullptr) return AV_PIX_FMT_NONE;
    const auto expectedFormat = deviceType == AV_HWDEVICE_TYPE_D3D11VA
        ? AV_PIX_FMT_D3D11 : deviceType == AV_HWDEVICE_TYPE_CUDA ? AV_PIX_FMT_CUDA : AV_PIX_FMT_NONE;
    if (expectedFormat == AV_PIX_FMT_NONE) return AV_PIX_FMT_NONE;
    for (int configIndex = 0;; ++configIndex) {
        const auto* hardware = avcodec_get_hw_config(codec, configIndex);
        if (hardware == nullptr) break;
        if (hardware->device_type == deviceType && hardware->pix_fmt == expectedFormat &&
            (hardware->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0)
            return expectedFormat;
    }
    return AV_PIX_FMT_NONE;
}

bool IsDedicatedCudaDecoder(const AVCodec* codec) noexcept {
    return codec != nullptr && codec->name != nullptr &&
        std::string_view(codec->name).ends_with("_cuvid");
}

bool IsHardwareFrame(const AVFrame* frame) noexcept {
    if (frame == nullptr) return false;
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    return descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL) != 0;
}

// A hardware frame transferred back to CPU memory comes out semi-planar (U and V packed
// into one plane). Reported source formats must not depend on which route decoded the
// picture, so name the planar layout that holds the same samples.
AVPixelFormat PlanarEquivalent(const AVPixelFormat format) noexcept {
    switch (format) {
    case AV_PIX_FMT_NV12: case AV_PIX_FMT_NV21: return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_NV16: return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_NV24: return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_NV20: case AV_PIX_FMT_P010: return AV_PIX_FMT_YUV420P10LE;
    case AV_PIX_FMT_P012: return AV_PIX_FMT_YUV420P12LE;
    case AV_PIX_FMT_P016: return AV_PIX_FMT_YUV420P16LE;
    case AV_PIX_FMT_P210: return AV_PIX_FMT_YUV422P10LE;
    case AV_PIX_FMT_P212: return AV_PIX_FMT_YUV422P12LE;
    case AV_PIX_FMT_P216: return AV_PIX_FMT_YUV422P16LE;
    case AV_PIX_FMT_P410: return AV_PIX_FMT_YUV444P10LE;
    case AV_PIX_FMT_P412: return AV_PIX_FMT_YUV444P12LE;
    case AV_PIX_FMT_P416: return AV_PIX_FMT_YUV444P16LE;
    default: return format;
    }
}

bool IsLoopAwareImageDemuxer(const AVInputFormat* inputFormat) noexcept {
    if (inputFormat == nullptr || inputFormat->name == nullptr) return false;
    const std::string_view name(inputFormat->name);
    return name == "gif" || name == "apng" || name == "webp_anim" || name == "jpegxl_anim";
}

// APNG's demuxer drops frames on repeated wraps; loop it with DoSeek(0) instead.
bool DemuxerLosesFrameOnWrap(const AVInputFormat* inputFormat) noexcept {
    if (inputFormat == nullptr || inputFormat->name == nullptr) return false;
    return std::string_view(inputFormat->name) == "apng";
}

// Set ignore_loop before decoding: APNG uses session rewind; WebP must use
// demuxer looping because its seek does not resume reads. GIF/JXL keep defaults.
bool ApplyLoopingOptions(AVFormatContext* format, const std::string_view demuxerName) noexcept {
    if (format == nullptr) return true;
    if (demuxerName == "apng") return av_opt_set_int(format->priv_data, "ignore_loop", 1, 0) >= 0;
    if (demuxerName == "webp_anim") return av_opt_set_int(format->priv_data, "ignore_loop", 0, 0) >= 0;
    return true;  // GIF/JXL: default single pass + the session rewind works.
}


bool IsStaticImageDemuxer(const AVInputFormat* inputFormat) noexcept {
    if (inputFormat == nullptr || inputFormat->name == nullptr) return false;
    const std::string_view name(inputFormat->name);
    // FFmpeg's single-image demuxers are image2 or codec-specific *_pipe
    // demuxers. Animated image demuxers deliberately remain timed video.
    return name == "image2" || name == "image2pipe" || name == "ico" ||
        (name.size() > 5 && name.ends_with("_pipe"));
}

// Prefer frame count for still-image detection (AVIF/HEIC use mov); when the
// count is unknown, fall back to image demuxer names.
bool IsStillImage(const AVFormatContext* format, const std::int32_t streamIndex) noexcept {
    if (format == nullptr || streamIndex < 0 ||
        streamIndex >= static_cast<std::int32_t>(format->nb_streams)) return false;
    // Animated image formats stay timed video even when they report one frame:
    // they have a real timeline and are expected to play.
    if (IsLoopAwareImageDemuxer(format->iformat)) return false;
    const auto frameCount = format->streams[streamIndex]->nb_frames;
    if (frameCount > 1) return false;  // image sequences and multi-frame sources are video
    if (frameCount == 1) return true;
    return IsStaticImageDemuxer(format->iformat);
}

// A still image must fit a single D3D11 texture (maximum side: 16384).
constexpr std::int32_t MaxStillImageDimension = 16384;

// Only a still-image container can carry a layered item, and only those files are
// read whole on the way to the demuxer: everything else keeps its file-backed
// input. 256 MiB is far above any still image that reaches here.
constexpr std::int64_t MaxRepairedStillImageBytes = 256 * 1024 * 1024;

// Cheap gate in front of AvifIlocShim, so no other media pays for the read.
bool IsStillImageContainerName(const std::string& path) noexcept {
    // Compared against the extension without its dot.
    static const std::string_view kNames[] = {"avif", "avifs", "heic", "heif", "heics", "heifs"};
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return false;
    const std::string_view extension(path.data() + dot + 1, path.size() - dot - 1);
    for (const auto name : kNames) {
        if (extension.size() != name.size()) continue;
        bool match = true;
        for (std::size_t index = 0; index < name.size(); ++index) {
            const auto character = extension[index];
            const auto lower = character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a') : character;
            if (lower != name[index]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

std::int64_t EstimateDuration100ns(const AVFormatContext* format) noexcept {
    if (format == nullptr) return 0;
    if (format->duration > 0)
        return av_rescale(format->duration, 10, 1);

    std::int64_t streamDuration = 0;
    std::int64_t streamBitRate = 0;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        const auto* stream = format->streams[index];
        const auto* parameters = stream->codecpar;
        if (parameters->codec_type != AVMEDIA_TYPE_VIDEO &&
            parameters->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) continue;

        if (stream->duration > 0 && stream->duration != AV_NOPTS_VALUE &&
            stream->time_base.num > 0 && stream->time_base.den > 0) {
            streamDuration = std::max(streamDuration,
                av_rescale_q(stream->duration, stream->time_base,
                    AVRational{1, static_cast<int>(TicksPerSecond)}));
        } else if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && stream->nb_frames > 0) {
            const auto frameRate = stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0
                ? stream->avg_frame_rate : stream->r_frame_rate;
            if (frameRate.num > 0 && frameRate.den > 0) {
                streamDuration = std::max(streamDuration,
                    av_rescale_q(stream->nb_frames, av_inv_q(frameRate),
                        AVRational{1, static_cast<int>(TicksPerSecond)}));
            }
        }
        if (parameters->bit_rate > 0)
            streamBitRate += parameters->bit_rate;
    }
    if (streamDuration > 0) return streamDuration;

    const auto fileSize = format->pb == nullptr ? 0 : avio_size(format->pb);
    const auto bitRate = format->bit_rate > 0 ? format->bit_rate : streamBitRate;
    if (fileSize <= 0 || bitRate <= 0) return 0;
    return av_rescale(fileSize, 8 * TicksPerSecond, bitRate);
}

// IAMF puts one audio element's channels across SEVERAL elementary streams, and
// libavformat/iamfdec.c marks every one of them AV_DISPOSITION_DEPENDENT when the
// element has more than one substream -- none is DEFAULT:
//
//     else if (audio_element->nb_layers > 1 || layers[0].substream_count > 1)
//         st->disposition |= AV_DISPOSITION_DEPENDENT;
//
// FindDefaultOrFirstStream therefore fell through to "the first audio stream",
// which for a 7.1.4 element is a single stereo pair: the player reported stereo
// for a 12-channel source and silently dropped the other ten channels. Verified
// against those same out-of-tree fixtures -- one
// substream decodes to 2 channels while the full mix is 12.
//
// This resolves the layout the IAMF mix presentation actually asks for, so the
// reported channel count is the truth rather than one substream's share.
struct IamfLayoutInfo {
    bool isIamf = false;
    // Channels the mix presentation targets (0 when no presentation is found).
    std::uint32_t presentationChannels = 0;
    std::string presentationLayoutName;
    // Audio elements carry the channel breakdown; used when there is no presentation.
    std::uint32_t elementChannels = 0;
    // Number of elementary streams the element is split across.
    std::uint32_t substreamCount = 0;
};

std::string DescribeLayout(const AVChannelLayout& layout) noexcept {
    if (layout.nb_channels <= 0) return {};
    char text[256]{};
    return av_channel_layout_describe(&layout, text, sizeof(text)) >= 0
        ? std::string(text) : std::string{};
}

IamfLayoutInfo ResolveIamfLayout(const AVFormatContext* format) noexcept {
    IamfLayoutInfo info{};
    if (format == nullptr || format->iformat == nullptr || format->iformat->name == nullptr)
        return info;
    // Only the "iamf" demuxer produces these stream groups.
    if (std::string_view(format->iformat->name).find("iamf") == std::string_view::npos)
        return info;
    info.isIamf = true;
    if (format->stream_groups == nullptr) return info;

    for (unsigned g = 0; g < format->nb_stream_groups; ++g) {
        const auto* group = format->stream_groups[g];
        if (group == nullptr) continue;
        if (group->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT) {
            const auto* element = group->params.iamf_audio_element;
            if (element == nullptr) continue;
            info.substreamCount = group->nb_streams;
            // Sum the element's layer channel counts: that is how many channels the
            // element contributes before any mix presentation is applied.
            // AVIAMFLayer exposes only ch_layout (no substream count), and for a
            // channel-based element one substream carries one layer's channels.
            std::uint32_t channels = 0;
            for (unsigned l = 0; l < element->nb_layers; ++l) {
                const auto* layer = element->layers[l];
                if (layer == nullptr) continue;
                channels += static_cast<std::uint32_t>(
                    std::max(0, layer->ch_layout.nb_channels));
            }
            if (channels > info.elementChannels) info.elementChannels = channels;
        } else if (group->type == AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION) {
            const auto* mix = group->params.iamf_mix_presentation;
            if (mix == nullptr) continue;
            // A presentation may offer SEVERAL submix layouts (measured: stereo and
            // 5.1(side) in the same file). Report the largest, because that is the
            // best rendering the content actually carries -- reporting the first or
            // smallest would understate the file just as the old code did.
            for (unsigned s = 0; s < mix->nb_submixes; ++s) {
                const auto* submix = mix->submixes[s];
                if (submix == nullptr) continue;
                for (unsigned l = 0; l < submix->nb_layouts; ++l) {
                    const auto* layout = submix->layouts[l];
                    if (layout == nullptr) continue;
                    const auto channels = static_cast<std::uint32_t>(
                        layout->sound_system.nb_channels);
                    if (channels > info.presentationChannels) {
                        info.presentationChannels = channels;
                        info.presentationLayoutName = DescribeLayout(layout->sound_system);
                    }
                }
            }
        }
    }
    // Ambisonic / scene-only fixtures legitimately carry no mix presentation, so the
    // presentation fields stay 0. Fall back to the element's channel total: it is the
    // only layout information the file has, and reporting 0 would look like a parse
    // failure rather than "this file declares no presentation".
    if (info.presentationChannels == 0 && info.elementChannels > 0) {
        info.presentationChannels = info.elementChannels;
        if (info.presentationLayoutName.empty())
            info.presentationLayoutName = "(element, no mix presentation)";
    }
    return info;
}

std::int32_t FindDefaultOrFirstStream(AVFormatContext* format, AVMediaType type) noexcept {
    if (format == nullptr) return -1;
    std::int32_t first = -1;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        const auto* stream = format->streams[index];
        if (stream->codecpar->codec_type != type ||
            (type == AVMEDIA_TYPE_VIDEO && (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0)) continue;
        if (first < 0) first = static_cast<std::int32_t>(index);
        if ((stream->disposition & AV_DISPOSITION_DEFAULT) != 0) return static_cast<std::int32_t>(index);
    }
    return first;
}

// The first video stream that is not the base layer: cover art is skipped the
// same way FindDefaultOrFirstStream skips it. Only consulted when the host has
// asked for an auxiliary composition, so a file with several video tracks pays
// nothing until then.
std::int32_t FindAuxiliaryVideoStream(AVFormatContext* format,
    const std::int32_t primary) noexcept {
    if (format == nullptr) return -1;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        if (static_cast<std::int32_t>(index) == primary) continue;
        const auto* stream = format->streams[index];
        if (stream == nullptr || stream->codecpar == nullptr) continue;
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) continue;
        return static_cast<std::int32_t>(index);
    }
    return -1;
}

// AVIF/HEIC sequences may put a still item first. Select a later animated track
// only when the mov-family container and frame counts make that unambiguous.
std::int32_t FindAnimatedImageStream(AVFormatContext* format) noexcept {
    if (format == nullptr || format->iformat == nullptr || format->iformat->name == nullptr) return -1;
    const std::string_view demuxer(format->iformat->name);
    if (demuxer.find("mov") == std::string_view::npos) return -1;
    std::int32_t first = -1;
    std::int32_t busiest = -1;
    std::int64_t busiestFrames = 1;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        const auto* stream = format->streams[index];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) continue;
        if (first < 0) first = static_cast<std::int32_t>(index);
        if (stream->nb_frames > busiestFrames) {
            busiestFrames = stream->nb_frames;
            busiest = static_cast<std::int32_t>(index);
        }
    }
    if (first < 0 || busiest < 0 || busiest == first) return -1;
    if (format->streams[first]->nb_frames > 1) return -1;
    return busiest;
}

// APNG and timed AVIF/HEIC rewind per pass; other demuxers loop internally.
bool IsAnimatedImageSequence(AVFormatContext* format) noexcept {
    if (format == nullptr) return false;
    if (IsLoopAwareImageDemuxer(format->iformat)) return true;
    return FindAnimatedImageStream(format) >= 0;
}

std::int32_t FindCoverArtStream(AVFormatContext* format) noexcept {    if (format == nullptr) return -1;
    for (unsigned index = 0; index < format->nb_streams; ++index) {
        const auto* stream = format->streams[index];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) return static_cast<std::int32_t>(index);
    }
    return -1;
}

const char* MediaTypeName(const AVMediaType type) noexcept {
    switch (type) { case AVMEDIA_TYPE_VIDEO: return "video"; case AVMEDIA_TYPE_AUDIO: return "audio";
    case AVMEDIA_TYPE_SUBTITLE: return "subtitle"; default: return "other"; }
}

int PixelFormatBitDepth(const AVPixelFormat format) noexcept {
    const auto* descriptor = av_pix_fmt_desc_get(format);
    return descriptor != nullptr && descriptor->nb_components > 0 ? descriptor->comp[0].depth : 0;
}

std::string HardwareAccelerationName(const AVCodecContext* decoder) {
    if (decoder == nullptr || decoder->hw_device_ctx == nullptr || decoder->hw_device_ctx->data == nullptr)
        return {};
    const auto* context = reinterpret_cast<const AVHWDeviceContext*>(decoder->hw_device_ctx->data);
    const auto* type = av_hwdevice_get_type_name(context->type);
    if (type == nullptr) return {};
    if (context->type == AV_HWDEVICE_TYPE_D3D11VA) return "DXVA (D3D11VA)";
    return type;
}
}

// "No preference" is answered, not refused: the GPU is the default route and the
// CPU is what a refusal falls back to. Every decision downstream reads decodeMode_,
// so normalizing once here keeps the rest of the session a two-valued question
// (D3D11 or Cpu) and has the snapshot report the route that is actually attempted.
PlayerSession::PlayerSession(const FFF3FPConfiguration& configuration)
    : decodeMode_(configuration.decodeMode == FFF3FPDecodeMode::Unspecified
              ? FFF3FPDecodeMode::Gpu
              : configuration.decodeMode),
      callback_(configuration.eventCallback),
      callbackContext_(configuration.eventCallbackContext), terminate_(false), format_(nullptr),
      playbackPacket_(nullptr), stillImagePacket_(nullptr), externalAudioPacket_(nullptr), videoDecodeFrame_(nullptr),
      videoTransferFrame_(nullptr), audioDecodeFrame_(nullptr), externalAudioDecodeFrame_(nullptr),
      videoDecoder_(nullptr), videoStream_(-1),
      audioStream_(-1), coverArtStream_(-1), coverArtFrame_(nullptr), stillImageFrame_(nullptr),
      externalFormat_(nullptr), externalAudioDecoder_(nullptr),
      externalAudioStream_(-1), externalAudioOffset100ns_(0), audioDelay100ns_(0),
      videoRenderer_([this] { NotifyVideoRecovery(); }), audioExclusive_(false), volume_(1.0f), muted_(false),
      clockOriginPosition100ns_(0), clockOriginQpc_(0), playbackPosition100ns_(0),
      playbackClockSampleQpc_(0), playbackClockLimit100ns_(0), playbackClockSequence_(0),
      state_(FFF3FPState::Idle), qpcFrequency_(0), seekTarget100ns_(-1), seekTargetFrame_(-1),
      keyframeSeekPending_(false), lastVideoFrameDuration100ns_(0), nextUntimedVideoPosition100ns_(0),
      framePtsIndexBase_(0),
      rebuildingFrameIndex_(false),
      audioBlockedUntilVideoFrame_(false),
      playbackPreroll_(false),
      audioUnblockVideoGeneration_(0), audioResumePendingAfterVideoFrame_(false),
      stepScheduled_(false), stepRepeatRequested_(false), pendingStepOperation_(StepOperation::Frame),
      pendingStepDirection_(0), pendingVideoPacketBytes_(0), pendingAudioPacketBytes_(0),
      publishedBitRateSecond_(-1),
      draining_(false), demuxEnded_(false), audioDecoderDrained_(false),
      externalAudioDrained_(false), audioClockFinished_(false),
      staticImage_(false), animatedImage_(false), sessionWrapLoop_(false), sessionWrapFailures_(0),
      tileGrid_(false), tileGridWidth_(0), tileGridHeight_(0), tileGridBackground_{},
      hardwareFallbackPending_(false), internalAudioFailurePending_(false),
      internalAudioFailureResult_(FFFResult::Success), internalAudioDecodeErrorCount_(0) {
    snapshot_ = {};
    snapshot_.size = sizeof(snapshot_); snapshot_.version = 20; snapshot_.state = FFF3FPState::Idle;
    snapshot_.decodeMode = decodeMode_; snapshot_.requestedColorMode = configuration.colorMode;
    snapshot_.actualColorMode = FFF3FPColorMode::MapToSdr; snapshot_.frameIndex = -1;
    snapshot_.framePts = AV_NOPTS_VALUE; snapshot_.selectedVideoStream = -1; snapshot_.selectedAudioStream = -1;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); qpcFrequency_ = frequency.QuadPart;
    if (configuration.audioEndpointIdUtf8 != nullptr) audioEndpointId_ = FromUtf8(configuration.audioEndpointIdUtf8);
    videoRenderer_.SetWindow(static_cast<HWND>(configuration.outputWindow));
    // Must be applied before the first EnsureDevice(),
    // which happens lazily on the render path — hence here in the constructor.
    videoRenderer_.SetPreferredAdapterIndex(configuration.preferredAdapterIndex);
    videoRenderer_.SetScalingQuality(configuration.videoScalingQuality);
    // Adaptive downscale-before-upload: opt-in, and only ever engages for software
    // decoding with sustained dropped frames. Gated on the host's reported struct
    // size: these fields were appended, so a host built against the older
    // FFF3FPConfiguration must not have them read, or the read walks past the end of
    // its struct. Treat a short struct as "disabled", which matches the default.
    const auto hasAdaptiveDownscale = configuration.size >=
        offsetof(FFF3FPConfiguration, adaptiveDownscaleDropPercent) + sizeof(std::uint32_t);
    videoRenderer_.ConfigureAdaptiveDownscale(
        hasAdaptiveDownscale && configuration.adaptiveDownscaleBeforeUpload != 0,
        hasAdaptiveDownscale ? configuration.adaptiveDownscaleDropPercent : 20u);
    // Adaptive software-decoder thread tier. Gated the same way: these fields were
    // also appended, so a host built against an older header must not have them read.
    const auto hasAdaptiveThreads = configuration.size >=
        offsetof(FFF3FPConfiguration, decoderShrinkDropPercent) + sizeof(std::uint32_t);
    if (hasAdaptiveThreads) {
        adaptiveThreadsEnabled_ = configuration.adaptiveDecoderThreads != 0;
        threadLadderMin_ = std::clamp(configuration.minSoftwareDecoderThreads, 1u, 64u);
        threadLadderMax_ = std::clamp(configuration.maxSoftwareDecoderThreads, 1u, 64u);
        if (threadLadderMax_ < threadLadderMin_) std::swap(threadLadderMax_, threadLadderMin_);
    }
    // No cadence bound is read from the configuration: see the note on
    // FFF3FPConfiguration's tail -- reading a field that is not there would require
    // widening sizeof, and FFF3FP_Create rejects any host that has not been rebuilt
    // alongside this struct. The bound is therefore computed in the renderer instead.
    // Applied before the colour mode: SetColorMode evaluates the scRGB gate and
    // must already know the SDR presentation policy.
    videoRenderer_.SetSdrScRgbMode(configuration.sdrScRgbMode);
    videoRenderer_.SetColorMode(configuration.colorMode, configuration.sdrPeakNits,
        configuration.hdrPeakNits, configuration.sdrPaperWhiteNits,
        configuration.forceHdrOutput != 0);
    snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    publishedSnapshot_ = snapshot_;
    // Create the D3D11 device now rather than inside the first Open. Measured on this
    // machine: 195-245 ms the first time and free afterwards. In the image-browsing
    // pattern -- one player, one picture at a time -- that made the first picture take
    // ~365 ms while every following one took 54-78 ms, so it is the whole of the delay
    // before the first frame. The window is already known here, so the cost moves into
    // host setup. Failure is ignored on purpose: Open still creates the device on demand
    // and reports any real problem with full context.
    //
    // Only worth paying when the session has a video output window. EnsureDevice() uses
    // window_ for adapter *selection* only -- it does not decline to create the device
    // when there is none -- so an unconditional call made audio-only and headless
    // sessions pay the same full ~150-170 ms of device creation inside FFF3FP_Create,
    // which the caller waits on, for a device that session may never touch. Measured with
    // create_cost_probe: 170 ms without a window against 150 ms with one, versus ~0 ms
    // for both before this warm-up was added. The window test is the same one
    // EnsureDevice() applies, so a non-null but invalid HWND does not qualify either,
    // and a session that turns out to need the device later is unaffected: the render
    // path still creates it on demand.
    const auto outputWindow = static_cast<HWND>(configuration.outputWindow);
    if (outputWindow != nullptr && IsWindow(outputWindow)) (void)videoRenderer_.WarmDevice();
    worker_ = std::thread(&PlayerSession::Worker, this);
}

PlayerSession::~PlayerSession() {
    discCancel_.store(true);
    { std::lock_guard lock(mutex_); terminate_ = true; commands_.clear(); }
    commandCondition_.notify_all();
    if (worker_.joinable()) worker_.join();
    // The plug's context is the kernel's from the install that took it, so releasing
    // the session releases it. This runs after the join on purpose: the renderer only
    // ever borrows the pointer, so nothing can be inside plug->render()/reset() here,
    // and its own teardown (a member destructor, below) must not try to free it again.
    {
        FFF3FPAudioRenderPlug held{};
        bool holding = false;
        std::vector<FFF3FPAudioRenderPlug> retired;
        {
            std::lock_guard lock(audioPlugMutex_);
            if (audioPlugInstalled_) { held = audioPlug_; holding = true; }
            audioPlug_ = {};
            audioPlugName_.clear();
            audioPlugInstalled_ = false;
            // Commands were cleared above, so a replacement whose release had not
            // run yet is still parked here. Release it rather than leak the context
            // the kernel took ownership of.
            retired.swap(audioPlugRetired_);
        }
        for (const auto& plug : retired)
            if (plug.destroy != nullptr && plug.context != nullptr)
                plug.destroy(plug.context);
        if (holding && held.destroy != nullptr && held.context != nullptr)
            held.destroy(held.context);
    }
    // Must run AFTER the join: the worker's P3 still-image path
    // (ConvertPrimariesToBt2020) is still using the gamutGraph_ filter graph, so
    // releasing it first would be a use-after-free. DoClose(Closed) on the worker's
    // way out already released it; this call is the belt-and-braces no-op.
    ReleasePrimariesCarrierFilter();
}

void PlayerSession::Enqueue(Command command) noexcept {
    try { { std::lock_guard lock(mutex_); if (terminate_) return; commands_.push_back(std::move(command)); } commandCondition_.notify_one(); }
    catch (...) { ReportError(FFFResult::NativeFailure, "Could not queue the playback command."); }
}

void PlayerSession::NotifyAudioRestart() noexcept {
    commandCondition_.notify_one();
}

void PlayerSession::NotifyVideoRecovery() noexcept {
    commandCondition_.notify_one();
}

bool PlayerSession::ShouldDelayAudioUntilVideoFrame() const noexcept {
    return audioBlockedUntilVideoFrame_ && ShouldGateAudioAtVideoStart();
}

std::int64_t PlayerSession::TimelineOrigin100ns(const AVFormatContext* owner) const noexcept {
    if (owner == nullptr) return 0;
    if (owner->start_time != AV_NOPTS_VALUE)
        return av_rescale_q(owner->start_time, AV_TIME_BASE_Q,
            AVRational{1, static_cast<int>(TicksPerSecond)});

    // Some raw/transport demuxers do not publish a format start. Use the
    // earliest stream timestamp so selected audio and video still share one
    // media origin instead of silently acquiring a stream-dependent offset.
    std::int64_t origin = AV_NOPTS_VALUE;
    for (unsigned index = 0; index < owner->nb_streams; ++index) {
        const auto* stream = owner->streams[index];
        if (stream == nullptr || stream->start_time == AV_NOPTS_VALUE) continue;
        const auto value = av_rescale_q(stream->start_time, stream->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)});
        origin = origin == AV_NOPTS_VALUE ? value : std::min(origin, value);
    }
    return origin == AV_NOPTS_VALUE ? 0 : origin;
}

std::int64_t PlayerSession::StreamTimestampPosition100ns(const AVFormatContext* owner,
    const std::int32_t streamIndex, const std::int64_t timestamp) const noexcept {
    if (owner == nullptr || streamIndex < 0 ||
        streamIndex >= static_cast<std::int32_t>(owner->nb_streams) ||
        timestamp == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
    const auto* stream = owner->streams[streamIndex];
    if (stream == nullptr) return AV_NOPTS_VALUE;
    return av_rescale_q(timestamp, stream->time_base,
        AVRational{1, static_cast<int>(TicksPerSecond)}) - TimelineOrigin100ns(owner) +
        (disc_ && owner == format_ ? discPositionOffset_ : 0);
}

bool PlayerSession::ShouldGateAudioAtVideoStart() const noexcept {
    if (format_ == nullptr || videoStream_ < 0 ||
        videoStream_ >= static_cast<std::int32_t>(format_->nb_streams)) return false;
    const auto* stream = format_->streams[videoStream_];
    if (stream == nullptr || stream->start_time == AV_NOPTS_VALUE) return true;
    const auto start = av_rescale_q(stream->start_time, stream->time_base,
        AVRational{1, static_cast<int>(TicksPerSecond)});
    // If the first video timestamp is materially later than the common media
    // origin, audio is valid before the first picture and must not be gated.
    constexpr std::int64_t GateTolerance100ns = 100'000;
    return start - TimelineOrigin100ns(format_) <= GateTolerance100ns;
}

void PlayerSession::ArmAudioUntilVideoFrame() noexcept {
    // Opening an animation while stopped must present its first frame without
    // Playing-only preroll. Play() arms preroll when playback starts.
    playbackPreroll_ = videoStream_ >= 0 && !staticImage_ && !animatedImage_;
    audioBlockedUntilVideoFrame_ = ShouldGateAudioAtVideoStart();
    audioUnblockVideoGeneration_ = 0;
    audioResumePendingAfterVideoFrame_ = audioBlockedUntilVideoFrame_ &&
        state_.load() == FFF3FPState::Playing;
    if ((audioResumePendingAfterVideoFrame_ || playbackPreroll_) && audioRenderer_)
        audioRenderer_->SetPaused(true);
}

std::size_t PlayerSession::VideoQueuePipelineReservation() const noexcept {
    // Scoped to animated images: this accounting can only ever shorten the queue,
    // and the video path's pacing has its own large regression body behind it. The
    // measured problem aimed at here is a 4K animation's per-loop memory peak, so
    // the change is validated there and left byte-identical for video.
    if (!animatedImage_ || !AnimQueueReserveEnabled()) return 0;
    if (videoDecoder_ == nullptr) return 0;
    // active_thread_type is what the codec actually ended up using; thread_type is
    // only what was asked for at open time. A decoder that never runs frame threads
    // hands pictures out as it decodes them and holds no pipeline to charge.
    if ((videoDecoder_->active_thread_type & FF_THREAD_FRAME) == 0) return 0;
    return static_cast<std::size_t>(std::max(0, videoDecoder_->thread_count));
}

std::size_t PlayerSession::VideoQueueLimit() const noexcept {
    const auto pipelineFrames = VideoQueuePipelineReservation();
    const auto originalLimit = VideoFrameQueueLimit(videoFrameQueue_, pipelineFrames,
        !staticImage_ && !animatedImage_, SoftwarePoolBudgets[softwarePoolRung_],
        SoftwarePoolFrameCaps[softwarePoolRung_], performance_);
    const auto limit = std::max(MinimumMemoryBoundVideoFrames,
        originalLimit - std::min(originalLimit - 1, static_cast<std::size_t>(softwareVideoOutputLimit_ - 1)));
    // The two numbers this slice stands on, said by the kernel itself: whether frame
    // threading is really active for this decoder, and the depth the budget then
    // allows. Without them "the honest queue is shorter" cannot be checked against
    // the probe's queued-frames readout.
    if (animatedImage_ && (pipelineFrames != tracedQueuePipelineFrames_ ||
        limit != tracedQueueLimit_)) {
        tracedQueuePipelineFrames_ = static_cast<std::int64_t>(pipelineFrames);
        tracedQueueLimit_ = static_cast<std::int64_t>(limit);
        const auto bytesPerFrame = videoFrameQueue_.empty()
            ? 0ULL : static_cast<unsigned long long>(
                EstimateDecodedFrameBytes(videoFrameQueue_.back()));
        std::fprintf(stderr, "[queue] animated bytesPerFrame=%llu pipeline=%lld limit=%lld\n",
            bytesPerFrame, static_cast<long long>(pipelineFrames),
            static_cast<long long>(limit));
    }
    return limit;
}

bool PlayerSession::VideoQueueSaturated() const noexcept {
    if (videoFrameQueue_.empty()) return false;
    const auto lookAhead = VideoFramePosition(videoFrameQueue_.back()) - ClockPosition();
    const auto queueLimit = VideoQueueLimit();
    return videoFrameQueue_.size() >= std::max<std::size_t>(1, queueLimit - 1) ||
        (videoFrameQueue_.size() >= MinimumQueuedVideoFrames &&
            lookAhead >= performance_.VideoLookAhead());
}

void PlayerSession::TryCompletePlaybackPreroll() noexcept {
    if (!playbackPreroll_ || state_.load() != FFF3FPState::Playing) return;
    if (ShouldDelayAudioUntilVideoFrame() && videoRenderer_.HasOutputWindow()) return;
    const auto packetBound = pendingVideoPackets_.size() >= MaximumPendingVideoPackets ||
        pendingVideoPacketBytes_ >= MaximumPendingVideoPacketBytes ||
        pendingAudioPackets_.size() >= MaximumPendingAudioPackets ||
        pendingAudioPacketBytes_ >= MaximumPendingAudioPacketBytes;
    if (!draining_ && !(disc_ && disc_->Menu() && snapshot_.framePts != AV_NOPTS_VALUE) && !VideoQueueSaturated() &&
        !(packetBound && !videoFrameQueue_.empty())) return;
    const auto audioDrained = externalFormat_ != nullptr ? externalAudioDrained_ : audioDecoderDrained_;
    if (!draining_ && !packetBound && audioRenderer_ && !audioDrained &&
        audioRenderer_->Buffered100ns() < audioTargetBuffer100ns_) return;
    if (snapshot_.framePts == AV_NOPTS_VALUE && !videoFrameQueue_.empty() &&
        videoRenderer_.HasOutputWindow()) {
        auto* frame = videoFrameQueue_.front();
        AVFrame* transferred = nullptr;
        if (IsHardwareFrame(frame) && !CanPresentHardwareFrameDirectly(frame, videoRenderer_)) {
            transferred = av_frame_alloc();
            if (transferred == nullptr || av_hwframe_transfer_data(transferred, frame, 0) < 0) {
                av_frame_free(&transferred);
                if (snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
                    FallbackToSoftwareVideoDecoder("Hardware video preroll transfer failed; continuing with CPU decoding.") == FFFResult::Success)
                    return;
                Fail(FFFResult::FfmpegFailure, "Could not prepare the initial hardware video frame.", "preroll");
                return;
            }
            av_frame_copy_props(transferred, frame);
            frame = transferred;
        }
        const auto result = videoRenderer_.Render(frame, HdrProcessor::kUnknownFrameIndex, false, false, true);
        av_frame_free(&transferred);
        if (result != FFFResult::Success) {
            if (result == FFFResult::DeviceFailure && videoRenderer_.RequestRecoveryIfDeviceLost()) return;
            if (result == FFFResult::NotSupported && snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
                FallbackToSoftwareVideoDecoder("The hardware preroll format is unsupported; continuing with CPU decoding.") == FFFResult::Success)
                return;
            Fail(result, videoRenderer_.LastError(), "preroll");
            return;
        }
    }
    const auto position = clockOriginPosition100ns_.load();
    playbackPreroll_ = false;
    ResetClock(position);
    ApplyAudioPlaybackPause(true);
}

void PlayerSession::UpdateDrainedAudioClock() noexcept {
    if (audioClockFinished_ || playbackPreroll_ || !audioRenderer_) return;
    const auto drained = externalFormat_ != nullptr ? externalAudioDrained_ : audioDecoderDrained_;
    if (!drained || audioRenderer_->Buffered100ns() > 0) return;
    const auto position = tempoOrigin_ + static_cast<std::int64_t>((audioRenderer_->Position100ns() - tempoOrigin_) * playbackRate_.load());
    audioClockFinished_ = true;
    ResetClock(position);
}

void PlayerSession::DrainInternalAudio() noexcept {
    if (!demuxEnded_ || !pendingAudioPackets_.empty() || audioDecoderDrained_ || externalFormat_ != nullptr) return;
    audioDecoderDrained_ = true;
    if (audioDecoder_) DecodePacket(audioDecoder_, nullptr, false, format_);
    if (audioRenderer_ && tempoGraph_) TempoAudio(nullptr, 0);
    if (audioRenderer_ && audioRenderer_->Finish() != FFFResult::Success)
        Fail(FFFResult::FfmpegFailure, "Could not drain audio output.", "audio-drain");
}

void PlayerSession::TryReleaseAudioAfterVideoPresentation() noexcept {
    if (!ShouldDelayAudioUntilVideoFrame() || audioUnblockVideoGeneration_ == 0 ||
        videoRenderer_.PresentedVideoGeneration() < audioUnblockVideoGeneration_)
        return;
    audioBlockedUntilVideoFrame_ = false;
    audioUnblockVideoGeneration_ = 0;
    if (!playbackPreroll_ && audioResumePendingAfterVideoFrame_ && audioRenderer_)
        audioRenderer_->SetPaused(false);
    audioResumePendingAfterVideoFrame_ = false;
}

void PlayerSession::ReleaseAudioWithoutVideo() noexcept {
    audioBlockedUntilVideoFrame_ = false;
    audioUnblockVideoGeneration_ = 0;
    if (!playbackPreroll_ && audioResumePendingAfterVideoFrame_ && audioRenderer_)
        audioRenderer_->SetPaused(false);
    audioResumePendingAfterVideoFrame_ = false;
}

void PlayerSession::ApplyAudioPlaybackPause(const bool playing) noexcept {
    if (!audioRenderer_) {
        audioResumePendingAfterVideoFrame_ = false;
        if (!videoRenderer_.HasOutputWindow()) ReleaseAudioWithoutVideo();
        return;
    }
    if (!playing) {
        audioResumePendingAfterVideoFrame_ = false;
        audioRenderer_->SetPaused(true);
        return;
    }
    if (playbackPreroll_) {
        audioResumePendingAfterVideoFrame_ = true;
        audioRenderer_->SetPaused(true);
        if (!videoRenderer_.HasOutputWindow()) ReleaseAudioWithoutVideo();
        return;
    }
    if (!ShouldDelayAudioUntilVideoFrame()) {
        audioResumePendingAfterVideoFrame_ = false;
        audioRenderer_->SetPaused(false);
        return;
    }
    if (videoRenderer_.HasOutputWindow()) {
        audioResumePendingAfterVideoFrame_ = true;
        audioRenderer_->SetPaused(true);
    } else {
        // A headless session has no visible Present boundary. Its audio clock
        // may start immediately, but a later window bind will re-arm the gate.
        ReleaseAudioWithoutVideo();
        audioRenderer_->SetPaused(false);
    }
}

bool PlayerSession::PresentAudioBoundary() noexcept {
    if (!ShouldDelayAudioUntilVideoFrame() ||
        audioUnblockVideoGeneration_ == 0) return true;
    if (videoRenderer_.PresentedVideoGeneration() < audioUnblockVideoGeneration_) {
        // The first visible frame is the transition boundary for audio.
        // Present it synchronously once so an earlier blank-overlay Present
        // cannot consume the asynchronous wake and stall the media clock.
        const auto result = videoRenderer_.PresentTimedText();
        if (result != FFFResult::Success) {
            if (result == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return false;
            Fail(result, videoRenderer_.LastError(), "first-frame-present");
            return false;
        }
    }
    TryReleaseAudioAfterVideoPresentation();
    return true;
}

FFFResult PlayerSession::ConfigurePlaybackPerformance(
    const std::int32_t decodeAdapterIndex, const std::uint32_t videoBufferPolicy,
    const std::uint32_t audioBufferPolicy) noexcept {
    if (decodeAdapterIndex < -1 || decodeAdapterIndex > 15 ||
        videoBufferPolicy > 2 || audioBufferPolicy > 2)
        return FFFResult::InvalidArgument;
    if (state_.load(std::memory_order_acquire) != FFF3FPState::Idle)
        return FFFResult::InvalidState;
    Enqueue([this, decodeAdapterIndex, videoBufferPolicy, audioBufferPolicy] {
        decodeAdapterIndex_ = decodeAdapterIndex;
        performance_ = {videoBufferPolicy, audioBufferPolicy};
        audioTargetBuffer100ns_ = performance_.AudioInitial();
        unsupportedHardwareRoutes_.clear();
    });
    return FFFResult::Success;
}

FFFResult PlayerSession::Open(const char* path) noexcept {
    std::string normalized, error;
    if (!NormalizeLocalPath(path, normalized, error)) { ReportError(FFFResult::InvalidArgument, std::move(error), "open"); return FFFResult::InvalidArgument; }
    if (state_.exchange(FFF3FPState::Opening) == FFF3FPState::Opening) return FFFResult::InvalidState;
    { std::lock_guard lock(snapshotMutex_); publishedSnapshot_.state = FFF3FPState::Opening; }
    discCancel_.store(false);
    Emit(FFF3FPEvent::StateChanged, "{\"state\":1}");
    Enqueue([this, value = std::move(normalized)] { DoOpen(value); }); return FFFResult::Success;
}
FFFResult PlayerSession::Play() noexcept {
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Paused &&
        state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this] {
        const auto current = state_.load();
        if (current == FFF3FPState::Ended) DoSeek(0);
        if (ResumeAudioRenderer() != FFFResult::Success) return;
        if (videoStream_ >= 0 && !staticImage_) playbackPreroll_ = true;
        ResetClock(snapshot_.position100ns);
        ApplyAudioPlaybackPause(true);
        SetState(FFF3FPState::Playing, "play");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::Pause() noexcept {
    if (state_.load() != FFF3FPState::Playing) return FFFResult::InvalidState;
    Enqueue([this] {
        snapshot_.position100ns = ClockPosition();
        SuspendAudioRenderer(true);
        audioResumePendingAfterVideoFrame_ = false;
        SetState(FFF3FPState::Paused, "pause");
    });
    return FFFResult::Success;
}

FFFResult PlayerSession::DiscardAudioOutput() noexcept {
    const auto currentState = state_.load();
    if (currentState != FFF3FPState::Ready && currentState != FFF3FPState::Playing &&
        currentState != FFF3FPState::Paused && currentState != FFF3FPState::Ended)
        return FFFResult::InvalidState;

    struct Completion final {
        std::mutex mutex;
        std::condition_variable condition;
        bool finished = false;
        FFFResult result = FFFResult::NativeFailure;
    };

    auto completion = std::make_shared<Completion>();
    try {
        {
            std::lock_guard lock(mutex_);
            if (terminate_) return FFFResult::InvalidState;
            commands_.push_back([this, completion] {
                auto result = FFFResult::Success;
                try {
                    const auto state = state_.load();
                    if (state == FFF3FPState::Playing)
                        snapshot_.position100ns = ClockPosition();
                    if (audioRenderer_) {
                        audioRenderer_->Stop();
                        audioRenderer_.reset();
                    }
                    audioBlockedUntilVideoFrame_ = false;
                    audioUnblockVideoGeneration_ = 0;
                    audioResumePendingAfterVideoFrame_ = false;
                    audioRuntimeState_.ClearValues();
                    ResetClock(std::max<std::int64_t>(0, snapshot_.position100ns));
                    UpdateAudioDiagnostics();
                    if (state == FFF3FPState::Playing) SetState(FFF3FPState::Paused, "pause");
                    else PublishSnapshot();
                } catch (...) {
                    result = FFFResult::NativeFailure;
                }
                {
                    std::lock_guard completionLock(completion->mutex);
                    completion->result = result;
                    completion->finished = true;
                }
                completion->condition.notify_one();
            });
        }
        commandCondition_.notify_one();
        std::unique_lock completionLock(completion->mutex);
        if (!completion->condition.wait_for(completionLock, std::chrono::seconds(5),
            [&completion] { return completion->finished; })) {
            ReportError(FFFResult::NativeFailure,
                "Timed out while stopping the previous audio output.", "discard-audio");
            return FFFResult::NativeFailure;
        }
        if (completion->result != FFFResult::Success)
            ReportError(completion->result, "Could not stop the previous audio output.", "discard-audio");
        return completion->result;
    } catch (...) {
        ReportError(FFFResult::NativeFailure,
            "Could not queue the previous audio output for stopping.", "discard-audio");
        return FFFResult::NativeFailure;
    }
}
FFFResult PlayerSession::Stop() noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this] { DoClose(FFF3FPState::Idle); }); return FFFResult::Success; }
FFFResult PlayerSession::Close() noexcept { discCancel_.store(true); Enqueue([this] { DoClose(); }); return FFFResult::Success; }
FFFResult PlayerSession::Seek(const std::int64_t value) noexcept { if (value < 0) return FFFResult::InvalidArgument; const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, value] {
        // 定位失败时 DoSeek 会提前返回：不动队列、不装 seekTarget100ns_、也不改位置。这时
        // 照常去 DecodeUntilSeekTarget()，就是朝一个"根本没装上的靶"抽干 demuxer——EOF 一到
        // 而 pendingVideoPackets_ 已空，FlushAtEnd 直接把会话判成 Ended，而 DoSeek 自己的
        // 错误消息写的是 "playback remained active"。实测裸 Annex-B：探针一句 Seek(0) ⇒
        // 解码 0 帧＋state=Ended；跳过那句 seek，同一个文件正常播（88 帧、位置推进 3.41s）。
        const auto seeked = DoSeek(value);
        if (seeked == FFFResult::Success && state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget(); Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek\",\"position100ns\":" + std::to_string(snapshot_.position100ns) + "}"); }); return FFFResult::Success; }
FFFResult PlayerSession::SeekKeyframe(const std::int64_t value) noexcept {
    if (value < 0) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this, value] {
        const auto hasVideo = videoStream_ >= 0 && videoDecoder_ != nullptr;
        DoSeek(value, -1, !hasVideo);
        if (hasVideo && state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget();
        Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek-keyframe\",\"position100ns\":" +
            std::to_string(snapshot_.position100ns) + "}");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SeekFrame(const std::int64_t value) noexcept {
    if (DiscStatus() != "{}") return FFFResult::NotSupported;
    if (value < 0) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    { std::lock_guard lock(snapshotMutex_); if ((state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) || publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState; }
    Enqueue([this, value] {
        const auto end = framePtsIndexBase_ + static_cast<std::int64_t>(framePtsIndex_.size());
        if (value >= framePtsIndexBase_ && value < end) {
            DoSeek(StreamTimestampPosition100ns(format_, videoStream_,
                framePtsIndex_[static_cast<std::size_t>(value - framePtsIndexBase_)]), value);
        } else {
            // Preserve exact frame navigation outside the bounded index window.
            // Rebuild the sparse rolling index from the stream start while decoding.
            framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = true;
            DoSeek(0, value);
            // DoSeek stores targetFrame-1 so a normal exact frame seek can
            // resume at the requested ordinal. A rebuild must count from zero
            // while decoding the stream prefix toward that ordinal.
            snapshot_.frameIndex = -1;
            PublishSnapshot();
        }
        if (state_.load() != FFF3FPState::Playing) DecodeUntilSeekTarget();
        Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"seek-frame\",\"frame\":" +
            std::to_string(snapshot_.frameIndex) + "}");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::StepFrame(const std::int32_t direction) noexcept {
    if (DiscStatus() != "{}") return FFFResult::NotSupported;
    if (direction != -1 && direction != 1) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    {
        std::lock_guard lock(snapshotMutex_);
        if ((state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
            state != FFF3FPState::Paused && state != FFF3FPState::Ended) ||
            publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState;
    }
    return ScheduleStep(StepOperation::Frame, direction);
}

FFFResult PlayerSession::ScheduleStep(const StepOperation operation,
    const std::int32_t direction) noexcept {
    try {
        {
            std::lock_guard lock(mutex_);
            if (terminate_) return FFFResult::InvalidState;
            pendingStepOperation_ = operation;
            pendingStepDirection_ = direction;
            if (stepScheduled_) {
                stepRepeatRequested_ = true;
                return FFFResult::Success;
            }
            stepScheduled_ = true;
            stepRepeatRequested_ = false;
            commands_.push_back([this] { ProcessStep(); });
        }
        commandCondition_.notify_one();
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stepScheduled_ = false;
            stepRepeatRequested_ = false;
        }
        ReportError(FFFResult::NativeFailure, "Could not queue media stepping.", "step");
        return FFFResult::NativeFailure;
    }
    return FFFResult::Success;
}

void PlayerSession::ProcessStep() noexcept {
    StepOperation operation = StepOperation::Frame;
    std::int32_t direction = 0;
    {
        std::lock_guard lock(mutex_);
        operation = pendingStepOperation_;
        direction = pendingStepDirection_;
        stepRepeatRequested_ = false;
    }
    try {
        if (operation == StepOperation::Frame) DoStepFrame(direction);
        else DoStepKeyframe(direction);
    } catch (...) {
        ReportError(FFFResult::NativeFailure, "An unhandled media-step exception occurred.", "step");
    }

    try {
        std::lock_guard lock(mutex_);
        if (!terminate_ && stepRepeatRequested_) {
            stepRepeatRequested_ = false;
            commands_.push_back([this] { ProcessStep(); });
        } else {
            stepScheduled_ = false;
        }
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stepScheduled_ = false;
            stepRepeatRequested_ = false;
        }
        ReportError(FFFResult::NativeFailure, "Could not queue the trailing media step.", "step");
    }
    commandCondition_.notify_one();
}

void PlayerSession::DoStepFrame(const std::int32_t direction) {
    if (format_ == nullptr || videoStream_ < 0 || videoDecoder_ == nullptr) return;
    SuspendAudioRenderer(true);
    auto* stream = format_->streams[videoStream_];
    const auto originalPosition = snapshot_.position100ns;
    auto currentPts = snapshot_.framePts;
    if (currentPts == AV_NOPTS_VALUE) {
        currentPts = av_rescale_q(originalPosition + TimelineOrigin100ns(format_),
            AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base);
    }

    // Repeated key messages at the media boundary must not trigger an expensive
    // seek/decode cycle for the same first frame.
    if ((direction < 0 && originalPosition <= 0) ||
        (direction > 0 && snapshot_.duration100ns > 0 &&
            originalPosition >= snapshot_.duration100ns)) {
        SetState(FFF3FPState::Paused, "step-frame");
        Emit(FFF3FPEvent::OperationCompleted,
            "{\"operation\":\"step-frame\",\"direction\":" +
            std::to_string(direction) + ",\"position100ns\":" +
            std::to_string(originalPosition) + ",\"boundary\":true}");
        return;
    }

    std::int64_t targetPosition = 0;
    auto targetPts = AV_NOPTS_VALUE;
    if (direction < 0) {
        auto previous = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), currentPts);
        if (previous != framePtsIndex_.begin()) targetPts = *--previous;
    } else {
        const auto next = std::upper_bound(framePtsIndex_.begin(), framePtsIndex_.end(), currentPts);
        if (next != framePtsIndex_.end()) targetPts = *next;
    }

    if (targetPts != AV_NOPTS_VALUE) {
        targetPosition = StreamTimestampPosition100ns(format_, videoStream_, targetPts);
    } else {
        auto frameQuantum = lastVideoFrameDuration100ns_;
        if (frameQuantum <= 0) {
            const auto frameRate = av_guess_frame_rate(format_, stream, nullptr);
            if (frameRate.num > 0 && frameRate.den > 0) {
                frameQuantum = av_rescale_q(1, av_inv_q(frameRate),
                    AVRational{1, static_cast<int>(TicksPerSecond)});
            }
        }
        if (frameQuantum <= 0) {
            frameQuantum = av_rescale_q(1, stream->time_base,
                AVRational{1, static_cast<int>(TicksPerSecond)});
        }
        frameQuantum = std::max<std::int64_t>(1, frameQuantum);
        targetPosition = originalPosition + direction * frameQuantum;
    }
    // An unknown duration is not an upper bound. Loop-aware image demuxers publish
    // duration100ns == 0 on purpose (see the IsLoopAwareImageDemuxer branch in DoOpen),
    // and clamping to it pinned every forward step back onto frame 0. DoSeek() right
    // below already treats a non-positive duration as "no upper bound".
    const auto stepUpperBound = snapshot_.duration100ns > 0 ? snapshot_.duration100ns : targetPosition;
    targetPosition = std::max<std::int64_t>(0, std::min(targetPosition, stepUpperBound));
    DoSeek(targetPosition);
    DecodeUntilSeekTarget();

    // Rounding a VFR timestamp to 100 ns can select the original frame again.
    // The first decode has populated the local PTS index, so retry its strict
    // predecessor without making the user press the key a second time.
    if (direction < 0 && snapshot_.framePts != AV_NOPTS_VALUE && snapshot_.framePts >= currentPts) {
        auto previous = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), currentPts);
        if (previous != framePtsIndex_.begin()) {
            const auto strictPreviousPts = *--previous;
            const auto strictPreviousPosition = std::max<std::int64_t>(0,
                StreamTimestampPosition100ns(format_, videoStream_, strictPreviousPts));
            DoSeek(strictPreviousPosition);
            DecodeUntilSeekTarget();
        }
    }

    SetState(FFF3FPState::Paused, "step-frame");
    Emit(FFF3FPEvent::OperationCompleted,
        "{\"operation\":\"step-frame\",\"direction\":" +
        std::to_string(direction) + ",\"position100ns\":" +
        std::to_string(snapshot_.position100ns) + "}");
}

FFFResult PlayerSession::StepKeyframe(const std::int32_t direction) noexcept {
    if (DiscStatus() != "{}") return FFFResult::NotSupported;
    if (direction != -1 && direction != 1) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    {
        std::lock_guard lock(snapshotMutex_);
        if ((state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
            state != FFF3FPState::Paused && state != FFF3FPState::Ended) ||
            publishedSnapshot_.selectedVideoStream < 0) return FFFResult::InvalidState;
    }
    return ScheduleStep(StepOperation::Keyframe, direction);
}

void PlayerSession::DoStepKeyframe(const std::int32_t direction) {
    StopSoftwareVideoDecode();
    if (format_ == nullptr || videoStream_ < 0 || videoDecoder_ == nullptr) return;
    SuspendAudioRenderer(true);
    if ((direction < 0 && snapshot_.position100ns <= 0) ||
        (direction > 0 && snapshot_.duration100ns > 0 &&
            snapshot_.position100ns >= snapshot_.duration100ns)) {
        SetState(FFF3FPState::Paused, "step-keyframe");
        Emit(FFF3FPEvent::OperationCompleted,
            "{\"operation\":\"step-keyframe\",\"direction\":" +
            std::to_string(direction) + ",\"position100ns\":" +
            std::to_string(snapshot_.position100ns) + ",\"boundary\":true}");
        return;
    }

    auto* stream = format_->streams[videoStream_];
    auto currentTimestamp = snapshot_.framePts;
    if (currentTimestamp == AV_NOPTS_VALUE) {
        currentTimestamp = av_rescale_q(snapshot_.position100ns + TimelineOrigin100ns(format_),
            AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base);
    }
    const auto searchTimestamp = currentTimestamp + direction;
    const auto flags = direction < 0 ? AVSEEK_FLAG_BACKWARD : 0;
    const auto entryIndex = av_index_search_timestamp(stream, searchTimestamp, flags);
    const auto* entry = entryIndex >= 0 ? avformat_index_get_entry(stream, entryIndex) : nullptr;

    if (entry != nullptr) {
        const auto targetPosition = std::max<std::int64_t>(0,
            StreamTimestampPosition100ns(format_, videoStream_, entry->timestamp));
        DoSeek(targetPosition, -1, false);
    } else {
        if (av_seek_frame(format_, videoStream_, searchTimestamp, flags) < 0) {
            SetState(FFF3FPState::Paused, "step-keyframe");
            return;
        }
        ClearVideoQueue();
        avcodec_flush_buffers(videoDecoder_);
        if (audioDecoder_) avcodec_flush_buffers(audioDecoder_);
        seekTarget100ns_ = -1;
        seekTargetFrame_ = -1;
        keyframeSeekPending_ = true;
        draining_ = false;
        demuxEnded_ = audioDecoderDrained_ = externalAudioDrained_ = audioClockFinished_ = false;
        // 只清内容周期，不清帧率散布：DoStepKeyframe 是同一个文件里的单帧步进，而
        // "恒定/可变"是这个文件的属性，步进一次就把判词退回"还没测出来"是错的。
        // 散布只在媒体身份变化处清（DoOpen / DoClose）。
        lastVideoFrameDuration100ns_ = 0;
        snapshot_.frameIndex = -1;
        PublishSnapshot();
    }
    DecodeUntilSeekTarget();
    SetState(FFF3FPState::Paused, "step-keyframe");
    Emit(FFF3FPEvent::OperationCompleted,
        "{\"operation\":\"step-keyframe\",\"direction\":" +
        std::to_string(direction) + ",\"position100ns\":" +
        std::to_string(snapshot_.position100ns) + "}");
}
FFFResult PlayerSession::SelectVideoStream(const std::int32_t index) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, index] { DoSelectStream(index, true); }); return FFFResult::Success; }
FFFResult PlayerSession::SelectAudioStream(const std::int32_t index) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, index] { DoSelectStream(index, false); }); return FFFResult::Success; }

FFFResult PlayerSession::LoadExternalAudio(const char* path, const std::int32_t index, const std::int64_t offset) noexcept {
    if (DiscStatus() != "{}") return FFFResult::NotSupported;
    const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    std::string normalized, error; if (!NormalizeLocalPath(path, normalized, error)) return FFFResult::InvalidArgument;
    Enqueue([this, value = std::move(normalized), index, offset] { DoLoadExternalAudio(value, index, offset); }); return FFFResult::Success;
}
FFFResult PlayerSession::ClearExternalAudio() noexcept {
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended)
        return FFFResult::InvalidState;
    Enqueue([this] {
        const auto position = state_.load() == FFF3FPState::Playing
            ? ClockPosition() : snapshot_.position100ns;
        if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_);
        CloseFormat(&externalFormat_, externalFormatIo_);
        externalAudioStream_ = -1;
        externalAudioPath_.clear();
        // The offset has to go with the track it belongs to; DoClose resets both, and
        // leaving the snapshot carrying a stale offset makes the host report a delay
        // that no longer applies to any stream.
        externalAudioOffset100ns_ = 0;
        snapshot_.externalAudioOffset100ns = 0;
        snapshot_.isExternalAudio = 0;
        // Main-container audio packets were intentionally skipped while the
        // external track was active. A full seek is required to put demux,
        // video, audio and the resampler back on one timeline.
        DoSeek(position);
        Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"clear-external-audio\"}");
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetExternalAudioOffset(const std::int64_t offset) noexcept { const auto state = state_.load(); if (state != FFF3FPState::Ready && state != FFF3FPState::Playing && state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState; Enqueue([this, offset] { externalAudioOffset100ns_ = offset; snapshot_.externalAudioOffset100ns = offset; if (externalFormat_) DoSeek(snapshot_.position100ns); else PublishSnapshot(); }); return FFFResult::Success; }

FFFResult PlayerSession::SetAudioDelay(const std::int64_t delay) noexcept {
    // ±10 minutes: larger values are almost certainly caller bugs, and the
    // renderer's silence-insertion alignment was never designed for wider gaps.
    if (delay < -6000000000LL || delay > 6000000000LL) return FFFResult::InvalidArgument;
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this, delay] {
        audioDelay100ns_ = delay;
        snapshot_.audioDelay100ns = delay;
        // Frames already queued carry positions computed with the old delay, so re-seek:
        // that flushes them and re-demuxes from the current position -- the same repair
        // SetExternalAudioOffset makes when an external track owns the audio, except that
        // here it is unconditional because there is always an internal timeline to move.
        // This is the only writer of the member and DoClose never touches it,
        // which is what the header's "survives Close/Open" claim rests on.
        DoSeek(snapshot_.position100ns);
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetPresentConfig(const bool enableTearing) noexcept {
    // Pacing preference: safe in any state; the renderer applies it on the next
    // present and remembers the request across device/chain re-creation.
    Enqueue([this, enableTearing] { videoRenderer_.SetPresentConfig(enableTearing); });
    return FFFResult::Success;
}
FFFResult PlayerSession::GetPresentPacingStatus(
    FFF3FPPresentPacingStatus& status) noexcept {
    // Same contract as GetRenderTargetInfo: the caller declares the layout it allocated by
    // size and version, and nothing is written past it.
    if (status.size < sizeof(FFF3FPPresentPacingStatus) || status.version != 20)
        return FFFResult::InvalidArgument;
    const auto pacing = videoRenderer_.QueryPresentPacing();
    status.size = sizeof(status);
    status.version = 20;
    status.declaredRefreshHz = pacing.declaredRefreshHz;
    status.measuredRefreshHz = pacing.measuredRefreshHz;
    status.frameRateBoundHz = pacing.frameRateBoundHz;
    status.gridLocked = pacing.gridLocked;
    status.pacingBypass = pacing.pacingBypass;
    status.lastSlotError100ns = pacing.lastSlotError100ns;
    status.holdSampled = pacing.holdSampled;
    status.holdWithinRounding = pacing.holdWithinRounding;
    status.holdWorstDeviationRefreshes = pacing.holdWorstDeviationRefreshes;
    status.neverCompositedPresents = pacing.neverCompositedPresents;
    status.displayRateDiscardedFrames = displayRateDiscardedFrames_.load(std::memory_order_relaxed);
    return FFFResult::Success;
}
FFFResult PlayerSession::SetColorMode(const FFF3FPColorMode mode, const float sdr, const float hdr,
    const float paper, const bool forceHdrOutput) noexcept {
    if (mode > FFF3FPColorMode::MapToHdr || !std::isfinite(sdr) || sdr <= 0 ||
        !std::isfinite(hdr) || hdr < 0 || hdr > 10000 || !std::isfinite(paper) || paper <= 0)
        return FFFResult::InvalidArgument;
    Enqueue([this, mode, sdr, hdr, paper, forceHdrOutput] {
        // Preserve intent until stream classification is available; the renderer
        // decides whether HDR/wide-gamut presentation is achievable.
        snapshot_.requestedColorMode = mode;
        const auto previous = snapshot_.actualColorMode;
        const auto result = videoRenderer_.SetColorMode(
            mode, sdr, hdr, paper, forceHdrOutput);
        if (result != FFFResult::Success) {
            if (result == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return;
            Fail(result, "The color output configuration is invalid.", "color-mode");
            return;
        }
        const auto redrawResult = videoRenderer_.Redraw();
        if (redrawResult != FFFResult::Success) {
            if (redrawResult == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return;
            Fail(FFFResult::DeviceFailure, videoRenderer_.LastError(), "redraw"); return;
        }
        snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
        PublishSnapshot();
        const auto reason = videoRenderer_.FallbackReason();
        std::ostringstream json;
        json << "{\"requested\":" << static_cast<unsigned>(mode)
            << ",\"actual\":" << static_cast<unsigned>(snapshot_.actualColorMode)
            << ",\"reason\":\"" << EscapeJson(reason) << "\"}";
        if (previous != snapshot_.actualColorMode || mode != snapshot_.actualColorMode)
            Emit(FFF3FPEvent::ColorModeChanged, json.str());
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetOutputWindow(void* window) noexcept {
    if (window != nullptr && !IsWindow(static_cast<HWND>(window))) return FFFResult::InvalidArgument;
    Enqueue([this, window] {
        const auto hadOutputWindow = videoRenderer_.HasOutputWindow();
        const auto result = videoRenderer_.SetWindow(static_cast<HWND>(window));
        if (result != FFFResult::Success) {
            Fail(result, "The playback window handle is invalid.", "output-window");
            return;
        }
        // Audio media is opened before the controller binds its final HWND.
        // Headless Render intentionally owns no GPU cache, so submit the retained
        // attached picture once when a real target first becomes available.
        const auto newlyVisibleVideo = !hadOutputWindow && window != nullptr &&
            videoStream_ >= 0 && state_.load() == FFF3FPState::Playing;
        if (window == nullptr) ReleaseAudioWithoutVideo();
        if (newlyVisibleVideo) {
            // A temporary headless play can occur while a WinForms host is
            // rebuilding its HWND. Bind the audio boundary to the first frame
            // of the newly visible target instead of leaking audio into black.
            const auto position = ClockPosition();
            ArmAudioUntilVideoFrame();
            ResetClock(position);
            ApplyAudioPlaybackPause(true);
        }
        // 候选会话无窗口时只保留解码帧；绑定窗口后必须先上传静态图片，才能重绘。
        // 普通视频在没有缓存帧时不能只靠 Redraw()：headless 打开期间解出的帧被
        // 刻意丢弃（Renderer 不替无窗会话保管 GPU 缓存），Redraw 此时必然空转，
        // 于是一个暂停/就绪态的新会话永远等不到首帧——换解码器、切文件后黑屏
        // 就是这么来的。改用 PresentAfterBind() 把交换链建起来并呈交一帧。
        const auto redrawResult = staticImage_ && stillImageFrame_ != nullptr && window != nullptr
            ? videoRenderer_.Render(stillImageFrame_, snapshot_.frameIndex, true, false, false, true)
            : coverArtFrame_ != nullptr && videoStream_ < 0 && window != nullptr
                ? videoRenderer_.Render(coverArtFrame_, HdrProcessor::kUnknownFrameIndex, true, true)
                : window != nullptr && videoStream_ >= 0 && !videoRenderer_.HasCachedVideo()
                    ? videoRenderer_.PresentAfterBind()
                    : videoRenderer_.Redraw();
        if (redrawResult == FFFResult::DeviceFailure &&
            videoRenderer_.RequestRecoveryIfDeviceLost()) return;
        if (redrawResult != FFFResult::Success) {
            Fail(FFFResult::DeviceFailure, videoRenderer_.LastError(), "redraw");
            return;
        }
        if (newlyVisibleVideo && videoRenderer_.SubmittedVideoGeneration() != 0) {
            audioUnblockVideoGeneration_ = videoRenderer_.SubmittedVideoGeneration();
            if (!PresentAudioBoundary()) return;
        }
    });
    return FFFResult::Success;
}

FFFResult PlayerSession::SetInteractiveMove(const bool enabled) noexcept {
    interactiveMove_.store(enabled, std::memory_order_release);
    videoRenderer_.SetInteractiveMove(enabled);
    return FFFResult::Success;
}

FFFResult PlayerSession::SetViewTransform(const float zoom, const float panX,
    const float panY) noexcept {
    if (!std::isfinite(zoom) || zoom <= 0.0f || !std::isfinite(panX) || !std::isfinite(panY))
        return FFFResult::InvalidArgument;
    // Atomic view updates bypass decode work and wake the presenter directly.
    // Disc playback owns a separate view path.
    if (discOpened_.load(std::memory_order_acquire)) return FFFResult::Success;
    const auto result = videoRenderer_.SetViewTransform(zoom, panX, panY);
    if (result != FFFResult::Success) return result;
    const auto redrawResult = videoRenderer_.Redraw();
    if (redrawResult != FFFResult::Success &&
        redrawResult != FFFResult::InvalidState &&
        videoRenderer_.RequestRecoveryIfDeviceLost()) return redrawResult;
    return FFFResult::Success;
}
FFFResult PlayerSession::ZoomViewAt(const float factor,
    const float anchorX, const float anchorY, float* const resultingZoom) noexcept {
    if (!std::isfinite(factor) || factor <= 0.0f ||
        !std::isfinite(anchorX) || !std::isfinite(anchorY))
        return FFFResult::InvalidArgument;
    if (discOpened_.load(std::memory_order_acquire)) return FFFResult::Success;
    const auto result = videoRenderer_.ZoomViewAt(factor, anchorX, anchorY, resultingZoom);
    if (result != FFFResult::Success) return result;
    // Redraw immediately for the same reason SetViewTransform does: a wheel step that
    // waits on the decode queue feels laggy.
    const auto redrawResult = videoRenderer_.Redraw();
    if (redrawResult != FFFResult::Success &&
        redrawResult != FFFResult::InvalidState &&
        videoRenderer_.RequestRecoveryIfDeviceLost()) return redrawResult;
    return FFFResult::Success;
}

FFFResult PlayerSession::PanView(const float deltaX, const float deltaY) noexcept {
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return FFFResult::InvalidArgument;
    if (discOpened_.load(std::memory_order_acquire)) return FFFResult::Success;
    const auto result = videoRenderer_.PanView(deltaX, deltaY);
    if (result != FFFResult::Success) return result;
    const auto redrawResult = videoRenderer_.Redraw();
    if (redrawResult != FFFResult::Success && redrawResult != FFFResult::InvalidState &&
        videoRenderer_.RequestRecoveryIfDeviceLost()) return redrawResult;
    return FFFResult::Success;
}

FFFResult PlayerSession::ImageZoom(float& zoom, float& minimum, float& maximum) const noexcept {
    return videoRenderer_.ImageZoom(zoom, minimum, maximum);
}

void PlayerSession::ViewTransform(float& zoom, float& panX, float& panY) const noexcept {
    videoRenderer_.ViewTransform(zoom, panX, panY);
}

FFFResult PlayerSession::SetViewRotation(
    const std::uint32_t quarterTurnsClockwise) noexcept {
    if (quarterTurnsClockwise > 3) return FFFResult::InvalidArgument;
    // Same rationale as SetViewTransform above: the rotation is a single atomic
    // inside the renderer, and queuing it behind decode would make the key
    // repeat feel unresponsive. Disc playback has its own geometry path.
    if (discOpened_.load(std::memory_order_acquire)) return FFFResult::Success;
    const auto result = videoRenderer_.SetViewRotation(quarterTurnsClockwise);
    if (result != FFFResult::Success) return result;
    // Present again now: while paused there is no incoming frame to pick the new
    // orientation up, and the whole point of rotating a still image is that it
    // must turn immediately.
    const auto redrawResult = videoRenderer_.Redraw();
    if (redrawResult != FFFResult::Success &&
        redrawResult != FFFResult::InvalidState &&
        videoRenderer_.RequestRecoveryIfDeviceLost()) return redrawResult;
    return FFFResult::Success;
}

std::uint32_t PlayerSession::ViewRotation() const noexcept {
    return videoRenderer_.ViewRotation();
}

FFFResult PlayerSession::SetFitLimitToNative(const bool enable) noexcept {
    // Disc playback owns its own geometry (see SetViewTransform above), so refuse
    // rather than report a Success that changes nothing.
    if (discOpened_.load(std::memory_order_acquire)) return FFFResult::NotSupported;
    const auto result = videoRenderer_.SetFitLimitToNative(enable);
    if (result != FFFResult::Success) return result;
    // Present again now: while paused there is no incoming frame to pick the new
    // geometry up, and the App reads back Dest* right after this call.
    const auto redrawResult = videoRenderer_.Redraw();
    if (redrawResult != FFFResult::Success &&
        redrawResult != FFFResult::InvalidState &&
        videoRenderer_.RequestRecoveryIfDeviceLost()) return redrawResult;
    return FFFResult::Success;
}
FFFResult PlayerSession::Set360View(const bool enabled, const float yaw,
    const float pitch, const float fovY) noexcept {
    if (enabled && DiscStatus() != "{}") return FFFResult::NotSupported;
    if (!std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(fovY) || fovY <= 0.0f)
        return FFFResult::InvalidArgument;
    // Camera motion is high-frequency input. Publish it directly to the
    // renderer's atomic view state; queuing one command per mouse/timer sample
    // causes visible latency when the playback worker is busy decoding.
    return videoRenderer_.Set360View(enabled, yaw, pitch, fovY);
}
FFFResult PlayerSession::RecreateAudioRenderer(const std::wstring& endpointId,
    const bool exclusive, const bool paused, std::string& error) noexcept {
    try {
        audioRenderer_.reset();
        auto replacement = std::make_unique<PlayerWasapiRenderer>(endpointId, exclusive,
            &audioRuntimeState_, [this] { NotifyAudioRestart(); });
        const auto result = replacement->Start();
        if (result != FFFResult::Success) {
            error = replacement->LastError();
            return result;
        }
        replacement->SetVolume(volume_, muted_);
        replacement->SetPaused(paused ||
            (state_.load() == FFF3FPState::Playing && videoStream_ >= 0));
        {
            // The installed plug outlives the renderer it was installed into: a
            // device switch, an exclusive-mode fallback or a suspend/resume all
            // rebuild the renderer, and the bed owner must not silently vanish with
            // it. The context is borrowed, never owned here.
            std::lock_guard lock(audioPlugMutex_);
            if (audioPlugInstalled_) replacement->SetAudioRenderPlug(&audioPlug_);
        }
        audioRenderer_ = std::move(replacement);
        // Floor latch: the pool may never target less than the buffer WASAPI
        // actually granted (a smaller target could not fill it). On a fresh
        // open the member was just reset to the default, so this is the whole
        // latch; on a mid-round device switch a risen target survives.
        audioTargetBuffer100ns_ = std::max(audioTargetBuffer100ns_,
            audioRenderer_->DeviceBufferDuration100ns());
        return FFFResult::Success;
    } catch (...) {
        error = "Could not allocate a replacement WASAPI renderer.";
        return FFFResult::NativeFailure;
    }
}

void PlayerSession::SuspendAudioRenderer(const bool releaseExclusive) noexcept {
    if (!audioRenderer_) return;
    audioRenderer_->SetPaused(true);
    if (releaseExclusive && audioExclusive_) {
        audioRenderer_->Stop();
        audioRenderer_.reset();
        audioRuntimeState_.ClearValues();
    }
}

FFFResult PlayerSession::ResumeAudioRenderer() noexcept {
    if (audioRenderer_ || (audioDecoder_ == nullptr && externalAudioDecoder_ == nullptr))
        return FFFResult::Success;
    const auto position = std::max<std::int64_t>(0, snapshot_.position100ns);
    std::string error;
    auto result = RecreateAudioRenderer(audioEndpointId_, audioExclusive_, true, error);
    auto exclusiveError = std::string{};
    bool fellBackToShared = false;
    if (result != FFFResult::Success && audioExclusive_) {
        exclusiveError = std::move(error);
        result = RecreateAudioRenderer(audioEndpointId_, false, true, error);
        if (result == FFFResult::Success) {
            audioExclusive_ = false;
            fellBackToShared = true;
        }
    }
    if (result != FFFResult::Success) {
        // A system-level exclusive owner also blocks shared Initialize. Keep
        // the media session playable on its video/QPC clock and remember the
        // user's effective mode as shared instead of failing the whole file.
        audioExclusive_ = false;
        const auto reason = error.empty()
            ? std::string("The Windows audio output device is currently unavailable.")
            : std::move(error);
        RebuildMediaInfo();
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"audio\",\"exclusive\":false,\"audioUnavailable\":true,\"reason\":\"" +
            EscapeJson(reason + " The media continued in shared mode without audio output.") + "\"}");
        return FFFResult::Success;
    }
    DoSeek(position);
    RebuildMediaInfo();
    if (fellBackToShared) {
        const auto reason = exclusiveError.empty()
            ? std::string("The audio endpoint is already in use or rejected exclusive mode.")
            : std::move(exclusiveError);
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"audio\",\"exclusive\":false,\"exclusiveFallback\":true,\"reason\":\"" +
            EscapeJson(reason + " Playback continued in shared mode.") + "\"}");
    }
    return FFFResult::Success;
}

bool PlayerSession::RecoverAudioDevice() noexcept {
    if (!audioRenderer_ || !audioRenderer_->RestartRequested()) return false;
    const auto paused = snapshot_.state != FFF3FPState::Playing;
    const auto position = std::max<std::int64_t>(0, ClockPosition());
    auto reason = audioRenderer_->LastError();
    std::string error;
    auto result = RecreateAudioRenderer(audioEndpointId_, audioExclusive_, paused, error);
    auto exclusiveError = std::string{};
    bool fellBackToShared = false;
    if (result != FFFResult::Success && audioExclusive_) {
        exclusiveError = std::move(error);
        result = RecreateAudioRenderer(audioEndpointId_, false, paused, error);
        if (result == FFFResult::Success) {
            audioExclusive_ = false;
            fellBackToShared = true;
        }
    }
    if (result != FFFResult::Success) {
        Fail(result, error.empty() ? "Could not reopen the Windows audio output device." :
            std::move(error), "audio-device-recovery");
        return true;
    }
    snapshot_.position100ns = position;
    DoSeek(position);
    RebuildMediaInfo();
    Emit(FFF3FPEvent::DeviceChanged,
        "{\"type\":\"audio\",\"recovered\":true,\"exclusive\":" +
        std::string(audioExclusive_ ? "true" : "false") + ",\"reason\":\"" +
        EscapeJson(reason.empty() ? "The Windows audio output device changed." : reason) + "\"}");
    if (fellBackToShared) {
        ReportError(FFFResult::DeviceFailure,
            exclusiveError.empty() ?
                "The new audio device does not support the active exclusive format; playback continued in shared mode." :
                exclusiveError + " Playback continued in shared mode.",
            "audio-device-recovery");
    }
    return true;
}

bool PlayerSession::RecoverVideoDevice() noexcept {
    if (!videoRenderer_.DeviceRecoveryRequested()) return false;
    StopSoftwareVideoDecode();

    const auto previousState = state_.load();
    const auto wasPlaying = previousState == FFF3FPState::Playing;
    const auto resumePosition = std::max<std::int64_t>(0,
        wasPlaying ? ClockPosition() : snapshot_.position100ns);
    auto redrawPosition = resumePosition;
    if (previousState == FFF3FPState::Ended && format_ != nullptr && videoStream_ >= 0 &&
        snapshot_.framePts != AV_NOPTS_VALUE) {
        redrawPosition = std::max<std::int64_t>(0,
            StreamTimestampPosition100ns(format_, videoStream_, snapshot_.framePts));
    }
    const auto reason = videoRenderer_.LastError();
    SuspendAudioRenderer(false);

    ClearVideoQueue();
    if (videoDecodeFrame_ != nullptr) av_frame_unref(videoDecodeFrame_);
    if (videoTransferFrame_ != nullptr) av_frame_unref(videoTransferFrame_);
    if (videoDecoder_ != nullptr) avcodec_free_context(&videoDecoder_);

    FFFResult result = FFFResult::DeviceFailure;
    {
        // SetTimedTextLayer can run outside the session queue. Keep it from
        // restarting the presenter while the old device is being dismantled.
        std::unique_lock contentLock(timedTextContentMutex_);
        constexpr int MaximumRecreateAttempts = 20;
        for (int attempt = 0; attempt < MaximumRecreateAttempts && !terminate_; ++attempt) {
            result = videoRenderer_.RecreateDeviceResources();
            if (result == FFFResult::Success) break;
            Sleep(attempt == 0 ? 50 : 200);
        }
    }
    if (terminate_) return true;
    if (result != FFFResult::Success) {
        Fail(result, videoRenderer_.LastError().empty()
            ? "Could not recreate the D3D11 playback device." : videoRenderer_.LastError(),
            "video-device-recovery");
        return true;
    }

    bool hardwareFallback = false;
    std::string hardwareFailureReason;
    if (format_ != nullptr && videoStream_ >= 0) {
        // The device is new, so a stream the GPU refused before is worth asking about
        // again -- unless the refusal was about the stream rather than the device, in
        // which case it is remembered and the attempt is skipped here too.
        const auto tryHardware = decodeMode_ == FFF3FPDecodeMode::D3D11 &&
            !HardwareRouteKnownUnsupported(format_->streams[videoStream_]);
        if (tryHardware) {
            result = OpenHardwareVideoDecoder(format_, videoStream_, &videoDecoder_,
                &hardwareFailureReason);
            if (result != FFFResult::Success) {
                result = OpenDecoder(format_, videoStream_, true, &videoDecoder_,
                    -1, nullptr, false);
                hardwareFallback = result == FFFResult::Success;
            }
        } else {
            result = OpenDecoder(format_, videoStream_, true, &videoDecoder_,
                -1, nullptr, false);
            if (result == FFFResult::Success && decodeMode_ == FFF3FPDecodeMode::D3D11) {
                hardwareFallback = true;
                hardwareFailureReason = "The GPU decoder was rejected for this stream earlier in this session; playback is using CPU decoding.";
            }
        }
        if (result != FFFResult::Success) {
            Fail(result, "Could not reopen the video decoder after rebuilding the graphics device.",
                "video-device-recovery");
            return true;
        }
        snapshot_.decodeMode = decodeMode_ == FFF3FPDecodeMode::D3D11 && !hardwareFallback
            ? FFF3FPDecodeMode::Gpu : FFF3FPDecodeMode::Cpu;
        if (staticImage_ && stillImageFrame_ != nullptr && IsHardwareFrame(stillImageFrame_)) {
            // The retained still names surfaces owned by the device that was just lost,
            // so it cannot be redrawn. Drop it and decode the picture again below.
            av_frame_unref(stillImageFrame_);
        }
        if (staticImage_ && stillImageFrame_ == nullptr) {
            const auto* stream = format_->streams[videoStream_];
            const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
            if (av_seek_frame(format_, videoStream_, start, AVSEEK_FLAG_BACKWARD) >= 0)
                avcodec_flush_buffers(videoDecoder_);
            const auto decodeResult = DecodeInitialFrame();
            if (decodeResult != FFFResult::Success) {
                Fail(decodeResult, "Could not re-decode the still image after rebuilding the graphics device.",
                    "video-device-recovery");
                return true;
            }
        } else if (staticImage_ && stillImageFrame_ != nullptr) {
            // imageMode selects the still-image scaling policy: enlarge by pixel
            // blocks, which keeps a picture's own pixel grid crisp, instead of the
            // smooth interpolation video wants.
            const auto renderResult = videoRenderer_.Render(stillImageFrame_,
                HdrProcessor::kUnknownFrameIndex, true, false, false, true);
            if (renderResult != FFFResult::Success) {
                if (renderResult == FFFResult::DeviceFailure &&
                    videoRenderer_.RequestRecoveryIfDeviceLost()) return true;
                Fail(renderResult, videoRenderer_.LastError(), "video-device-recovery");
                return true;
            }
        } else {
            DoSeek(redrawPosition);
            DecodeUntilSeekTarget();
        }
        if (videoRenderer_.DeviceRecoveryRequested() || state_.load() == FFF3FPState::Failed)
            return true;
    } else if (coverArtFrame_ != nullptr) {
        const auto renderResult = videoRenderer_.Render(coverArtFrame_, HdrProcessor::kUnknownFrameIndex, true, true);
        if (renderResult != FFFResult::Success) {
            if (renderResult == FFFResult::DeviceFailure &&
                videoRenderer_.RequestRecoveryIfDeviceLost()) return true;
            Fail(renderResult, videoRenderer_.LastError(), "video-device-recovery");
            return true;
        }
    }

    if (previousState == FFF3FPState::Ended) {
        snapshot_.position100ns = resumePosition;
        ResetClock(resumePosition);
    }
    ApplyAudioPlaybackPause(wasPlaying);
    RebuildMediaInfo();
    PublishSnapshot();
    std::ostringstream json;
    json << "{\"type\":\"video\",\"recovered\":true,\"decodeMode\":"
         << static_cast<unsigned>(snapshot_.decodeMode)
         << ",\"hardwareFallback\":" << (hardwareFallback ? "true" : "false")
         << ",\"reason\":\"" << EscapeJson(hardwareFallback && !hardwareFailureReason.empty()
            ? hardwareFailureReason
            : (reason.empty() ? "The Windows graphics device changed." : reason)) << "\"}";
    Emit(FFF3FPEvent::DeviceChanged, json.str());
    return true;
}

FFFResult PlayerSession::SetAudioEndpoint(const char* endpoint) noexcept {
    const auto value = endpoint == nullptr ? std::wstring{} : FromUtf8(endpoint);
    Enqueue([this, value] {
        if (!audioRenderer_) {
            audioEndpointId_ = value;
            return;
        }
        const auto previousEndpoint = audioEndpointId_;
        const auto paused = snapshot_.state != FFF3FPState::Playing;
        const auto position = std::max<std::int64_t>(0, ClockPosition());
        std::string error;
        const auto result = RecreateAudioRenderer(value, audioExclusive_, paused, error);
        if (result != FFFResult::Success) {
            std::string restoreError;
            if (RecreateAudioRenderer(previousEndpoint, audioExclusive_, paused, restoreError) ==
                FFFResult::Success) {
                DoSeek(position);
                RebuildMediaInfo();
                ReportError(result, error.empty() ? "Could not open the selected audio endpoint." :
                    std::move(error), "audio-endpoint");
            } else {
                Fail(result, restoreError.empty() ? "Could not restore the previous audio endpoint." :
                    std::move(restoreError), "audio-endpoint");
            }
            return;
        }
        audioEndpointId_ = value;
        snapshot_.position100ns = position;
        DoSeek(position);
        RebuildMediaInfo();
        Emit(FFF3FPEvent::DeviceChanged,
            std::string("{\"type\":\"audio\",\"default\":") +
            (audioEndpointId_.empty() ? "true}" : "false}"));
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetAudioExclusiveMode(const bool exclusive) noexcept {
    const auto state = state_.load();
    if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
        state != FFF3FPState::Paused && state != FFF3FPState::Ended) return FFFResult::InvalidState;
    Enqueue([this, exclusive] {
        if (audioExclusive_ == exclusive) {
            if (exclusive && snapshot_.state != FFF3FPState::Playing)
                SuspendAudioRenderer(true);
            Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
                (exclusive ? "true}" : "false}"));
            return;
        }
        if (exclusive && snapshot_.state != FFF3FPState::Playing) {
            // Exclusive mode is a preference while paused. Do not reserve the
            // endpoint until playback actually starts.
            audioExclusive_ = true;
            SuspendAudioRenderer(true);
            Emit(FFF3FPEvent::DeviceChanged,
                "{\"type\":\"audio\",\"exclusive\":true,\"releasedWhilePaused\":true}");
            return;
        }
        if (!audioRenderer_) {
            audioExclusive_ = exclusive;
            Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
                (exclusive ? "true}" : "false}"));
            return;
        }
        const auto paused = snapshot_.state != FFF3FPState::Playing;
        const auto position = std::max<std::int64_t>(0, ClockPosition());
        snapshot_.position100ns = position;
        std::string message;
        const auto result = RecreateAudioRenderer(audioEndpointId_, exclusive, paused, message);
        if (result != FFFResult::Success) {
            // Restore the previous renderer so an unsupported exclusive format
            // cannot turn a working session into a silent session.
            std::string restoreError;
            if (RecreateAudioRenderer(audioEndpointId_, audioExclusive_, paused, restoreError) ==
                FFFResult::Success) {
                DoSeek(position);
                RebuildMediaInfo();
            } else {
                Fail(result, restoreError.empty() ? "Could not restore shared-mode audio playback." :
                    std::move(restoreError), "audio-exclusive-mode");
                return;
            }
            if (exclusive) {
                const auto reason = message.empty()
                    ? std::string("The selected endpoint rejected exclusive mode.")
                    : std::move(message);
                Emit(FFF3FPEvent::DeviceChanged,
                    "{\"type\":\"audio\",\"exclusive\":false,\"exclusiveFallback\":true,\"reason\":\"" +
                    EscapeJson(reason + " Playback continued in shared mode.") + "\"}");
            } else {
                Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
                    (audioExclusive_ ? "true}" : "false}"));
                ReportError(result, message.empty() ? "The selected endpoint rejected shared mode." : message,
                    "audio-exclusive-mode");
            }
            return;
        }
        audioExclusive_ = exclusive;
        DoSeek(position);
        RebuildMediaInfo();
        Emit(FFF3FPEvent::DeviceChanged, std::string("{\"type\":\"audio\",\"exclusive\":") +
            (exclusive ? "true}" : "false}"));
    });
    return FFFResult::Success;
}
FFFResult PlayerSession::SetPlaybackRate(const double rate) noexcept {
    if (!std::isfinite(rate) || rate < 0.1 || rate > 16.0) return FFFResult::InvalidArgument;
    Enqueue([this, rate] {
        const auto position = state_.load() == FFF3FPState::Playing ? ClockPosition() : snapshot_.position100ns;
        playbackRate_ = rate;
        DoSeek(position);
    });
    return FFFResult::Success;
}
void PlayerSession::ResetTempo(const std::int64_t position) noexcept {
    if (tempoGraph_) avfilter_graph_free(&tempoGraph_);
    tempoInput_ = nullptr; tempoOutput_ = nullptr; tempoSamples_ = 0; tempoOrigin_ = position;
}
FFFResult PlayerSession::TempoAudio(AVFrame* frame, const std::int64_t position, const std::int64_t timestampResolution) noexcept {
    const double rate = playbackRate_.load();
    const bool passthrough = rate == 1.0 && !equalizerEnabled_ && waveVolume_ == 1;
    // A flush request (frame == nullptr) in the pass-through state is a no-op: there
    // is nothing buffered anywhere, and the renderer's Enqueue treats a null frame
    // as an invalid argument rather than a flush, so never forward it.
    if (passthrough && !frame) return FFFResult::Success;
    if (passthrough) return audioRenderer_->Enqueue(frame, position, timestampResolution);
    if (!tempoGraph_) {
        if (!frame) return FFFResult::Success;
        if(position!=AV_NOPTS_VALUE)tempoOrigin_=std::max(tempoOrigin_,position);
        // Build into locals and publish only on success. A half-configured graph left
        // in tempoGraph_ would make every later call skip the rebuild (the !tempoGraph_
        // gate above) and fail on each frame, which then disables the whole audio track
        // via DisableFailedInternalAudio until the next seek or stream switch.
        AVFilterGraph* graph = avfilter_graph_alloc();
        if (!graph) return FFFResult::NativeFailure;
        const auto abandon = [&graph]() noexcept { avfilter_graph_free(&graph); return FFFResult::FfmpegFailure; };
        AVFilterContext* graphInput = nullptr;
        AVFilterContext* graphSink = nullptr;
        char layout[128]{};AVChannelLayout channels{};
        if(frame->ch_layout.order==AV_CHANNEL_ORDER_UNSPEC)av_channel_layout_default(&channels,frame->ch_layout.nb_channels);
        else if(av_channel_layout_copy(&channels,&frame->ch_layout) < 0){av_channel_layout_uninit(&channels);return abandon();}
        if(av_channel_layout_describe(&channels,layout,sizeof(layout)) < 0){av_channel_layout_uninit(&channels);return abandon();}
        av_channel_layout_uninit(&channels);
        std::ostringstream args;
        args << "time_base=1/" << frame->sample_rate << ":sample_rate=" << frame->sample_rate
             << ":sample_fmt=" << av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)) << ":channel_layout=" << layout;
        if (avfilter_graph_create_filter(&graphInput, avfilter_get_by_name("abuffer"), "input", args.str().c_str(), nullptr, graph) < 0 ||
            avfilter_graph_create_filter(&graphSink, avfilter_get_by_name("abuffersink"), "output", nullptr, nullptr, graph) < 0) return abandon();
        AVFilterContext* previous = graphInput;
        double remainder = rate; int index = 0;
        do {
            const auto factor = std::clamp(remainder, 0.5, 2.0);
            AVFilterContext* filter = nullptr;
            const auto name = "tempo" + std::to_string(index++);
            const auto factorText = std::to_string(factor);
            if (avfilter_graph_create_filter(&filter, avfilter_get_by_name("atempo"), name.c_str(), factorText.c_str(), nullptr, graph) < 0 ||
                avfilter_link(previous, 0, filter, 0) < 0) return abandon();
            previous = filter; remainder /= factor;
        } while (std::abs(remainder - 1.0) > 0.00001);
        if (equalizerEnabled_) {
            constexpr int frequencies[]{60,170,310,600,1000,3000,6000,12000,14000,16000};
            for (int band=0;band<10;++band) {
                if (frequencies[band]>=frame->sample_rate/2) continue;
                AVFilterContext* filter=nullptr;
                const auto name="eq"+std::to_string(band);
                const auto options="f="+std::to_string(frequencies[band])+":t=o:w=1:g="+std::to_string(equalizerGains_[band]);
                if(avfilter_graph_create_filter(&filter,avfilter_get_by_name("equalizer"),name.c_str(),options.c_str(),nullptr,graph)<0 || avfilter_link(previous,0,filter,0)<0)return abandon();
                previous=filter;
            }
        }
        if(waveVolume_!=1) {
            AVFilterContext* filter=nullptr;const auto options=std::to_string(waveVolume_);
            if(avfilter_graph_create_filter(&filter,avfilter_get_by_name("volume"),"wave",options.c_str(),nullptr,graph)<0 || avfilter_link(previous,0,filter,0)<0)return abandon();
            previous=filter;
        }
        if (avfilter_link(previous, 0, graphSink, 0) < 0 || avfilter_graph_config(graph, nullptr) < 0) return abandon();
        tempoGraph_ = graph;
        tempoInput_ = graphInput;
        tempoOutput_ = graphSink;
    }
    AVFrame* input = frame ? av_frame_clone(frame) : nullptr;
    if (frame && !input) return FFFResult::NativeFailure;
    if (input) {
        if(input->ch_layout.order==AV_CHANNEL_ORDER_UNSPEC){const int count=input->ch_layout.nb_channels;av_channel_layout_uninit(&input->ch_layout);av_channel_layout_default(&input->ch_layout,count);}
        const auto skipped = position != AV_NOPTS_VALUE && position < tempoOrigin_
            ? std::min<std::int64_t>(input->nb_samples, av_rescale_rnd(tempoOrigin_ - position, input->sample_rate, 10000000, AV_ROUND_UP)) : 0;
        if (skipped >= input->nb_samples) { av_frame_free(&input); return FFFResult::Success; }
        if (skipped) {
            const auto format = static_cast<AVSampleFormat>(input->format);
            const auto planar = av_sample_fmt_is_planar(format);
            const auto planes = planar ? input->ch_layout.nb_channels : 1;
            const auto offset = skipped * av_get_bytes_per_sample(format) * (planar ? 1 : input->ch_layout.nb_channels);
            for (int plane = 0; plane < planes; ++plane) { input->extended_data[plane] += offset; if (plane < AV_NUM_DATA_POINTERS) input->data[plane] = input->extended_data[plane]; }
            input->nb_samples -= static_cast<int>(skipped);
        }
        input->pts = position == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : av_rescale_q(position - tempoOrigin_, AVRational{1, 10000000}, AVRational{1, frame->sample_rate}) + skipped;
    }
    const auto sent = av_buffersrc_add_frame_flags(tempoInput_, input, AV_BUFFERSRC_FLAG_KEEP_REF);
    av_frame_free(&input);
    if (sent < 0) return FFFResult::FfmpegFailure;
    AVFrame* output = av_frame_alloc();
    if (!output) return FFFResult::NativeFailure;
    FFFResult result = FFFResult::Success;
    while (av_buffersink_get_frame(tempoOutput_, output) >= 0) {
        const auto outputPosition = tempoOrigin_ + av_rescale(tempoSamples_, 10000000, output->sample_rate);
        result = audioRenderer_->Enqueue(output, outputPosition, 0);
        tempoSamples_ += output->nb_samples;
        av_frame_unref(output);
        if (result != FFFResult::Success) break;
    }
    av_frame_free(&output); return result;
}
FFFResult PlayerSession::SetAudioEffects(const bool enabled,const float* gains,const float wave) noexcept {
    if(!gains || !std::isfinite(wave) || wave<0 || wave>1)return FFFResult::InvalidArgument;
    std::array<float,10> values{};for(int i=0;i<10;++i){if(!std::isfinite(gains[i]) || gains[i]<-12 || gains[i]>12)return FFFResult::InvalidArgument;values[i]=gains[i];}
    Enqueue([this,enabled,values,wave]{
        const bool reuse=tempoGraph_ && equalizerEnabled_==enabled && waveVolume_==wave;
        equalizerEnabled_=enabled;waveVolume_=wave;
        for(int i=0;i<10;++i)equalizerGains_[i]=values[i];
        if(reuse) {
            bool updated=true;
            for(int i=0;enabled && i<10;++i){const auto name="eq"+std::to_string(i);if(!avfilter_graph_get_filter(tempoGraph_,name.c_str()))continue;const auto gain=std::to_string(values[i]);if(avfilter_graph_send_command(tempoGraph_,name.c_str(),"gain",gain.c_str(),nullptr,0,0)<0)updated=false;}
            if(updated)return;
        }
        ResetTempo(audioRenderer_?audioRenderer_->Position100ns():snapshot_.position100ns);
    });return FFFResult::Success;
}
FFFResult PlayerSession::SetVolume(const float volume, const bool muted) noexcept { if (!std::isfinite(volume) || volume < 0 || volume > 1) return FFFResult::InvalidArgument; Enqueue([this, volume, muted] { volume_ = volume; muted_ = muted; if (audioRenderer_) audioRenderer_->SetVolume(volume_, muted_); }); return FFFResult::Success; }

FFFResult PlayerSession::SetAudioRenderPlug(const FFF3FPAudioRenderPlug* const plug) noexcept {
    // Validate on the caller's thread, before anything moves: a struct that is not
    // this ABI's size/version, or a table with a missing entry, is refused and the
    // installed plug stays exactly as it was. Same "a bad call cannot half-apply"
    // rule the metadata injection entry points already follow.
    //
    // `context` is part of that rule: the contract requires every pointer except
    // destroy to be non-null, and a null context used to pass validation only to be
    // filtered later by SelectBedRoute's `plug_.context != nullptr` guard -- the bed
    // then fell back forever and every frame was counted as a decline, which reads
    // exactly like a plug that refuses. A malformed plug must fail at install.
    if (plug != nullptr && (plug->size != sizeof(FFF3FPAudioRenderPlug) ||
        plug->version != FFF3FP_AUDIO_PLUG_VERSION || plug->supports == nullptr ||
        plug->render == nullptr || plug->reset == nullptr || plug->context == nullptr))
        return FFFResult::InvalidArgument;
    {
        std::lock_guard lock(audioPlugMutex_);
        if (audioPlugInstalled_) {
            // Parked, not released: a worker pass frees it, so the release can never
            // land on top of a render() call that is already in flight.
            audioPlugRetired_.push_back(audioPlug_);
        }
        // The struct is copied, so the caller may free its own right after this
        // returns; the context it carries becomes the kernel's until the next
        // install, a clear, or the session's destruction.
        audioPlug_ = plug != nullptr ? *plug : FFF3FPAudioRenderPlug{};
        audioPlugName_ = plug != nullptr && plug->name != nullptr ? plug->name : std::string{};
        audioPlugInstalled_ = plug != nullptr;
    }
    // Swapping the seam is a worker-thread operation for the same reason a volume
    // change is: the renderer reads its plug view while it converts a frame, and the
    // two are serialized here. Releasing the previous context belongs in the same
    // command, so it can never be freed under a render() call already in flight.
    // A clear costs no device rebuild and no discontinuity count: the route changes
    // at the next frame boundary and the fold-down takes the bed back.
    Enqueue([this] {
        if (audioRenderer_ != nullptr) {
            std::lock_guard lock(audioPlugMutex_);
            audioRenderer_->SetAudioRenderPlug(audioPlugInstalled_ ? &audioPlug_ : nullptr);
        }
        std::vector<FFF3FPAudioRenderPlug> retired;
        { std::lock_guard lock(audioPlugMutex_); retired.swap(audioPlugRetired_); }
        for (const auto& plug : retired)
            if (plug.destroy != nullptr && plug.context != nullptr)
                plug.destroy(plug.context);
    });
    return FFFResult::Success;
}

void PlayerSession::GetAudioRenderPlugState(std::uint32_t& active, std::string& name,
    std::uint64_t& framesRendered, std::uint64_t& framesDeclined) const noexcept {
    {
        std::lock_guard lock(audioPlugMutex_);
        active = audioPlugInstalled_ ? 1u : 0u;
        name = audioPlugName_;
    }
    // Published by the renderer (PlayerAudioRuntimeState) rather than read off it:
    // a device switch may replace the renderer between one call and the next, and
    // the API thread must not dereference it. Per renderer instance, like the other
    // seam numbers; a seek deliberately does not clear them.
    framesRendered = audioRuntimeState_.plugFramesRendered.load(std::memory_order_relaxed);
    framesDeclined = audioRuntimeState_.plugFramesDeclined.load(std::memory_order_relaxed);
}

FFFResult PlayerSession::SetTimedTextLayer(const FFF3FPTimedTextLayer& input) noexcept {
    constexpr auto legacyLayerSize = offsetof(FFF3FPTimedTextLayer, targetFrameRate);
    constexpr auto frameRateLayerSize = offsetof(FFF3FPTimedTextLayer,
        coverBackdropBlurRadius);
    constexpr auto legacyLyricsLayerSize = offsetof(FFF3FPTimedTextLayer,
        coverRightPaddingPercentage);
    const auto targetFrameRate = input.size >= frameRateLayerSize
        ? input.targetFrameRate : 60.0f;
    if (input.size < legacyLayerSize || input.version != 20 ||
        input.canvasWidth == 0 || input.canvasHeight == 0 || input.commandCount > 4096 ||
        input.layerSlot > static_cast<std::uint32_t>(TimedTextLayerSlot::Lyrics) ||
        !std::isfinite(targetFrameRate) || targetFrameRate < 1.0f || targetFrameRate > 240.0f ||
        (input.commandCount != 0 && input.commands == nullptr)) return FFFResult::InvalidArgument;
    try {
        std::lock_guard contentLock(timedTextContentMutex_);
        const auto state = state_.load();
        if (state != FFF3FPState::Ready && state != FFF3FPState::Playing &&
            state != FFF3FPState::Paused && state != FFF3FPState::Ended)
            return FFFResult::InvalidState;
        TimedTextRenderLayer layer;
        layer.canvasWidth = input.canvasWidth;
        layer.canvasHeight = input.canvasHeight;
        layer.sequence = input.sequence;
        layer.targetFrameRate = targetFrameRate;
        if (input.layerSlot == static_cast<std::uint32_t>(TimedTextLayerSlot::Lyrics) &&
            input.size >= legacyLyricsLayerSize) {
            const auto coverRightPaddingPercentage =
                input.size >= sizeof(FFF3FPTimedTextLayer)
                ? input.coverRightPaddingPercentage
                : input.coverHorizontalPaddingPercentage;
            if (!std::isfinite(input.coverBackdropBlurRadius) ||
                input.coverBackdropBlurRadius < 1.0f || input.coverBackdropBlurRadius > 96.0f ||
                input.coverBackdropBlurPasses > 5 ||
                input.coverBackdropDownsampleFactor < 1 ||
                input.coverBackdropDownsampleFactor > 16 ||
                !std::isfinite(input.coverRegionWidthPercentage) ||
                !std::isfinite(input.lyricsRegionWidthPercentage) ||
                input.coverRegionWidthPercentage <= 0.0f ||
                input.lyricsRegionWidthPercentage <= 0.0f ||
                !std::isfinite(input.coverHorizontalPaddingPercentage) ||
                !std::isfinite(coverRightPaddingPercentage) ||
                !std::isfinite(input.coverVerticalPaddingPercentage) ||
                input.coverHorizontalPaddingPercentage < 0.0f ||
                input.coverHorizontalPaddingPercentage >= 50.0f ||
                coverRightPaddingPercentage < 0.0f ||
                coverRightPaddingPercentage >= 50.0f ||
                input.coverVerticalPaddingPercentage < 0.0f ||
                input.coverVerticalPaddingPercentage >= 50.0f)
                return FFFResult::InvalidArgument;
            layer.coverBackdropBlurRadius = input.coverBackdropBlurRadius;
            layer.coverBackdropBlurPasses = input.coverBackdropBlurPasses;
            layer.coverBackdropDownsampleFactor = input.coverBackdropDownsampleFactor;
            layer.coverBackdropTintArgb = input.coverBackdropTintArgb;
            layer.coverRegionWidthPercentage = input.coverRegionWidthPercentage;
            layer.lyricsRegionWidthPercentage = input.lyricsRegionWidthPercentage;
            layer.coverLeftPaddingPercentage =
                input.coverHorizontalPaddingPercentage;
            layer.coverRightPaddingPercentage = coverRightPaddingPercentage;
            layer.coverVerticalPaddingPercentage = input.coverVerticalPaddingPercentage;
        }
        layer.commands.reserve(input.commandCount);
        for (std::uint32_t index = 0; index < input.commandCount; ++index) {
            const auto& source = input.commands[index];
            constexpr auto legacyCommandSize = offsetof(FFF3FPTimedTextCommand, shadowArgb);
            if (source.size < legacyCommandSize || source.version != 20 ||
                source.type < FFF3FPTimedTextCommandType::Text || source.type > FFF3FPTimedTextCommandType::Bitmap ||
                !std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.width) ||
                !std::isfinite(source.height) || source.width <= 0 || source.height <= 0)
                return FFFResult::InvalidArgument;
            TimedTextRenderCommand command;
            command.type = source.type; command.flags = source.flags;
            command.x = source.x; command.y = source.y; command.width = source.width; command.height = source.height;
            command.foregroundArgb = source.foregroundArgb; command.outlineArgb = source.outlineArgb;
            command.fontSize = source.fontSize; command.outlineWidth = source.outlineWidth;
            if (source.size >= sizeof(FFF3FPTimedTextCommand)) {
                command.shadowArgb = source.shadowArgb;
                command.shadowOffsetX = source.shadowOffsetX;
                command.shadowOffsetY = source.shadowOffsetY;
            }
            command.horizontalAlignment = source.horizontalAlignment;
            command.verticalAlignment = source.verticalAlignment;
            command.contentId = source.contentId;
            if (source.type == FFF3FPTimedTextCommandType::Text) {
                if (source.textUtf8 == nullptr || !std::isfinite(source.fontSize) || source.fontSize <= 0 ||
                    !std::isfinite(source.outlineWidth) || source.outlineWidth < 0 ||
                    !std::isfinite(command.shadowOffsetX) || !std::isfinite(command.shadowOffsetY) ||
                    source.horizontalAlignment > FFF3FPTimedTextAlignment::Far ||
                    source.verticalAlignment > FFF3FPTimedTextAlignment::Far ||
                    source.fontFamilyUtf8 == nullptr) return FFFResult::InvalidArgument;
                const auto key = source.contentId == 0 ? 0 :
                    TimedTextContentKey(source.contentId, source.textUtf8, source.fontFamilyUtf8);
                if (key != 0) {
                    const auto cached = timedTextContentCache_.find(key);
                    if (cached != timedTextContentCache_.end()) command.content = cached->second;
                }
                if (!command.content) {
                    auto content = std::make_shared<TimedTextRenderCommand::TextContent>();
                    content->identity = TimedTextContentKey(source.contentId,
                        source.textUtf8, source.fontFamilyUtf8);
                    if (!FromUtf8Strict(source.textUtf8, content->text) || content->text.empty() ||
                        !FromUtf8Strict(source.fontFamilyUtf8, content->fontFamily)) return FFFResult::InvalidArgument;
                    if (content->fontFamily.empty()) content->fontFamily = L"Segoe UI";
                    command.content = content;
                    if (key != 0) {
                        constexpr std::size_t MaximumTimedTextContents = 512;
                        if (timedTextContentCache_.size() >= MaximumTimedTextContents)
                            timedTextContentCache_.clear();
                        timedTextContentCache_[key] = std::move(content);
                    }
                }
            } else {
                const auto required = static_cast<std::uint64_t>(source.bitmapStride) * source.bitmapHeight;
                if (source.bitmapBgra == nullptr || source.bitmapWidth == 0 || source.bitmapHeight == 0 ||
                    static_cast<std::uint64_t>(source.bitmapStride) <
                        static_cast<std::uint64_t>(source.bitmapWidth) * 4u || required > source.bitmapBytes ||
                    required > 256ull * 1024 * 1024) return FFFResult::InvalidArgument;
                command.bitmapWidth = source.bitmapWidth; command.bitmapHeight = source.bitmapHeight;
                command.bitmapStride = source.bitmapStride;
                const auto* bytes = static_cast<const std::uint8_t*>(source.bitmapBgra);
                command.bitmap.assign(bytes, bytes + static_cast<std::size_t>(required));
            }
            layer.commands.push_back(std::move(command));
        }
        // Timed text has its own high-precision 60 Hz producer. Routing it through
        // the decode command queue coalesced frames whenever video work was busy.
        const auto result = videoRenderer_.SetTimedTextLayer(std::move(layer),
            static_cast<TimedTextLayerSlot>(input.layerSlot));
        if (result != FFFResult::Success) {
            ReportError(result, videoRenderer_.LastError(), "timed-text");
            return result;
        }
        return FFFResult::Success;
    } catch (...) { return FFFResult::NativeFailure; }
}

FFFResult PlayerSession::SetVideoSuperResolution(
    const FFF3FPVideoSuperResolution mode) noexcept {
    // The renderer owns the device-facing decision, but the session refuses to
    // switch VSR on while a comparison-style mode is active. VSR runs an AI
    // model, so frames stop being a reproducible function of the decoded
    // image; a host that compares pictures pixel-for-pixel must not have that.
    // Turning it Off is always allowed.
    if (mode != FFF3FPVideoSuperResolution::Off &&
        mode != FFF3FPVideoSuperResolution::Auto)
        return FFFResult::InvalidArgument;
    if (mode == FFF3FPVideoSuperResolution::Auto && comparisonModeActive_)
        return FFFResult::InvalidState;
    const auto result = videoRenderer_.SetVideoSuperResolution(mode);
    if (result != FFFResult::Success)
        ReportError(result, videoRenderer_.LastError(), "video-super-resolution");
    return result;
}

FFFResult PlayerSession::SetComparisonMode(const bool active) noexcept {
    comparisonModeActive_ = active;
    // Entering comparison mode forces VSR off rather than merely blocking the
    // next enable: a host that flips the flag mid-playback would otherwise keep
    // presenting AI-altered frames into a diff view. Automatic HDR gets the
    // same treatment for the same reason (generated pixels are not a
    // reproducible function of the decoded frame).
    if (active && videoRenderer_.RequestedVideoSuperResolution() !=
            FFF3FPVideoSuperResolution::Off) {
        const auto result = videoRenderer_.SetVideoSuperResolution(
            FFF3FPVideoSuperResolution::Off);
        if (result != FFFResult::Success) return result;
        // Declared locally for the same reason as in ReportError below: the
        // sink lives in PlayerApi.cpp and is not part of any public header.
        extern void FFF3FP_KernelLogImpl(const char*) noexcept;
        FFF3FP_KernelLogImpl(
            "FFF.Native: RTX Video Super Resolution disabled (comparison mode active).");
    }
    if (active && videoRenderer_.RequestedVideoAutoHdr() != FFF3FPVideoAutoHdr::Off) {
        const auto result = videoRenderer_.SetVideoAutoHdr(FFF3FPVideoAutoHdr::Off);
        if (result != FFFResult::Success) return result;
        extern void FFF3FP_KernelLogImpl(const char*) noexcept;
        FFF3FP_KernelLogImpl(
            "FFF.Native: NVIDIA automatic HDR disabled (comparison mode active).");
    }
    return FFFResult::Success;
}

FFFResult PlayerSession::GetVideoSuperResolutionStatus(
    FFF3FPVideoSuperResolutionStatus& status) const noexcept {
    if (status.size < sizeof(FFF3FPVideoSuperResolutionStatus) || status.version != 20)
        return FFFResult::InvalidArgument;
    videoRenderer_.FillVideoSuperResolutionStatus(status);
    return FFFResult::Success;
}

FFFResult PlayerSession::SetVideoAutoHdr(const FFF3FPVideoAutoHdr mode) noexcept {
    // Same comparison-mode contract as VSR, and for a stronger reason: the
    // generated HDR pixels depend on the driver's model, so a frame-exact
    // comparison surface must never see them. Off is always allowed.
    if (mode != FFF3FPVideoAutoHdr::Off && mode != FFF3FPVideoAutoHdr::Auto)
        return FFFResult::InvalidArgument;
    if (mode == FFF3FPVideoAutoHdr::Auto && comparisonModeActive_)
        return FFFResult::InvalidState;
    const auto result = videoRenderer_.SetVideoAutoHdr(mode);
    if (result != FFFResult::Success)
        ReportError(result, videoRenderer_.LastError(), "video-auto-hdr");
    return result;
}

FFF3FPVideoAutoHdr PlayerSession::RequestedVideoAutoHdr() const noexcept {
    return videoRenderer_.RequestedVideoAutoHdr();
}

FFFResult PlayerSession::GetVideoAutoHdrStatus(
    FFF3FPVideoAutoHdrStatus& status) const noexcept {
    if (status.size < sizeof(FFF3FPVideoAutoHdrStatus) || status.version != 20)
        return FFFResult::InvalidArgument;
    videoRenderer_.FillVideoAutoHdrStatus(status);
    return FFFResult::Success;
}

FFFResult PlayerSession::GetTimedTextStatus(FFF3FPTimedTextStatus& status) noexcept {
    return videoRenderer_.GetTimedTextStatus(status, TimedTextLayerSlot::Subtitle);
}

FFFResult PlayerSession::GetDanmakuStatus(FFF3FPTimedTextStatus& status) noexcept {
    return videoRenderer_.GetTimedTextStatus(status, TimedTextLayerSlot::Danmaku);
}

FFFResult PlayerSession::GetLyricsStatus(FFF3FPTimedTextStatus& status) noexcept {
    return videoRenderer_.GetTimedTextStatus(status, TimedTextLayerSlot::Lyrics);
}

// Forward to the renderer (RTInfo under deviceMutex_).
FFFResult PlayerSession::GetRenderTargetInfo(FFF3FPRenderTargetInfo& info) noexcept {
    // Declared version 20 makes origins signed; size alone cannot distinguish the older
    // v1 layout. Same contract as GetSnapshot: the caller declares the size and version of
    // the struct it passes, so we never write past a smaller caller-side layout.
    // A v1 caller would read a negative origin as a huge unsigned value
    // (silently wrong content), and v1/v2 share the same field widths so
    // `size` cannot distinguish them -> the only honest gate is the version.
    if (info.size < sizeof(FFF3FPRenderTargetInfo) || info.version != 20)
        return FFFResult::InvalidArgument;
    info.size = sizeof(info);
    info.version = 20;
    PlayerVideoRenderer::RenderTargetInfo rtInfo{};
    const auto result = videoRenderer_.GetRenderTargetInfo(rtInfo);
    if (result != FFFResult::Success) return result;
    info.swapWidth = rtInfo.swapWidth;
    info.swapHeight = rtInfo.swapHeight;
    info.clientWidth = rtInfo.clientWidth;
    info.clientHeight = rtInfo.clientHeight;
    info.destX = rtInfo.destX;
    info.destY = rtInfo.destY;
    info.destWidth = rtInfo.destWidth;
    info.destHeight = rtInfo.destHeight;
    info.outputBitDepth = rtInfo.outputBitDepth;
    info.hdr = rtInfo.hdr ? 1 : 0;
    return FFFResult::Success;
}

// Re-present the last cached frame on the presenter thread.
FFFResult PlayerSession::Redraw() noexcept {
    const auto result = videoRenderer_.Redraw();
    if (result == FFFResult::DeviceFailure)
        videoRenderer_.RequestRecoveryIfDeviceLost();
    return result;
}

FFFResult PlayerSession::SetInjectedHdrMetadata(
    const FFF3FPHdrDynamicMetadataEntry* entries, const std::uint32_t count) noexcept {
    const auto result = videoRenderer_.SetInjectedHdrMetadata(entries, count);
    // `snapshot_` belongs to the worker thread (the frame path writes it through
    // mutex_ already released), so refreshing the ST 2094 diagnostics goes through
    // Enqueue like SetColorMode does. The renderer call itself stays synchronous: the API
    // promises an immediate InvalidArgument for a malformed table, and
    // FFF3FP_GetHdrMetadataSource reads the processor, not the snapshot.
    if (result == FFFResult::Success)
        Enqueue([this] {
            ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
            PublishSnapshot();
        });
    return result;
}

FFFResult PlayerSession::ClearInjectedHdrMetadata() noexcept {
    videoRenderer_.ClearInjectedHdrMetadata();
    Enqueue([this] {
        ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
        PublishSnapshot();
    });
    return FFFResult::Success;
}

std::uint32_t PlayerSession::HdrMetadataSource() const noexcept {
    return videoRenderer_.HdrMetadataSource();
}

FFFResult PlayerSession::SetDynamicMetadata(
    const FFF3FPDynamicMetadataPacket* packets, const std::uint32_t count) noexcept {
    const auto result = videoRenderer_.SetDynamicMetadata(packets, count);
    // Same synchronous-validate / async-snapshot split as the ST 2094-40
    // entry point: the call thread gets the immediate verdict, the snapshot's
    // ST 2094 diagnostics refresh on the session thread.
    if (result == FFFResult::Success)
        Enqueue([this] {
            ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
            PublishSnapshot();
        });
    return result;
}

FFFResult PlayerSession::ClearDynamicMetadata(const std::uint32_t kind) noexcept {
    videoRenderer_.ClearDynamicMetadata(kind);
    Enqueue([this] {
        ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
        PublishSnapshot();
    });
    return FFFResult::Success;
}

FFFResult PlayerSession::GetHdrSignalDiagnostics(
    FFF3FPHdrSignalDiagnostics& output) const noexcept {
    if (output.size < sizeof(FFF3FPHdrSignalDiagnostics) || output.version != 20)
        return FFFResult::InvalidArgument;
    output = videoRenderer_.HdrSignalDiagnostics();
    return FFFResult::Success;
}

FFFResult PlayerSession::GetSnapshot(FFF3FPSnapshot& output) const noexcept {
    if (output.size < sizeof(FFF3FPSnapshot) || output.version != 20) return FFFResult::InvalidArgument;
    { std::lock_guard lock(snapshotMutex_); output = publishedSnapshot_; }
    // Presentation completes asynchronously on the dedicated swap-chain owner;
    // expose its live counters even if no later decode-frame snapshot was needed.
    output.presentedVideoFrames = videoRenderer_.PresentedVideoFrames();
    output.coalescedVideoFrames = videoRenderer_.CoalescedVideoFrames();
    output.swapChainPresents = videoRenderer_.SwapChainPresents();
    output.presentWait100ns = videoRenderer_.PresentWait100ns();
    output.deviceLockWait100ns = videoRenderer_.DeviceLockWait100ns();
    // Which stage owned the last bed frame is a live mirror too. The snapshot is
    // published from the video-present path, and an audio-only file never walks
    // that path, so the published copy would freeze at whatever the last publish
    // happened to see -- the same reason 缓冲区/欠载 go stale for pure audio.
    // audioRuntimeState_ belongs to the session rather than to the renderer, so it
    // stays valid across a device swap and is safe to read here.
    output.audioBinaural =
        audioRuntimeState_.plugBedActive.load(std::memory_order_relaxed);
    output.softwareConvert100ns = videoRenderer_.SoftwareConvert100ns();
    output.videoUpload100ns = videoRenderer_.Upload100ns();
    output.uploadMapWait100ns = videoRenderer_.UploadMapWait100ns();
    output.uploadCopy100ns = videoRenderer_.UploadCopy100ns();
    output.downscaleResample100ns = videoRenderer_.DownscaleResample100ns();
    output.scalePassCount = videoRenderer_.ScalePassCount();
    output.uploadCount = videoRenderer_.UploadCount();
    output.uploadCopyBytes = videoRenderer_.UploadCopyBytes();
    output.uploadPath = videoRenderer_.UploadPath();
    output.videoOutputBitDepth = videoRenderer_.OutputBitDepth();
    output.videoScalingMode = videoRenderer_.ActualVideoScalingMode();
    if (output.state == FFF3FPState::Playing) {
        std::uint64_t firstSequence = 0;
        std::uint64_t secondSequence = 0;
        std::int64_t sampledPosition = 0;
        std::int64_t sampledQpc = 0;
        std::int64_t limit = 0;
        do {
            firstSequence = playbackClockSequence_.load(std::memory_order_acquire);
            if ((firstSequence & 1u) != 0) continue;
            sampledPosition = playbackPosition100ns_.load(std::memory_order_relaxed);
            sampledQpc = playbackClockSampleQpc_.load(std::memory_order_relaxed);
            limit = playbackClockLimit100ns_.load(std::memory_order_relaxed);
            secondSequence = playbackClockSequence_.load(std::memory_order_acquire);
        } while (firstSequence != secondSequence || (secondSequence & 1u) != 0);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const auto elapsed = sampledQpc > 0 && now.QuadPart > sampledQpc && qpcFrequency_ > 0
            ? (now.QuadPart - sampledQpc) * TicksPerSecond / qpcFrequency_ : 0;
        output.position100ns = sampledPosition + std::min(static_cast<std::int64_t>(elapsed * playbackRate_.load()),
            std::max<std::int64_t>(0, limit - sampledPosition));
        if (output.duration100ns > 0) output.position100ns = std::min(output.position100ns, output.duration100ns);
    }
    output.bufferedAudio100ns = audioRuntimeState_.buffered100ns.load(std::memory_order_relaxed);
    output.audioUnderruns = audioRuntimeState_.underruns.load(std::memory_order_relaxed);
    output.audioTimestampJitterFrames = audioRuntimeState_.timestampJitterFrames.load(std::memory_order_relaxed);
    output.audioDiscontinuities = audioRuntimeState_.discontinuities.load(std::memory_order_relaxed);
    output.audioInsertedSilenceFrames = audioRuntimeState_.insertedSilenceFrames.load(std::memory_order_relaxed);
    output.audioDroppedOverlapFrames = audioRuntimeState_.droppedOverlapFrames.load(std::memory_order_relaxed);
    if (output.selectedAudioStream >= 0 || output.isExternalAudio != 0)
        output.audioPosition100ns = output.position100ns;
    return FFFResult::Success;
}

FFFResult PlayerSession::ReadVideoPixel(FFF3FPVideoPixelProbe& probe) noexcept {
    const auto result = videoRenderer_.ReadPixel(probe);
    if (result == FFFResult::DeviceFailure)
        videoRenderer_.RequestRecoveryIfDeviceLost();
    return result;
}

FFFResult PlayerSession::ReadVideoPixelRegion(const std::uint32_t x, const std::uint32_t y,
    const std::uint32_t width, const std::uint32_t height, float* dst,
    const std::uint32_t dstFloatCount, std::uint32_t* outputBitDepth) noexcept {
    const auto result = videoRenderer_.ReadPixelRegion(
        x, y, width, height, dst, dstFloatCount, outputBitDepth);
    if (result == FFFResult::DeviceFailure)
        videoRenderer_.RequestRecoveryIfDeviceLost();
    return result;
}

FFFResult PlayerSession::GetAudioPeakLevels(FFF3FPAudioPeakLevels& output) const noexcept {
    if (output.size < sizeof(FFF3FPAudioPeakLevels) || output.version != 20)
        return FFFResult::InvalidArgument;
    output.channelCount = 0;
    output.inputChannelCount = 0;
    std::fill(std::begin(output.values), std::end(output.values), 0.0f);
    output.channelCount = audioRuntimeState_.Copy(output.values,
        static_cast<std::uint32_t>(std::size(output.values)));
    if (output.version >= 2) {
        output.inputChannelCount = std::min<std::uint32_t>(
            audioRuntimeState_.inputChannelCount.load(std::memory_order_acquire),
            static_cast<std::uint32_t>(std::size(output.inputValues)));
        for (std::size_t index = 0; index < std::size(output.inputValues); ++index)
            output.inputValues[index] = audioRuntimeState_.inputValues[index].load(std::memory_order_relaxed);
    }
    return FFFResult::Success;
}
std::string PlayerSession::MediaInfo() const { std::lock_guard lock(mutex_); return mediaInfoJson_; }
std::string PlayerSession::LastError() const { std::lock_guard lock(errorMutex_); return lastError_; }

void PlayerSession::Worker() noexcept {
    workerThreadId_ = GetCurrentThreadId();
    for (;;) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            if (commands_.empty() && state_.load() != FFF3FPState::Playing)
                commandCondition_.wait(lock, [this] {
                    return terminate_ || !commands_.empty() ||
                        state_.load() == FFF3FPState::Playing ||
                        videoRenderer_.DeviceRecoveryRequested() ||
                        (audioRenderer_ && audioRenderer_->RestartRequested());
                });
            if (terminate_) break;
            if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
        }
        if (command) { try { command(); } catch (...) { Fail(FFFResult::NativeFailure, "An unhandled player command exception occurred."); } }
        else if (!RecoverVideoDevice() && !RecoverAudioDevice() &&
            state_.load() == FFF3FPState::Playing) PumpPlayback();
    }
    DoClose(FFF3FPState::Closed);
}

FFFResult PlayerSession::OpenFormat(const std::string& path, AVFormatContext** output,
    std::unique_ptr<SharedFileInput>& io, std::string& error) noexcept {
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file,crypto,data", 0);
    av_dict_set(&options, "protocol_blacklist", "http,https,tcp,tls,udp,rtp,rtsp,srt,rist,ftp,ssh", 0);
    // A layered ("progressive") AVIF stores its primary item as several extents,
    // and the mov demuxer refuses those before any decoder runs. Repairing the box
    // means editing the bytes, so such a file is read and repaired here and handed
    // to the demuxer from memory; every other media keeps its file-backed input.
    // The gate is a container name check so nothing else pays for the read.
    std::vector<std::uint8_t> repairedBytes;
    std::string repairDetail;
    const bool repaired = IsStillImageContainerName(path) &&
        AvifIlocShim::ReadAndRepair(path.c_str(), MaxRepairedStillImageBytes,
            repairedBytes, repairDetail);
    io = repaired ? SharedFileInput::OpenMemory(repairedBytes.data(), repairedBytes.size(), error)
                  : SharedFileInput::Open(path.c_str(), error);
    if (io == nullptr) {
        av_dict_free(&options);
        return FFFResult::NativeFailure;
    }
    auto* context = avformat_alloc_context();
    if (context == nullptr) {
        av_dict_free(&options);
        io.reset();
        error = "Could not allocate the FFmpeg input context.";
        return FFFResult::NativeFailure;
    }
    context->pb = io->Context();
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    *output = context;
    auto result = avformat_open_input(output, path.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (result < 0) {
        if (*output != nullptr) avformat_free_context(*output);
        *output = nullptr;
        io.reset();
        error = "Could not open local media: " + FfmpegError(result);
        return FFFResult::FfmpegFailure;
    }
    if ((*output)->iformat == nullptr) {
        CloseFormat(output, io); error = "Network and virtual-device demuxers are disabled."; return FFFResult::NotSupported;
    }
    const std::string_view demuxerName(
        (*output)->iformat->name == nullptr ? "" : (*output)->iformat->name);
    if (demuxerName.find("hls") != std::string_view::npos ||
        demuxerName.find("dash") != std::string_view::npos) {
        CloseFormat(output, io); error = "Network and virtual-device demuxers are disabled."; return FFFResult::NotSupported;
    }
    // Set image-loop options before the first packet on every OpenFormat path.
    if (!ApplyLoopingOptions(*output, demuxerName)) {
        CloseFormat(output, io); error = "The image demuxer could not be configured."; return FFFResult::FfmpegFailure;
    }
    result = avformat_find_stream_info(*output, nullptr);
    if (result < 0) { CloseFormat(output, io); error = "Could not read media streams: " + FfmpegError(result); return FFFResult::FfmpegFailure; }
    return FFFResult::Success;
}

void PlayerSession::CloseFormat(AVFormatContext** format,
    std::unique_ptr<SharedFileInput>& io) noexcept {
    if (format != nullptr && *format != nullptr) avformat_close_input(format);
    io.reset();
}

int PlayerSession::ProbeInterrupt(void* opaque) noexcept {
    const auto* self = static_cast<const PlayerSession*>(opaque);
    return (self != nullptr && self->terminate_.load(std::memory_order_acquire)) ? 1 : 0;
}

FFFResult PlayerSession::OpenProbeFormat(const std::string& path, AVFormatContext** output,
    std::unique_ptr<SharedFileInput>& io, std::string& error) noexcept {
    // Mirrors OpenFormat rather than avformat_open_input(path): the trial used to
    // reopen the bare url, which skipped the protocol lists, the repaired-memory
    // input for layered AVIF and every demuxer gate -- so the "same file" it
    // measured was not always the file the session plays.
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file,crypto,data", 0);
    av_dict_set(&options, "protocol_blacklist", "http,https,tcp,tls,udp,rtp,rtsp,srt,rist,ftp,ssh", 0);
    std::vector<std::uint8_t> repairedBytes;
    std::string repairDetail;
    const bool repaired = IsStillImageContainerName(path) &&
        AvifIlocShim::ReadAndRepair(path.c_str(), MaxRepairedStillImageBytes,
            repairedBytes, repairDetail);
    io = repaired ? SharedFileInput::OpenMemory(repairedBytes.data(), repairedBytes.size(), error)
                  : SharedFileInput::Open(path.c_str(), error);
    if (io == nullptr) {
        av_dict_free(&options);
        return FFFResult::NativeFailure;
    }
    auto* context = avformat_alloc_context();
    if (context == nullptr) {
        av_dict_free(&options);
        io.reset();
        error = "Could not allocate the trial demuxer context.";
        return FFFResult::NativeFailure;
    }
    context->pb = io->Context();
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    // The read itself is a synchronous file read and cannot be cut short, but
    // avformat's own probing loops do consult this, and the packet loop below
    // checks the same flag, so the non-cancellable window shrinks to one read.
    context->interrupt_callback.callback = &PlayerSession::ProbeInterrupt;
    context->interrupt_callback.opaque = this;
    *output = context;
    const auto result = avformat_open_input(output, path.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (result < 0) {
        if (*output != nullptr) avformat_free_context(*output);
        *output = nullptr;
        io.reset();
        error = FfmpegError(result);
        return FFFResult::FfmpegFailure;
    }
    if ((*output)->iformat == nullptr) {
        CloseFormat(output, io);
        error = "Network and virtual-device demuxers are disabled.";
        return FFFResult::NotSupported;
    }
    const std::string_view demuxerName(
        (*output)->iformat->name == nullptr ? "" : (*output)->iformat->name);
    if (demuxerName.find("hls") != std::string_view::npos ||
        demuxerName.find("dash") != std::string_view::npos) {
        CloseFormat(output, io);
        error = "Network and virtual-device demuxers are disabled.";
        return FFFResult::NotSupported;
    }
    if (!ApplyLoopingOptions(*output, demuxerName)) {
        CloseFormat(output, io);
        error = "The image demuxer could not be configured.";
        return FFFResult::FfmpegFailure;
    }
    return FFFResult::Success;
}

// Reads how many clockwise quarter turns the source asks to be displayed with.
//
// Two independent mechanisms carry this, and real files use either one:
//
//   1) Container metadata `rotate` (Matroska ProjectionPoseRoll, and the tag
//      ffmpeg's own muxer writes). It never becomes a display matrix, so a
//      reader that only looks for AV_FRAME_DATA_DISPLAYMATRIX misses it --
//      measured with a `-metadata:s:v rotate=90` MKV.
//   2) A display matrix (MP4 tkhd / MOV, or frame side data). ffmpeg converts
//      the matrix form into AV_FRAME_DATA_DISPLAYMATRIX on decoded frames.
//
// The metadata tag is CCW-positive in ffmpeg's own convention (`rotate=90`
// means "rotate 90 degrees counter-clockwise to display"), while this renderer
// counts clockwise quarter turns, hence the 4 - n inversion. The matrix path
// already yields a clockwise angle via av_display_rotation_get's sign, so it is
// used as-is.
//
// Returns 0 when nothing usable is present. Never throws: a malformed matrix
// must degrade to "no rotation" rather than fail the open.
std::uint32_t SourceRotationQuarterTurns(AVFormatContext* format,
    const std::int32_t streamIndex) noexcept {
    if (format == nullptr || streamIndex < 0 ||
        streamIndex >= static_cast<std::int32_t>(format->nb_streams))
        return 0;

    // 1) Container metadata tag. av_dict_get is case-insensitive-ish in practice
    //    for these keys, but check both spellings that real muxers emit.
    const auto* stream = format->streams[streamIndex];
    for (const char* key : { "rotate", "ROTATE" }) {
        const auto* entry = av_dict_get(stream->metadata, key, nullptr, 0);
        if (entry == nullptr || entry->value == nullptr) continue;
        const auto degrees = std::atof(entry->value);
        if (!std::isfinite(degrees)) continue;
        auto turns = static_cast<long>(std::lround(degrees / 90.0)) % 4;
        if (turns < 0) turns += 4;
        // Metadata is CCW-positive; the renderer counts clockwise.
        return static_cast<std::uint32_t>((4 - turns) % 4);
    }

    // 2) Display matrix on the stream's coded side data (decoders copy this to
    //    each frame, but at open time no frame exists yet, so read it here).
    const auto* matrix = av_packet_side_data_get(stream->codecpar->coded_side_data,
        stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (matrix == nullptr || matrix->size < 9 * sizeof(std::int32_t)) return 0;
    const auto angle = av_display_rotation_get(
        reinterpret_cast<const std::int32_t*>(matrix->data));
    if (angle != angle) return 0; // NaN: no usable matrix
    auto normalized = angle;
    while (normalized < 0.0) normalized += 360.0;
    while (normalized >= 360.0) normalized -= 360.0;
    return static_cast<std::uint32_t>(normalized / 90.0 + 0.5) % 4u;
}

FFFResult PlayerSession::OpenDecoder(AVFormatContext* owner, const std::int32_t index, const bool video,    AVCodecContext** output, const std::int32_t hardwareDeviceType,
    std::int32_t* hardwarePixelFormat, const bool useConfiguredHardware,
    const AVCodec* codecOverride, std::string* failureReason) noexcept {
    const auto setFailure = [failureReason](std::string message) {
        if (failureReason != nullptr) *failureReason = std::move(message);
    };
    if (owner == nullptr || index < 0 || index >= static_cast<std::int32_t>(owner->nb_streams)) {
        setFailure("The requested stream index is invalid.");
        return FFFResult::InvalidArgument;
    }
    auto* stream = owner->streams[index];
    if (stream->codecpar->codec_type != (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO)) {
        setFailure("The requested stream has the wrong media type.");
        return FFFResult::InvalidArgument;
    }
    AVPixelFormat selectedHardwareFormat = AV_PIX_FMT_NONE;
    const auto deviceType = static_cast<AVHWDeviceType>(hardwareDeviceType);
    const auto hardwareRequested = video && useConfiguredHardware && decodeMode_ == FFF3FPDecodeMode::D3D11;
    const AVCodec* codec = hardwareRequested
        ? codecOverride
        : video && stream->codecpar->codec_id == AV_CODEC_ID_AV1
            ? avcodec_find_decoder_by_name("libdav1d") : nullptr;
    if (hardwareRequested) selectedHardwareFormat = FindHardwareFormat(codec, deviceType);
    if (codec == nullptr && !hardwareRequested)
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr || (hardwareRequested && selectedHardwareFormat == AV_PIX_FMT_NONE)) {
        setFailure("No compatible FFmpeg decoder exposes the requested hardware pixel format.");
        return FFFResult::NotSupported;
    }
    auto* context = avcodec_alloc_context3(codec);
    if (context == nullptr) {
        setFailure("FFmpeg could not allocate the decoder context.");
        return FFFResult::NativeFailure;
    }
    auto result = avcodec_parameters_to_context(context, stream->codecpar);
    context->pkt_timebase = stream->time_base;
    if (result >= 0 && video && !hardwareRequested) {
        // Widen the preferred CPU set to the decode budget before anything reads it as
        // the ladder's boundary: on parts where the fastest class alone seats fewer
        // threads than the budget (8P+16E and friends), a boundary pinned to that
        // class would cap the ladder early or over-subscribe the class on the way back
        // down. Idempotent, and a no-op on uniform or undiscovered topologies.
        placement_.EnsurePreferredCapacity(DecoderThreadBudget());
        // Worker count comes from the fixed resolution tier, or from the load-driven
        // ladder when the host opted in; both respect the shared thread budget. See
        // SelectSoftwareDecoderThreads.
        context->thread_count = static_cast<int>(SelectSoftwareDecoderThreads(
            static_cast<std::uint32_t>(stream->codecpar->width),
            static_cast<std::uint32_t>(stream->codecpar->height)));
        context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        // The two heavy AV1 classes the dav1d policy below enumerates. Hoisted so
        // the byte-keyed frame-delay block can yield to it instead of setting the
        // same option twice with a different bound.
        const auto heavy444 = std::strcmp(codec->name, "libdav1d") == 0 &&
            stream->codecpar->format == AV_PIX_FMT_YUV444P12LE &&
            stream->codecpar->width >= 3840 && stream->codecpar->height >= 2160;
        const auto large420 = std::strcmp(codec->name, "libdav1d") == 0 &&
            stream->codecpar->format == AV_PIX_FMT_YUV420P10LE &&
            stream->codecpar->width >= 7680 && stream->codecpar->height >= 4320;
        if (std::strcmp(codec->name, "libdav1d") == 0) {
            // dav1d's automatic delay opens only ceil(sqrt(workers)) frame
            // contexts. Single-tile AV1 needs frame parallelism as well as workers.
            const auto logical = std::max(1u, std::thread::hardware_concurrency());
            // Eight workers leave bandwidth and CPU time for upload, audio and subtitles.
            // 7940HX fullscreen: ~40 fps with 8/8 versus ~34 fps with 16/16.
            auto workers = heavy444 ? (std::min)(logical * 2, 32u) :
                (std::min)(logical, 8u);
            if (heavy444 || large420) {
                if (adaptiveThreadsEnabled_) {
                    const auto ceiling = (std::min)({logical, threadLadderMax_, 64u});
                    const auto floor = (std::min)(threadLadderMin_, ceiling);
                    workers = std::clamp(decoderThreads_ == 0 ? workers : decoderThreads_,
                        std::max(1u, floor), std::max(1u, ceiling));
                }
                context->thread_count = static_cast<int>(workers);
                result = av_opt_set_int(context->priv_data, "max_frame_delay", workers, 0);
            }
        }
        // One per-frame byte estimate drives both the pool's opening rung and the
        // decoder's frame parallelism: keying on the byte cost covers every
        // large-frame class (4:4:4 and 4:2:0 at 10/12-bit, 5K/6K/8K) without
        // enumerating pixel formats, and it is the same figure the queue budgets
        // already divide by.
        std::size_t bytesPerFrame = 0;
        if (stream->codecpar->width > 0 && stream->codecpar->height > 0) {
            const auto estimatedBytes = av_image_get_buffer_size(
                static_cast<AVPixelFormat>(stream->codecpar->format),
                stream->codecpar->width, stream->codecpar->height, 1);
            if (estimatedBytes > 0)
                bytesPerFrame = static_cast<std::size_t>(estimatedBytes);
        }
        // Pick the pool's opening rung by frame size. The load ladder owns everything
        // above it, but the opening must still offer the look-ahead depth the fixed
        // pool used to provide: the saturation gate closes one frame below the limit,
        // so opening with only two or three queued frames of ~100 MB (8K 4:2:0) keeps
        // the demux feed gated while the pipeline is still filling. Require room for
        // MinimumPoolLookAheadFrames frames plus one frame of accounting slack;
        // smaller-frame classes still open at the floor rung.
        if (bytesPerFrame > 0 && performance_.video != 2) {
            const auto floorBytes = (performance_.VideoFrames(MinimumPoolLookAheadFrames) + 1) * bytesPerFrame;
            std::size_t opening = 0;
            while (opening + 1 < SoftwarePoolRungCount &&
                   performance_.VideoBudget(SoftwarePoolBudgets[opening]) < floorBytes)
                ++opening;
            if (opening > softwarePoolRung_) {
                softwarePoolRung_ = static_cast<std::uint8_t>(opening);
                if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
                    std::fprintf(stderr,
                        "[ladder] frame pool opens at %llu MiB (%llu-frame floor) for %llu MB frames\n",
                        static_cast<unsigned long long>(
                            SoftwarePoolBudgets[opening] / (1024 * 1024)),
                        static_cast<unsigned long long>(MinimumPoolLookAheadFrames),
                        static_cast<unsigned long long>(bytesPerFrame / (1024 * 1024)));
            }
        }
        // libdav1d's automatic frame delay opens only ceil(sqrt(workers)) frame
        // contexts, which under-serves any large frame: the worker count stays with
        // the shared budget and the load ladder, and the frame delay follows it. Both
        // tiers key on the byte estimate above (no pixel-format enumeration): at or
        // above HeavyDecoderFrameBytes the delay equals the worker count; at or above
        // CappedDecoderFrameBytes it is bounded at MaximumDecoderFrameDelay so the
        // resident pipeline stays finite on the largest frames (an 8K context is
        // ~100 MB). Smaller frames keep dav1d's automatic value; the two classes the
        // dav1d policy above already tuned are left to it.
        if (std::strcmp(codec->name, "libdav1d") == 0 && bytesPerFrame > 0 &&
            bytesPerFrame >= HeavyDecoderFrameBytes && !(heavy444 || large420)) {
            const auto capped = bytesPerFrame >= CappedDecoderFrameBytes;
            const auto frameDelay = capped
                ? (std::min)(context->thread_count, MaximumDecoderFrameDelay)
                : context->thread_count;
            const auto setResult =
                av_opt_set_int(context->priv_data, "max_frame_delay", frameDelay, 0);
            if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
                std::fprintf(stderr, "[ladder] dav1d frame delay=%d for %llu MB frames%s set=%d\n",
                    frameDelay,
                    static_cast<unsigned long long>(bytesPerFrame / (1024 * 1024)),
                    capped ? " (capped)" : "", setResult);
        }
        if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
            std::fprintf(stderr,
                "[ladder] soft decode workers=%d budget=%u adaptive=%d boundary=%u\n",
                context->thread_count, DecoderThreadBudget(), adaptiveThreadsEnabled_ ? 1 : 0,
                placement_.PreferredDomainThreads());
    }
    if (disc_ && disc_->Menu() && video &&
        stream->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        context->flags |= AV_CODEC_FLAG_LOW_DELAY;
        context->thread_count = 1;
        context->thread_type = FF_THREAD_SLICE;
    }
    if (result >= 0 && hardwareRequested) {
        // The decode part of hardware decoding is exactly one thread: FFmpeg drives the
        // hwaccel from the calling thread and runs no worker pool. FFmpeg sets this
        // itself; setting it explicitly keeps the thread accounting honest (see the
        // budget in DecoderThreadBudget).
        context->thread_count = static_cast<int>(HardwareDecoderThreads);
        if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
            std::fprintf(stderr, "[ladder] hardware decode workers=%u\n", HardwareDecoderThreads);
        // Reserve only what the presentation queue plus in-flight copies actually need.
        //
        // This was MaxQueuedVideoFrames + 2 = 10, a fixed count that ignores the cost of
        // a surface: each one is a full decoded frame in VRAM. Measured on 8K HEVC that
        // reservation is what dominates GPU memory -- 3201 MB at 10, 2626 MB at 4 and
        // 2435 MB at 2 -- because a single 8K P010 surface is 99.6 MB, so eight surplus
        // surfaces cost ~800 MB. The queue itself is memory-bounded and needs only two
        // or three frames at 8K, so the surplus bought nothing: playback measured 714
        // frames with zero drops at 10, 4 and 2 alike.
        //
        // Three covers the bounded queue (2 at 8K, 5 at 4K, capped at 8) plus a copy in
        // flight, without the large fixed surplus. A decoder that needs more will still
        // request it: extra_hw_frames is a request on top of the codec's own DPB, and
        // FFmpeg raises it if the DPB requirement exceeds what was asked for.
        constexpr int ExtraHardwareFrames = 3;
        context->extra_hw_frames = std::max(context->extra_hw_frames, ExtraHardwareFrames);
        AVBufferRef* hardwareDevice = nullptr;
        result = CreateDecodeHardwareDevice(&hardwareDevice, deviceType, decodeAdapterIndex_, videoRenderer_);
        if (result < 0)
            setFailure("FFmpeg could not create the selected decode device: " + FfmpegError(result));
        if (result >= 0) {
            context->hw_device_ctx = av_buffer_ref(hardwareDevice);
            context->opaque = reinterpret_cast<void*>(static_cast<std::intptr_t>(selectedHardwareFormat));
            context->get_format = SelectHardwareFormat;
        }
        av_buffer_unref(&hardwareDevice);
    }
    if (result >= 0) result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        if (failureReason != nullptr && failureReason->empty())
            setFailure("FFmpeg could not open decoder " + std::string(codec->name == nullptr ? "unknown" : codec->name) +
                ": " + FfmpegError(result));
        avcodec_free_context(&context);
        return hardwareRequested ? FFFResult::NotSupported : FFFResult::FfmpegFailure;
    }
    *output = context;
    if (hardwarePixelFormat != nullptr) *hardwarePixelFormat = selectedHardwareFormat;
    return FFFResult::Success;
}

FFFResult PlayerSession::LoadCoverArt() noexcept {
    if (format_ == nullptr || coverArtStream_ < 0 ||
        coverArtStream_ >= static_cast<std::int32_t>(format_->nb_streams)) return FFFResult::InvalidState;
    auto* stream = format_->streams[coverArtStream_];
    if (stream->attached_pic.size <= 0) return FFFResult::NotSupported;
    AVCodecContext* decoder = nullptr;
    auto result = OpenDecoder(format_, coverArtStream_, true, &decoder, -1, nullptr, false);
    if (result != FFFResult::Success) return result;
    AVFrame* frame = av_frame_alloc();
    if (frame == nullptr) { avcodec_free_context(&decoder); return FFFResult::NativeFailure; }
    auto decodeResult = avcodec_send_packet(decoder, &stream->attached_pic);
    if (decodeResult >= 0) decodeResult = avcodec_receive_frame(decoder, frame);
    if (decodeResult == AVERROR(EAGAIN)) {
        avcodec_send_packet(decoder, nullptr);
        decodeResult = avcodec_receive_frame(decoder, frame);
    }
    if (decodeResult < 0) result = FFFResult::FfmpegFailure;
    else {
        if (coverArtFrame_ != nullptr) av_frame_free(&coverArtFrame_);
        coverArtFrame_ = av_frame_clone(frame);
        if (coverArtFrame_ == nullptr) result = FFFResult::NativeFailure;
        else result = videoRenderer_.Render(coverArtFrame_, HdrProcessor::kUnknownFrameIndex, true, true);
    }
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    return result;
}

FFFResult PlayerSession::OpenHardwareVideoDecoder(AVFormatContext* owner, const std::int32_t index,
    AVCodecContext** output, std::string* failureReason) noexcept {
    if (owner == nullptr || index < 0 || index >= static_cast<std::int32_t>(owner->nb_streams))
        return FFFResult::InvalidArgument;
    // There is exactly one gate and it is a measurement, not a prediction: each
    // backend's decoder is opened and then asked to produce a frame from the
    // stream's own first packets (HardwareDecoderProducesFrame).
    //
    // Nothing here decides by pixel format, codec, resolution or device. Every such
    // rule has to be maintained per hwaccel and per GPU generation, and a rule that
    // is wrong in the "unsupported" direction silently disables a path that works:
    // NVDEC decodes HEVC 4:4:4 from Turing onwards, H.264 4:4:4 on Turing and again
    // from Blackwell, and HEVC 4:2:2 from Blackwell, while DXVA has no 4:2:2 or
    // 4:4:4 decoder surface at all. The same stream is therefore supported and
    // unsupported depending on the backend, and only asking tells them apart.
    //
    // Doing it here -- inside the open, before the presentation queue, the media
    // info and the seek path exist -- is what makes it cheap. The same failure
    // discovered later during playback costs DoSeek + ClearVideoQueue +
    // RebuildMediaInfo; discovered here it costs one avio seek back to where the
    // reader already was.
    const AVHWDeviceType Backends[] = {
        // Prefer the renderer's own D3D11 device: decoded surfaces can then be
        // sampled directly without a GPU-to-CPU-to-GPU round trip. CUDA remains
        // available for profiles rejected by D3D11VA.
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_CUDA,
    };
    // A still image is presented one frame and then kept, so the renderer's own device
    // is the preferred route: a D3D11 surface is sampled as-is, which is the path every
    // sampled file measures bit-exact on. A backend whose surfaces the renderer cannot
    // sample directly (CUDA) has to be consumed through a frame transfer into
    // semi-planar CPU memory, and that route measured two things against the CPU arm:
    //   - it is not bit-exact: the CUDA JPEG decode differs from libjpeg by 1/255 at a
    //     stripe edge (09h) and by 4/255 on a 4:4:4 sample, and a 4:2:0 surface loses
    //     the chroma detail of a 4:4:4 source outright;
    //   - it is slower: +64 ms to +196 ms measured, because the decoder is built for
    //     one frame that never amortises it.
    // Neither is worth paying while the alternative is a CPU decode of one frame, so
    // stills ask D3D11VA first and only extend the list to CUDA once it has refused: a
    // stream the renderer's own device will not take (4:4:4 HEVC, some AV1, MJPEG) then
    // still decodes on the GPU instead of falling all the way to the CPU. The backend
    // order is what makes this a fallback -- CUDA is only reached after a refusal,
    // never before. FFF3FP_HWCAP_STILL_CUDA=0 restores the D3D11VA-only list. Video
    // keeps the full list either way: there a CUDA or QSV frame is transferred every
    // frame anyway, and the semi-planar upload in VideoRenderer is what carries it.
    const std::size_t backendCount =
        (staticImage_ && !StillCudaFallbackEnabled()) ? 1u : std::size(Backends);
    std::string lastFailure = "No compatible hardware decoder was found.";
    // What this refusal says, told apart for the memory written at the end: only an
    // answer the GPU itself gave is worth remembering (see the comment on
    // HardwareRouteKnownUnsupported). A decoder that never opened says nothing.
    std::size_t hardwareCandidates = 0;
    std::size_t decoderOpenFailures = 0;
    std::size_t trialRefusals = 0;
    for (std::size_t backendIndex = 0; backendIndex < backendCount; ++backendIndex) {
        const auto backend = Backends[backendIndex];
        // A backend can expose both a dedicated wrapper and the native FFmpeg
        // decoder. Try both: profiles accepted by one parser are not always
        // accepted by the other, notably 4:2:2/4:4:4 and some AV1 streams.
        for (int dedicatedPass = 0; dedicatedPass < 2; ++dedicatedPass) {
            void* iterator = nullptr;
            while (const auto* codec = av_codec_iterate(&iterator)) {
                const auto* parameters = owner->streams[index]->codecpar;
                if (codec->id != parameters->codec_id || !av_codec_is_decoder(codec) ||
                    (IsDedicatedCudaDecoder(codec) ? 0 : 1) != dedicatedPass ||
                    FindHardwareFormat(codec, backend) == AV_PIX_FMT_NONE) continue;
                // 2026-10-21: ask the driver before building the decoder. The old path
                // trusted the pixel format the codec merely *declares* and only learned
                // that the driver disagrees on the first real packet, which costs a
                // DoSeek + ClearVideoQueue + RebuildMediaInfo round trip (measured
                // -15%..-30% fps on a 4:4:4 source). The question asked here is purely
                // "does a decoder configuration exist" -- never "is it fast enough",
                // because throughput has no standard API and single-stream decoding is
                // assumed to fit.
                if (decodeAdapterIndex_ < 0 && backend == AV_HWDEVICE_TYPE_D3D11VA && HardwareCapabilityProbeEnabled()) {
                    const auto* probe = owner->streams[index]->codecpar;
                    const auto support = videoRenderer_.QueryD3D11DecodeSupport(
                        static_cast<std::int32_t>(probe->codec_id), probe->profile,
                        static_cast<std::int32_t>(probe->format), probe->width, probe->height);
                    if (HardwareCapabilityTrace())
                        std::fprintf(stderr, "[hwcap] codec=%d %dx%d fmt=%d -> %s\n",
                            static_cast<int>(probe->codec_id), probe->width, probe->height,
                            static_cast<int>(probe->format),
                            support == PlayerVideoRenderer::D3D11DecodeSupport::Supported ? "supported"
                                : support == PlayerVideoRenderer::D3D11DecodeSupport::Unsupported
                                    ? "unsupported" : "unknown");
                    if (support == PlayerVideoRenderer::D3D11DecodeSupport::Unsupported) {
                        lastFailure = "The D3D11 driver exposes no decoder configuration for this stream.";
                        continue;
                    }
                }
                ++hardwareCandidates;
                AVCodecContext* candidate = nullptr;
                std::int32_t hardwareFormat = AV_PIX_FMT_NONE;
                std::string candidateFailure;
                if (OpenDecoder(owner, index, true, &candidate, backend, &hardwareFormat,
                    true, codec, &candidateFailure) != FFFResult::Success) {
                    ++decoderOpenFailures;
                    if (!candidateFailure.empty()) lastFailure = std::move(candidateFailure);
                    continue;
                }
                // The gate. This backend has just proved it can open a decoder for
                // this codec, which says nothing about whether it can decode this
                // stream, so it now has to produce a frame from the stream's own
                // first packets before it is accepted.
                if (HardwareTrialDecodeEnabled() &&
                    !HardwareDecoderProducesFrame(owner, index, candidate, &candidateFailure)) {
                    ++trialRefusals;
                    if (candidateFailure.empty())
                        candidateFailure = "The GPU decoder produced no frame from this stream.";
                    lastFailure = std::move(candidateFailure);
                    avcodec_free_context(&candidate);
                    continue;
                }
                *output = candidate;
                return FFFResult::Success;
            }
        }
    }
    // Two answers are worth keeping. No hardware candidate at all means this codec has
    // no hardware decoder here, and a trial refusal means the driver was asked and said
    // no. Anything that involved a decoder failing to open is left out on purpose: it
    // may have run out of concurrent decode sessions rather than capability.
    const auto refusedByCapability = hardwareCandidates == 0 || trialRefusals > 0;
    if (HardwareRouteMemoryEnabled() && refusedByCapability && decoderOpenFailures == 0) {
        RememberUnsupportedHardwareRoute(owner->streams[index]);
        if (HardwareCapabilityTrace())
            std::fprintf(stderr, "[hwcap] memory stream=%d remembered (candidates=%zu trialRefused=%zu openFailed=%zu)\n",
                index, hardwareCandidates, trialRefusals, decoderOpenFailures);
    }
    if (failureReason != nullptr) *failureReason = std::move(lastFailure);
    return FFFResult::NotSupported;
}

bool PlayerSession::HardwareRouteKnownUnsupported(const AVStream* stream) const noexcept {
    if (stream == nullptr || stream->codecpar == nullptr) return false;
    const auto key = HardwareRouteKey(stream->codecpar);
    for (const auto remembered : unsupportedHardwareRoutes_)
        if (remembered == key) return true;
    return false;
}

void PlayerSession::RememberUnsupportedHardwareRoute(const AVStream* stream) noexcept {
    if (stream == nullptr || stream->codecpar == nullptr) return;
    const auto key = HardwareRouteKey(stream->codecpar);
    for (const auto remembered : unsupportedHardwareRoutes_)
        if (remembered == key) return;
    try { unsupportedHardwareRoutes_.push_back(key); } catch (...) {}
}

bool PlayerSession::HardwareDecoderProducesFrame(AVFormatContext* owner, const std::int32_t index,
    AVCodecContext* decoder, std::string* failureReason) noexcept {
    if (owner == nullptr || decoder == nullptr) return false;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    auto finish = [&](const bool accept, std::string detail) noexcept {
        av_frame_free(&frame);
        av_packet_free(&packet);
        // Leave the decoder empty: whatever it buffered during the trial is not part
        // of playback, which starts from the beginning of the stream.
        avcodec_flush_buffers(decoder);
        if (!accept && failureReason != nullptr)
            *failureReason = detail.empty()
                ? "The GPU decoder produced no frame from this stream; playback continued with CPU decoding."
                : "The GPU decoder rejected this stream (" + detail + "); playback continued with CPU decoding.";
        if (HardwareCapabilityTrace())
            std::fprintf(stderr, "[hwcap] trial stream=%d accept=%d err=%s\n",
                index, accept ? 1 : 0, detail.c_str());
        return accept;
    };
    if (packet == nullptr || frame == nullptr) return finish(true, "out of memory");
    // Read the trial packets from a context of our own. Rewinding the caller's
    // context instead -- avio_seek back plus avformat_flush -- restores the byte
    // position but not the demuxer's own read state, and measured worse than not
    // checking at all: a 3 s HEVC stream decoded 0 of 90 frames and playback ended
    // immediately. Playback's context is therefore never touched here.
    //
    // That second context goes through OpenProbeFormat, so it carries the session's
    // own IO policy and an interrupt callback tied to terminate_; the bare url this
    // used to open skipped the protocol lists, the repaired-memory input and the
    // demuxer gate, and could not be cancelled at all.
    //
    // A trial that cannot be run accepts the decoder: missing information is not a
    // verdict, and the pre-existing fallback in DecodePacket still covers a decoder
    // that fails later. A cancelled trial is in the same class.
    if (owner->url == nullptr || owner->url[0] == '\0') return finish(true, "no url");
    AVFormatContext* probe = nullptr;
    std::unique_ptr<SharedFileInput> probeIo;
    std::string probeError;
    if (OpenProbeFormat(owner->url, &probe, probeIo, probeError) != FFFResult::Success ||
        probe == nullptr)
        return finish(true, "reopen failed: " + probeError);
    const auto streamCountMatches = probe->nb_streams == owner->nb_streams;
    bool produced = false;
    bool cancelled = false;
    std::string detail;
    // 试解的中间量。`err=` 为空时旧 trace 只说"没解出帧"，分不清是没读到包、包没落到
    // 这条流、还是解码器真拒了 —— 而这三者的修法方向相反，所以必须把计数打出来。
    int readCount = 0;
    int matchedCount = 0;
    int eagainCount = 0;
    int seenMask = 0;
    std::string stopDetail;
    if (streamCountMatches) {
        // First frame only, and that is the intended scope. A frame coming out of the
        // decoder answers the question this gate asks -- this backend can decode this
        // stream -- because the decoder configuration has already been negotiated and
        // the first frame is the one that exercises it. A decoder that fails on a later
        // frame is a different failure (a damaged slice, a mid-stream format change)
        // and it is handled where it happens, by the pre-existing fallback in
        // DecodePacket, which rewinds and rebuilds once instead of guessing per frame.
        // Decoding N frames here would add demux and decode work to every open without
        // answering a second question, so N stays 1.
        // Enough packets to cover a decoder with output delay plus a reopened GOP;
        // stop as soon as a frame arrives, which for nearly every stream is the first
        // packet of the stream.
        //
        // The 32 budget belongs to *this* stream. Counting every packet instead let an
        // audio-dense prefix eat the whole budget before a single video packet arrived:
        // measured on an AV1/Opus WebM (ffprobe: 0=av1 video, 1=opus audio) the trial
        // read 32 packets, matched 0, saw no error, and then rejected hardware decode
        // with an empty reason -- and remembered the route as unsupported for the
        // session. The same elementary stream remuxed into MP4 passed on packet 1, so
        // the defect was the budget, not the decoder, the codec, the bit depth or the
        // encoder. `reads` bounds the work so a stream that never carries this index
        // cannot spin the loop.
        for (int reads = 0, fed = 0; reads < 512 && fed < 32 && !produced; ++reads) {
            // Shutdown must not wait out the whole budget on a slow source; the
            // destructor joins this thread without a timeout.
            if (terminate_.load(std::memory_order_acquire)) {
                cancelled = true;
                detail = "cancelled";
                stopDetail = "cancelled";
                break;
            }
            const auto readResult = av_read_frame(probe, packet);
            if (readResult < 0) {
                if (readResult == AVERROR_EXIT) cancelled = true;
                if (cancelled) detail = "cancelled";
                stopDetail = cancelled ? "cancelled" : FfmpegError(readResult);
                break;
            }
            ++readCount;
            seenMask |= (packet->stream_index >= 0 && packet->stream_index < 31)
                ? (1 << packet->stream_index) : 0;
            if (packet->stream_index != index) { av_packet_unref(packet); continue; }
            ++fed;
            ++matchedCount;
            const auto sendResult = avcodec_send_packet(decoder, packet);
            av_packet_unref(packet);
            if (sendResult < 0) { detail = FfmpegError(sendResult); stopDetail = detail; break; }
            const auto receiveResult = avcodec_receive_frame(decoder, frame);
            if (receiveResult == 0) produced = true;
            else if (receiveResult == AVERROR(EAGAIN)) ++eagainCount;
            else if (receiveResult != AVERROR_EOF) {
                detail = FfmpegError(receiveResult); stopDetail = detail; break;
            }
        }
        // A decoder with output delay answers EAGAIN until it is drained, and draining
        // is safe here: the decoder is flushed on the way out either way. A cancelled
        // trial does not drain -- there is nothing left to learn from it.
        if (!produced && !cancelled && detail.empty() &&
            avcodec_send_packet(decoder, nullptr) == 0 &&
            avcodec_receive_frame(decoder, frame) == 0) produced = true;
    } else detail = "stream count differs";
    if (HardwareCapabilityTrace())
        std::fprintf(stderr,
            "[hwcap] trial detail stream=%d fmt=%s extradata=%dB read=%d matched=%d eagain=%d"
            " seen=%08x probeStreams=%d probeCodec0=%d wantCodec=%d stop=%s produced=%d\n",
            index,
            (probe != nullptr && probe->iformat != nullptr) ? probe->iformat->name : "?",
            decoder->extradata_size, readCount, matchedCount, eagainCount, seenMask,
            probe != nullptr ? static_cast<int>(probe->nb_streams) : -1,
            (probe != nullptr && probe->nb_streams > 0 &&
                probe->streams[0]->codecpar != nullptr)
                ? static_cast<int>(probe->streams[0]->codecpar->codec_id) : -1,
            static_cast<int>(decoder->codec_id),
            stopDetail.empty() ? "-" : stopDetail.c_str(), produced ? 1 : 0);
    CloseFormat(&probe, probeIo);
    return finish(produced || !streamCountMatches || cancelled, detail);
}

void PlayerSession::DoOpen(std::string path) noexcept {
    // Keep the flip-model chain for a same-HWND media switch. DXGI can reject
    // an immediate replacement while the previous chain is still retiring.
    DoClose(FFF3FPState::Opening, true);
    // A new file is a new audio-pool round: back to the default target.
    audioTargetBuffer100ns_ = performance_.AudioInitial();
    seenAudioUnderruns_ = 0;
    seenAudioJitterFrames_ = 0;
    seenAudioDiscontinuities_ = 0;
    seenAudioInsertedSilence_ = 0;
    seenAudioDroppedOverlap_ = 0;
    std::string openError;
    auto openResult = FFFResult::Success;
    if (DiscInput::IsDiscPath(path)) {
        try {
            disc_ = std::make_unique<DiscInput>(discCancel_);
            if (!disc_->Open(path) || !disc_->OpenDemux(&format_)) {
                openResult = FFFResult::NotSupported; openError = disc_->Error();
            } else discOpened_.store(true, std::memory_order_release);
        } catch (...) { openResult = FFFResult::NativeFailure; openError = "Could not open the disc."; }
    } else openResult = OpenFormat(path, &format_, formatIo_, openError);
    if (openResult != FFFResult::Success) { Fail(openResult, std::move(openError), "open"); return; }
    // Remembered so the IAMF decoder can re-open the file and drive the raw OBU stream.
    mediaPath_ = path;
    videoStream_ = FindDefaultOrFirstStream(format_, AVMEDIA_TYPE_VIDEO);
    animatedImage_ = IsLoopAwareImageDemuxer(format_ != nullptr ? format_->iformat : nullptr);
    // An AVIF/HEIC image sequence keeps its animation on a later track than the
    // primary still item, so switch to that track before anything reads the
    // frame count - otherwise the file is mistaken for a one-frame picture.
    if (const auto animationStream = FindAnimatedImageStream(format_); animationStream >= 0) {
        videoStream_ = animationStream;
        animatedImage_ = true;
    }
    // The video stream is final for this open: capture its stream-level side
    // data (container records the demuxer declares once at binding time) so
    // the host can read them through FFF3FP_CopyStreamSideData. The guard
    // covers the audio-only / cover-art-less case, where there is no stream
    // to bind. Clear first so a capture failure cannot leave stale records
    // from a previous media.
    ClearStreamSideData();
    if (videoStream_ >= 0) CacheStreamSideDataFromStream(format_->streams[videoStream_]);
    // Select session rewind only after stream classification is available.
    sessionWrapLoop_ = IsAnimatedImageSequence(format_);
    staticImage_ = IsStillImage(format_, videoStream_);
    // A tiled still image is one stream per tile, so the stream picked above is only
    // the first tile. Recognize such a file from its tile grid group and read every
    // tile's packets now, while the demuxer is still at the start of the file: the
    // canvas is assembled from them, and a still image has no timeline to rewind.
    ResetTileGrid();
    if (staticImage_ &&
        AvifTileGridComposer::Describe(format_, tileGridWidth_, tileGridHeight_,
            tileGridTiles_, tileGridBackground_)) {
        tileGrid_ = true;
        ReadTileGridPackets();
    }
    // Rotation is a *view* property, so it is applied through the same renderer
    // path the image mode and the host's manual R key use. Decoding stays
    // untouched: the frame keeps its native orientation and only the sampling
    // coordinate (plus the fit box) turns. That keeps the display matrix out of
    // the pixel pipeline, where it would otherwise have to be re-applied after
    // every hardware-transfer / colour step.
    sourceRotation_ = SourceRotationQuarterTurns(format_, videoStream_);
    (void)videoRenderer_.SetViewRotation(sourceRotation_);
    // Still images are uploaded as one texture, so an oversized source can only
    // fail later inside the renderer ("open-image" with an opaque reason).
    // Reject it here with the real limit instead. Video is not checked here and
    // keeps its existing behaviour.
    if (staticImage_) {
        // A tiled still is checked against the canvas it assembles into, not against
        // the single tile the stream points at.
        const auto imageWidth = tileGrid_ ? tileGridWidth_
            : format_->streams[videoStream_]->codecpar->width;
        const auto imageHeight = tileGrid_ ? tileGridHeight_
            : format_->streams[videoStream_]->codecpar->height;
        if (imageWidth > MaxStillImageDimension ||
            imageHeight > MaxStillImageDimension) {
            Fail(FFFResult::NotSupported,
                "The still image is too large to display: " +
                std::to_string(imageWidth) + "x" +
                std::to_string(imageHeight) +
                " exceeds the limit of " + std::to_string(MaxStillImageDimension) +
                " pixels per side.", "open-image-size");
            return;
        }
    }
    coverArtStream_ = FindCoverArtStream(format_);
    audioStream_ = FindDefaultOrFirstStream(format_, AVMEDIA_TYPE_AUDIO);
    if (videoStream_ < 0 && audioStream_ < 0) { Fail(FFFResult::NotSupported, "The file contains no playable video or audio stream.", "open"); return; }
    snapshot_.decodeMode = decodeMode_;
    bool hardwareFallback = false;
    std::string hardwareFailureReason;
    if (videoStream_ >= 0) {
        // Still images are included here on purpose: the hardware decoder is tried
        // first for every media type and the retry below takes the CPU decoder when the
        // GPU refuses. Formats with no hardware path (PNG, WebP, TIFF, JPEG XL, JXR) are
        // refused inside the hardware open before any device is created, so probing them
        // costs nothing.
        // A tiled still is assembled on the CPU, so a hardware decoder would only
        // add a device per tile and a download per tile: ask for the CPU decoder
        // outright and say so in the snapshot, which reports the route actually used.
        // A stream the GPU already refused in this session is not asked again: the
        // attempt costs a decoder open plus a reopened demux for the trial packets,
        // and the answer cannot have changed.
        const auto tryHardware = decodeMode_ == FFF3FPDecodeMode::D3D11 &&
            !HardwareRouteKnownUnsupported(format_->streams[videoStream_]);
        auto result = tileGrid_ || !tryHardware
            ? OpenDecoder(format_, videoStream_, true, &videoDecoder_, -1, nullptr, false)
            : OpenHardwareVideoDecoder(format_, videoStream_, &videoDecoder_, &hardwareFailureReason);
        if (result == FFFResult::Success && tileGrid_) snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
        if (result == FFFResult::Success && !tileGrid_ && !tryHardware &&
            decodeMode_ == FFF3FPDecodeMode::D3D11) {
            // Took the CPU route from memory. Report the route actually used, and say
            // why, so a host that surfaces the fallback can explain it.
            snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
            hardwareFallback = true;
            hardwareFailureReason = "The GPU decoder was rejected for this stream earlier in this session; playback is using CPU decoding.";
        }
        if (result != FFFResult::Success && decodeMode_ == FFF3FPDecodeMode::D3D11 && !tileGrid_) {
            // OpenDecoder's default consults decodeMode_, so the retry has to ask for
            // the software decoder explicitly or it fails the same way.
            result = OpenDecoder(format_, videoStream_, true, &videoDecoder_, -1, nullptr, false);
            if (result == FFFResult::Success) {
                snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
                hardwareFallback = true;
            }
        }
        if (result != FFFResult::Success) { Fail(result, "Could not open a hardware or software video decoder.", "open"); return; }
    }
    if (audioStream_ >= 0) {
        // IAMF first: FFmpeg cannot mix an IAMF element's substreams, so its
        // decode of the first substream is a fraction of the content. Try the AOM
        // reference decoder, and fall back to FFmpeg if it cannot handle this
        // particular file -- libiamf has real gaps (FLAC carriage is unimplemented,
        // some HOA layouts fail to configure), and a file that plays in stereo is
        // better than one that does not play at all.
        if (TryOpenIamfAudioDecoder()) {
            audioDecoder_ = nullptr;
        } else if (OpenDecoder(format_, audioStream_, false, &audioDecoder_) != FFFResult::Success) {
            audioStream_ = -1;
        }
    }
    videoRenderer_.ConfigureHdrStream(videoStream_ >= 0 ?
        format_->streams[videoStream_]->codecpar : nullptr);
    if (audioStream_ >= 0 && !audioExclusive_) {
        std::string audioError;
        const auto result = RecreateAudioRenderer(audioEndpointId_, audioExclusive_, true, audioError);
        if (result != FFFResult::Success) {
            // The endpoint may currently be held in system-level exclusive
            // mode. Opening the media must still succeed; Play retries the
            // shared renderer and publishes a user-facing availability notice.
            audioRenderer_.reset();
        }
    }
    if (videoStream_ < 0 && coverArtStream_ >= 0) {
        const auto result = LoadCoverArt();
        if (result != FFFResult::Success) { Fail(result, "Could not decode or render the attached cover art.", "cover-art"); return; }
    }
    bool forcedSdrOutput = false;
    // Source bit depth of the video stream, for the scRGB-path gate below.
    // Computed from codec parameters the same way the open event's media
    // details do, because the decoder-level value is not populated yet at
    // this point in DoOpen.
    std::uint32_t sourceBitDepth = 0;
    if (videoStream_ >= 0) {
        const auto* parameters = format_->streams[videoStream_]->codecpar;
        sourceBitDepth = static_cast<std::uint32_t>(std::max(
            parameters->bits_per_raw_sample,
            PixelFormatBitDepth(static_cast<AVPixelFormat>(parameters->format))));
    }
    {
        std::lock_guard lock(mutex_);
        snapshot_.duration100ns = IsLoopAwareImageDemuxer(format_->iformat)
            ? 0 : EstimateDuration100ns(format_);
        snapshot_.position100ns = 0; snapshot_.frameIndex = -1; snapshot_.framePts = AV_NOPTS_VALUE;
        snapshot_.selectedVideoStream = videoStream_; snapshot_.selectedAudioStream = audioStream_;
        snapshot_.videoWidth = snapshot_.videoHeight = 0; snapshot_.isHdrSource = 0;
        // A tiled still image is presented as its canvas, so that is what the host
        // has to see: the decoder belongs to one tile and would report a fraction.
        if (videoStream_ >= 0) {
            snapshot_.videoWidth = tileGrid_ ? tileGridWidth_ : videoDecoder_->width;
            snapshot_.videoHeight = tileGrid_ ? tileGridHeight_ : videoDecoder_->height;
            ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
        }
        else if (coverArtFrame_ != nullptr) { snapshot_.videoWidth = coverArtFrame_->width; snapshot_.videoHeight = coverArtFrame_->height; snapshot_.isHdrSource = 0; }
        // Downgrade only if HDR/wide-gamut output is unachievable; EnsureSwapChain
        // rechecks this gate before selecting the actual format. The scRGB
        // presentation path is not exclusive to HDR sources -- Auto SDR admits
        // a >8-bit SDR source onto it -- so WantsScRgbPresentationPath must be
        // part of the gate. Without it, a 10-bit SDR file requested on the HDR
        // mode is downgraded here even though the renderer would have kept the
        // scRGB chain, and the ColorModeChanged event then contradicts the
        // renderer's own decision.
        if (snapshot_.isHdrSource == 0 && !videoRenderer_.IsWideGamutSource() &&
            !videoRenderer_.WantsScRgbPresentationPath(sourceBitDepth) &&
            snapshot_.requestedColorMode == FFF3FPColorMode::MapToHdr) {
            snapshot_.requestedColorMode = FFF3FPColorMode::MapToSdr;
            forcedSdrOutput = true;
        }
    }
    if (forcedSdrOutput) {
        const auto resetResult = videoRenderer_.ForceSdrOutputForSdrSource();
        if (resetResult != FFFResult::Success) {
            Fail(resetResult, videoRenderer_.LastError(), "sdr-output-reset");
            return;
        }
        snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    }
    framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = false;
    seekTarget100ns_ = -1; seekTargetFrame_ = -1; keyframeSeekPending_ = false;
    lastVideoFrameDuration100ns_ = 0; nextUntimedVideoPosition100ns_ = 0; draining_ = false; ResetCadenceSpread();
    // A media change restarts every adaptive response: the pacer's floor lives in the
    // renderer, so it has to be released here or the next file inherits this file's
    // deficit cadence.
    if (paceEngaged_) ReleasePresentationPace("media-change");
    // Fresh media identity: panel-only until the ceiling is (re)published below.
    // DoOpen's leading DoClose already cleared it, this makes the line self-evident.
    videoRenderer_.SetMediaFrameRateCeilingHz(0.0);
    ArmAudioUntilVideoFrame();
    // Ready sessions do not pump playback, so present the first image frame here.
    if (staticImage_ || animatedImage_) {
        const auto imageResult = DecodeInitialFrame();
        if (imageResult != FFFResult::Success) {
            Fail(imageResult, animatedImage_
                ? "Could not decode or render the first frame of the animation during open."
                : "Could not decode or render the still image during open.",
                animatedImage_ ? "open-animation" : "open-image");
            return;
        }
    }
    RebuildMediaInfo(); SetState(FFF3FPState::Ready, "open");
    // Publish the image details once, from the worker thread while the decoded
    // frames (and their side data) are still owned by this one - GetImageInfo
    // hands out this copy under snapshotMutex_ instead of touching them.
    if (staticImage_ || animatedImage_) {
        BuildImageInfo(publishedImageInfo_);
        { std::lock_guard lock(snapshotMutex_); hasPublishedImageInfo_ = true; }
    }
    PublishDisc();
    Emit(FFF3FPEvent::OpenCompleted, "{\"success\":true}");
    if (hardwareFallback)
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"video\",\"decodeMode\":1,\"fallback\":true,\"reason\":\"" +
            EscapeJson(hardwareFailureReason.empty()
                ? "The GPU rejected the video stream; playback is using CPU decoding."
                : hardwareFailureReason) + "\"}");
    if (forcedSdrOutput)
        // "requested" is the host's original ask (MapToHdr == 2 -- the
        // downgrade only fires in that case) and "actual" is read from the
        // renderer after ForceSdrOutputForSdrSource. The old text emitted
        // requested:0 next to a reason explaining a MapToHdr refusal, which
        // contradicted itself and made hosts show "no change" for a change.
        Emit(FFF3FPEvent::ColorModeChanged,
            "{\"requested\":2,\"actual\":" +
            std::to_string(static_cast<unsigned>(snapshot_.actualColorMode)) +
            ",\"reason\":\"" +
            EscapeJson(videoRenderer_.FallbackReason().empty()
                ? std::string("True HDR output is only available for HDR or wide-gamut sources.")
                : videoRenderer_.FallbackReason()) + "\"}");
    else if (snapshot_.requestedColorMode == FFF3FPColorMode::MapToHdr) {
        // Same rule as the branch above: compare and report the renderer's
        // actual mode from the same read, so a host never sees a claimed change
        // the renderer did not make. The per-frame path re-emits with the
        // settled value once the first frame has been presented.
        const auto actualMode = videoRenderer_.ActualColorMode();
        if (actualMode != snapshot_.requestedColorMode)
            Emit(FFF3FPEvent::ColorModeChanged,
                "{\"requested\":2,\"actual\":" +
                std::to_string(static_cast<unsigned>(actualMode)) +
                ",\"reason\":\"" + EscapeJson(videoRenderer_.FallbackReason()) + "\"}");
    }
}

void PlayerSession::ResetTileGrid() noexcept {
    tileGrid_ = false;
    tileGridWidth_ = 0;
    tileGridHeight_ = 0;
    for (auto& byte : tileGridBackground_) byte = 0;
    tileGridTiles_.clear();
    AvifTileGridComposer::ReleaseSources(tileGridSources_);
}

void PlayerSession::ReadTileGridPackets() noexcept {
    if (format_ == nullptr || !tileGrid_) return;
    // One bucket per tile stream. The whole file is walked once: a grid still is a
    // handful of packets and there is no timeline to come back for them later.
    tileGridSources_.clear();
    for (const auto& tile : tileGridTiles_) {
        auto& source = tileGridSources_.emplace_back();
        source.streamIndex = tile.streamIndex;
    }
    auto* packet = av_packet_alloc();
    if (packet == nullptr) { ResetTileGrid(); return; }
    while (av_read_frame(format_, packet) >= 0) {
        for (auto& source : tileGridSources_) {
            if (source.streamIndex != packet->stream_index) continue;
            auto* copy = av_packet_alloc();
            if (copy == nullptr || av_packet_ref(copy, packet) < 0) {
                av_packet_free(&copy);
                av_packet_unref(packet);
                av_packet_free(&packet);
                ResetTileGrid();
                return;
            }
            source.packets.push_back(copy);
            break;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    for (const auto& source : tileGridSources_) {
        if (source.packets.empty()) { ResetTileGrid(); return; }
    }
}

FFFResult PlayerSession::ComposeTileGridFrame() noexcept {
    if (format_ == nullptr) return FFFResult::InvalidState;
    auto* canvas = AvifTileGridComposer::Compose(format_, tileGridTiles_, tileGridSources_,
        tileGridWidth_, tileGridHeight_, tileGridBackground_);
    if (canvas == nullptr) return FFFResult::FfmpegFailure;
    const auto before = videoRenderer_.PresentedVideoFrames();
    NormalizeVideoFrameTimestamp(canvas);
    PresentVideoFrame(canvas, format_);
    av_frame_free(&canvas);
    // The canvas is the only frame this picture has, so a presentation that did not
    // happen is a failed open rather than a silently empty one.
    if (videoRenderer_.PresentedVideoFrames() == before) return FFFResult::FfmpegFailure;
    return FFFResult::Success;
}

FFFResult PlayerSession::DecodeInitialFrame() noexcept {
    if (format_ == nullptr || videoDecoder_ == nullptr || videoStream_ < 0)
        return FFFResult::InvalidState;
    if (!staticImage_ && !animatedImage_) return FFFResult::InvalidState;
    // A tiled still image has nothing to read: its packets were taken at open and
    // the picture is the canvas assembled from them.
    if (tileGrid_) return ComposeTileGridFrame();
    if (playbackPacket_ == nullptr) playbackPacket_ = av_packet_alloc();
    if (playbackPacket_ == nullptr) return FFFResult::NativeFailure;
    if (stillImagePacket_ == nullptr) stillImagePacket_ = av_packet_alloc();
    if (stillImagePacket_ == nullptr) return FFFResult::NativeFailure;
    av_packet_unref(stillImagePacket_);
    const auto before = videoRenderer_.PresentedVideoFrames();
    int readResult = 0;
    while (videoRenderer_.PresentedVideoFrames() == before &&
        (readResult = av_read_frame(format_, playbackPacket_)) >= 0) {
        if (playbackPacket_->stream_index == videoStream_) {
            if (staticImage_) {
                av_packet_unref(stillImagePacket_);
                if (av_packet_ref(stillImagePacket_, playbackPacket_) < 0) return FFFResult::NativeFailure;
            }
            const auto decodeResult = DecodePacket(videoDecoder_, playbackPacket_, true, format_);
            av_packet_unref(playbackPacket_);
            if (decodeResult != FFFResult::Success) return decodeResult;
        } else {
            av_packet_unref(playbackPacket_);
        }
    }
    if (videoRenderer_.PresentedVideoFrames() == before) {
        const auto drainResult = DecodePacket(videoDecoder_, nullptr, true, format_);
        if (drainResult != FFFResult::Success) return drainResult;
    }
    // Still-image retry after the GPU refused the packet: FallbackToSoftwareVideoDecoder
    // has already swapped in the CPU decoder, but image2 and the *_pipe demuxers have no
    // timeline to seek back to, so the packet cannot be re-read from the demuxer. Submit
    // the retained copy instead of failing the open.
    if (staticImage_ && videoRenderer_.PresentedVideoFrames() == before &&
        stillImagePacket_->size > 0 && snapshot_.decodeMode == FFF3FPDecodeMode::Cpu) {
        // The CPU decoder swapped in by the fallback has already been drained with an
        // EOF packet above, so it refuses this one unless its buffers are reset first.
        avcodec_flush_buffers(videoDecoder_);
        const auto retryResult = DecodePacket(videoDecoder_, stillImagePacket_, true, format_);
        av_packet_unref(stillImagePacket_);
        if (retryResult != FFFResult::Success) return retryResult;
        const auto retryDrain = DecodePacket(videoDecoder_, nullptr, true, format_);
        if (retryDrain != FFFResult::Success) return retryDrain;
    }
    const auto* stream = format_->streams[videoStream_];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    if (av_seek_frame(format_, videoStream_, start, AVSEEK_FLAG_BACKWARD) >= 0)
    {
        avcodec_flush_buffers(videoDecoder_);
    }
    if (state_.load() == FFF3FPState::Failed) return FFFResult::DeviceFailure;
    if (videoRenderer_.PresentedVideoFrames() <= before) return FFFResult::FfmpegFailure;
    // Still images carry their EXIF orientation as FRAME side data, which only
    // exists after the first decode -- the open-time stream probe cannot see it
    // (measured: a stream-level lookup on the Orientation=6 JPEG finds nothing,
    // while the decoded frame reports rotation=3). Apply it now so an image is
    // presented already upright, with no visible first-frame flip.
    ApplyRotationFromDecodedFrame();
    return FFFResult::Success;
}

// Re-reads the rotation from the most recently decoded frame and applies it when
// it differs from what the stream reported at open time. Frame side data wins:
// for still images it is the only source, and for video a per-frame matrix is
// more specific than the container tag.
void PlayerSession::ApplyRotationFromDecodedFrame() noexcept {
    const auto* frame = stillImageFrame_ != nullptr ? stillImageFrame_ : videoDecodeFrame_;
    if (frame == nullptr) return;
    const auto* matrix = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
    if (matrix == nullptr || matrix->size < 9 * sizeof(std::int32_t)) return;
    const auto angle = av_display_rotation_get(
        reinterpret_cast<const std::int32_t*>(matrix->data));
    if (angle != angle) return; // NaN: no usable matrix
    auto normalized = angle;
    while (normalized < 0.0) normalized += 360.0;
    while (normalized >= 360.0) normalized -= 360.0;
    const auto turns = static_cast<std::uint32_t>(normalized / 90.0 + 0.5) % 4u;
    if (turns == sourceRotation_) return;
    sourceRotation_ = turns;
    (void)videoRenderer_.SetViewRotation(turns);
}

std::uint32_t PlayerSession::DecoderThreadBudget() const noexcept {
    // Total player threads stay below the CPU count: decoder workers + worker + audio
    // renderer + present thread + one core of scheduler headroom.
    const auto hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    constexpr std::uint32_t Reserved = ReservedPlaybackThreads + SchedulerHeadroomThreads;
    return hardwareThreads > Reserved ? hardwareThreads - Reserved : 1u;
}

std::uint32_t PlayerSession::SelectSoftwareDecoderThreads(const std::uint32_t width,
    const std::uint32_t height) const noexcept {
    const auto budget = DecoderThreadBudget();
    if (!adaptiveThreadsEnabled_) {
        // Fixed policy shape, now also capped by the thread budget: on a small machine
        // the decoder plus the player's own threads used to reach the CPU count (eight
        // workers on an eight-thread box), which the budget exists to prevent.
        const auto largeFrame = width >= 3840 && height >= 2160;
        const auto limit = largeFrame ? 16u : MaximumSoftwareDecoderThreads;
        return (std::min)(limit, budget);
    }

    // Load-driven, not resolution-driven. Resolution says nothing about whether this
    // machine can decode this codec: an 8K file that starves a 4-core laptop is
    // comfortable on a 20-thread desktop, and the reverse holds for heavy 4K AV1. The
    // rung is therefore decided by measured load (EvaluateDecoderLoad).
    //
    // Start at the floor -- 4 workers by default -- and only ever climb. An earlier
    // revision started at 16 because a measured 4K AV1 sample peaked there; that
    // permanently overshot bandwidth-bound sources (upstream measured 6 workers best
    // on 8K HEVC, 2.14x slower at 32) and on a 20-thread machine it consumed the whole
    // budget, leaving the ladder nothing to climb. The climb keeps that measurement's
    // caution: one rung per starving window, and no step jumps past the preferred
    // domain boundary in one go (see EvaluateDecoderLoad). Below roughly eight threads
    // the budget wins over the floor: four workers plus three player threads would
    // already reach the CPU count.
    const auto ceiling = (std::min)({ threadLadderMax_, 64u, budget });
    const auto floor = (std::min)(threadLadderMin_, ceiling);
    const auto wanted = decoderThreads_ == 0 ? floor : decoderThreads_;
    return std::clamp(wanted, std::max(floor, 1u), std::max(ceiling, 1u));
}

void PlayerSession::EvaluateDecoderLoad() noexcept {
    // CPU placement runs even when the host opted out of the thread ladder: they share
    // the trigger below but they are separate mechanisms.
    if (format_ == nullptr || videoStream_ < 0) return;
    if (snapshot_.decodeMode != FFF3FPDecodeMode::Cpu) return;
    if (videoDecoder_ == nullptr) return;

    // The one situation placement cannot improve: the decoder is at its thread budget,
    // so the software stack (workers + worker + audio + present + the scheduler
    // headroom) has claimed every logical processor the budget hands out. No affinity
    // move can add a core from here, so the process holds HIGH priority -- the rule is
    // "software threads == CPU threads, then priority is the lever". Prioritize, not
    // Boost: priority alone is the lever the rule asks for, and it leaves the CPU-set
    // tiers where the load path put them instead of freeing the decoder workers onto
    // the slower cores just to raise the class.
    const auto threadClaimExhausted =
        static_cast<std::uint32_t>(std::max(0, videoDecoder_->thread_count)) >=
        DecoderThreadBudget();
    if (threadClaimExhausted) placement_.Prioritize();

    const auto now = CurrentSteadyTicks();
    // A rung costs a reopen and a seek, so do not judge the new rung until its own
    // seek fallout has aged out. The deadline is wall-clock: the pump loop calls this
    // evaluator thousands of times per second, so a guard measured in loop iterations
    // (the earlier design) expired in milliseconds instead of covering the re-prime.
    if (ladderSettlingUntil100ns_ != 0) {
        if (now < ladderSettlingUntil100ns_) return;
        ladderSettlingUntil100ns_ = 0;
    }

    constexpr std::int64_t Window100ns = 20'000'000;   // 2 seconds
    if (ladderWindowStart100ns_ == 0) {
        // (Re)arm the window and capture the counter baselines here, at the moment the
        // window actually starts. Zeroing them at a rung change instead made the first
        // window after the reopen subtract from zero -- i.e. from everything since the
        // session started -- which always read as a burst of surplus frames and hence
        // as a healthy window.
        ladderWindowStart100ns_ = now;
        ladderWindowDecoded_ = snapshot_.decodedVideoFrames;
        ladderWindowPresented_ = snapshot_.presentedVideoFrames;
        pumpIterations_ = 0;
        pumpSaturatedIterations_ = 0;
        pumpQueueBoundIterations_ = 0;
        pumpStarvedIterations_ = 0;
        ladderWindowUpload100ns_ = snapshot_.videoUpload100ns;
        ladderWindowConvert100ns_ = snapshot_.softwareConvert100ns;
        ladderWindowDropped_ = snapshot_.droppedVideoFrames;
        return;
    }
    const auto elapsed = now - ladderWindowStart100ns_;
    if (elapsed < Window100ns) return;

    const auto decoded = snapshot_.decodedVideoFrames;
    const auto presented = snapshot_.presentedVideoFrames;
    const auto decodedDelta = decoded - ladderWindowDecoded_;
    const auto presentedDelta = presented - ladderWindowPresented_;
    ladderWindowDecoded_ = decoded;
    ladderWindowPresented_ = presented;
    ladderWindowStart100ns_ = now;
    const auto windowPumpIters = pumpIterations_;
    const auto windowSaturatedIters = pumpSaturatedIterations_;
    const auto windowBoundIters = pumpQueueBoundIterations_;
    const auto windowStarvedIters = pumpStarvedIterations_;
    pumpIterations_ = 0;
    pumpSaturatedIterations_ = 0;
    pumpQueueBoundIterations_ = 0;
    pumpStarvedIterations_ = 0;
    const auto windowUpload100ns = snapshot_.videoUpload100ns - ladderWindowUpload100ns_;
    const auto windowConvert100ns = snapshot_.softwareConvert100ns - ladderWindowConvert100ns_;
    const auto droppedDelta = snapshot_.droppedVideoFrames - ladderWindowDropped_;
    ladderWindowUpload100ns_ = snapshot_.videoUpload100ns;
    ladderWindowConvert100ns_ = snapshot_.softwareConvert100ns;
    ladderWindowDropped_ = snapshot_.droppedVideoFrames;
    // Re-pick the preferred cache domain once per window (no-op on hybrid machines).
    placement_.Tick();
    if (presentedDelta == 0 && decodedDelta == 0) return;

    // Is the source keeping up at all? Compare against the stream's own frame rate.
    auto* stream = format_->streams[videoStream_];
    const auto frameRate = stream != nullptr
        ? av_guess_frame_rate(format_, stream, nullptr) : AVRational{0, 1};
    const auto sourceFps = frameRate.den != 0
        ? static_cast<double>(frameRate.num) / frameRate.den : 0.0;
    const auto seconds = static_cast<double>(elapsed) / 10'000'000.0;
    const auto presentedFps = static_cast<double>(presentedDelta) / seconds;

    // Two conditions must hold before spending a reopen:
    //  1. Playback is actually missing the source rate.
    //  2. Decoding has no spare capacity, OR presentation has stalled completely while
    //     decode still produces. The no-spare test is the part that separates a decode
    //     bottleneck from an upload one: at 8K the decoder produces frames far faster
    //     than they can be uploaded, and adding workers there changes nothing (measured
    //     across 4..32 rungs: 3.3-4.7 fps, all within noise); without it, the ladder
    //     would climb on every dropped frame and waste reopens on a bottleneck that
    //     more threads cannot fix. The stall clause matters because a badly starved
    //     decoder can itself look like an upload bottleneck: decode keeps emitting
    //     frames (measured ~48 fps at 4 workers on 8K) while nothing reaches the
    //     screen, which satisfies "has spare capacity" even though more workers is
    //     precisely the fix. Genuine upload saturation looks different -- frames are
    //     decoded and presented at a steady low rate, so presentedDelta stays non-zero.
    const auto missingSourceRate = sourceFps > 0.0 && presentedFps < sourceFps * 0.9;
    const auto decoderHasNoSpare = decodedDelta <= presentedDelta * 3 / 2;
    const auto presentationStalled = presentedDelta == 0 && decodedDelta > 0;
    if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
        std::fprintf(stderr,
            "[ladder] window decoded=%llu presented=%llu presentedFps=%.2f sourceFps=%.2f "
            "missingRate=%d noSpare=%d stalled=%d saturated=%llu/%llu\n",
            static_cast<unsigned long long>(decodedDelta),
            static_cast<unsigned long long>(presentedDelta), presentedFps, sourceFps,
            missingSourceRate ? 1 : 0, decoderHasNoSpare ? 1 : 0, presentationStalled ? 1 : 0,
            static_cast<unsigned long long>(windowSaturatedIters),
            static_cast<unsigned long long>(windowPumpIters));

    // Both bounds belong in one ceiling: the hardware count keeps the ladder honest
    // about the machine, and the shared budget reserves the player's own threads plus
    // one core of scheduler headroom so the decoder can never oversubscribe the CPU.
    // Read from the live context, once: a dav1d-capable open may have rewritten the
    // worker count after the budget picked it, so only the context is the truth.
    const auto current = static_cast<std::uint32_t>(videoDecoder_->thread_count);
    const auto ceiling = (std::min)({ std::max(1u, std::thread::hardware_concurrency()),
        threadLadderMax_, 64u, DecoderThreadBudget() });
    // The look-ahead pool climbs only when the pool itself starved the pipeline. A
    // window that misses the source rate is equally produced by a decode-limited or a
    // presentation-limited stream, and a deeper cushion helps neither of those -- the
    // thread ladder and the placement path already cover them. The verdict reads the
    // evidence this window collected:
    //   pool    -- the queue sat at its admission bound, the decoder was not starved,
    //              the upload path had room to spare, and frames were actually
    //              dropped: the cushion was too thin to absorb a burst.
    //   decode  -- the decoder ran starved; the admission bound was never the active
    //              constraint, so a deeper queue stays empty.
    //   present -- the upload path ate the frame period; deeper frames cannot leave.
    //   stalled -- nothing reached the screen; the thread ladder's stall clause owns it.
    //   hold    -- anything else, including the gray band between the upload shares.
    // The dropped-frames requirement doubles as the guard against a container that
    // over-reports its frame rate (VFR with an index): a comfortable stream never
    // drops, whatever the guessed rate says.
    const double boundRatio = windowPumpIters != 0
        ? static_cast<double>(windowBoundIters) / static_cast<double>(windowPumpIters) : 0.0;
    const double starvedRatio = windowPumpIters != 0
        ? static_cast<double>(windowStarvedIters) / static_cast<double>(windowPumpIters) : 0.0;
    const auto framePeriod100ns = sourceFps > 0.0
        ? static_cast<std::int64_t>(10'000'000.0 / sourceFps) : 0;
    const std::int64_t uploadCost100ns = presentedDelta != 0
        ? static_cast<std::int64_t>((windowUpload100ns + windowConvert100ns) / presentedDelta) : 0;
    enum class PoolVerdict { Climb, Decode, Present, Stalled, Hold };
    const auto poolVerdict = [&]() noexcept {
        if (presentedDelta == 0) return PoolVerdict::Stalled;
        if (missingSourceRate && starvedRatio > PoolStarvedRatioCeiling)
            return PoolVerdict::Decode;
        if (framePeriod100ns > 0 && boundRatio >= PoolBoundRatioFloor &&
            uploadCost100ns >= static_cast<std::int64_t>(framePeriod100ns * PoolUploadSaturateShare))
            return PoolVerdict::Present;
        if (missingSourceRate && droppedDelta > 0 &&
            boundRatio >= PoolBoundRatioFloor && starvedRatio <= PoolStarvedRatioCeiling &&
            (framePeriod100ns == 0 ||
                uploadCost100ns < static_cast<std::int64_t>(framePeriod100ns * PoolUploadHeadroomShare)))
            return PoolVerdict::Climb;
        return PoolVerdict::Hold;
    }();
    const char* poolVerdictName = "hold";
    switch (poolVerdict) {
        case PoolVerdict::Climb: poolVerdictName = "pool"; break;
        case PoolVerdict::Decode: poolVerdictName = "decode"; break;
        case PoolVerdict::Present: poolVerdictName = "present"; break;
        case PoolVerdict::Stalled: poolVerdictName = "stalled"; break;
        case PoolVerdict::Hold: break;
    }
    if (performance_.video != 2 && poolVerdict == PoolVerdict::Climb && softwarePoolRung_ + 1 < SoftwarePoolRungCount) {
        const auto bytesPerFrame = videoFrameQueue_.empty()
            ? std::size_t{0} : EstimateDecodedFrameBytes(videoFrameQueue_.back());
        // Skip rungs that would not deepen the queue for this frame size: a budget
        // whose frame cap already binds declares more memory without admitting one
        // more frame, and the ladder only gets one move per window.
        auto target = softwarePoolRung_;
        do {
            ++target;
        } while (target + 1 < SoftwarePoolRungCount &&
                 SoftwarePoolRungDepth(target, bytesPerFrame, performance_) <=
                     SoftwarePoolRungDepth(softwarePoolRung_, bytesPerFrame, performance_));
        const auto previousBudget = SoftwarePoolBudgets[softwarePoolRung_];
        const auto previousFrames = SoftwarePoolFrameCaps[softwarePoolRung_];
        softwarePoolRung_ = static_cast<std::uint8_t>(target);
        if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
            std::fprintf(stderr,
                "[ladder] frame pool %llu -> %llu MiB (%llu -> %llu frames) gate=pool "
                "bound=%.2f starved=%.2f dropped=%llu presented=%.2f of %.2f fps\n",
                static_cast<unsigned long long>(previousBudget / (1024 * 1024)),
                static_cast<unsigned long long>(
                    SoftwarePoolBudgets[softwarePoolRung_] / (1024 * 1024)),
                static_cast<unsigned long long>(previousFrames),
                static_cast<unsigned long long>(SoftwarePoolFrameCaps[softwarePoolRung_]),
                boundRatio, starvedRatio,
                static_cast<unsigned long long>(droppedDelta),
                presentedFps, sourceFps);
    } else if ((missingSourceRate || presentationStalled) &&
               GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0)) {
        // A window that missed but earned no rung: say which gate held it back, so
        // the silence is attributable instead of looking like a missed climb.
        std::fprintf(stderr,
            "[ladder] frame pool holds at %llu MiB gate=%s bound=%.2f starved=%.2f "
            "dropped=%llu uploadMs=%.2f frameMs=%.2f\n",
            static_cast<unsigned long long>(
                SoftwarePoolBudgets[softwarePoolRung_] / (1024 * 1024)),
            poolVerdictName, boundRatio, starvedRatio,
            static_cast<unsigned long long>(droppedDelta),
            static_cast<double>(uploadCost100ns) / 10'000.0,
            static_cast<double>(framePeriod100ns) / 10'000.0);
    }

    if (!missingSourceRate || (!decoderHasNoSpare && !presentationStalled)) {
        // Healthy window. Walk one placement tier back after three in a row, so a single
        // lucky two-second sample cannot undo an escalation that was earned. The
        // priority overlay (Prioritize) is not a tier and survives this walk-back.
        if (++placementGoodWindows_ >= 3) {
            placementGoodWindows_ = 0;
            // The ladder never descends, so the worker count can sit above what the
            // preferred set seats; pulling the process default back then would
            // over-subscribe the preferred group and oscillate with the next spill.
            // Stay spilled until the workers fit the preferred set again.
            if (decoderThreads_ <= placement_.PreferredDomainThreads())
                placement_.Recover();
        }
        return;
    }
    placementGoodWindows_ = 0;

    // Cheapest action first: lifting the preferred-domain restriction is one call and
    // costs no decoder reopen, while a rung costs a seek. Only when the extra domain is
    // already in use does the ladder get to spend its reopen, and only when both are
    // exhausted does the process priority move.
    if (placement_.Spill()) return;

    if (!adaptiveThreadsEnabled_) { placement_.Boost(); return; }

    // The ladder maximum and the shared budget both cap the next rung; exhausting
    // them hands over to the priority boost. The climb below self-guards as well
    // (no rung qualifies once the ceiling is reached).
    if (current >= ceiling) { placement_.Boost(); return; }

    // Coarse ladder, up only. Descending was implemented earlier and removed: giving
    // workers back costs another reopen and seek, and the drops each seek produces were
    // read as fresh pressure, which oscillated (observed 19 -> 20 -> 19) and left 4K
    // AV1 at 40 fps against 56 for a single fixed choice.
    constexpr std::uint32_t Ladder[] = {4, 8, 16, 24, 32};
    std::uint32_t next = ceiling;
    for (const auto rung : Ladder) {
        if (rung > current && rung <= ceiling) { next = rung; break; }
    }
    if (next <= current) return;

    // Do not cross a physical boundary in one step: on a hybrid part the first rung
    // above the performance class puts workers on the efficiency class, and on a
    // multi-CCD part it pushes them onto the second L3 domain. Climb to the boundary
    // itself first (12 on a 6P+8E machine, where the 16 rung would already spill), and
    // only continue past it when a window at the boundary is still starving. The
    // boundary is 0 on uniform machines, where there is nothing to cross.
    const auto boundary = placement_.PreferredDomainThreads();
    const auto cappedAtBoundary = boundary > current && next > boundary;
    if (cappedAtBoundary) next = boundary;

    AVCodecContext* replacement = nullptr;
    const auto previous = decoderThreads_;
    decoderThreads_ = next;
    if (OpenDecoder(format_, videoStream_, true, &replacement, -1, nullptr, false)
            != FFFResult::Success) {
        decoderThreads_ = previous;   // keep the working decoder and rung
        return;
    }
    const auto resume = std::max<std::int64_t>(0, snapshot_.position100ns);
    StopSoftwareVideoDecode();
    avcodec_free_context(&videoDecoder_);
    videoDecoder_ = replacement;
    if (videoDecodeFrame_ != nullptr) av_frame_unref(videoDecodeFrame_);
    if (videoTransferFrame_ != nullptr) av_frame_unref(videoTransferFrame_);
    ClearVideoQueue();
    DoSeek(resume);
    // Ignore the frames this seek itself drops; they are the rung's cost, not evidence
    // that the rung is too small. The deadline is wall-clock because the pump loop can
    // spin thousands of times per second; the window that follows arms its own counter
    // baselines (see the start-of-function block), so it measures true deltas.
    ladderSettlingUntil100ns_ = CurrentSteadyTicks() + 10'000'000;   // 1 s of re-prime
    ladderWindowStart100ns_ = 0;
    if (cappedAtBoundary)
        std::fprintf(stderr, "[threads] software decoder %u -> %u (presented %.1f of %.1f fps, "
            "domain boundary %u)\n", current, next, presentedFps, sourceFps, boundary);
    else
        std::fprintf(stderr, "[threads] software decoder %u -> %u (presented %.1f of %.1f fps)\n",
            current, next, presentedFps, sourceFps);
}

FFFResult PlayerSession::FallbackToSoftwareVideoDecoder(const char* reason) noexcept {
    StopSoftwareVideoDecode();
    if (format_ == nullptr || videoStream_ < 0 || snapshot_.decodeMode != FFF3FPDecodeMode::Gpu)
        return FFFResult::FfmpegFailure;
    AVCodecContext* softwareDecoder = nullptr;
    const auto openResult = OpenDecoder(format_, videoStream_, true, &softwareDecoder,
        -1, nullptr, false);
    if (openResult != FFFResult::Success) return openResult;
    const auto resumePosition = std::max<std::int64_t>(0, snapshot_.position100ns);
    avcodec_free_context(&videoDecoder_);
    videoDecoder_ = softwareDecoder;
    if (videoDecodeFrame_ != nullptr) av_frame_unref(videoDecodeFrame_);
    if (videoTransferFrame_ != nullptr) av_frame_unref(videoTransferFrame_);
    ClearVideoQueue();
    snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
    if (staticImage_) {
        // DoSeek is a deliberate no-op for a still image, and its single packet was
        // already consumed by the failed hardware attempt, so rewind here or the CPU
        // decoder is handed nothing and the open fails. Same seek as DecodeInitialFrame.
        const auto* stream = format_->streams[videoStream_];
        const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        if (av_seek_frame(format_, videoStream_, start, AVSEEK_FLAG_BACKWARD) >= 0)
            avcodec_flush_buffers(videoDecoder_);
    } else {
        DoSeek(resumePosition);
    }
    RebuildMediaInfo();
    Emit(FFF3FPEvent::DeviceChanged,
        "{\"type\":\"video\",\"decodeMode\":1,\"fallback\":true,\"reason\":\"" + EscapeJson(reason == nullptr ?
            "The hardware decoder failed." : reason) + "\"}");
    return FFFResult::Success;
}

void PlayerSession::PumpPlayback() noexcept {
    const auto movingWindow = interactiveMove_.load(std::memory_order_acquire);
    if (movingWindow) {
        // 移动窗口前主动增加解码储备；本轮保留已增加的水位，避免连续拖动反复欠载。
        audioTargetBuffer100ns_ = std::max(audioTargetBuffer100ns_,
            performance_.AudioMoveFloor());
    }
    if (format_ == nullptr) { SetState(FFF3FPState::Failed); return; }
    // The host asked for an auxiliary composition: open the second video
    // stream on this worker, once, before the next demux. A file without such
    // a stream simply never gets one; the base path is untouched.
    if (auxLayerRequested_.load(std::memory_order_acquire) && !auxOpenAttempted_ &&
        videoStream_ >= 0 && !staticImage_ && !animatedImage_ && !tileGrid_) {
        auxOpenAttempted_ = true;
        (void)OpenAuxiliaryDecoder();
    }
    if (!CollectSoftwareVideoFrame()) return;
    // Window evidence for EvaluateDecoderLoad: how many playback ticks ran and how
    // many of them found the decode pipeline at its bound. Diagnostics only --
    // consumed and reported when an evaluation window closes.
    ++pumpIterations_;
    if (SoftwareVideoInputFull() || VideoQueueSaturated()) ++pumpSaturatedIterations_;
    // The pool gate needs the two causes apart: input-full is packets banked in front
    // of the decoder (a decode fingerprint), queue-bound is the admission limit doing
    // the gating (a pool or presentation fingerprint).
    if (VideoQueueSaturated()) ++pumpQueueBoundIterations_;
    if (videoFrameQueue_.size() < MinimumQueuedVideoFrames) ++pumpStarvedIterations_;
    if (disc_) {
        PublishDisc();
        if (disc_->RestartRequired() && !disc_->Held()) {
            if (!ReopenDiscDemux()) return;
        }
        if (discDrained_) { HoldDisc(); return; }
    }
    TryReleaseAudioAfterVideoPresentation();
    DrainInternalAudio();
    if (state_.load() == FFF3FPState::Failed) return;
    TryCompletePlaybackPreroll();
    if (state_.load() == FFF3FPState::Failed) return;
    UpdateDrainedAudioClock();
    UpdateBitRateForPosition(ClockPosition());
    // Judge decoder load on the playback tick. This is where the peak frame-rate
    // counters are already fresh, and it runs on the worker thread that owns the
    // decoder, so a rung change cannot race a decode in progress.
    EvaluateDecoderLoad();
    if (cadenceModeRefreshPending_) {
        cadenceModeRefreshPending_ = false;
        RebuildMediaInfo();
        publishedFrameRateMode_ = MeasuredFrameRateMode();   // 记下真正发布了哪一版
        if (GetEnvironmentVariableW(L"FFF_CADENCE_TRACE", nullptr, 0))
            std::fprintf(stderr, "CADENCE rebuild\n");
    }
    if (videoStream_ < 0 && audioStream_ >= 0 && !audioRenderer_) {
        // Windows can block both shared and exclusive initialization while a
        // different process owns the endpoint. Keep audio-only media on its
        // normal wall clock instead of draining the entire file immediately.
        snapshot_.position100ns = std::min(ClockPosition(), snapshot_.duration100ns);
        PublishSnapshot();
        if (snapshot_.duration100ns > 0 && snapshot_.position100ns >= snapshot_.duration100ns)
            FlushAtEnd();
        else
            Sleep(10);
        return;
    }
    const auto audioBuffered = audioRenderer_ ? audioRenderer_->Buffered100ns() : 0;
    // Rise-only pool: any newly observed warning batch (any counter growth since
    // the last pump) buys one step of target headroom, capped at the maximum.
    // Underruns are the hard signal; timestamp jitter and discontinuity-family
    // counters mean the decoded timeline itself is unsteady, and the pool must
    // open for them too -- a stream that warns while the queue stays shallow is
    // exactly the case the fixed default cannot absorb. A seek rewinds the
    // renderer's counters below what we have seen -- that is a counter reset,
    // not a drop, so it only re-baselines the comparison.
    if (audioRenderer_) {
        auto grew = false;
        const auto track = [&grew](const std::uint64_t now, std::uint64_t& seen) {
            if (now < seen) { seen = now; return; }   // counter reset (seek): re-baseline
            if (now > seen) { seen = now; grew = true; }
        };
        const auto underruns = audioRenderer_->UnderrunCount();
        const auto jitter = audioRenderer_->TimestampJitterCount();
        const auto discontinuities = audioRenderer_->DiscontinuityCount();
        track(underruns, seenAudioUnderruns_);
        track(jitter, seenAudioJitterFrames_);
        track(discontinuities, seenAudioDiscontinuities_);
        track(audioRenderer_->InsertedSilenceFrames(), seenAudioInsertedSilence_);
        track(audioRenderer_->DroppedOverlapFrames(), seenAudioDroppedOverlap_);
        if (grew) {
            const auto previousTarget = audioTargetBuffer100ns_;
            const auto deviceFloor = audioRenderer_->DeviceBufferDuration100ns();
            audioTargetBuffer100ns_ = std::max(deviceFloor, std::min(performance_.AudioMaximum(),
                audioTargetBuffer100ns_ + kAudioTargetBufferStep100ns));
            if (GetEnvironmentVariableW(L"FFF_AUDIO_TARGET_TRACE", nullptr, 0))
                std::fprintf(stderr, "AUDIO_TARGET underruns=%llu jitter=%llu disc=%llu target=%lld -> %lld buffered=%lld\n",
                    static_cast<unsigned long long>(underruns),
                    static_cast<unsigned long long>(jitter),
                    static_cast<unsigned long long>(discontinuities),
                    static_cast<long long>(previousTarget),
                    static_cast<long long>(audioTargetBuffer100ns_),
                    static_cast<long long>(audioBuffered));
        }
    }
    const auto delayAudioUntilVideo = audioRenderer_ && ShouldDelayAudioUntilVideoFrame();
    const auto videoSaturated = [this] { return SoftwareVideoInputFull() || VideoQueueSaturated(); };
    if (!delayAudioUntilVideo && !pendingAudioPackets_.empty() &&
        audioBuffered < audioTargetBuffer100ns_) {
        DecodePendingPacket(false);
        return;
    }
    // IAMF is pushed from its own streaming decoder rather than the demuxer's
    // packet queue, but it honours the same buffer target and the same
    // "wait for video" rule so it stays in step with the clock.
    if (iamfDecoder_ != nullptr && !delayAudioUntilVideo &&
        audioBuffered < audioTargetBuffer100ns_) {
        PumpIamfAudio();
        if (state_.load() == FFF3FPState::Failed) return;
    }
    // Feed already-demuxed audio before doing a due frame's enhancement/render
    // work. Otherwise a costly frame can starve a short WASAPI buffer.
    if (PumpVideoPresentation()) return;
    if (!pendingVideoPackets_.empty() && !videoSaturated()) {
        DecodePendingPacket(true);
        return;
    }
    if (videoStream_ < 0 && audioRenderer_ && audioBuffered >= audioTargetBuffer100ns_) { Sleep(2); return; }
    if (externalFormat_ != nullptr && audioRenderer_ && !delayAudioUntilVideo &&
        audioBuffered < audioTargetBuffer100ns_) PumpExternalAudio();
    if (demuxEnded_) {
        if (pendingVideoPackets_.empty()) FlushAtEnd();
        else Sleep(1);
        return;
    }
    const auto videoPacketsFull = pendingVideoPackets_.size() >= MaximumPendingVideoPackets ||
        pendingVideoPacketBytes_ >= MaximumPendingVideoPacketBytes;
    const auto audioPacketsFull = pendingAudioPackets_.size() >= MaximumPendingAudioPackets ||
        pendingAudioPacketBytes_ >= MaximumPendingAudioPacketBytes;
    if (videoSaturated() &&
        ((!audioRenderer_ || audioBuffered >= audioTargetBuffer100ns_) || videoPacketsFull)) {
        Sleep(1);
        return;
    }
    if (audioRenderer_ && audioBuffered >= audioTargetBuffer100ns_ && audioPacketsFull &&
        pendingVideoPackets_.empty()) {
        Sleep(1);
        return;
    }
    if (playbackPacket_ == nullptr) playbackPacket_ = av_packet_alloc();
    if (playbackPacket_ == nullptr) { Fail(FFFResult::NativeFailure, "Could not allocate a playback packet."); return; }
    const auto result = av_read_frame(format_, playbackPacket_);
    if (GetEnvironmentVariableW(L"FFF_SEEK_TRACE", nullptr, 0) && ++readTraceCount_ <= 6)
        std::fprintf(stderr, "READ rc=%d tell=%lld pendingV=%zu q=%zu demuxEnded=%d\n", result,
            format_->pb ? (long long)avio_tell(format_->pb) : -1, pendingVideoPackets_.size(),
            videoFrameQueue_.size(), demuxEnded_ ? 1 : 0);
    if (result < 0) {
        av_packet_unref(playbackPacket_);
        if (result != AVERROR_EOF) {
            if (disc_ && result == AVERROR_INVALIDDATA && !disc_->Failed() && ++discInvalidPackets_ <= 32) {
                return;
            }
            Fail(FFFResult::FfmpegFailure, "Could not read media packets: " + FfmpegError(result), "demux");
            return;
        }
        if (disc_ && !disc_->Ended()) {
            discDrained_ = true;
            HoldDisc();
            return;
        }
        demuxEnded_ = true;
        if (!pendingVideoPackets_.empty()) return;
        FlushAtEnd(); return;
    }
    TrackPacketBitRate(playbackPacket_, format_);
    discInvalidPackets_ = 0;
    if (playbackPacket_->stream_index == videoStream_) {
        // The decoder can free an input slot while av_read_frame runs. New
        // packets must still follow the compressed packets already held here.
        if (!pendingVideoPackets_.empty() || videoSaturated()) {
            auto* retained = av_packet_clone(playbackPacket_);
            if (retained == nullptr) {
                av_packet_unref(playbackPacket_);
                Fail(FFFResult::NativeFailure, "Could not retain a bounded compressed video packet.", "decode");
                return;
            }
            pendingVideoPacketBytes_ += static_cast<std::size_t>(std::max(retained->size, 0));
            pendingVideoPackets_.push_back(retained);
        } else {
            DecodePacket(videoDecoder_, playbackPacket_, true, format_);
        }
    }
    else if (playbackPacket_->stream_index == audioStream_ && externalFormat_ == nullptr) {
        // IAMF bypasses the FFmpeg decoder entirely: the reference decoder owns the
        // substream mixing FFmpeg does not implement. Queue buffering stays the
        // same either way, so a saturated audio queue is handled identically.
        const auto decodeAudioPacket = [this]() {
            if (iamfDecoder_ != nullptr) {
                // PumpFile and Decode share handle_, the pcm_ scratch buffer and the
                // byte cursor, so feeding a demuxer packet into the same pass in which
                // the file pump is running corrupts the pump's access-unit framing --
                // libiamf rejects an audio-frame OBU for a substream it already holds.
                // The pump is therefore the sole owner whenever it can run; the
                // compressed packet is dropped instead (FallBackFromIamf clears
                // iamfDecoder_ and re-opens FFmpeg if the pump gives up). A session
                // with no audio renderer has no pump at all, so there the packet path
                // stays the only feeder.
                if (audioRenderer_ != nullptr) return;
                (void)DecodeIamfPacket(playbackPacket_);
                return;
            }
            DecodePacket(audioDecoder_, playbackPacket_, false, format_);
        };
        if (audioRenderer_ && (delayAudioUntilVideo || audioBuffered >= audioTargetBuffer100ns_)) {
            if (delayAudioUntilVideo && audioPacketsFull) {
                decodeAudioPacket();
                av_packet_unref(playbackPacket_);
                return;
            }
            auto* retained = av_packet_clone(playbackPacket_);
            if (retained == nullptr) {
                av_packet_unref(playbackPacket_);
                Fail(FFFResult::NativeFailure, "Could not retain a bounded compressed audio packet.", "decode");
                return;
            }
            pendingAudioPacketBytes_ += static_cast<std::size_t>(std::max(retained->size, 0));
            pendingAudioPackets_.push_back(retained);
        } else {
            decodeAudioPacket();
        }
    }
    else if (auxStream_ >= 0 && playbackPacket_->stream_index == auxStream_) {
        // The auxiliary track is decoded on this worker, synchronously, and its
        // frames go to their own bounded queue. It never enters the base-layer
        // packet budget, bitrate counters or snapshot counters.
        DecodeAuxiliaryPacket(playbackPacket_);
    }
    if (disc_) disc_->DecodeSubtitle(playbackPacket_, format_);
    av_packet_unref(playbackPacket_);
}

FFFResult PlayerSession::CompleteHardwareFallback(const char* failureMessage) noexcept {
    hardwareFallbackPending_ = false;
    auto reason = pendingHardwareFallbackReason_.empty()
        ? std::string("The GPU frame could not be transferred for presentation; playback continued with CPU decoding.")
        : std::move(pendingHardwareFallbackReason_);
    pendingHardwareFallbackReason_.clear();
    const auto result = FallbackToSoftwareVideoDecoder(reason.c_str());
    if (result != FFFResult::Success) Fail(result, failureMessage);
    return result;
}


// True when the container is the raw IAMF demuxer.
static bool IsIamfFormat(const AVFormatContext* format) noexcept {
    return format != nullptr && format->iformat != nullptr &&
        format->iformat->name != nullptr &&
        std::string_view(format->iformat->name).find("iamf") != std::string_view::npos;
}

bool PlayerSession::TryOpenIamfAudioDecoder() noexcept {
    if (!IsIamfFormat(format_)) return false;
    if (audioStream_ < 0 || audioStream_ >= static_cast<std::int32_t>(format_->nb_streams))
        return false;
    if (!IamfAudioDecoder::Available()) return false;

    auto decoder = std::make_unique<IamfAudioDecoder>();
    std::string error;
    // IAMF cannot be decoded packet-by-packet the way the rest of the pipeline
    // does it: FFmpeg's demuxer hands out one packet per *substream*, while
    // libiamf needs whole access units (every substream's OBUs together) and
    // rejects per-substream feeds. The reference decoder therefore drives the raw
    // OBU stream from the file itself; frames are pulled lazily by
    // PumpIamfAudio so a 328 MB fixture does not decode up front.
    if (!decoder->OpenFile(mediaPath_, error)) {
        // Not fatal. libiamf has genuine gaps (FLAC carriage is unimplemented and
        // some HOA layouts will not configure), so the track falls back to FFmpeg
        // instead of failing: a file that plays in stereo beats one that does not
        // play at all. Announced like the video-decoder fallback, because playback
        // continues and the user should know why it sounds flat.
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"audio\",\"iamf\":true,\"fallback\":true,\"reason\":\"" +
            EscapeJson(error) + "\"}");
        return false;
    }
    iamfDecoder_ = std::move(decoder);
    // Report the layout the content actually has, so diagnostics stop presenting
    // one substream's stereo pair as if it were the whole track.
    snapshot_.iamfActive = true;
    snapshot_.iamfChannels = iamfDecoder_->Channels();
    // Content layout, not the render target; see FFF3FPSnapshot::iamfContentChannels.
    snapshot_.iamfContentChannels = static_cast<std::uint32_t>(
        std::max(0, iamfDecoder_->ContentChannels()));
    snapshot_.iamfSoundSystem = iamfDecoder_->SoundSystem();
    return true;
}

// Pulls decoded IAMF frames into the renderer until the audio buffer is satisfied.
// Called from the same worker that feeds every other codec, so ordering against
// video and the playback clock is unchanged.
void PlayerSession::PumpIamfAudio() noexcept {
    if (iamfDecoder_ == nullptr) return;
    if (audioRenderer_ == nullptr) return;
    // Respect the same buffer target the ordinary path uses; over-filling would
    // push audio ahead of the clock just as it would for any other codec.
    constexpr int MaxFramesPerPump = 16;
    std::vector<std::pair<AVFrame*, std::int64_t>> frames;
    bool atEnd = false;
    std::string error;
    if (!iamfDecoder_->PumpFile(MaxFramesPerPump, frames, atEnd, error)) {
        // Decoding can fail *after* the stream looked openable: libiamf reports
        // some unsupported carriages only once it reaches the audio frames.
        // Falling back keeps the track audible -- without it the file would go
        // silent, which is worse than the stereo it played before.
        for (auto& entry : frames) av_frame_free(&entry.first);
        FallBackFromIamf(error);
        return;
    }
    // The content layout only becomes known once libiamf has configured, which happens
    // inside this first PumpFile -- so TryOpenIamfAudioDecoder cannot publish it at open
    // time and the snapshot would carry 0 forever. Read it *after* the pump, on the one
    // thread that owns the decoder, and only until it is known.
    // Measured ordering that made this necessary: the old check ran before PumpFile and
    // saw 0, while the configure that follows sets 8 for a 7.1 file.
    if (snapshot_.iamfContentChannels == 0) {
        const auto contentChannels = iamfDecoder_->ContentChannels();
        if (contentChannels > 0)
            snapshot_.iamfContentChannels = static_cast<std::uint32_t>(contentChannels);
    }
    for (auto& entry : frames) {
        // The decoder numbers its frames in 100 ns units, but QueueAudioFrame reads a
        // frame's PTS in the *stream's* time base -- for the raw IAMF demuxer that is
        // 1/48000 -- and rescales it from there. Handing over 100 ns values unchanged
        // therefore multiplied every timestamp by sampleRate/1e7: a 4096-sample frame
        // advancing 0.085 s looked like it advanced 17.7 s. The renderer treats a jump
        // that large as an edit and synthesises silence to cover it, so a 1.02 s file
        // queued 207 s of PCM, and playback stalled while that was drained.
        //
        // Converting here keeps one convention for every codec: PTS in stream time base
        // on the AVFrame, position in 100 ns only after StreamTimestampPosition100ns.
        if (entry.first->pts != AV_NOPTS_VALUE && audioStream_ >= 0 &&
            audioStream_ < static_cast<std::int32_t>(format_->nb_streams)) {
            entry.first->pts = av_rescale_q(entry.first->pts,
                AVRational{1, static_cast<int>(TicksPerSecond)},
                format_->streams[audioStream_]->time_base);
            entry.first->best_effort_timestamp = entry.first->pts;
        }
        QueueAudioFrame(entry.first, format_, audioStream_);
        av_frame_free(&entry.first);
    }
    if (!frames.empty()) iamfProducedAnyAudio_ = true;
    // A stream that ends without ever producing a frame is content libiamf
    // accepted at configure time but cannot actually render (measured on
    // conv_bedonly.iamf, a HOA fixture). Treat it like a hard failure so the track
    // falls back to FFmpeg instead of playing silence.
    if (atEnd && frames.empty()) {
        if (iamfProducedAnyAudio_) audioDecoderDrained_ = true;
        else FallBackFromIamf("libiamf produced no audio from this IAMF stream.");
    }
}

// Gives up on the reference IAMF decoder and re-opens the track through FFmpeg.
// Used when libiamf fails on content it cannot handle (FLAC carriage, some HOA
// layouts); the alternative is silence.
void PlayerSession::FallBackFromIamf(const std::string& reason) noexcept {
    ReleaseIamfDecoder();
    Emit(FFF3FPEvent::DeviceChanged,
        "{\"type\":\"audio\",\"iamf\":true,\"fallback\":true,\"reason\":\"" +
        EscapeJson(reason) + "\"}");
    // Re-open the ordinary decoder for the same stream. The demuxer is untouched,
    // so packets already read stay valid; only the decode path changes.
    if (audioDecoder_ == nullptr && audioStream_ >= 0) {
        AVCodecContext* replacement = nullptr;
        if (OpenDecoder(format_, audioStream_, false, &replacement) == FFFResult::Success)
            audioDecoder_ = replacement;
    }
}

// Bytes read from the head of an IAMF stream to reach its descriptor OBUs. The
// descriptors (sequence header, codec configs, audio elements, mix
// presentations) always precede the first audio frame; libiamf's own reference
// tool uses 960*6*2*16 = 184320 for the same purpose, so that is a proven size.

void PlayerSession::ReleaseIamfDecoder() noexcept {
    iamfDecoder_.reset();
    snapshot_.iamfActive = false;
    iamfProducedAnyAudio_ = false;
    snapshot_.iamfChannels = 0;
    snapshot_.iamfSoundSystem = -1;
}

FFFResult PlayerSession::DecodeIamfPacket(AVPacket* packet) noexcept {
    if (iamfDecoder_ == nullptr) return FFFResult::InvalidState;
    if (packet == nullptr || packet->size <= 0) return FFFResult::Success;
    // The packet's position drives the frame timestamps, so seek anchoring and
    // gap repair behave exactly as they do for every other codec.
    auto position = AV_NOPTS_VALUE;
    if (packet->pts != AV_NOPTS_VALUE)
        position = StreamTimestampPosition100ns(format_, audioStream_, packet->pts);

    std::string error;
    const auto ok = iamfDecoder_->Decode(packet->data, static_cast<std::size_t>(packet->size),
        position, [this](AVFrame* decoded) {
            // The reference decoder stamps PTS in 100 ns ticks while QueueAudioFrame
            // rescales by the stream time base, so an un-converted value is read as
            // if 1e-7 s were 1/48000 s: a 1024-sample frame's 213333 becomes 4.44 s
            // (~208x), and the renderer treats the jump as an edit and synthesises
            // silence to cover it. Same conversion, same convention as the file pump.
            if (decoded->pts != AV_NOPTS_VALUE && audioStream_ >= 0 &&
                audioStream_ < static_cast<std::int32_t>(format_->nb_streams)) {
                decoded->pts = av_rescale_q(decoded->pts,
                    AVRational{1, static_cast<int>(TicksPerSecond)},
                    format_->streams[audioStream_]->time_base);
                decoded->best_effort_timestamp = decoded->pts;
            }
            QueueAudioFrame(decoded, format_, audioStream_);
        },
        error);
    if (!ok) {
        // A single bad access unit must not take audio down: report and continue,
        // mirroring how the ordinary path treats recoverable decode errors.
        ++internalAudioDecodeErrorCount_;
        if (error.empty()) return FFFResult::FfmpegFailure;
        Emit(FFF3FPEvent::Error, "{\"type\":\"audio\",\"iamf\":true,\"reason\":\"" +
            EscapeJson(error) + "\"}");
        return FFFResult::Success;
    }
    return FFFResult::Success;
}

FFFResult PlayerSession::QueueSoftwareVideoDecode(AVPacket* packet) noexcept {
    auto* retained = packet ? av_packet_clone(packet) : nullptr;
    if (packet && !retained) {
        Fail(FFFResult::NativeFailure, "Could not retain the software video packet.", "decode");
        return FFFResult::NativeFailure;
    }
    try {
        std::lock_guard lock(softwareVideoMutex_);
        if (!softwareVideoWorker_.joinable()) {
            softwareVideoOutputLimit_ =
                videoDecoder_->width >= 7680 && videoDecoder_->height >= 4320 ? 2u : 1u;
            softwareVideoWorker_ = std::thread(&PlayerSession::SoftwareVideoDecodeWorker, this, videoDecoder_);
        }
        softwareVideoPackets_.push_back(retained);
    } catch (...) {
        av_packet_free(&retained);
        Fail(FFFResult::NativeFailure, "Could not start the software video worker.", "decode");
        return FFFResult::NativeFailure;
    }
    softwareVideoCondition_.notify_one();
    return FFFResult::Success;
}

void PlayerSession::SoftwareVideoDecodeWorker(AVCodecContext* decoder) noexcept {
    auto* frame = av_frame_alloc();
    const auto receive = [&] {
        int result;
        for (;;) {
            {
                std::lock_guard lock(softwareVideoCodecMutex_);
                result = avcodec_receive_frame(decoder, frame);
            }
            if (result < 0) return result;
            std::unique_lock lock(softwareVideoMutex_);
            softwareVideoCondition_.wait(lock, [this] {
                return softwareVideoOutputs_.size() < softwareVideoOutputLimit_ || softwareVideoStopping_;
            });
            if (softwareVideoStopping_) { av_frame_unref(frame); return AVERROR_EXIT; }
            auto* retained = av_frame_clone(frame);
            if (!retained) { av_frame_unref(frame); return AVERROR(ENOMEM); }
            softwareVideoOutputs_.push_back(retained);
            av_frame_unref(frame);
            commandCondition_.notify_one();
        }
    };
    for (;;) {
        AVPacket* packet;
        {
            std::unique_lock lock(softwareVideoMutex_);
            softwareVideoCondition_.wait(lock, [this] { return softwareVideoStopping_ || !softwareVideoPackets_.empty(); });
            if (softwareVideoStopping_) break;
            packet = softwareVideoPackets_.front();
            softwareVideoPackets_.pop_front();
            softwareVideoBusy_ = true;
        }
        const auto send = [&] {
            std::lock_guard lock(softwareVideoCodecMutex_);
            return frame ? avcodec_send_packet(decoder, packet) : AVERROR(ENOMEM);
        };
        auto result = send();
        if (result == AVERROR(EAGAIN)) {
            result = receive();
            if (result == AVERROR(EAGAIN)) result = send();
        }
        if (result >= 0 || result == AVERROR_EOF) result = receive();
        av_packet_free(&packet);
        {
            std::lock_guard lock(softwareVideoMutex_);
            if (softwareVideoResult_ >= 0 || softwareVideoResult_ == AVERROR(EAGAIN) || softwareVideoResult_ == AVERROR_EOF)
                softwareVideoResult_ = result;
            softwareVideoBusy_ = false;
        }
        commandCondition_.notify_one();
    }
    av_frame_free(&frame);
}

bool PlayerSession::SoftwareVideoDecodePending() noexcept {
    std::lock_guard lock(softwareVideoMutex_);
    return softwareVideoBusy_ || !softwareVideoPackets_.empty() || !softwareVideoOutputs_.empty();
}

bool PlayerSession::SoftwareVideoInputFull() noexcept {
    std::lock_guard lock(softwareVideoMutex_);
    return softwareVideoPackets_.size() >= MaxQueuedVideoFrames;
}

bool PlayerSession::CollectSoftwareVideoFrame() noexcept {
    const auto queueLimit = VideoQueueLimit();
    AVFrame* frame = nullptr;
    int result;
    {
        std::lock_guard lock(softwareVideoMutex_);
        if (videoFrameQueue_.size() < queueLimit && !softwareVideoOutputs_.empty()) {
            frame = softwareVideoOutputs_.front();
            softwareVideoOutputs_.pop_front();
        }
        result = softwareVideoResult_;
        softwareVideoResult_ = 0;
    }
    if (frame) {
        softwareVideoCondition_.notify_one();
        NormalizeVideoFrameTimestamp(frame);
        QueueVideoFrame(frame);
        av_frame_free(&frame);
    }
    if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        Fail(FFFResult::FfmpegFailure, "Software video decoder failed: " + FfmpegError(result), "decode");
        return false;
    }
    return true;
}

void PlayerSession::StopSoftwareVideoDecode() noexcept {
    {
        std::lock_guard lock(softwareVideoMutex_);
        softwareVideoStopping_ = true;
    }
    softwareVideoCondition_.notify_one();
    if (softwareVideoWorker_.joinable()) softwareVideoWorker_.join();
    for (auto* packet : softwareVideoPackets_) av_packet_free(&packet);
    softwareVideoPackets_.clear();
    for (auto* output : softwareVideoOutputs_) av_frame_free(&output);
    softwareVideoOutputs_.clear();
    softwareVideoBusy_ = false;
    softwareVideoStopping_ = false;
    softwareVideoResult_ = 0;
    softwareVideoOutputLimit_ = 1;
}

FFFResult PlayerSession::DecodePacket(AVCodecContext* decoder, AVPacket* packet, const bool video,
    AVFormatContext* owner) noexcept {
    if (decoder == nullptr) return FFFResult::Success;
    if (packet != nullptr && packet->size == 0 && packet->side_data_elems == 0)
        return FFFResult::Success;
    if (video && decoder == videoDecoder_ && decoder->codec && !decoder->hw_device_ctx &&
        !disc_ && !staticImage_ && !animatedImage_ &&
        (std::strcmp(decoder->codec->name, "libdav1d") == 0 || decoder->codec_id == AV_CODEC_ID_HEVC) &&
        state_.load() == FFF3FPState::Playing &&
        seekTarget100ns_ < 0 && seekTargetFrame_ < 0 && !keyframeSeekPending_)
        return QueueSoftwareVideoDecode(packet);
    AVFrame*& reusableFrame = video ? videoDecodeFrame_ :
        (owner == externalFormat_ ? externalAudioDecodeFrame_ : audioDecodeFrame_);
    if (reusableFrame == nullptr) reusableFrame = av_frame_alloc();
    if (reusableFrame == nullptr) return FFFResult::NativeFailure;
    auto* frame = reusableFrame;
    av_frame_unref(frame);
    // A paused seek displays its target once; retain the decoder's later
    // pictures for resume instead of advancing the preview past that target.
    const bool pausedSeek = video && state_.load() != FFF3FPState::Playing &&
        (seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_);
    const auto handleFrame = [this, video, owner, pausedSeek](AVFrame* decoded) {
        if (video) {
            NormalizeVideoFrameTimestamp(decoded);
            const auto seeking = seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_;
            if (!seeking && (state_.load() == FFF3FPState::Playing || pausedSeek)) QueueVideoFrame(decoded);
            else PresentVideoFrame(decoded, owner);
        } else QueueAudioFrame(decoded, owner, owner == format_ ? audioStream_ : externalAudioStream_);
    };
    auto receiveFrames = [&]() {
        int receiveResult = 0;
        while ((receiveResult = avcodec_receive_frame(decoder, frame)) >= 0) {
            handleFrame(frame);
            av_frame_unref(frame);
            if (hardwareFallbackPending_ || internalAudioFailurePending_) break;
        }
        return receiveResult;
    };
    if (video) {
        FilterAv1HardwareTimecodeMetadata(decoder, packet);
    }
    auto result = avcodec_send_packet(decoder, packet);
    if (result == AVERROR(EAGAIN)) {
        const auto receiveResult = receiveFrames();
        if (hardwareFallbackPending_) {
            return CompleteHardwareFallback("Could not fall back to CPU decoding after a GPU frame-transfer failure.");
        }
        if (!video && owner == format_ && internalAudioFailurePending_) {
            const auto failureResult = internalAudioFailureResult_;
            auto message = audioRenderer_ ? audioRenderer_->LastError() : std::string{};
            DisableFailedInternalAudio(failureResult,
                message.empty() ? "The selected audio track could not be rendered." : std::move(message));
            return failureResult;
        }
        if (receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF) {
            if (video && snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
                FallbackToSoftwareVideoDecoder(("The GPU stopped decoding the video stream: " +
                    FfmpegError(receiveResult) + "; playback continued with CPU decoding.").c_str()) == FFFResult::Success)
                return FFFResult::Success;
            const auto message = std::string(video
                ? "Video decoder failed while making room for a packet: "
                : "Audio decoder failed while making room for a packet: ") + FfmpegError(receiveResult);
            if (!video && owner == format_) HandleInternalAudioDecodeFailure(FFFResult::FfmpegFailure, message);
            else Fail(FFFResult::FfmpegFailure, message);
            return FFFResult::FfmpegFailure;
        }
        result = avcodec_send_packet(decoder, packet);
    }
    if (result < 0 && result != AVERROR_EOF) {
        if (video && snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
            FallbackToSoftwareVideoDecoder(("The GPU rejected a video packet: " +
                FfmpegError(result) + "; playback continued with CPU decoding.").c_str()) == FFFResult::Success)
            return FFFResult::Success;
        const auto message = "Decoder rejected packet: " + FfmpegError(result);
        if (!video && owner == format_) HandleInternalAudioDecodeFailure(FFFResult::FfmpegFailure, message);
        else Fail(FFFResult::FfmpegFailure, message);
        return FFFResult::FfmpegFailure;
    }
    result = receiveFrames();
    if (!video && owner == format_ && internalAudioFailurePending_) {
        const auto failureResult = internalAudioFailureResult_;
        auto message = audioRenderer_ ? audioRenderer_->LastError() : std::string{};
        DisableFailedInternalAudio(failureResult,
            message.empty() ? "The selected audio track could not be rendered." : std::move(message));
        return failureResult;
    }
    if (hardwareFallbackPending_) {
        return CompleteHardwareFallback("Could not fall back to CPU decoding after a GPU frame-transfer failure.");
    }
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF && video &&
        snapshot_.decodeMode == FFF3FPDecodeMode::Gpu &&
        FallbackToSoftwareVideoDecoder(("The GPU stopped decoding the video stream: " +
            FfmpegError(result) + "; playback continued with CPU decoding.").c_str()) == FFFResult::Success)
        return FFFResult::Success;
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF && !video && owner == format_) {
        HandleInternalAudioDecodeFailure(FFFResult::FfmpegFailure,
            "The selected audio track stopped decoding: " + FfmpegError(result));
        return FFFResult::FfmpegFailure;
    }
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF ? FFFResult::Success : FFFResult::FfmpegFailure;
}

bool PlayerSession::PumpVideoPresentation() noexcept {
    if (videoFrameQueue_.empty()) return false;
    if (videoRenderer_.HasPendingVideoPresentation()) return false;
    auto* frame = videoFrameQueue_.front();
    const auto position = VideoFramePosition(frame);
    const auto now = ClockPosition();
    const auto primesVisibleVideo = ShouldDelayAudioUntilVideoFrame() &&
        audioUnblockVideoGeneration_ == 0 && videoRenderer_.HasOutputWindow();
    if (playbackPreroll_ && !primesVisibleVideo) return false;
    if (primesVisibleVideo || position <= now + 20'000) {
        videoFrameQueue_.pop_front();
        PresentVideoFrame(frame, format_);
        av_frame_unref(frame);
        videoFramePool_.push_back(frame);
        if (hardwareFallbackPending_) {
            (void)CompleteHardwareFallback("Could not fall back to CPU decoding after a GPU frame-transfer failure.");
        }
        return true;
    }
    const auto lookAhead = VideoFramePosition(videoFrameQueue_.back()) - now;
    const auto queueLimit = VideoQueueLimit();
    const auto saturated = videoFrameQueue_.size() >= std::max<std::size_t>(1, queueLimit - 1) ||
        (videoFrameQueue_.size() >= MinimumQueuedVideoFrames &&
            lookAhead >= performance_.VideoLookAhead());
    // Continue demuxing toward audio while video is ahead, retaining compressed
    // video packets instead of several additional full decoded 4K frames.
    const auto audioNeedsData = audioRenderer_ &&
        audioRenderer_->Buffered100ns() < audioTargetBuffer100ns_;
    const auto packetCapacityAvailable =
        pendingVideoPackets_.size() < MaximumPendingVideoPackets &&
        pendingVideoPacketBytes_ < MaximumPendingVideoPacketBytes;
    if (saturated && (!audioNeedsData || !packetCapacityAvailable)) {
        std::unique_lock lock(mutex_);
        // The media clock owns presentation.  Once the bounded queue is full,
        // sleep until its front frame becomes due (or a command arrives) instead
        // of polling every millisecond and spending an idle core on look-ahead.
        const auto wait100ns = std::max<std::int64_t>(0, position - now - 20'000);
        const auto waitMilliseconds = std::clamp<std::int64_t>((wait100ns + 9'999) / 10'000, 1, 20);
        if (commands_.empty() && !terminate_)
            commandCondition_.wait_for(lock, std::chrono::milliseconds(waitMilliseconds));
        return true;
    }
    return false;
}

void PlayerSession::QueueVideoFrame(AVFrame* frame) noexcept {
    AVFrame* queued = nullptr;
    if (videoFramePool_.empty()) queued = av_frame_alloc();
    else { queued = videoFramePool_.back(); videoFramePool_.pop_back(); }
    if (queued == nullptr || av_frame_ref(queued, frame) < 0) {
        if (queued != nullptr) { av_frame_unref(queued); videoFramePool_.push_back(queued); }
        Fail(FFFResult::NativeFailure, "Could not queue the decoded video frame.", "decode");
        return;
    }
    videoFrameQueue_.push_back(queued);
    ++snapshot_.decodedVideoFrames;
    snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
}

void PlayerSession::ClearVideoQueue() noexcept {
    // Frames dropped here (seek, rung reopen, fallback, close) land in no drop counter;
    // keep a running count so the ladder trace can account for them.
    const auto dropped = videoFrameQueue_.size();
    clearedVideoFrames_ += dropped;
    if (dropped != 0 && GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
        std::fprintf(stderr, "[ladder] queue cleared dropped=%zu total=%llu\n",
            dropped, static_cast<unsigned long long>(clearedVideoFrames_));
    for (auto* frame : videoFrameQueue_) {
        av_frame_unref(frame);
        videoFramePool_.push_back(frame);
    }
    videoFrameQueue_.clear();
    snapshot_.queuedVideoFrames = 0;
    // The presentation index restarts at every flush point, so cached blobs
    // keyed to the old indices would be handed out under a wrong frame number.
    ClearSideDataRing();
    // Same reasoning for the auxiliary layer: its queued frames and its
    // per-index requests belong to the timeline that just ended.
    FlushAuxiliaryLayer();
    ClearPendingPackets();
}

// ---- optional auxiliary video layer ----------------------------------------

FFFResult PlayerSession::SetAuxiliaryComposition(
    const FFF3FPAuxiliaryComposition& composition) noexcept {
    if (composition.size != sizeof(composition) ||
        composition.version != FFF3FP_AUXILIARY_COMPOSITION_VERSION)
        return FFFResult::InvalidArgument;
    {
        std::lock_guard lock(auxMutex_);
        auxCompositionRing_.push_back(AuxCompositionRequest{
            composition.frameIndex, composition.enabled,
            composition.residualCenter, composition.residualShift,
            composition.maxValue});
        while (auxCompositionRing_.size() > MaximumAuxiliaryCompositionRequests)
            auxCompositionRing_.pop_front();
    }
    // enabled != 0 is the only thing that arms the worker; an explicit
    // "skip this frame" request must not open a second decoder.
    if (composition.enabled != 0)
        auxLayerRequested_.store(true, std::memory_order_release);
    return FFFResult::Success;
}

bool PlayerSession::SelectAuxiliaryComposition(const std::int64_t frameIndex,
    AuxCompositionRequest& out) const noexcept {
    std::lock_guard lock(auxMutex_);
    if (auxCompositionRing_.empty()) return false;
    // The kind-3 table's rule, verbatim: the last request at or before the
    // presented index, or the first entry when the index precedes all of them.
    const auto* selected = &auxCompositionRing_.front();
    for (const auto& entry : auxCompositionRing_) {
        if (entry.frameIndex <= frameIndex) selected = &entry;
        else break;
    }
    out = *selected;
    return true;
}

FFFResult PlayerSession::OpenAuxiliaryDecoder() noexcept {
    if (auxDecoder_ != nullptr) return FFFResult::Success;
    if (format_ == nullptr || videoStream_ < 0) return FFFResult::InvalidState;
    auxStream_ = FindAuxiliaryVideoStream(format_, videoStream_);
    if (auxStream_ < 0) return FFFResult::NotSupported;
    // Software on purpose: the composition reference reads planar 10-bit
    // samples, and the auxiliary track is small next to the base layer. The
    // base layer keeps whatever hardware or software route it already chose.
    const auto result = OpenDecoder(format_, auxStream_, true, &auxDecoder_, -1, nullptr, false);
    if (GetEnvironmentVariableW(L"FFF_EL_TRACE", nullptr, 0) != 0)
        std::fprintf(stderr, "[el] open base=%d aux=%d result=%d\n",
            videoStream_, auxStream_, static_cast<int>(result));
    if (result != FFFResult::Success) {
        auxDecoder_ = nullptr;
        auxStream_ = -1;
        return result;
    }
    if (auxDecodeFrame_ == nullptr) auxDecodeFrame_ = av_frame_alloc();
    if (auxDecodeFrame_ == nullptr) {
        avcodec_free_context(&auxDecoder_);
        auxStream_ = -1;
        return FFFResult::NativeFailure;
    }
    return FFFResult::Success;
}

void PlayerSession::FlushAuxiliaryLayer() noexcept {
    if (auxDecoder_ != nullptr) avcodec_flush_buffers(auxDecoder_);
    for (auto& entry : auxFrameQueue_) av_frame_free(&entry.second);
    auxFrameQueue_.clear();
    if (auxDecodeFrame_ != nullptr) av_frame_unref(auxDecodeFrame_);
    std::lock_guard lock(auxMutex_);
    auxCompositionRing_.clear();
}

void PlayerSession::CloseAuxiliaryLayer() noexcept {
    FlushAuxiliaryLayer();
    if (auxDecodeFrame_ != nullptr) av_frame_free(&auxDecodeFrame_);
    if (auxDecoder_ != nullptr) avcodec_free_context(&auxDecoder_);
    auxStream_ = -1;
    auxOpenAttempted_ = false;
    auxLayerRequested_.store(false, std::memory_order_release);
}

void PlayerSession::DecodeAuxiliaryPacket(AVPacket* packet) noexcept {
    if (auxDecoder_ == nullptr || packet == nullptr) return;
    if (auxDecodeFrame_ == nullptr) auxDecodeFrame_ = av_frame_alloc();
    if (auxDecodeFrame_ == nullptr) return;
    const auto sent = avcodec_send_packet(auxDecoder_, packet);
    if (sent < 0 && sent != AVERROR(EAGAIN)) return;
    while (true) {
        const auto received = avcodec_receive_frame(auxDecoder_, auxDecodeFrame_);
        if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) break;
        if (received < 0) break;
        const auto pts = auxDecodeFrame_->best_effort_timestamp == AV_NOPTS_VALUE
            ? auxDecodeFrame_->pts : auxDecodeFrame_->best_effort_timestamp;
        auto* copy = av_frame_alloc();
        if (copy != nullptr && av_frame_ref(copy, auxDecodeFrame_) >= 0) {
            auxFrameQueue_.emplace_back(pts, copy);
            while (auxFrameQueue_.size() > MaximumAuxiliaryQueuedFrames) {
                av_frame_free(&auxFrameQueue_.front().second);
                auxFrameQueue_.pop_front();
            }
        } else {
            av_frame_free(&copy);
        }
        av_frame_unref(auxDecodeFrame_);
    }
}

AVFrame* PlayerSession::TakeAuxiliaryFrame(const std::int64_t basePts,
    AVFormatContext* owner) noexcept {
    if (auxFrameQueue_.empty() || auxStream_ < 0 || owner == nullptr ||
        owner != format_ || videoStream_ < 0 || basePts == AV_NOPTS_VALUE)
        return nullptr;
    const auto* auxiliary = format_->streams[auxStream_];
    const auto* base = format_->streams[videoStream_];
    if (auxiliary == nullptr || base == nullptr) return nullptr;
    const auto target = av_rescale_q(basePts, base->time_base, auxiliary->time_base);
    const auto period = lastVideoFrameDuration100ns_ > 0 ? lastVideoFrameDuration100ns_ : 1'000'000;
    auto tolerance = av_rescale_q(period,
        AVRational{1, static_cast<int>(TicksPerSecond)}, auxiliary->time_base);
    if (tolerance <= 0) tolerance = 1;
    std::size_t best = auxFrameQueue_.size();
    std::int64_t bestDistance = INT64_MAX;
    for (std::size_t index = 0; index < auxFrameQueue_.size(); ++index) {
        const auto pts = auxFrameQueue_[index].first;
        if (pts == AV_NOPTS_VALUE) continue;
        const auto distance = pts >= target ? pts - target : target - pts;
        if (distance < bestDistance) { bestDistance = distance; best = index; }
    }
    if (best == auxFrameQueue_.size() || bestDistance > tolerance) {
        // Nothing close enough: drop what can no longer pair, keep the rest.
        while (!auxFrameQueue_.empty()) {
            const auto pts = auxFrameQueue_.front().first;
            if (pts == AV_NOPTS_VALUE || pts >= target - tolerance) break;
            av_frame_free(&auxFrameQueue_.front().second);
            auxFrameQueue_.pop_front();
        }
        return nullptr;
    }
    AVFrame* result = nullptr;
    for (std::size_t index = 0; index <= best; ++index) {
        auto& entry = auxFrameQueue_.front();
        if (index == best) result = entry.second;
        else av_frame_free(&entry.second);
        auxFrameQueue_.pop_front();
    }
    return result;
}

void PlayerSession::ClearSideDataRing() noexcept {    std::lock_guard lock(sideDataMutex_);
    sideDataRing_.clear();
    sideDataRingBytes_ = 0;
}

void PlayerSession::CacheFrameSideData(const AVFrame* frame,
    const std::int64_t frameIndex) noexcept {
    if (frame == nullptr || frame->side_data == nullptr || frame->nb_side_data <= 0)
        return;
    try {
        CachedSideDataFrame entry;
        entry.frameIndex = frameIndex;
        std::size_t entryBytes = 0;
        for (std::int32_t index = 0; index < frame->nb_side_data; ++index) {
            const auto* side = frame->side_data[index];
            // side->size is size_t in this libavutil, so test it for emptiness
            // rather than against a signed bound.
            if (side == nullptr || side->data == nullptr || side->size == 0) continue;
            const std::size_t size = side->size;
            if (size > MaximumCachedSideDataBlobBytes) continue;
            // Byte-for-byte copy, type carried as a plain number: nothing here
            // reads the payload, which is the whole point of the neutral seam.
            entry.blobs.push_back(SideDataBlob{ static_cast<std::uint32_t>(side->type),
                std::vector<std::uint8_t>(side->data, side->data + size) });
            entryBytes += size;
        }
        if (entry.blobs.empty()) return;
        std::lock_guard lock(sideDataMutex_);
        sideDataRing_.push_back(std::move(entry));
        sideDataRingBytes_ += entryBytes;
        while (!sideDataRing_.empty() && (sideDataRing_.size() > MaximumSideDataRingFrames ||
            sideDataRingBytes_ > MaximumSideDataRingBytes)) {
            std::size_t released = 0;
            for (const auto& blob : sideDataRing_.front().blobs) released += blob.bytes.size();
            sideDataRingBytes_ -= std::min(sideDataRingBytes_, released);
            sideDataRing_.pop_front();
        }
    } catch (...) {
        // The cache is advisory: a blob that could not be stored costs the host
        // one NotFound answer, never a dropped frame or a failed presentation.
    }
}

FFFResult PlayerSession::CopyFrameSideDataAfter(const std::uint32_t sideDataType,
    const std::int64_t fromFrameIndex, void* buffer, const std::uint32_t capacity,
    std::uint32_t& writtenSize, std::int64_t& foundFrameIndex) noexcept {
    try {
        std::lock_guard lock(sideDataMutex_);
        const CachedSideDataFrame* bestFrame = nullptr;
        const SideDataBlob* bestBlob = nullptr;
        for (const auto& entry : sideDataRing_) {
            if (entry.frameIndex < fromFrameIndex) continue;
            if (bestFrame != nullptr && entry.frameIndex >= bestFrame->frameIndex) continue;
            for (const auto& blob : entry.blobs) {
                if (blob.type != sideDataType) continue;
                bestFrame = &entry;
                bestBlob = &blob;
                break;
            }
        }
        if (bestBlob == nullptr) {
            writtenSize = 0;
            foundFrameIndex = -1;
            return FFFResult::NotFound;
        }
        // Reported on every path that found an entry, including BufferTooSmall:
        // that is how the caller learns what to allocate for the retry.
        writtenSize = static_cast<std::uint32_t>(bestBlob->bytes.size());
        foundFrameIndex = bestFrame->frameIndex;
        if (buffer == nullptr) return FFFResult::Success;  // size probe, writes nothing
        if (capacity < writtenSize) return FFFResult::BufferTooSmall;
        std::memcpy(buffer, bestBlob->bytes.data(), writtenSize);
        return FFFResult::Success;
    } catch (...) {
        writtenSize = 0;
        foundFrameIndex = -1;
        return FFFResult::NativeFailure;
    }
}

void PlayerSession::ClearStreamSideData() noexcept {
    std::lock_guard lock(sideDataMutex_);
    streamSideData_.clear();
}

void PlayerSession::CacheStreamSideDataFromStream(const AVStream* stream) noexcept {
    if (stream == nullptr || stream->codecpar == nullptr) return;
    const auto* parameters = stream->codecpar;
    if (parameters->coded_side_data == nullptr || parameters->nb_coded_side_data <= 0) return;
    try {
        std::vector<SideDataBlob> captured;
        std::size_t capturedBytes = 0;
        for (std::int32_t index = 0; index < parameters->nb_coded_side_data; ++index) {
            const auto& side = parameters->coded_side_data[index];
            // side.size is size_t in this libavcodec, so test it for emptiness
            // rather than against a signed bound.
            if (side.data == nullptr || side.size == 0) continue;
            const std::size_t size = side.size;
            if (size > MaximumCachedSideDataBlobBytes) continue;
            // First-seen wins: the demuxer declares these once, so a repeat of
            // the same type further down the list adds nothing.
            bool duplicate = false;
            for (const auto& blob : captured) {
                if (blob.type == static_cast<std::uint32_t>(side.type)) { duplicate = true; break; }
            }
            if (duplicate) continue;
            if (captured.size() >= MaximumStreamSideDataBlobs) break;
            if (capturedBytes + size > MaximumStreamSideDataBytes) continue;
            // Byte-for-byte copy, type carried as a plain number: nothing here
            // reads the payload, which is the whole point of the neutral seam.
            captured.push_back(SideDataBlob{ static_cast<std::uint32_t>(side.type),
                std::vector<std::uint8_t>(side.data, side.data + size) });
            capturedBytes += size;
        }
        // Replace rather than append: the cache belongs to exactly one stream
        // binding, so the new track's records can never be mixed with the
        // previous track's.
        std::lock_guard lock(sideDataMutex_);
        streamSideData_ = std::move(captured);
    } catch (...) {
        // The cache is advisory: a record that could not be stored costs the
        // host one NotFound answer, never a failed open or a stalled stream.
    }
}

FFFResult PlayerSession::CopyStreamSideData(const std::uint32_t sideDataType,
    void* buffer, const std::uint32_t capacity, std::uint32_t& writtenSize) noexcept {
    try {
        std::lock_guard lock(sideDataMutex_);
        const SideDataBlob* found = nullptr;
        for (const auto& blob : streamSideData_) {
            if (blob.type != sideDataType) continue;
            found = &blob;
            break;
        }
        if (found == nullptr) {
            writtenSize = 0;
            return FFFResult::NotFound;
        }
        // Reported on every path that found an entry, including BufferTooSmall:
        // that is how the caller learns what to allocate for the retry.
        writtenSize = static_cast<std::uint32_t>(found->bytes.size());
        if (buffer == nullptr) return FFFResult::Success;  // size probe, writes nothing
        if (capacity < writtenSize) return FFFResult::BufferTooSmall;
        std::memcpy(buffer, found->bytes.data(), writtenSize);
        return FFFResult::Success;
    } catch (...) {
        writtenSize = 0;
        return FFFResult::NativeFailure;
    }
}

void PlayerSession::ClearPendingPackets() noexcept {
    ClearPacketQueue(pendingVideoPackets_, pendingVideoPacketBytes_);
    ClearPacketQueue(pendingAudioPackets_, pendingAudioPacketBytes_);
}

void PlayerSession::ClearPacketQueue(std::deque<AVPacket*>& queue, std::size_t& bytes) noexcept {
    for (auto*& packet : queue) av_packet_free(&packet);
    queue.clear();
    bytes = 0;
}

void PlayerSession::DecodePendingPacket(const bool video) noexcept {
    auto& queue = video ? pendingVideoPackets_ : pendingAudioPackets_;
    auto& bytes = video ? pendingVideoPacketBytes_ : pendingAudioPacketBytes_;
    if (queue.empty()) return;
    auto* packet = queue.front();
    queue.pop_front();
    bytes -= static_cast<std::size_t>(std::max(packet->size, 0));
    // Upstream added a decodeEnhancement flag so a packet whose EL was already
    // decoded ahead of time (its `opaque` is marked) does not decode the EL twice.
    // Our IAMF branch replaces the ordinary audio/video path entirely, so the two
    // changes are independent: keep the flag on the path it belongs to, and keep the
    // IAMF interception ahead of it.
    // Queued audio still has to go through the IAMF decoder when it owns the
    // track and no pump is feeding it, otherwise buffered packets would silently
    // play through FFmpeg. With the pump alive the packet is dropped: one owner
    // per shared libiamf handle (see the same rule in PumpPlayback).
    if (!video && iamfDecoder_ != nullptr && audioRenderer_ != nullptr) {
        av_packet_free(&packet);
        return;
    }
    if (!video && iamfDecoder_ != nullptr) (void)DecodeIamfPacket(packet);
    else DecodePacket(video ? videoDecoder_ : audioDecoder_, packet, video, format_);
    av_packet_free(&packet);
}

void PlayerSession::NormalizeVideoFrameTimestamp(AVFrame* frame) noexcept {
    if (frame == nullptr || format_ == nullptr || videoStream_ < 0) return;
    auto* stream = format_->streams[videoStream_];
    const auto originalPts = frame->best_effort_timestamp == AV_NOPTS_VALUE
        ? frame->pts : frame->best_effort_timestamp;
    std::int64_t duration = 0;
    if (frame->duration > 0) {
        duration = av_rescale_q(frame->duration, stream->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)});
    } else {
        const auto frameRate = stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0
            ? stream->avg_frame_rate : stream->r_frame_rate;
        if (frameRate.num > 0 && frameRate.den > 0) {
            duration = av_rescale_q(1, av_inv_q(frameRate),
                AVRational{1, static_cast<int>(TicksPerSecond)});
        }
    }
    // Raw elementary streams commonly carry neither PTS nor duration metadata.
    // A 25 fps fallback keeps their presentation clock monotonic until the
    // demuxer or decoder exposes a better cadence.
    if (duration <= 0) duration = TicksPerSecond / 25;

    if (originalPts == AV_NOPTS_VALUE) {
        const auto position = std::max<std::int64_t>(0, nextUntimedVideoPosition100ns_);
        const auto timestamp = av_rescale_q(position,
            AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base) +
            av_rescale_q(TimelineOrigin100ns(format_),
                AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base);
        frame->pts = timestamp;
        frame->best_effort_timestamp = timestamp;
        nextUntimedVideoPosition100ns_ = position + duration;
        return;
    }

    const auto position = StreamTimestampPosition100ns(format_, videoStream_, originalPts);
    if (disc_ && snapshot_.frameIndex < 3 && GetEnvironmentVariableW(L"FFF_DISC_TRACE", nullptr, 0))
        std::fprintf(stderr, "DISC frame pts=%lld position=%lld origin=%lld offset=%lld\n", originalPts, position, TimelineOrigin100ns(format_), discPositionOffset_);
    nextUntimedVideoPosition100ns_ = std::max(nextUntimedVideoPosition100ns_, position + duration);
}

void PlayerSession::ResetCadenceSpread() noexcept {
    cadenceSpreadMin100ns_ = 0; cadenceSpreadMax100ns_ = 0;
    cadenceSpreadSamples_ = 0; cadenceModeRefreshPending_ = false;
    publishedFrameRateMode_ = nullptr;
    // 观测到的最快速率与散布同生命周期：只在媒体身份变化时清。上限是"初值与观测峰值里
    // 取大的那个"，所以重新发布（seek、设备恢复、EOF 那些路径都会走 RebuildMediaInfo）
    // 不能把已经抬上去的观测值抹掉——实测 12a 那件就因此从 60 掉回 25。
    mediaObservedPeakHz_ = 0.0;
}

const char* PlayerSession::MeasuredFrameRateMode() const noexcept {
    if (cadenceSpreadSamples_ < MinimumCadenceModeSamples || cadenceSpreadMax100ns_ <= 0)
        return nullptr;
    const auto spread = static_cast<double>(cadenceSpreadMax100ns_ - cadenceSpreadMin100ns_) /
        static_cast<double>(cadenceSpreadMax100ns_);
    // 5% 这条线两侧都有实测点：CFR 的取整散布只有 0.6%（16.6 与 16.7 ms 交替），而两段
    // 变速的夹具一上来就是 50%（20 与 40 fps）。12 帧这个门槛是为了不让片头那几帧的
    // 抖动单独定判词。
    return spread > 0.05 ? "variable" : "constant";
}

const char* PlayerSession::FrameRateModeLabel(const AVStream* stream, const AVRational guessed,
    const AVRational nominal) const noexcept {
    if (const auto* measured = MeasuredFrameRateMode()) return measured;
    // nominalFrameRate 在 r_frame_rate 无效时就是 guessed 的复制，比较它等于自己比自己，
    // 那个"constant"没有证据。此时报 unknown——宿主的映射本来就把别的值画成"—"
    // （FFF.Player/Form媒体信息.vb:183），不会多出一个要改的分支。
    if (stream == nullptr || stream->r_frame_rate.num <= 0 || stream->r_frame_rate.den <= 0)
        return "unknown";
    return av_cmp_q(guessed, nominal) == 0 ? "constant" : "variable";
}

std::int64_t PlayerSession::VideoFramePosition(const AVFrame* frame) const noexcept {
    if (frame == nullptr || format_ == nullptr || videoStream_ < 0) return snapshot_.position100ns;
    const auto pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) return snapshot_.position100ns;
    return StreamTimestampPosition100ns(format_, videoStream_, pts);
}

void PlayerSession::EngagePresentationPace(const double floorHz, const double contentHz,
    const std::int64_t lag100ns, const char* via) noexcept {
    paceEngaged_ = true;
    paceHz_ = floorHz;
    paceOverDeliverStreak_ = 0;
    pacePacedFrames_ = 0;
    videoRenderer_.SetPresentPaceFloorHz(floorHz);
    if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
        std::fprintf(stderr,
            "[pace] engage via=%s floor=%.1fHz content=%.1fHz lag=%lldms\n",
            via, floorHz, contentHz, static_cast<long long>(lag100ns / 10'000));
    paceLateStreak_ = 0;
    ResetPaceWindow();
}

void PlayerSession::ReleasePresentationPace(const char* reason) noexcept {
    if (!paceEngaged_) return;
    paceEngaged_ = false;
    videoRenderer_.SetPresentPaceFloorHz(0.0);
    if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
        std::fprintf(stderr, "[pace] release reason=%s paced=%llu floor=%.1fHz\n",
            reason, static_cast<unsigned long long>(pacePacedFrames_), paceHz_);
    paceHz_ = 0.0;
    paceLateStreak_ = 0;
    paceOverDeliverStreak_ = 0;
    pacePacedFrames_ = 0;
    ResetPaceWindow();
}

void PlayerSession::ResetPaceWindow() noexcept {
    paceWindowFrames_ = 0;
    paceWindowLate_ = 0;
    paceWindowLateLagSum100ns_ = 0;
}

bool PlayerSession::SettlePaceWindow(const std::int64_t lag100ns, const std::int64_t clock100ns,
    const double contentHz) noexcept {
    if (paceWindowFrames_ < PaceWindowFrames) return false;
    const auto windowFrames = paceWindowFrames_;
    const auto windowLate = paceWindowLate_;
    const auto windowLateLagSum100ns = paceWindowLateLagSum100ns_;
    const auto windowStart100ns = paceWindowStart100ns_;
    // Every outcome restarts the window, so the next settlement measures a fresh
    // stretch instead of averaging across a regime change.
    ResetPaceWindow();
    if (contentHz <= 0.0 || windowLate == 0) return false;
    const auto share = static_cast<double>(windowLate) / static_cast<double>(windowFrames);
    const auto span100ns = clock100ns - windowStart100ns;
    const auto meanLateLag100ns = windowLateLagSum100ns / static_cast<std::int64_t>(windowLate);
    // The evidence bar: persistent lateness, a span long enough for a rate to
    // mean something, and a typical lag still inside the drift budget. The mean
    // is the cap test, not the settling frame's lag alone: that frame is one
    // sawtooth sample, and on the 8K stream it read 20 ms on windows whose late
    // pictures had all sat at 600-740 ms. The settling frame's own lag is
    // checked as well, mirroring the streak path's over-cap hold: taking the
    // cadence over from a picture that is already past the lip-sync budget
    // buys one paced frame and an immediate drift-cap release.
    if (share < PaceWindowLateShare || span100ns < PaceEstimateMinSpan100ns ||
        meanLateLag100ns > PaceDriftCap100ns || lag100ns > PaceDriftCap100ns) {
        return false;
    }
    // The sustainable delivery rate, measured directly: while the pacer is off
    // every decoded picture becomes exactly one arrival (shown or discarded), so
    // the arrival rate over the window IS the rate the pipeline sustains. The
    // lag-growth estimate the streak uses reads an oscillating stream's sawtooth
    // phase rather than its trend -- a window settling on an on-time picture
    // measures negative growth -- so the arrival rate replaces it here. The
    // min/max form rather than std::clamp because a sub-10 Hz source would make
    // the clamp's bounds cross.
    const auto spanSeconds = static_cast<double>(span100ns) / 10'000'000.0;
    const auto popHz = spanSeconds > 0.0 ? static_cast<double>(windowFrames) / spanSeconds : 0.0;
    const auto floorHz = std::min(contentHz, std::max(popHz, PaceMinimumHz));
    if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
        std::fprintf(stderr,
            "[pace] window share=%.2f meanLag=%lldms pop=%.1fHz floor=%.1f of %.1fHz%s\n",
            share, static_cast<long long>(meanLateLag100ns / 10'000), popHz, floorHz, contentHz,
            floorHz > contentHz * PaceDeficitFraction ? " (no deficit)" : "");
    if (floorHz > contentHz * PaceDeficitFraction) return false;
    EngagePresentationPace(floorHz, contentHz, lag100ns, "window");
    return true;
}

void PlayerSession::PresentVideoFrame(AVFrame* frame, AVFormatContext* owner) noexcept {
    placement_.SamplePresentThread();
    auto* stream = owner->streams[videoStream_];
    const auto pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    const auto position = pts == AV_NOPTS_VALUE ? snapshot_.position100ns :
        StreamTimestampPosition100ns(owner, videoStream_, pts);
    // 内容周期 = 这一帧要在屏上占多久，是渲染器丢弃分桶与停留判带的操作数来源。它不再
    // 是翻帧率上限——上限改成 per-媒体，打开媒体时由 RebuildMediaInfo 推一次
    // （MediaFrameRateCeilingHz）。按"离呈现时钟最近"取，四级：
    //   ① 队列里下一帧的 PTS − 本帧 PTS。释放闸比的就是这个 position，所以用同一个差
    //      去定这一帧的停留，两者不可能互相掐。可变帧率的间距只有这里是**测出来**的。
    //   ② frame->duration —— 队列已空（片尾、静图）时没有 ① 可用。
    //   ③ 容器率 avg→r —— 整片一个常数，可变帧率在这一档必然失真。实测形状：VFR 的 .ts
    //      包级 duration 170/170 全是 N/A，而 avg 被 demuxer 按流头填成 25/1，于是
    //      后 2 秒的 60fps 被上限掐到 25，4 秒里丢 68 帧（present_pacing_probe ⑥ 的晚段）。
    //   ④ 0 = 未知 —— 渲染器的 min() 自动只剩面板那一半。这一档不是可选项：缺了它，
    //      ①②③ 全落空时这里会静默留着**上一条媒体**的周期，正是 §A9 ⑤ 要抓的形状。
    std::int64_t contentPeriod100ns = 0;
    bool perFrameEvidence = false;
    // ① 只认队列路（PresentVideoFrame 的另一个调用点是暂停/seek 单帧路，那时队列里
    //    还留着 seek 前的帧，front() 不是本帧的下一帧）。判据就用调用方自己那一条
    //    Playing 谓词，不另立标准。
    if (!videoFrameQueue_.empty() && state_.load() == FFF3FPState::Playing) {
        const auto next = VideoFramePosition(videoFrameQueue_.front());
        if (next > position) { contentPeriod100ns = next - position; perFrameEvidence = true; }
    }
    if (contentPeriod100ns <= 0 && frame->duration > 0) {
        contentPeriod100ns = av_rescale_q(frame->duration, stream->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)});
        perFrameEvidence = true;
    }
    if (contentPeriod100ns <= 0) {
        const auto frameRate = stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0
            ? stream->avg_frame_rate : stream->r_frame_rate;
        if (frameRate.num > 0 && frameRate.den > 0)
            contentPeriod100ns = av_rescale_q(1, av_inv_q(frameRate),
                AVRational{1, static_cast<int>(TicksPerSecond)});
    }
    lastVideoFrameDuration100ns_ = contentPeriod100ns > 0 ? contentPeriod100ns : 0;
    if (perFrameEvidence && contentPeriod100ns > 0) {
        if (cadenceSpreadSamples_ == 0)
            cadenceSpreadMin100ns_ = cadenceSpreadMax100ns_ = contentPeriod100ns;
        else {
            cadenceSpreadMin100ns_ = std::min(cadenceSpreadMin100ns_, contentPeriod100ns);
            cadenceSpreadMax100ns_ = std::max(cadenceSpreadMax100ns_, contentPeriod100ns);
        }
        ++cadenceSpreadSamples_;
        // 上限只允许被"看见的更快的那一帧"抬高，永不自己回落（换媒体才整份替换）。
        // 这条不是保险，是必需的：`r_frame_rate` 的误差方向**不是**只会偏松——TS 没有全局索引，
        // 解容器按流头采样，`12b_vfr_20then40.ts` 实测 `r_frame_rate=20/1`、`av_guess=20/1`，
        // 而后半段真的是 40fps（电池读到的平台段是 [2.60s@20][1.88s@40]）。
        // 消融实测（docs/03 §A12）说清危害的边界：关掉上抬以后报出去的上限停在 20，那是个假数，
        // 但**视频翻帧并没有被它掐住**——后半段仍以 40.1Hz 呈现、传统口径丢弃 0，因为有新画面时
        // presenter 不等排程。所以危害是"读数错"＋"没有新画面时那条覆盖层排程按错周期走"，不是
        // 丢帧；真要让上限去钳视频就只能丢真帧，那是错的。偏松同理无害：上限松只等于让面板那一半
        // （vblank 钳 + 丢弃分桶）继续当限制。
        mediaObservedPeakHz_ = std::max(mediaObservedPeakHz_,
            10'000'000.0 / static_cast<double>(cadenceSpreadMin100ns_));
        videoRenderer_.RaiseMediaFrameRateCeilingHz(mediaObservedPeakHz_);
        // 判词第一次成立时刷一次 media info：RebuildMediaInfo 只在 open／设备恢复／换轨
        // 那些路径被调，不刷的话这个字段会永远停在"还没测出来"的那一版。只刷一次。
        // 判词变了才重发布（不是每帧刷 JSON，也不是一次性发布）。
        const char* mode = MeasuredFrameRateMode();
        if (mode != nullptr && mode != publishedFrameRateMode_)
            cadenceModeRefreshPending_ = true;
        if (GetEnvironmentVariableW(L"FFF_CADENCE_TRACE", nullptr, 0))
            std::fprintf(stderr, "CADENCE n=%d min=%lld max=%lld pending=%d mode=%s\n",
                cadenceSpreadSamples_, static_cast<long long>(cadenceSpreadMin100ns_),
                static_cast<long long>(cadenceSpreadMax100ns_), cadenceModeRefreshPending_ ? 1 : 0,
                MeasuredFrameRateMode());
    }
    // Hand the source period to the cadence lock. It is what lets the presenter size each
    // flip in refresh slots (2.4 slots for 60 fps on 144 Hz) instead of taking one slot per
    // flip, which is how a display-synced loop ends up re-scanning-out the same frame.
    videoRenderer_.SetContentFramePeriod100ns(lastVideoFrameDuration100ns_);
    auto nextIndex = snapshot_.frameIndex + 1;
    if (nextIndex < 0) nextIndex = 0;
    if (pts != AV_NOPTS_VALUE) {
        if (rebuildingFrameIndex_) {
            // During an out-of-window SeekFrame rebuild, the rolling PTS table
            // is rebuilt from the stream start. Its absolute index must follow
            // the decoded ordinal rather than the deque's local size.
            nextIndex = std::max<std::int64_t>(0, snapshot_.frameIndex + 1);
            auto indexed = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), pts);
            if (indexed == framePtsIndex_.end() || *indexed != pts)
                framePtsIndex_.insert(indexed, pts);
        } else if (framePtsIndex_.empty() || pts > framePtsIndex_.back()) {
            framePtsIndex_.push_back(pts);
            nextIndex = framePtsIndexBase_ + static_cast<std::int64_t>(framePtsIndex_.size() - 1);
        } else {
            auto indexed = std::lower_bound(framePtsIndex_.begin(), framePtsIndex_.end(), pts);
            if (indexed == framePtsIndex_.end() || *indexed != pts)
                indexed = framePtsIndex_.insert(indexed, pts);
            nextIndex = framePtsIndexBase_ +
                static_cast<std::int64_t>(std::distance(framePtsIndex_.begin(), indexed));
        }
        while (framePtsIndex_.size() > MaximumIndexedVideoFrames) {
            framePtsIndex_.pop_front();
            ++framePtsIndexBase_;
        }
    }
    const auto fulfillingSeek = seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_;
    if ((!keyframeSeekPending_ && seekTarget100ns_ >= 0 && position < seekTarget100ns_) ||
        (seekTargetFrame_ >= 0 && nextIndex < seekTargetFrame_)) { snapshot_.frameIndex = nextIndex; return; }

    // Frames skipped while chasing a seek target never reached this index, but
    // every frame from here on has a final presentation index -- including one
    // dropped for lateness, which still carried metadata for that index. The
    // ring is keyed by that index so the host's injection can use the same
    // matching rule the renderer already applies.
    CacheFrameSideData(frame, nextIndex);

    if (keyframeSeekPending_) {
        keyframeSeekPending_ = false;
        seekTarget100ns_ = -1;
        seekTargetFrame_ = -1;
        ResetClock(position);
        if (audioRenderer_) audioRenderer_->Reset(position);
    }

    const auto frameDuration = std::max<std::int64_t>(0, lastVideoFrameDuration100ns_);
    const auto lateTolerance = std::max<std::int64_t>(frameDuration * 2, 500'000);
    const auto clock100ns = ClockPosition();
    // An "arrival" is a picture the worker popped while the session is really
    // playing. The rolling engage window counts every arrival, late or not, so
    // a deficit that oscillates still fills a window even though it never
    // builds the streak the fast path needs. Pause, seek and the pre-first-frame
    // gap are not arrivals: they would stretch the window's span and corrupt
    // its rate estimate, so they reset it instead. The first-frame exclusion
    // carries over from the discard test below -- until one picture has been
    // presented there is no timing relationship to measure.
    const auto arrivalCounts = state_.load() == FFF3FPState::Playing && !fulfillingSeek &&
        !audioBlockedUntilVideoFrame_ && snapshot_.framePts != AV_NOPTS_VALUE;
    const auto late = arrivalCounts &&
        // Drop a late picture only when a newer one can replace it. A decoder
        // below realtime must keep showing its newest output, not freeze forever.
        !videoFrameQueue_.empty() &&
        position + lateTolerance < clock100ns;
    const auto contentHz = frameDuration > 0 ? 10'000'000.0 / static_cast<double>(frameDuration) : 0.0;
    // A late picture is either discarded or, while the pacer is engaged and within
    // its drift budget, shown at the slower cadence the renderer is holding. The
    // bucketing below runs for both outcomes because it decides which is which.
    bool discardLateFrame = false;
    // True only when the panel is the binding constraint. That frame belongs to
    // displayRateDiscardedFrames_ alone: it must not also move the traditional counter
    // or feed the adaptive-downscale policy (the panel owns the limit, a smaller
    // picture cannot fix it). The comment below has always said this; the accounting
    // at the end of this block used to ignore the distinction.
    bool displayRateDiscard = false;
    if (late) {
        snapshot_.frameIndex = nextIndex;
        // Two different reasons land at this one test, and they must not share a counter.
        //
        //   content rate > panel rate -- the frame is late because the display can only
        //     scan out DisplayCadenceHz() frames per second while the source delivers more.
        //     Nothing in the pipeline is failing: at 240 fps on a 144 Hz panel the queue
        //     gains ~2.4 ms of lateness per frame and must overflow any tolerance. Counted as
        //     display-rate discards.
        //   otherwise -- the pipeline fell behind the media clock (decode, upload, a costly
        //     frame). Counted as droppedVideoFrames, the traditional reading.
        //
        // The distinction is not cosmetic: the adaptive-downscale policy below exists to
        // relieve a decode/upload bottleneck, and feeding it display-rate discards would make
        // it resample 240 fps content down toward the window size to fix something a smaller
        // picture cannot fix -- the panel is the binding constraint. So it is not fed here.
        //
        // Caveat kept honest: a genuine pipeline stall *during* such content is classified
        // into the display-rate bucket too. The count therefore means "discarded while the
        // panel was the binding constraint", which is what a caller acts on, and it is
        // reported separately so the traditional reading stays clean.
        const auto displayHz = videoRenderer_.DisplayCadenceHz();
        // HasOutputWindow() is part of the test, not decoration: with no window the renderer
        // never runs a display detection, so DisplayCadenceHz() falls back to its 60.0f
        // construction default and headless/clip playback of anything faster than 60 fps would
        // be labelled "the panel cannot keep up" when there is no panel in the session at all.
        // Those drops stay in the traditional bucket, where they belong.
        // The 0.5% margin is a deliberate trade-off, and one edge is knowingly on the wrong
        // side of it: 60 fps content on a 59.94 Hz panel really is panel-limited, but 60 is
        // not greater than 59.94 x 1.005, so those ~0.06 discards per second land in the
        // traditional bucket. Without the margin the two buckets would flip back and forth
        // every time the declared refresh rate moved with a display-mode change, which is
        // worse than a stable, documented 0.1% misattribution.
        const auto lag100ns = clock100ns - position;
        if (videoRenderer_.HasOutputWindow() && contentHz > displayHz * 1.005) {
            // The panel is the binding constraint: no cadence can present more pictures
            // per second than the display scans out, so this bucket never engages the
            // pacer and an engaged pacer steps aside for it.
            displayRateDiscardedFrames_.fetch_add(1, std::memory_order_relaxed);
            if (paceEngaged_) ReleasePresentationPace("panel-limited");
            paceLateStreak_ = 0;
            ResetPaceWindow();
            discardLateFrame = true;
            displayRateDiscard = true;
        } else if (paceEngaged_) {
            if (lag100ns > PaceDriftCap100ns) {
                // Past the lip-sync budget the smooth trade is no longer worth it:
                // hand the frame back to the discard path, which burns the lag down.
                ReleasePresentationPace("drift-cap");
                discardLateFrame = true;
            } else {
                ++pacePacedFrames_;
                ++paceOverDeliverStreak_;
                if (paceOverDeliverStreak_ >= PaceOverDeliverFrames) {
                    // Every one of the last pictures found a newer one already queued:
                    // the pipeline is delivering faster than the pacer presents (it
                    // recovered, or the floor is now too slow). Hand cadence control
                    // back and let the discard path rebalance the queue.
                    ReleasePresentationPace("over-delivery");
                    discardLateFrame = true;
                }
                // else: show this picture. It falls through to the render path; the
                // renderer's pace floor is what holds it on screen longer.
            }
        } else {
            ++paceLateStreak_;
            if (paceLateStreak_ == 1) {
                paceStreakClock100ns_ = clock100ns;
                paceStreakLag100ns_ = lag100ns;
            }
            // Rolling-window accounting. This arrival counts whether or not the
            // streak fast path fires below: the two are independent evidence of
            // the same deficit, and the window is the only one an oscillating
            // stream ever produces.
            if (paceWindowFrames_ == 0) paceWindowStart100ns_ = clock100ns;
            ++paceWindowFrames_;
            ++paceWindowLate_;
            paceWindowLateLagSum100ns_ += lag100ns;
            if (paceLateStreak_ >= PaceEngageLateFrames && lag100ns > PaceDriftCap100ns) {
                // Already past the lip-sync budget: the classic path owns this stream,
                // and re-evaluating every streak would only spam the trace.
                if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
                    std::fprintf(stderr,
                        "[pace] hold streak=%u lag=%lldms (over %lldms drift cap)\n",
                        paceLateStreak_, static_cast<long long>(lag100ns / 10'000),
                        static_cast<long long>(PaceDriftCap100ns / 10'000));
                paceLateStreak_ = 0;
                discardLateFrame = true;
            } else if (paceLateStreak_ >= PaceEngageLateFrames && contentHz > 0.0) {
                // Sustainable delivery rate, measured on the streak itself: the lag
                // grows at (1 - delivered fraction) per second of clock, so the
                // fraction is 1 - dLag/dt from the first late picture of the streak.
                // A burst of late pictures lands inside one render loop and reads as
                // "no growth" -- that is the backlog-burn case, and the full content
                // rate is the right floor for it.
                double fraction = 1.0;
                const auto span100ns = clock100ns - paceStreakClock100ns_;
                if (span100ns >= PaceEstimateMinSpan100ns) {
                    const auto growth = static_cast<double>(lag100ns - paceStreakLag100ns_) /
                        static_cast<double>(span100ns);
                    fraction = std::clamp(1.0 - growth, 0.05, 1.0);
                }
                const auto floorHz = std::clamp(fraction * contentHz, PaceMinimumHz, contentHz);
                if (floorHz <= contentHz * PaceDeficitFraction) {
                    EngagePresentationPace(floorHz, contentHz, lag100ns, "streak");
                    ++pacePacedFrames_;
                } else {
                    // Lateness with no measurable deficit behind it (a one-off costly
                    // frame): pacing at the content rate would only add lag.
                    if (GetEnvironmentVariableW(L"FFF_LADDER_TRACE", nullptr, 0))
                        std::fprintf(stderr,
                            "[pace] hold streak=%u estimate=%.1f of %.1fHz (no deficit)\n",
                            paceLateStreak_, floorHz, contentHz);
                    paceLateStreak_ = 0;
                    discardLateFrame = true;
                }
            } else if (SettlePaceWindow(lag100ns, clock100ns, contentHz)) {
                // The window found the deficit the streak could not: this late
                // picture is the one the pacer shows, at the cadence the window
                // just measured.
                ++pacePacedFrames_;
            } else {
                discardLateFrame = true;
            }
        }
        if (discardLateFrame) {
            if (!displayRateDiscard) {
                ++snapshot_.droppedVideoFrames;
                // Feed the drop to the adaptive-downscale policy. Only software decoding can
                // benefit, because a hardware frame never crosses PCIe as CPU planes.
                if (!IsHardwareFrame(frame))
                    videoRenderer_.ObserveFrameForAdaptiveDownscale(true);
            }
        }
    } else if (arrivalCounts) {
        // An on-time picture breaks both streaks: whatever deficit (or over-delivery)
        // the last stretch showed is over as far as this frame is concerned. While the
        // pacer is engaged this is the steady state -- the queue is empty at pop, so
        // the floor is holding the cadence, not the discard path.
        paceLateStreak_ = 0;
        paceOverDeliverStreak_ = 0;
        if (!paceEngaged_) {
            // It also counts into the rolling window: on an oscillating stream the
            // on-time arrivals are half the evidence, and skipping them would make
            // every window look saturated with lateness.
            if (paceWindowFrames_ == 0) paceWindowStart100ns_ = clock100ns;
            ++paceWindowFrames_;
            if (SettlePaceWindow(clock100ns - position, clock100ns, contentHz))
                ++pacePacedFrames_;
        }
    } else if (paceEngaged_ || paceLateStreak_ != 0 || paceOverDeliverStreak_ != 0 ||
        paceWindowFrames_ != 0) {
        // Not an arrival (pause, seek, the pre-first-frame gap): the timing
        // relationship any window was measuring is gone, so its span would be
        // meaningless. Drop the streaks and the half-filled window alike.
        paceLateStreak_ = 0;
        paceOverDeliverStreak_ = 0;
        ResetPaceWindow();
    }
    if (discardLateFrame) {
        snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
        PublishSnapshot();
        return;
    }

    AVFrame* renderFrame = frame;
    // Only a D3D11 surface can be handed to the renderer as-is. Anything else (the CUDA
    // backend, or QSV) has to come back to CPU memory first as a semi-planar frame.
    // A still image is downloaded even when it is a D3D11 surface, because the two routes
    // do not agree on chroma siting: the texture route substitutes AVCHROMA_LOC_LEFT for an
    // unspecified site (VideoRenderer::ResolveChromaOffset's caller) while the plane route
    // applies no offset, and images have always been presented through the plane route.
    // Measured: hardware-decoded AVIF stills at 640x480 / 960x540 / 1280x720 differed from
    // the CPU arm on 1.07%-2.24% of pixels, in full-height vertical stripes up to 0.518
    // (docs/99 section 13). Downloading puts both arms on the route that already matched
    // HEVC and every >=1920x1080 sample exactly.
    if (IsHardwareFrame(frame) &&
        (!CanPresentHardwareFrameDirectly(frame, videoRenderer_) || (staticImage_ && owner == format_))) {
        if (videoTransferFrame_ == nullptr) videoTransferFrame_ = av_frame_alloc();
        if (videoTransferFrame_ == nullptr) {
            Fail(FFFResult::NativeFailure, "Could not allocate the reusable hardware-transfer frame.");
            return;
        }
        av_frame_unref(videoTransferFrame_);
        const auto transferStart = std::chrono::steady_clock::now();
        const auto transferResult = av_hwframe_transfer_data(videoTransferFrame_, frame, 0);
        if (transferResult < 0) {
            if (snapshot_.decodeMode == FFF3FPDecodeMode::Gpu) {
                hardwareFallbackPending_ = true;
                pendingHardwareFallbackReason_ = "The GPU frame transfer failed: " +
                    FfmpegError(transferResult) + "; playback continued with CPU decoding.";
                return;
            }
            Fail(FFFResult::FfmpegFailure, "Could not transfer the hardware-decoded frame for presentation.");
            return;
        }
        snapshot_.hardwareTransfer100ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - transferStart).count() / 100);
        av_frame_copy_props(videoTransferFrame_, frame);
        renderFrame = videoTransferFrame_;
    }
    const auto previousColorMode = snapshot_.actualColorMode;
    const auto firstFrameForMedia = snapshot_.framePts == AV_NOPTS_VALUE;
    // P3 -> BT.2020 runs **before** the frame is cached and rendered, so the
    // retained recovery frame is already converted and a device-loss redraw
    // does not lose the gamut again. It reads CPU planes, so a frame still held on
    // the GPU is left to the renderer's own wide-gamut path instead.
    const AVFrame* frameToRender = renderFrame;
    AVFrame* gamutConverted = nullptr;
    if (staticImage_ && owner == format_ && !IsHardwareFrame(renderFrame) &&
        NeedsPrimariesCarrier(renderFrame)) {
        if (ConvertPrimariesToBt2020(renderFrame, &gamutConverted) == FFFResult::Success &&
            gamutConverted != nullptr) {
            frameToRender = gamutConverted;
        }
    }
    if (staticImage_ && owner == format_) {
        if (stillImageFrame_ == nullptr) stillImageFrame_ = av_frame_alloc();
        if (stillImageFrame_ == nullptr) {
            Fail(FFFResult::NativeFailure, "Could not retain the still-image frame for graphics recovery.");
            if (gamutConverted != nullptr) av_frame_free(&gamutConverted);
            return;
        }
        av_frame_unref(stillImageFrame_);
        const auto retainResult = av_frame_ref(stillImageFrame_, frameToRender);
        if (retainResult < 0) {
            Fail(FFFResult::NativeFailure,
                "Could not retain the still-image frame for graphics recovery: " +
                FfmpegError(retainResult));
            if (gamutConverted != nullptr) av_frame_free(&gamutConverted);
            return;
        }
    }
    // A frame that reaches the renderer counts against the adaptive-downscale drop
    // ratio; hardware frames never engage the policy, so they are excluded here too.
    if (renderFrame != nullptr && !IsHardwareFrame(renderFrame))
        videoRenderer_.ObserveFrameForAdaptiveDownscale(false);
    // Optional auxiliary layer: the caller's per-frame request says "this picture
    // is expected to carry the composed signal". The frame is paired by PTS within
    // one base-frame period; when nothing pairs, the params still go to the
    // renderer so it withholds the caller's colour table instead of sampling it on
    // an uncomposed picture.
    AVFrame* auxiliaryFrame = nullptr;
    FFF3FPAuxiliaryComposition auxiliaryComposition{};
    bool elCompositionRequested = false;
    if (auxLayerRequested_.load(std::memory_order_acquire)) {
        AuxCompositionRequest request{};
        if (SelectAuxiliaryComposition(nextIndex, request)) {
            elCompositionRequested = request.enabled != 0;
            if (elCompositionRequested) {
                auxiliaryFrame = TakeAuxiliaryFrame(pts, owner);
                auxiliaryComposition = FFF3FPAuxiliaryComposition{
                    sizeof(FFF3FPAuxiliaryComposition),
                    FFF3FP_AUXILIARY_COMPOSITION_VERSION, nextIndex,
                    request.enabled, request.residualCenter, request.residualShift,
                    request.maxValue};
            }
        }
    }
    const auto renderResult = videoRenderer_.Render(frameToRender, nextIndex,
        staticImage_ && owner == format_, false, false, staticImage_ && owner == format_,
        auxiliaryFrame, elCompositionRequested ? &auxiliaryComposition : nullptr);
    // Diagnostic (FFF_EL_TRACE), the same shape as the ladder/cadence traces:
    // "requested but not paired" is the state that withholds the colour table, and
    // it is invisible in a headless session otherwise.
    if (GetEnvironmentVariableW(L"FFF_EL_TRACE", nullptr, 0) != 0 &&
        (elCompositionRequested || auxiliaryFrame != nullptr))
        std::fprintf(stderr, "[el] frame=%lld request=%u paired=%u aux=%dx%d fmt=%d stream=%d queued=%zu\n",
            static_cast<long long>(nextIndex), elCompositionRequested ? 1u : 0u,
            auxiliaryFrame != nullptr ? 1u : 0u,
            auxiliaryFrame != nullptr ? auxiliaryFrame->width : 0,
            auxiliaryFrame != nullptr ? auxiliaryFrame->height : 0,
            auxiliaryFrame != nullptr ? auxiliaryFrame->format : -1,
            auxStream_, auxFrameQueue_.size());
    if (auxiliaryFrame != nullptr) av_frame_free(&auxiliaryFrame);
    if (gamutConverted != nullptr) av_frame_free(&gamutConverted);
    if (renderResult != FFFResult::Success) {
        if (renderResult == FFFResult::DeviceFailure &&
            videoRenderer_.RequestRecoveryIfDeviceLost()) return;
        if (renderResult == FFFResult::NotSupported &&
            snapshot_.decodeMode == FFF3FPDecodeMode::Gpu) {
            hardwareFallbackPending_ = true;
            pendingHardwareFallbackReason_ = videoRenderer_.LastError();
            if (!pendingHardwareFallbackReason_.empty())
                pendingHardwareFallbackReason_ += " Playback continued with CPU decoding.";
            return;
        }
        Fail(renderResult, videoRenderer_.LastError(), "render"); return;
    }
    if (ShouldDelayAudioUntilVideoFrame() &&
        audioUnblockVideoGeneration_ == 0 && videoRenderer_.HasOutputWindow()) {
        audioUnblockVideoGeneration_ = videoRenderer_.SubmittedVideoGeneration();
        if (!PresentAudioBoundary()) return;
    }
    snapshot_.queuedVideoFrames = static_cast<std::uint32_t>(videoFrameQueue_.size());
    snapshot_.position100ns = position; snapshot_.frameIndex = nextIndex; snapshot_.framePts = pts;
    snapshot_.frameTimeBaseNumerator = stream->time_base.num;
    snapshot_.frameTimeBaseDenominator = stream->time_base.den;
    snapshot_.actualColorMode = videoRenderer_.ActualColorMode();
    snapshot_.sourcePeakNits = static_cast<std::uint32_t>(std::lround(videoRenderer_.SourcePeakNits()));
    ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
    UpdateAudioDiagnostics();
    PublishSnapshot();
    if (firstFrameForMedia) RebuildMediaInfo();
    if (snapshot_.actualColorMode != previousColorMode) {
        std::ostringstream json; json << "{\"requested\":" << static_cast<unsigned>(snapshot_.requestedColorMode)
            << ",\"actual\":" << static_cast<unsigned>(snapshot_.actualColorMode) << ",\"reason\":\""
            << EscapeJson(videoRenderer_.FallbackReason()) << "\"}";
        Emit(FFF3FPEvent::ColorModeChanged, json.str());
    }
    seekTarget100ns_ = -1; seekTargetFrame_ = -1; keyframeSeekPending_ = false;
    rebuildingFrameIndex_ = false;
}

void PlayerSession::QueueAudioFrame(AVFrame* frame, AVFormatContext* owner, const std::int32_t streamIndex) noexcept {
    if (!audioRenderer_ || streamIndex < 0) return;
    UpdateInputAudioPeakLevels(frame);
    if (ShouldDelayAudioUntilVideoFrame()) return;
    auto* stream = owner->streams[streamIndex];
    const auto pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? frame->pts : frame->best_effort_timestamp;
    auto position = AV_NOPTS_VALUE;
    if (pts != AV_NOPTS_VALUE) {
        position = StreamTimestampPosition100ns(owner, streamIndex, pts);
        if (owner == externalFormat_) position += externalAudioOffset100ns_;
        // The internal track -- plain decode and the IAMF-rendered frames, which are
        // both queued with owner == format_ -- gets the session delay instead. Chosen
        // by owner, so a frame is never shifted by both knobs. Applied before the
        // seek filter below, i.e. the filter compares already-shifted positions against
        // seekTarget100ns_, exactly as the external offset has always done. Untimed
        // frames skip this whole block (pts == AV_NOPTS_VALUE), so they are not shifted.
        else position += audioDelay100ns_;
    }
    if (pts != AV_NOPTS_VALUE && seekTarget100ns_ >= 0) {
        if (position + av_rescale(frame->nb_samples, TicksPerSecond, frame->sample_rate) < seekTarget100ns_) return;
    }
    ++snapshot_.decodedAudioFrames;
    const auto timestampResolution = av_rescale_q(1, stream->time_base, AVRational{1, TicksPerSecond});
    const auto result = TempoAudio(frame, position, timestampResolution);
    if (result == FFFResult::Success && owner == format_) internalAudioDecodeErrorCount_ = 0;
    if (result == FFFResult::BufferTooSmall) ++snapshot_.audioRejectedFrames;
    else if (result != FFFResult::Success) {
        if (owner == format_) {
            internalAudioFailureResult_ = result;
            internalAudioFailurePending_ = true;
        } else {
            Fail(result, audioRenderer_->LastError(), "audio-render");
        }
    }
}

void PlayerSession::UpdateInputAudioPeakLevels(const AVFrame* frame) noexcept {
    if (frame == nullptr || frame->ch_layout.nb_channels <= 0 || frame->nb_samples <= 0) return;
    const auto channels = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(frame->ch_layout.nb_channels), PlayerAudioRuntimeState::MaximumChannels);
    audioRuntimeState_.inputChannelCount.store(channels, std::memory_order_release);
    std::array<float, PlayerAudioRuntimeState::MaximumChannels> peaks{};
    const auto format = static_cast<AVSampleFormat>(frame->format);
    const auto planar = av_sample_fmt_is_planar(format) != 0;
    const auto packedFormat = planar ? av_get_packed_sample_fmt(format) : format;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const auto* data = frame->extended_data[planar ? channel : 0];
        for (int sample = 0; sample < frame->nb_samples; ++sample) {
            const auto index = planar ? sample : sample * frame->ch_layout.nb_channels + channel;
            float value = 0.0f;
            switch (packedFormat) {
                case AV_SAMPLE_FMT_FLT: value = reinterpret_cast<const float*>(data)[index]; break;
                case AV_SAMPLE_FMT_DBL: value = static_cast<float>(reinterpret_cast<const double*>(data)[index]); break;
                case AV_SAMPLE_FMT_U8: value = (static_cast<int>(data[index]) - 128) / 128.0f; break;
                case AV_SAMPLE_FMT_S16: value = reinterpret_cast<const std::int16_t*>(data)[index] / 32768.0f; break;
                case AV_SAMPLE_FMT_S32: value = reinterpret_cast<const std::int32_t*>(data)[index] / 2147483648.0f; break;
                default: break;
            }
            if (std::isfinite(value)) peaks[channel] = std::max(peaks[channel], std::abs(value));
        }
        audioRuntimeState_.inputValues[channel].store(std::clamp(peaks[channel], 0.0f, 1.0f),
            std::memory_order_relaxed);
    }
    for (auto channel = channels; channel < PlayerAudioRuntimeState::MaximumChannels; ++channel)
        audioRuntimeState_.inputValues[channel].store(0.0f, std::memory_order_relaxed);
}

bool PlayerSession::HandleInternalAudioDecodeFailure(const FFFResult result, std::string message) noexcept {
    // A stream switch seeks on the video timeline. MPEG-TS can land on a partial
    // cinema-surround access unit before the next audio sync word; reject that
    // boundary packet and let the decoder lock onto subsequent complete packets.
    if (++internalAudioDecodeErrorCount_ < 32) {
        if (audioDecoder_ != nullptr) avcodec_flush_buffers(audioDecoder_);
        return false;
    }
    DisableFailedInternalAudio(result, std::move(message));
    return true;
}

void PlayerSession::DisableFailedInternalAudio(const FFFResult result, std::string message) noexcept {
    try {
        const auto failedStream = audioStream_;
        const auto position = std::max<std::int64_t>(0, snapshot_.position100ns);
        if (audioDecoder_ != nullptr) avcodec_free_context(&audioDecoder_);
        ReleaseIamfDecoder();
        if (audioDecodeFrame_ != nullptr) av_frame_unref(audioDecodeFrame_);
        audioStream_ = -1;
        snapshot_.selectedAudioStream = -1;
        internalAudioFailurePending_ = false;
        internalAudioFailureResult_ = FFFResult::Success;
        internalAudioDecodeErrorCount_ = 0;
        ClearPacketQueue(pendingAudioPackets_, pendingAudioPacketBytes_);
        if (audioRenderer_) audioRenderer_->Reset(position);
        ResetClock(position);
        UpdateAudioDiagnostics();
        PublishSnapshot();
        if (message.empty()) message = "The selected audio track failed.";
        message += " Playback continued without this audio track; select another track to restore audio.";
        ReportError(result, std::move(message), "audio-track");
        Emit(FFF3FPEvent::OperationCompleted,
            "{\"operation\":\"disable-failed-audio\",\"stream\":" +
            std::to_string(failedStream) + "}");
    } catch (...) {
        ReportError(FFFResult::NativeFailure,
            "The failed audio track could not be disabled cleanly, but the playback session remains available.",
            "audio-track");
    }
}

void PlayerSession::UpdateAudioDiagnostics() noexcept {
    if (audioRenderer_ == nullptr) {
        snapshot_.audioPosition100ns = 0;
        snapshot_.bufferedAudio100ns = 0;
        snapshot_.audioUnderruns = 0;
        snapshot_.audioTimestampJitterFrames = 0;
        snapshot_.audioDiscontinuities = 0;
        snapshot_.audioInsertedSilenceFrames = 0;
        snapshot_.audioDroppedOverlapFrames = 0;
        // No renderer, therefore no bed owner. `audioBinaural` had no writer at all
        // before the plug seam existed -- the field could only ever report the value
        // the reset path left in it, which is why the two arms of the seam write it
        // here and DoClose clears it with the other per-media counters.
        snapshot_.audioBinaural = 0;
        return;
    }
    snapshot_.audioPosition100ns = audioRenderer_->Position100ns();
    snapshot_.bufferedAudio100ns = audioRenderer_->Buffered100ns();
    snapshot_.audioUnderruns = audioRenderer_->UnderrunCount();
    snapshot_.audioTimestampJitterFrames = audioRenderer_->TimestampJitterCount();
    snapshot_.audioDiscontinuities = audioRenderer_->DiscontinuityCount();
    snapshot_.audioInsertedSilenceFrames = audioRenderer_->InsertedSilenceFrames();
    snapshot_.audioDroppedOverlapFrames = audioRenderer_->DroppedOverlapFrames();
    // 1 while the external renderer produced the last bed frame, 0 while the matrix
    // fold-down did -- including a mono bed the plug placed, which is a real
    // placement and not a fold-down (2026-10-18 decision). Multichannel devices and
    // 2.0 sources never enter the seam, so they keep it at 0.
    //
    // Read from the published runtime state, not off `audioRenderer_`: this function
    // also runs on paths where the renderer has just been replaced, and
    // `plugBedActive` is written by the renderer on every frame
    // (PlayerWasapiRenderer::PublishRuntimeDiagnostics), so the value the listener
    // sees is the bed owner as of the last processed frame rather than as of
    // whatever publish happened to run last.
    snapshot_.audioBinaural =
        audioRuntimeState_.plugBedActive.load(std::memory_order_relaxed);
}

void PlayerSession::ResetBitRateTracking() noexcept {
    videoBitRateBuckets_.clear();
    audioBitRateBuckets_.clear();
    publishedBitRateSecond_ = -1;
    snapshot_.videoBitRate = snapshot_.audioBitRate = 0;
}

void PlayerSession::TrackPacketBitRate(const AVPacket* packet, AVFormatContext* owner) noexcept {
    if (packet == nullptr || owner == nullptr || packet->size <= 0 ||
        packet->stream_index < 0 || packet->stream_index >= static_cast<int>(owner->nb_streams)) return;
    const auto* stream = owner->streams[packet->stream_index];
    if (stream == nullptr || stream->codecpar == nullptr) return;
    const bool video = stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    const bool audio = stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
    if ((!video || packet->stream_index != videoStream_) &&
        (!audio || (owner == format_ && packet->stream_index != audioStream_) ||
            (owner == externalFormat_ && packet->stream_index != externalAudioStream_))) return;

    const auto timestamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
    if (timestamp == AV_NOPTS_VALUE) return;
    auto position = StreamTimestampPosition100ns(owner, packet->stream_index, timestamp);
    if (owner == externalFormat_) position += externalAudioOffset100ns_;
    const auto second = std::max<std::int64_t>(0, position) / TicksPerSecond;
    auto& buckets = video ? videoBitRateBuckets_ : audioBitRateBuckets_;
    auto found = std::find_if(buckets.rbegin(), buckets.rend(), [second](const BitRateBucket& item) {
        return item.secondIndex == second;
    });
    if (found != buckets.rend()) {
        found->bytes += static_cast<std::uint32_t>(packet->size);
    } else {
        const auto insertion = std::lower_bound(buckets.begin(), buckets.end(), second,
            [](const BitRateBucket& item, const std::int64_t value) {
                return item.secondIndex < value;
            });
        buckets.insert(insertion, {second, static_cast<std::uint32_t>(packet->size)});
    }
    constexpr std::size_t MaximumBitRateBuckets = 16;
    while (buckets.size() > MaximumBitRateBuckets) buckets.pop_front();
}

void PlayerSession::UpdateBitRateForPosition(const std::int64_t position) noexcept {
    const auto currentSecond = std::max<std::int64_t>(0, position) / TicksPerSecond;
    if (currentSecond == publishedBitRateSecond_) return;
    publishedBitRateSecond_ = currentSecond;
    const auto completedSecond = currentSecond - 1;
    const auto rateFor = [completedSecond](const std::deque<BitRateBucket>& buckets) {
        if (completedSecond < 0) return std::uint64_t{0};
        const auto found = std::find_if(buckets.rbegin(), buckets.rend(),
            [completedSecond](const BitRateBucket& item) {
                return item.secondIndex == completedSecond;
            });
        if (found == buckets.rend() || found->bytes > std::numeric_limits<std::uint64_t>::max() / 8)
            return std::uint64_t{0};
        return found->bytes * 8;
    };
    snapshot_.videoBitRate = rateFor(videoBitRateBuckets_);
    snapshot_.audioBitRate = rateFor(audioBitRateBuckets_);
    PublishSnapshot();
}

void PlayerSession::PumpExternalAudio() noexcept {
    if (externalFormat_ == nullptr || externalAudioDecoder_ == nullptr || externalAudioDrained_) return;
    if (ShouldDelayAudioUntilVideoFrame()) return;
    if (externalAudioPacket_ == nullptr) externalAudioPacket_ = av_packet_alloc();
    if (externalAudioPacket_ == nullptr) {
        Fail(FFFResult::NativeFailure, "Could not allocate an external audio packet.");
        return;
    }
    const auto result = av_read_frame(externalFormat_, externalAudioPacket_);
    if (result < 0) {
        av_packet_unref(externalAudioPacket_);
        if (result != AVERROR_EOF) {
            Fail(FFFResult::FfmpegFailure, "Could not read external audio: " + FfmpegError(result), "external-audio");
            return;
        }
        externalAudioDrained_ = true;
        DecodePacket(externalAudioDecoder_, nullptr, false, externalFormat_);
        // EOF the tempo graph as well, or its internal buffer (atempo/equalizer
        // latency) holds back the tail of the track and Finish() below ends
        // rendering before that tail ever plays out.
        if (audioRenderer_ && tempoGraph_) TempoAudio(nullptr, 0);
        if (audioRenderer_ && audioRenderer_->Finish() != FFFResult::Success)
            Fail(FFFResult::FfmpegFailure, "Could not drain external audio output.", "external-audio");
        return;
    }
    if (externalAudioPacket_->stream_index == externalAudioStream_) {
        TrackPacketBitRate(externalAudioPacket_, externalFormat_);
        DecodePacket(externalAudioDecoder_, externalAudioPacket_, false, externalFormat_);
    }
    av_packet_unref(externalAudioPacket_);
}

FFFResult PlayerSession::GetImageInfo(FFF3FPImageInfo& info) const noexcept {
    if (info.size < sizeof(FFF3FPImageInfo) || info.version != 20) return FFFResult::InvalidArgument;
    // The details were built on the worker thread while the media was opened;
    // handing out that copy keeps the decoder-owned frames off the caller's
    // thread (same contract as GetSnapshot).
    { std::lock_guard lock(snapshotMutex_);
      if (!hasPublishedImageInfo_) return FFFResult::InvalidState;
      info = publishedImageInfo_; }
    return FFFResult::Success;
}

void PlayerSession::BuildImageInfo(FFF3FPImageInfo& info) const noexcept {
    if (format_ == nullptr || videoStream_ < 0) return;

    const auto* stream = format_->streams[videoStream_];
    const auto* parameters = stream->codecpar;

    info.frameCount = stream->nb_frames > 0 ?
        static_cast<std::int32_t>(stream->nb_frames) : -1;
    // FFmpeg exposes no generic loop count; animated demuxers loop unless told
    // otherwise, which OpenWithOptions is meant to cover.
    info.loopCount = -1;

    std::uint32_t flags = 0;
    if (staticImage_) flags |= FFF3FP_IMAGE_FLAG_STATIC;
    if (animatedImage_) flags |= FFF3FP_IMAGE_FLAG_ANIMATED;
    if (stream->nb_frames > 1) flags |= FFF3FP_IMAGE_FLAG_MULTI_FRAME;

    const auto* frame = stillImageFrame_ != nullptr ? stillImageFrame_ : videoDecodeFrame_;

    // EXIF orientation arrives as a display matrix on the decoded frame.
    // av_display_rotation_get returns NaN when there is no usable matrix, which
    // stays reported as unknown rather than silently meaning "no rotation".
    info.rotationQuarterTurns = 0xFFFFFFFFu;
    if (frame != nullptr) {
        const auto* matrix = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);
        if (matrix != nullptr && matrix->size >= 9 * sizeof(std::int32_t)) {
            const auto angle = av_display_rotation_get(
                reinterpret_cast<const std::int32_t*>(matrix->data));
            if (angle == angle) {
                auto normalized = angle;
                while (normalized < 0.0) normalized += 360.0;
                while (normalized >= 360.0) normalized -= 360.0;
                info.rotationQuarterTurns = static_cast<std::uint32_t>(normalized / 90.0 + 0.5) % 4u;
                flags |= FFF3FP_IMAGE_FLAG_HAS_ROTATION;
            }
        }
    }

    // Prefer the decoded frame's pixel format, but fall back to the codec's when
    // the frame carries none: a presented frame is unreffed afterwards, which
    // resets its format to AV_PIX_FMT_NONE, and reading the flags off that would
    // silently drop alpha (and misreport the bit depth).
    //
    // A hardware frame is the second unusable case, and for the same reason: its
    // format names the GPU surface layout (AV_PIX_FMT_D3D11 and friends) rather
    // than the source. Those descriptors declare no components at all, so
    // reporting one misnames the source format AND leaves the bit depth on its
    // 8-bit default -- a 10-bit still was reported as 8-bit. A retained still
    // image frame is a hardware frame, so this is reachable; fall through to the
    // container parameters (which avformat_find_stream_info fills from a software
    // decode, hence independent of the decode path), then to the frame's own
    // underlying software format.
    // A still that was hardware-decoded arrives here as the transferred CPU frame, and a
    // transfer yields a semi-planar layout (NV12/P010/...) for what the source actually
    // stores as separate planes. Report the planar equivalent: the number is a property of
    // the picture, not of whichever decode route the host happened to configure, and the
    // two arms otherwise disagree (measured: 8K HEVC still reported 0 on the CPU arm and
    // 23 on the hardware arm, with the presented pixels identical).
    const auto frameFormat = frame != nullptr && !IsHardwareFrame(frame) ?
        PlanarEquivalent(static_cast<AVPixelFormat>(frame->format)) : AV_PIX_FMT_NONE;
    auto pixelFormat = frameFormat != AV_PIX_FMT_NONE ?
        frameFormat : static_cast<AVPixelFormat>(parameters->format);
    if (pixelFormat == AV_PIX_FMT_NONE && frame != nullptr && frame->hw_frames_ctx != nullptr) {
        const auto* frames = reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data);
        if (frames != nullptr) pixelFormat = static_cast<AVPixelFormat>(frames->sw_format);
    }
    info.sourcePixelFormat = static_cast<std::int32_t>(pixelFormat);
    info.sourceBitDepth = parameters->bits_per_raw_sample > 0 ?
        static_cast<std::uint32_t>(parameters->bits_per_raw_sample) : 8u;
    if (const auto* descriptor = av_pix_fmt_desc_get(pixelFormat)) {
        if (descriptor != nullptr && descriptor->nb_components > 0 &&
            descriptor->comp[0].depth > 0) info.sourceBitDepth = descriptor->comp[0].depth;
        if (descriptor != nullptr &&
            (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0) flags |= FFF3FP_IMAGE_FLAG_HAS_ALPHA;
    }

    // ICC profiles arrive as frame side data from the codecs that embed them.
    info.iccProfileSizeBytes = 0;
    if (frame != nullptr) {
        if (const auto* icc = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE)) {
            info.iccProfileSizeBytes = static_cast<std::uint32_t>(icc->size);
            flags |= FFF3FP_IMAGE_FLAG_HAS_ICC;
        }
    }

    // Colour description. The decoded frame is authoritative when it carries
    // one; the container parameters are the fallback.
    auto primaries = parameters->color_primaries;
    auto colorSpace = parameters->color_space;
    auto transfer = parameters->color_trc;
    if (frame != nullptr) {
        if (frame->color_primaries != AVCOL_PRI_UNSPECIFIED) primaries = frame->color_primaries;
        if (frame->colorspace != AVCOL_SPC_UNSPECIFIED) colorSpace = frame->colorspace;
        if (frame->color_trc != AVCOL_TRC_UNSPECIFIED) transfer = frame->color_trc;
    }
    info.colorPrimaries = primaries == AVCOL_PRI_UNSPECIFIED ? -1 : static_cast<std::int32_t>(primaries);
    info.colorSpace = colorSpace == AVCOL_SPC_UNSPECIFIED ? -1 : static_cast<std::int32_t>(colorSpace);
    info.colorTransfer = transfer == AVCOL_TRC_UNSPECIFIED ? -1 : static_cast<std::int32_t>(transfer);
    // Wider than Rec.709 means the colour cannot survive an SDR swap chain.
    if (primaries == AVCOL_PRI_BT2020 || primaries == AVCOL_PRI_SMPTE431 ||
        primaries == AVCOL_PRI_SMPTE432) {
        flags |= FFF3FP_IMAGE_FLAG_WIDE_GAMUT;
    }

    info.flags = flags;
}

namespace {

// Display P3 and DCI-P3 fit inside BT.2020, so converting to BT.2020 carries
// the gamut **without** clipping it — which is the whole point. Converting to
// Rec.709 instead would throw the extra colours away.
constexpr bool IsP3Primaries(const AVColorPrimaries primaries) noexcept {
    return primaries == AVCOL_PRI_SMPTE431 || primaries == AVCOL_PRI_SMPTE432;
}

}  // namespace

bool PlayerSession::NeedsPrimariesCarrier(const AVFrame* frame) const noexcept {
    if (frame == nullptr) return false;
    // BT.2020 sources already travel the shader's wide-gamut path untouched.
    return IsP3Primaries(static_cast<AVColorPrimaries>(frame->color_primaries));
}

void PlayerSession::ReleasePrimariesCarrierFilter() noexcept {
    if (gamutGraph_ != nullptr) {
        avfilter_graph_free(&gamutGraph_);
        gamutSource_ = nullptr;
        gamutSink_ = nullptr;
        gamutSourceWidth_ = 0;
        gamutSourceHeight_ = 0;
        gamutSourceFormat_ = -1;
    }
}

FFFResult PlayerSession::ConvertPrimariesToBt2020(const AVFrame* input,
    AVFrame** output) noexcept {
    if (input == nullptr || output == nullptr) return FFFResult::InvalidArgument;
    *output = nullptr;
    if (input->width <= 0 || input->height <= 0) return FFFResult::InvalidState;

    if (gamutGraph_ == nullptr || gamutSourceWidth_ != input->width ||
        gamutSourceHeight_ != input->height || gamutSourceFormat_ != input->format) {
        ReleasePrimariesCarrierFilter();
        const auto* bufferSource = avfilter_get_by_name("buffer");
        const auto* bufferSink = avfilter_get_by_name("buffersink");
        const auto* zscale = avfilter_get_by_name("zscale");
        if (bufferSource == nullptr || bufferSink == nullptr || zscale == nullptr)
            return FFFResult::NotSupported;
        auto* graph = avfilter_graph_alloc();
        if (graph == nullptr) return FFFResult::NativeFailure;
        char arguments[192];
        std::snprintf(arguments, sizeof(arguments),
            "video_size=%dx%d:pix_fmt=%d:time_base=1/25:pixel_aspect=1/1",
            input->width, input->height, input->format);
        AVFilterContext* source = nullptr;
        AVFilterContext* sink = nullptr;
        AVFilterContext* gamut = nullptr;
        auto failed = false;
        if (avfilter_graph_create_filter(&source, bufferSource, "in", arguments,
                nullptr, graph) < 0) failed = true;
        if (!failed && avfilter_graph_create_filter(&sink, bufferSink, "out",
                nullptr, nullptr, graph) < 0) failed = true;
        // zscale infers the input primaries from the frame and is told only the
        // output: BT.2020, the gamut the shader already understands.
        if (!failed && avfilter_graph_create_filter(&gamut, zscale, "gamut",
                "primaries=bt2020", nullptr, graph) < 0) failed = true;
        if (!failed && avfilter_link(source, 0, gamut, 0) < 0) failed = true;
        if (!failed && avfilter_link(gamut, 0, sink, 0) < 0) failed = true;
        if (!failed && avfilter_graph_config(graph, nullptr) < 0) failed = true;
        if (failed) {
            avfilter_graph_free(&graph);
            return FFFResult::FfmpegFailure;
        }
        gamutGraph_ = graph;
        gamutSource_ = source;
        gamutSink_ = sink;
        gamutSourceWidth_ = input->width;
        gamutSourceHeight_ = input->height;
        gamutSourceFormat_ = input->format;
    }

    if (av_buffersrc_add_frame_flags(gamutSource_, const_cast<AVFrame*>(input),
            AV_BUFFERSRC_FLAG_KEEP_REF) < 0) return FFFResult::FfmpegFailure;
    auto* converted = av_frame_alloc();
    if (converted == nullptr) return FFFResult::NativeFailure;
    if (av_buffersink_get_frame(gamutSink_, converted) < 0) {
        av_frame_free(&converted);
        return FFFResult::FfmpegFailure;
    }
    *output = converted;
    return FFFResult::Success;
}

void PlayerSession::FlushAtEnd() noexcept {
    if (SoftwareVideoDecodePending()) { Sleep(1); return; }
    if (!draining_) {
        draining_ = true;
        if (videoDecoder_) DecodePacket(videoDecoder_, nullptr, true, format_);
        if (SoftwareVideoDecodePending()) return;
        if (audioBlockedUntilVideoFrame_ && videoFrameQueue_.empty())
            ReleaseAudioWithoutVideo();
    }
    if (!pendingAudioPackets_.empty()) return;
    DrainInternalAudio();
    if (state_.load() == FFF3FPState::Failed) return;
    TryCompletePlaybackPreroll();
    UpdateDrainedAudioClock();
    // Some single-frame image decoders publish their only frame while draining.
    // Do not enter Ended before that queued frame has reached the renderer.
    if (!videoFrameQueue_.empty()) {
        if (!PumpVideoPresentation()) Sleep(1);
        return;
    }
    if (audioRenderer_ && audioRenderer_->Buffered100ns() > 0) { Sleep(2); return; }
    if (videoRenderer_.HasOutputWindow() && videoRenderer_.SubmittedVideoGeneration() >
        videoRenderer_.PresentedVideoGeneration()) {
        const auto result = videoRenderer_.PresentTimedText();
        if (result != FFFResult::Success) {
            if (result == FFFResult::DeviceFailure && videoRenderer_.RequestRecoveryIfDeviceLost()) return;
            Fail(result, videoRenderer_.LastError(), "last-frame-present");
            return;
        }
        if (videoRenderer_.SubmittedVideoGeneration() > videoRenderer_.PresentedVideoGeneration()) return;
    }
    auto endPosition = snapshot_.position100ns;
    if (videoStream_ >= 0 && lastVideoFrameDuration100ns_ > 0)
        endPosition += lastVideoFrameDuration100ns_;
    if (audioRenderer_) endPosition = std::max(endPosition, audioRenderer_->Position100ns());
    if (!staticImage_ && ClockPosition() < endPosition) { Sleep(1); return; }
    // A still image has no playback end; keep it visible and interactive.
    if (staticImage_) {
        RebuildMediaInfo();
        SuspendAudioRenderer(true);
        if (state_.load() == FFF3FPState::Playing) SetState(FFF3FPState::Paused, "end-still-image");
        return;
    }
    // Rewind session-looped animations at EOF while retaining the last visible frame.
    if (sessionWrapLoop_) {
        // Retry temporarily refused rewinds, but bound retries for broken seek implementations.
        if (DoSeek(0) != FFFResult::Success) {
            if (++sessionWrapFailures_ >= 8) {
                sessionWrapLoop_ = false;  // fall through to the ended path below
            } else {
                return;
            }
        } else {
            sessionWrapFailures_ = 0;
            RebuildMediaInfo();
            return;
        }
    }
    snapshot_.duration100ns = std::max(snapshot_.duration100ns, endPosition);
    snapshot_.position100ns = snapshot_.duration100ns;
    RebuildMediaInfo();
    SuspendAudioRenderer(true);
    SetState(FFF3FPState::Ended, "end"); Emit(FFF3FPEvent::PlaybackEnded, "{}");
}

FFFResult PlayerSession::DoSeek(std::int64_t position, const std::int64_t targetFrame,
    const bool exact) noexcept {
    StopSoftwareVideoDecode();
    if (disc_) {
        if (!disc_->Seek(position)) { ReportError(FFFResult::NotSupported, "This disc position cannot be seeked.", "seek"); return FFFResult::NotSupported; }
        if (ReopenDiscDemux() && state_.load() != FFF3FPState::Playing && videoStream_ >= 0)
            seekTarget100ns_ = discPositionOffset_;
        return FFFResult::Success;
    }
    if (!format_) return FFFResult::Success;
    position = std::clamp<std::int64_t>(position, 0, snapshot_.duration100ns > 0 ? snapshot_.duration100ns : position);
    // Still-image seek is a no-op: single-image demuxers have no seekable timeline.
    if (staticImage_) { PublishSnapshot(); return FFFResult::Success; }
    auto decodeStartPosition = position;
    const auto referenceStream = videoStream_ >= 0 ? videoStream_ : audioStream_;
    // 时间戳定位对"没有 read_seek 的解容器"会退到去找一对时间戳——那要把输入一路读下去。
    // 找不到时它返回失败，可输入指针已经停在末尾：裸 Annex-B 就是这个形状，Seek 返回 -5
    // 之后会话读到 EOF、pending 已空，FlushAtEnd 直接判 Ended（实测解码 0 帧，而同一个
    // 文件不 seek 能正常播 88 帧）。所以"定位失败＝没有副作用"不能只写在注释里，必须
    // 真的把读位置还原；avformat_flush 是文档规定的配套调用（绕过解容器去 seek AVIO 之后）。
    const auto byteOffsetBeforeSeek = format_->pb != nullptr ? avio_tell(format_->pb) : -1;
    auto timestamp = av_rescale_q(position + TimelineOrigin100ns(format_),
        AVRational{1, static_cast<int>(TicksPerSecond)},
        format_->streams[referenceStream]->time_base);
    auto seekResult = av_seek_frame(format_, referenceStream, timestamp, AVSEEK_FLAG_BACKWARD);
    if (seekResult < 0 && format_->pb != nullptr &&
        (format_->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0) {
        const auto fileSize = avio_size(format_->pb);
        const auto timelineExtent = snapshot_.duration100ns > 0
            ? snapshot_.duration100ns
            : std::max(nextUntimedVideoPosition100ns_, snapshot_.position100ns);
        const auto byteExtent = snapshot_.duration100ns > 0
            ? fileSize : std::min(fileSize, avio_tell(format_->pb));
        if (timelineExtent > 0 && byteExtent > 0) {
            constexpr std::int64_t ByteSeekPreroll = 16ll * 1024 * 1024;
            const auto estimatedBytePosition = av_rescale(position, byteExtent, timelineExtent);
            const auto bytePosition = std::max<std::int64_t>(0, estimatedBytePosition - ByteSeekPreroll);
            seekResult = av_seek_frame(format_, -1, bytePosition, AVSEEK_FLAG_BYTE);
            if (seekResult >= 0)
                decodeStartPosition = av_rescale(bytePosition, timelineExtent, byteExtent);
        }
        // 回到开头不需要任何"时间↔字节"映射，所以它不该被上面那个 timelineExtent>0 的门槛
        // 挡掉——而恰恰是无时长源（裸 Annex-B）才需要这条：实测 Seek(0) 在那种文件上把读
        // 位置甩到文件末尾（before=450560 after=1102103=整个文件大小），于是会话立刻
        // Ended、解码 0 帧，而同一个文件不动它能在 450560 处继续正常播。
        // 还原到原字节偏移也救不回来：那个偏移是**带缓冲的 AVIO 的逻辑位置**，多半落在
        // 一个访问单元中间，解出来就是"读得到包、一帧都不出"（实测就是这形状）。
        if (seekResult < 0 && position == 0)
            seekResult = av_seek_frame(format_, -1, 0, AVSEEK_FLAG_BYTE);
    }
    if (seekResult < 0) {
        if (GetEnvironmentVariableW(L"FFF_SEEK_TRACE", nullptr, 0))
            std::fprintf(stderr, "SEEK fail rc=%d before=%lld after=%lld seekable=%d\n", seekResult,
                (long long)byteOffsetBeforeSeek, format_->pb ? (long long)avio_tell(format_->pb) : -1,
                format_->pb ? (int)(format_->pb->seekable & AVIO_SEEKABLE_NORMAL) : -1);
        // 还原读位置，让"playback remained active"这句话真的成立。还原失败时不再含糊其辞，
        // 直接把输入已到末尾这件事写进消息——那种情况下会话确实会继续走到结束。
        auto rolledBack = false;
        if (format_->pb != nullptr && byteOffsetBeforeSeek >= 0 &&
            (format_->pb->seekable & AVIO_SEEKABLE_NORMAL) != 0 &&
            avio_seek(format_->pb, byteOffsetBeforeSeek, SEEK_SET) >= 0) {
            avformat_flush(format_);
            rolledBack = true;
        }
        ReportError(FFFResult::FfmpegFailure, rolledBack
            ? "FFmpeg could not seek to the requested position; the reader was rolled back and playback remained active."
            : "FFmpeg could not seek to the requested position and the reader could not be rolled back; playback will run to the end.",
            "seek");
        return FFFResult::FfmpegFailure;
    }
    ResetTempo(position);
    ClearVideoQueue();
    ResetBitRateTracking();
    if (videoDecoder_) avcodec_flush_buffers(videoDecoder_);
    if (audioDecoder_) avcodec_flush_buffers(audioDecoder_);
    internalAudioDecodeErrorCount_ = 0;
    seekTarget100ns_ = position; seekTargetFrame_ = targetFrame;
    keyframeSeekPending_ = !exact && videoStream_ >= 0; draining_ = false;
    demuxEnded_ = audioDecoderDrained_ = externalAudioDrained_ = audioClockFinished_ = false;
    ArmAudioUntilVideoFrame();
    lastVideoFrameDuration100ns_ = 0;
    nextUntimedVideoPosition100ns_ = decodeStartPosition;
    snapshot_.position100ns = position;
    snapshot_.frameIndex = targetFrame >= 0 && position > 0 ? targetFrame - 1 : -1;
    ++snapshot_.timelineGeneration;
    ResetClock(position); if (audioRenderer_) audioRenderer_->Reset(position);
    if (state_.load() == FFF3FPState::Ended)
        SetState(FFF3FPState::Paused, "seek");
    else
        PublishSnapshot();
    if (externalFormat_ && externalAudioStream_ >= 0) {
        auto externalPosition = std::max<std::int64_t>(0, position - externalAudioOffset100ns_);
        auto* stream = externalFormat_->streams[externalAudioStream_];
        auto externalTimestamp = av_rescale_q(externalPosition + TimelineOrigin100ns(externalFormat_),
            AVRational{1, static_cast<int>(TicksPerSecond)}, stream->time_base);
        av_seek_frame(externalFormat_, externalAudioStream_, externalTimestamp, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(externalAudioDecoder_);
    }
    // The IAMF path does not go through audioDecoder_, so flushing that decoder does
    // nothing for it: libiamf reads the raw file through its own handle and keeps its
    // position. Without this the seek was silently ignored and audio kept playing from
    // wherever it had reached.
    //
    // RestartFileAt anchors the replayed stream at the seek target rather than at zero.
    // That matters: the decoder always replays from byte 0, so raw timestamps would name
    // the wrong position, and QueueAudioFrame's filter (which drops audio before
    // seekTarget100ns_) would then discard every frame until playback caught up --
    // measured as a track that goes silent after a forward seek.
    if (iamfDecoder_ != nullptr) {
        std::string iamfError;
        if (!iamfDecoder_->RestartFileAt(position, iamfError)) {
            // A failed restart must not leave a decoder still reporting old positions;
            // drop it and let the FFmpeg fallback take over.
            FallBackFromIamf("Could not restart the IAMF decoder after a seek: " + iamfError);
        }
        iamfProducedAnyAudio_ = false;
    }
    return FFFResult::Success;
}

void PlayerSession::DecodeUntilSeekTarget() noexcept {
    if (format_ == nullptr || videoDecoder_ == nullptr) return;
    if (playbackPacket_ == nullptr) playbackPacket_ = av_packet_alloc();
    if (playbackPacket_ == nullptr) return;
    while ((seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_) && !terminate_) {
        if (av_read_frame(format_, playbackPacket_) < 0) {
            // Delayed B-frames may contain the target nearest EOF. Drain once,
            // then clear any still-unfulfilled seek so later playback does not
            // remain permanently on the seeking path.
            DecodePacket(videoDecoder_, nullptr, true, format_);
            if (seekTarget100ns_ >= 0 || seekTargetFrame_ >= 0 || keyframeSeekPending_) {
                seekTarget100ns_ = -1;
                seekTargetFrame_ = -1;
                keyframeSeekPending_ = false;
                rebuildingFrameIndex_ = false;
                PublishSnapshot();
            }
            break;
        }
        TrackPacketBitRate(playbackPacket_, format_);
        if (playbackPacket_->stream_index == videoStream_)
            DecodePacket(videoDecoder_, playbackPacket_, true, format_);
        av_packet_unref(playbackPacket_);
    }
}

void PlayerSession::DoSelectStream(const std::int32_t index, const bool video) noexcept {
    if (!format_ || index < 0 || index >= static_cast<std::int32_t>(format_->nb_streams) ||
        format_->streams[index]->codecpar->codec_type != (video ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO) ||
        (video && (format_->streams[index]->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0)) { ReportError(FFFResult::InvalidArgument, "The requested media stream is invalid.", "select-stream"); return; }
    AVCodecContext* replacement = nullptr;
    const auto previousDecodeMode = snapshot_.decodeMode;
    // Same memory as the open path: a track whose hardware route was already refused
    // is opened on the CPU directly instead of asking the GPU again.
    const auto tryHardware = video && decodeMode_ == FFF3FPDecodeMode::D3D11 &&
        !HardwareRouteKnownUnsupported(format_->streams[index]);
    auto result = tryHardware
        ? OpenHardwareVideoDecoder(format_, index, &replacement)
        : OpenDecoder(format_, index, video, &replacement, -1, nullptr, false);
    if (result != FFFResult::Success && video && decodeMode_ == FFF3FPDecodeMode::D3D11) {
        result = OpenDecoder(format_, index, true, &replacement, -1, nullptr, false);
        if (result == FFFResult::Success) snapshot_.decodeMode = FFF3FPDecodeMode::Cpu;
    } else if (result == FFFResult::Success && video) {
        snapshot_.decodeMode = tryHardware ? decodeMode_ : FFF3FPDecodeMode::Cpu;
    }
    if (result != FFFResult::Success) { ReportError(result, "Could not open the selected media stream.", "select-stream"); return; }
    if (video) {
        StopSoftwareVideoDecode();
        if (videoDecoder_) avcodec_free_context(&videoDecoder_);
        videoDecoder_ = replacement; videoStream_ = index;
        // The auxiliary stream was chosen against the previous base stream, so
        // forget it; the host's next enabled request re-selects against this one.
        CloseAuxiliaryLayer();
        // Rebinding the video stream rebinds its stream-level side data too:
        // the previous track's records must never be answered for the new one.
        ClearStreamSideData();
        CacheStreamSideDataFromStream(format_->streams[index]);
        snapshot_.selectedVideoStream = index;
        videoRenderer_.ConfigureHdrStream(format_->streams[index]->codecpar);
        ApplyHdrState(snapshot_, videoRenderer_.StreamHdrState(), videoRenderer_.HdrState());
        framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = false;
    }
    else {
        if (audioDecoder_) avcodec_free_context(&audioDecoder_);
        ReleaseIamfDecoder();
        audioDecoder_ = replacement; audioStream_ = index; snapshot_.selectedAudioStream = index;
        // A new audio track is a new pool round, same as a new file.
        audioTargetBuffer100ns_ = performance_.AudioInitial();
        seenAudioUnderruns_ = 0;
        seenAudioJitterFrames_ = 0;
        seenAudioDiscontinuities_ = 0;
        seenAudioInsertedSilence_ = 0;
        seenAudioDroppedOverlap_ = 0;
        // Re-evaluate the IAMF path for the newly selected track: switching onto an
        // IAMF stream must engage the reference decoder, and switching away from one
        // must release it. No fallback is needed here -- if IAMF cannot be opened we
        // have already opened the FFmpeg decoder above.
        if (TryOpenIamfAudioDecoder()) {
            avcodec_free_context(&audioDecoder_);
        }
        internalAudioFailurePending_ = false;
        internalAudioFailureResult_ = FFFResult::Success;
        internalAudioDecodeErrorCount_ = 0;
    }
    if (disc_ && !video) {
        disc_->SelectAudioStream(format_, index);
        if (audioRenderer_) audioRenderer_->Reset(snapshot_.position100ns);
        ClearPendingPackets();
    } else DoSeek(snapshot_.position100ns);
    RebuildMediaInfo();
    if (video && snapshot_.decodeMode != previousDecodeMode) {
        // Same shape as every other route change: "type" names the subsystem and the
        // fallback flag tells a host that surfaces this event that the GPU route was
        // given up rather than chosen. A track the GPU cannot decode is exactly that,
        // and without the flag the host reports a graphics-device change the user
        // never experienced (and resets screen-capture routing for nothing).
        const auto fellBackToCpu = previousDecodeMode == FFF3FPDecodeMode::Gpu &&
            snapshot_.decodeMode == FFF3FPDecodeMode::Cpu;
        Emit(FFF3FPEvent::DeviceChanged,
            "{\"type\":\"video\",\"decodeMode\":" + std::to_string(static_cast<unsigned>(snapshot_.decodeMode)) +
            ",\"fallback\":" + (fellBackToCpu ? "true" : "false") +
            ",\"reason\":\"" + EscapeJson(fellBackToCpu
                ? "The selected video stream cannot be decoded on the GPU; playback is using CPU decoding."
                : "The selected video stream changed the active decoder.") + "\"}");
    }
    Emit(FFF3FPEvent::OperationCompleted, std::string("{\"operation\":\"select-") + (video ? "video" : "audio") + "\",\"stream\":" + std::to_string(index) + "}");
}

void PlayerSession::DoLoadExternalAudio(std::string path, const std::int32_t requestedIndex, const std::int64_t offset) noexcept {
    AVFormatContext* replacementFormat = nullptr;
    std::unique_ptr<SharedFileInput> replacementIo;
    std::string openError;
    const auto openResult = OpenFormat(path, &replacementFormat, replacementIo, openError);
    if (openResult != FFFResult::Success) { ReportError(openResult, std::move(openError), "external-audio"); return; }
    auto index = requestedIndex;
    if (index < 0) index = av_find_best_stream(replacementFormat, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVCodecContext* replacementDecoder = nullptr;
    const auto result = OpenDecoder(replacementFormat, index, false, &replacementDecoder);
    if (result != FFFResult::Success) { CloseFormat(&replacementFormat, replacementIo); ReportError(result, "Could not open the external audio stream.", "external-audio"); return; }
    if (!audioRenderer_ && (!audioExclusive_ || snapshot_.state == FFF3FPState::Playing)) {
        std::string audioError;
        const auto audioResult = RecreateAudioRenderer(audioEndpointId_, audioExclusive_,
            snapshot_.state != FFF3FPState::Playing, audioError);
        if (audioResult != FFFResult::Success) {
            avcodec_free_context(&replacementDecoder);
            CloseFormat(&replacementFormat, replacementIo);
            ReportError(audioResult, std::move(audioError), "external-audio");
            return;
        }
    }
    if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); CloseFormat(&externalFormat_, externalFormatIo_);
    externalFormat_ = replacementFormat; externalAudioDecoder_ = replacementDecoder; externalAudioStream_ = index;
    externalFormatIo_ = std::move(replacementIo);
    externalAudioOffset100ns_ = offset; externalAudioPath_ = std::move(path);
    snapshot_.isExternalAudio = 1; snapshot_.externalAudioOffset100ns = offset; DoSeek(snapshot_.position100ns);
    ApplyAudioPlaybackPause(snapshot_.state == FFF3FPState::Playing);
    Emit(FFF3FPEvent::OperationCompleted, "{\"operation\":\"load-external-audio\",\"stream\":" + std::to_string(index) + "}");
}

void PlayerSession::DoClose(const FFF3FPState finalState, const bool preserveVideoOutput) noexcept {
    ResetTempo(0);
    StopSoftwareVideoDecode();
    placement_.Release();
    if (audioRenderer_) { audioRenderer_->Stop(); audioRenderer_.reset(); }
    // SetTimedTextLayer is intentionally callable outside the session command
    // queue. Gate renderer teardown with its content lock so a final timer tick
    // cannot restart the presenter while this close is joining it.
    std::unique_lock contentLock(timedTextContentMutex_);
    if (preserveVideoOutput) videoRenderer_.ResetMedia();
    else videoRenderer_.Close();
    if (externalAudioDecoder_) avcodec_free_context(&externalAudioDecoder_); CloseFormat(&externalFormat_, externalFormatIo_);
    CloseAuxiliaryLayer();
    if (videoDecoder_) avcodec_free_context(&videoDecoder_);
    if (audioDecoder_) avcodec_free_context(&audioDecoder_);
    ReleaseIamfDecoder();
    if (coverArtFrame_) av_frame_free(&coverArtFrame_);
    ReleasePrimariesCarrierFilter();
    if (stillImageFrame_) av_frame_free(&stillImageFrame_);
    if (videoDecodeFrame_) av_frame_free(&videoDecodeFrame_);
    if (videoTransferFrame_) av_frame_free(&videoTransferFrame_);
    if (audioDecodeFrame_) av_frame_free(&audioDecodeFrame_);
    if (externalAudioDecodeFrame_) av_frame_free(&externalAudioDecodeFrame_);
    if (playbackPacket_) av_packet_free(&playbackPacket_);
    if (stillImagePacket_) av_packet_free(&stillImagePacket_);
    if (externalAudioPacket_) av_packet_free(&externalAudioPacket_);
    // The tile packets belong to the format being closed, so they go with it.
    ResetTileGrid();
    ClearVideoQueue();
    for (auto*& frame : videoFramePool_) av_frame_free(&frame);
    videoFramePool_.clear();
    // Publish "closed" before releasing, so no new caller-thread entry can
    // observe the flag after the object is gone.
    if (disc_) { discOpened_.store(false, std::memory_order_release); disc_->CloseDemux(&format_); disc_.reset(); }
    else CloseFormat(&format_, formatIo_);
    discPositionOffset_ = 0; discGraphicsSequence_ = 0; discDrained_ = false;
    discInvalidPackets_ = 0;
    videoRenderer_.SetDiscAspect(0);
    { std::lock_guard lock(snapshotMutex_); discStatus_ = "{}"; }
    videoStream_ = audioStream_ = coverArtStream_ = externalAudioStream_ = -1; externalAudioPath_.clear(); framePtsIndex_.clear(); framePtsIndexBase_ = 0; rebuildingFrameIndex_ = false;
    // The bound video stream is gone; its stream-level side data must not
    // outlive it in the cache.
    ClearStreamSideData();
    timedTextContentCache_.clear();
    externalAudioOffset100ns_ = 0; seekTarget100ns_ = seekTargetFrame_ = -1;
    keyframeSeekPending_ = false; lastVideoFrameDuration100ns_ = 0; ResetCadenceSpread();
    // The session is losing its media identity: with the content period zeroed the
    // ceiling must go panel-only too, or Stop/Close followed by nothing would keep
    // the previous file's bound, and the same-HWND re-open path below DoOpen would
    // show it during the new media's first frames. DoClose never runs through
    // RebuildMediaInfo, so this is the anti-leak site for every close path.
    videoRenderer_.SetMediaFrameRateCeilingHz(0.0);
    nextUntimedVideoPosition100ns_ = 0; draining_ = false;
    demuxEnded_ = audioDecoderDrained_ = externalAudioDrained_ = audioClockFinished_ = false;
    staticImage_ = false;
    animatedImage_ = false;
    sessionWrapLoop_ = false; sessionWrapFailures_ = 0;
    { std::lock_guard lock(snapshotMutex_); hasPublishedImageInfo_ = false; }
    audioBlockedUntilVideoFrame_ = false;
    playbackPreroll_ = false;
    audioUnblockVideoGeneration_ = 0;
    audioResumePendingAfterVideoFrame_ = false;
    hardwareFallbackPending_ = false;
    pendingHardwareFallbackReason_.clear();
    internalAudioFailurePending_ = false;
    internalAudioFailureResult_ = FFFResult::Success;
    internalAudioDecodeErrorCount_ = 0;
    placementGoodWindows_ = 0;
    ladderWindowDecoded_ = 0;
    ladderWindowPresented_ = 0;
    ladderWindowStart100ns_ = 0;
    ladderSettlingUntil100ns_ = 0;
    pumpIterations_ = pumpSaturatedIterations_ = 0;
    pumpQueueBoundIterations_ = pumpStarvedIterations_ = 0;
    ladderWindowUpload100ns_ = ladderWindowConvert100ns_ = ladderWindowDropped_ = 0;
    clearedVideoFrames_ = 0;
    // The load ladder and the pool rung are per-media state: carrying them across a
    // close hands the next file the previous file's worst case (a VFR-misjudged
    // climb would follow the session). Reset both so every media re-derives its
    // opening rung and starts the thread ladder from its floor.
    decoderThreads_ = 0;
    softwarePoolRung_ = 0;
    // The pacer is per-media state as well, and its floor lives in the renderer: a
    // close must clear it there or the next session would inherit the cadence.
    if (paceEngaged_) ReleasePresentationPace("close");
    paceStreakClock100ns_ = 0;
    paceStreakLag100ns_ = 0;
    ResetPaceWindow();
    ResetBitRateTracking();
    {
        std::lock_guard lock(mutex_); snapshot_.state = finalState; snapshot_.position100ns = 0; snapshot_.duration100ns = 0;
        snapshot_.frameIndex = -1; snapshot_.selectedVideoStream = -1; snapshot_.selectedAudioStream = -1;
        snapshot_.videoWidth = snapshot_.videoHeight = 0; snapshot_.isHdrSource = 0;
        snapshot_.isExternalAudio = 0; snapshot_.externalAudioOffset100ns = 0;
        snapshot_.decodedVideoFrames = snapshot_.presentedVideoFrames = snapshot_.droppedVideoFrames = 0;
        displayRateDiscardedFrames_.store(0, std::memory_order_relaxed);
        snapshot_.decodedAudioFrames = snapshot_.audioUnderruns = 0;
        snapshot_.audioPosition100ns = snapshot_.bufferedAudio100ns = 0;
        snapshot_.audioTimestampJitterFrames = snapshot_.audioDiscontinuities = 0;
        snapshot_.audioInsertedSilenceFrames = snapshot_.audioDroppedOverlapFrames = 0;
        // The seam's bed owner is per-media state like every counter around it: a new
        // media starts with nobody rendering its bed, whether or not a plug is still
        // installed (it is a session-level preference and survives Close/Open, like
        // audioDelay100ns; the next media's first frame re-decides the route).
        snapshot_.audioBinaural = 0;
        snapshot_.coalescedVideoFrames = snapshot_.audioRejectedFrames = 0;
        snapshot_.swapChainPresents = snapshot_.presentWait100ns = 0;
        snapshot_.deviceLockWait100ns = snapshot_.hardwareTransfer100ns = 0;
        snapshot_.softwareConvert100ns = 0;
    snapshot_.videoUpload100ns = 0;
    snapshot_.uploadMapWait100ns = snapshot_.uploadCopy100ns = 0;
    snapshot_.downscaleResample100ns = snapshot_.scalePassCount = 0;
    snapshot_.uploadCount = snapshot_.uploadCopyBytes = 0;
    snapshot_.uploadPath = 0;
        snapshot_.videoBitRate = snapshot_.audioBitRate = 0;
        snapshot_.queuedVideoFrames = 0; snapshot_.sourcePeakNits = 0;
        ApplyHdrState(snapshot_, {}, {});
        // During same-HWND media replacement the flip chain is intentionally
        // retained. Keep reporting its real mode while Opening; the next source
        // commits SDR only after ForceSdrOutputForSdrSource has reconfigured DXGI.
        snapshot_.actualColorMode = preserveVideoOutput
            ? videoRenderer_.ActualColorMode() : FFF3FPColorMode::MapToSdr;
        mediaInfoJson_.clear();
    }
    state_.store(finalState);
    PublishSnapshot();
    if (finalState == FFF3FPState::Closed || finalState == FFF3FPState::Idle) {
        Emit(FFF3FPEvent::StateChanged, "{\"state\":" +
            std::to_string(static_cast<unsigned>(finalState)) +
            (finalState == FFF3FPState::Idle ? ",\"operation\":\"stop\"}" : "}"));
    }
}

// Produces the per-media frame-rate ceiling, in Hz, that bounds the renderer's flip
// rate (FrameRateBoundHz = min(declared panel rate, this)). It is computed once when
// a media is opened / switched and pushed from RebuildMediaInfo, instead of being
// re-derived from every presented frame's duration.
//
// Why r_frame_rate, and why its looseness is safe here:
//   - r_frame_rate is NOT "the maximum frame rate recorded in the file". Measured on
//     this machine: the same content (a 25 fps segment plus a 60 fps segment) reports
//     25/1 or 60/1 depending only on the order the two segments were muxed, and a
//     24+30 file reports 120/1 while its real peak is 30. The field's own header note
//     (third_party/ffmpeg/include/libavformat/avformat.h) calls it a "Real base
//     framerate ... least common multiple ... just a guess".
//   - avg_frame_rate is the whole-file average and cannot see VFR at all (a real VFR
//     .ts reports avg == r == 20/1), so it is only a fallback for files whose
//     r_frame_rate is absent.
//   - What the error direction really is, and what the session does about it: for
//     container-indexed formats r_frame_rate is derived from every sample timestamp,
//     so it sits at or above the peak (a 24+30 file reports 120/1; bare Annex-B reports
//     40/1 while its rate is 20). For MPEG-TS the demuxer has no index and samples the
//     stream head, so the field can sit **below** a later fast segment - measured on
//     12b_vfr_20then40.ts: r_frame_rate = 20/1 and av_guess = 20/1 while the back half
//     really is 40 fps. That is why this value is only the initial ceiling and
//     PresentVideoFrame raises it (never lowers it) as faster per-frame evidence
//     arrives; a media switch replaces the whole thing via DoClose/RebuildMediaInfo.
//   - Being loose costs nothing here: the presenter flips on new content, so a wide
//     ceiling just leaves the panel half binding. Hence no full-file scan for the exact
//     peak, and the per-frame period keeps feeding the discard buckets and hold bands
//     separately.
//   - 0 = no usable bound (no stream, no valid rate field): the renderer keeps the
//     declared-panel half only.
double PlayerSession::MediaFrameRateCeilingHz(const AVStream* stream) const noexcept {
    if (stream != nullptr && stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0)
        return static_cast<double>(stream->r_frame_rate.num) /
            static_cast<double>(stream->r_frame_rate.den);
    if (stream != nullptr && stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0)
        return static_cast<double>(stream->avg_frame_rate.num) /
            static_cast<double>(stream->avg_frame_rate.den);
    return 0.0;
}

void PlayerSession::RebuildMediaInfo() noexcept {
    std::lock_guard codecLock(softwareVideoCodecMutex_);
    if (!format_) return;
    // Publish the per-media frame-rate ceiling on every media-info rebuild. This runs
    // from every path that can change the media identity (open, select-stream, disc
    // title change) as well as the idempotent re-rebuilds (device recovery, hardware
    // fallback, first frame, end-of-file refresh); recomputing from the current
    // videoStream_ is correct in all of them. A media switch must never carry the
    // previous file's ceiling over - DoClose clears it separately for the paths that
    // tear the session down without reaching this function.
    const AVStream* ceilingStream = nullptr;
    if (videoStream_ >= 0 && videoStream_ < static_cast<std::int32_t>(format_->nb_streams))
        ceilingStream = format_->streams[videoStream_];
    // 发布的是"容器初值与已观测峰值里大的那个"：RebuildMediaInfo 也被 seek／设备恢复／
    // EOF 这些路径调用，那里必须只是重新发布同一份上限，不能把抬上去的观测值抹回容器值。
    videoRenderer_.SetMediaFrameRateCeilingHz(
        std::max(MediaFrameRateCeilingHz(ceilingStream), mediaObservedPeakHz_));
    std::ostringstream json;
    const auto formatName = format_->iformat && format_->iformat->name ? format_->iformat->name : "";
    const auto formatLongName = format_->iformat && format_->iformat->long_name ? format_->iformat->long_name : "";
    const auto* majorBrand = av_dict_get(format_->metadata, "major_brand", nullptr, 0);
    const auto* compatibleBrands = av_dict_get(format_->metadata, "compatible_brands", nullptr, 0);
    json << "{\"format\":\"" << EscapeJson(formatName)
        << "\",\"formatLongName\":\"" << EscapeJson(formatLongName)
        << "\",\"formatCodecId\":\"" << EscapeJson(majorBrand ? majorBrand->value : "")
        << "\",\"compatibleBrands\":\"" << EscapeJson(compatibleBrands ? compatibleBrands->value : "")
        << "\",\"duration100ns\":" << snapshot_.duration100ns
        << ",\"startTime100ns\":" << (format_->start_time == AV_NOPTS_VALUE ? 0 :
            av_rescale_q(format_->start_time, AV_TIME_BASE_Q, AVRational{1, static_cast<int>(TicksPerSecond)}))
        << ",\"bitRate\":" << std::max<std::int64_t>(0, format_->bit_rate)
        << ",\"fileSize\":" << std::max<std::int64_t>(0, format_->pb ? avio_size(format_->pb) : 0)
        << ",\"probeScore\":" << format_->probe_score
        << ",\"staticImage\":" << (staticImage_ ? "true" : "false")
        << ",\"tileGrid\":" << (tileGrid_ ? "true" : "false")
        << ",\"tileGridWidth\":" << tileGridWidth_
        << ",\"tileGridHeight\":" << tileGridHeight_
        << ",\"tileGridTileCount\":" << tileGridTiles_.size()
        << ",\"metadata\":";
    AppendDictionaryJson(json, format_->metadata);
    json << ",\"chapters\":[";
    for (unsigned index = 0; index < format_->nb_chapters; ++index) {
        if (index) json << ',';
        const auto* chapter = format_->chapters[index];
        const auto* title = av_dict_get(chapter->metadata, "title", nullptr, 0);
        const auto start100ns = av_rescale_q(chapter->start, chapter->time_base,
            AVRational{1, static_cast<int>(TicksPerSecond)}) - TimelineOrigin100ns(format_);
        json << "{\"startTime100ns\":" << std::max<std::int64_t>(0, start100ns)
            << ",\"title\":\"" << EscapeJson(title && title->value ? title->value : "") << "\"}";
    }
    json << ']';
    json << ",\"streams\":[";
    for (unsigned index = 0; index < format_->nb_streams; ++index) {
        if (index) json << ',';
        auto* stream = format_->streams[index];
        const auto* parameters = stream->codecpar;
        const auto* descriptor = avcodec_descriptor_get(parameters->codec_id);
        const auto frameRate = av_guess_frame_rate(format_, stream, nullptr);
        const auto nominalFrameRate = stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0
            ? stream->r_frame_rate : frameRate;
        const auto sampleAspect = av_guess_sample_aspect_ratio(format_, stream, nullptr);
        const auto displayAspect = (sampleAspect.num > 0 && sampleAspect.den > 0)
            ? av_mul_q(AVRational{std::max(1, parameters->width), std::max(1, parameters->height)}, sampleAspect)
            : AVRational{std::max(1, parameters->width), std::max(1, parameters->height)};
        const auto streamBitRate = std::max<std::int64_t>(0, parameters->bit_rate);
        std::int64_t streamSize = 0;
        if (streamBitRate > 0 && stream->duration != AV_NOPTS_VALUE && stream->time_base.den != 0) {
            const auto seconds = static_cast<long double>(stream->duration) * stream->time_base.num / stream->time_base.den;
            const auto bytes = seconds > 0 ? seconds * streamBitRate / 8.0L : 0.0L;
            streamSize = bytes >= static_cast<long double>(INT64_MAX) ? INT64_MAX :
                static_cast<std::int64_t>(std::llround(std::max(0.0L, bytes)));
        }
        const auto* pixelDescriptor = parameters->codec_type == AVMEDIA_TYPE_VIDEO
            ? av_pix_fmt_desc_get(static_cast<AVPixelFormat>(parameters->format)) : nullptr;
        const auto sourceBitDepth = pixelDescriptor == nullptr ? parameters->bits_per_raw_sample :
            std::max(parameters->bits_per_raw_sample, PixelFormatBitDepth(static_cast<AVPixelFormat>(parameters->format)));
        const auto isLossless = descriptor != nullptr &&
            (descriptor->props & AV_CODEC_PROP_LOSSLESS) != 0 && (descriptor->props & AV_CODEC_PROP_LOSSY) == 0;
        json << "{\"index\":" << index << ",\"type\":\"" << MediaTypeName(parameters->codec_type)
             << "\",\"streamId\":" << stream->id
             << ",\"codec\":\"" << EscapeJson(descriptor ? descriptor->name : "unknown")
             << "\",\"codecLongName\":\"" << EscapeJson(descriptor && descriptor->long_name ? descriptor->long_name : "")
             << "\",\"codecTag\":\"" << EscapeJson(CodecTagName(parameters->codec_tag))
             << "\",\"timeBaseNumerator\":"
             << stream->time_base.num << ",\"timeBaseDenominator\":" << stream->time_base.den
             << ",\"bitRate\":" << streamBitRate
             << ",\"streamSize\":" << streamSize
             << ",\"lossless\":" << (isLossless ? "true" : "false")
             << ",\"startTime100ns\":" << (stream->start_time == AV_NOPTS_VALUE ? 0 :
                 av_rescale_q(stream->start_time, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}))
             << ",\"duration100ns\":" << (stream->duration == AV_NOPTS_VALUE ? 0 :
                 av_rescale_q(stream->duration, stream->time_base, AVRational{1, static_cast<int>(TicksPerSecond)}))
             << ",\"frames\":" << std::max<std::int64_t>(0, stream->nb_frames)
             << ",\"extradataSize\":" << std::max(0, parameters->extradata_size)
             << ",\"default\":" << ((stream->disposition & AV_DISPOSITION_DEFAULT) != 0 ? "true" : "false")
             << ",\"forced\":" << ((stream->disposition & AV_DISPOSITION_FORCED) != 0 ? "true" : "false")
             << ",\"disposition\":\"";
        std::vector<std::string> dispositions;
        const auto addDisposition = [&](const int flag, const char* name) {
            if ((stream->disposition & flag) != 0) dispositions.emplace_back(name);
        };
        addDisposition(AV_DISPOSITION_DEFAULT, "default"); addDisposition(AV_DISPOSITION_DUB, "dub");
        addDisposition(AV_DISPOSITION_ORIGINAL, "original"); addDisposition(AV_DISPOSITION_COMMENT, "comment");
        addDisposition(AV_DISPOSITION_LYRICS, "lyrics"); addDisposition(AV_DISPOSITION_KARAOKE, "karaoke");
        addDisposition(AV_DISPOSITION_FORCED, "forced"); addDisposition(AV_DISPOSITION_HEARING_IMPAIRED, "hearing_impaired");
        addDisposition(AV_DISPOSITION_VISUAL_IMPAIRED, "visual_impaired"); addDisposition(AV_DISPOSITION_CLEAN_EFFECTS, "clean_effects");
        addDisposition(AV_DISPOSITION_ATTACHED_PIC, "attached_pic"); addDisposition(AV_DISPOSITION_TIMED_THUMBNAILS, "timed_thumbnails");
        for (std::size_t item = 0; item < dispositions.size(); ++item) {
            if (item) json << ',';
            json << EscapeJson(dispositions[item]);
        }
        json << "\",\"metadata\":";
        AppendDictionaryJson(json, stream->metadata);
        const auto* profile = avcodec_profile_name(parameters->codec_id, parameters->profile);
        if (profile != nullptr) json << ",\"profile\":\"" << EscapeJson(profile) << "\"";
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
            json << ",\"width\":" << parameters->width << ",\"height\":" << parameters->height
                 << ",\"averageFrameRateNumerator\":" << frameRate.num
                 << ",\"averageFrameRateDenominator\":" << frameRate.den
                 << ",\"nominalFrameRateNumerator\":" << nominalFrameRate.num
                 << ",\"nominalFrameRateDenominator\":" << nominalFrameRate.den
                 << ",\"frameRateMode\":\"" <<
                    FrameRateModeLabel(index == static_cast<unsigned>(videoStream_) ? stream : nullptr,
                        frameRate, nominalFrameRate) << "\""
                 << ",\"sampleAspectNumerator\":" << sampleAspect.num
                 << ",\"sampleAspectDenominator\":" << sampleAspect.den
                 << ",\"displayAspectNumerator\":" << displayAspect.num
                 << ",\"displayAspectDenominator\":" << displayAspect.den
                  << ",\"hdr\":" << ((parameters->color_trc == AVCOL_TRC_SMPTE2084 ||
                    parameters->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
                    av_packet_side_data_get(parameters->coded_side_data,
                        parameters->nb_coded_side_data, AV_PKT_DATA_DYNAMIC_HDR10_PLUS) != nullptr) ? "true" : "false")
                  << ",\"attachedPicture\":" << ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0 ? "true" : "false");
            const auto* sphericalData = av_packet_side_data_get(parameters->coded_side_data,
                parameters->nb_coded_side_data, AV_PKT_DATA_SPHERICAL);
            if (sphericalData != nullptr && sphericalData->size >= sizeof(AVSphericalMapping)) {
                const auto* spherical = reinterpret_cast<const AVSphericalMapping*>(sphericalData->data);
                const auto* projectionName = av_spherical_projection_name(spherical->projection);
                if (projectionName != nullptr)
                    json << ",\"projection\":\"" << EscapeJson(projectionName) << "\"";
            }
            const auto pixelFormat = static_cast<AVPixelFormat>(parameters->format);
            const auto* pixelFormatName = av_get_pix_fmt_name(pixelFormat);
            if (pixelFormatName != nullptr)
                json << ",\"pixelFormat\":\"" << EscapeJson(pixelFormatName) << "\"";
            const auto sourceBitDepth = std::max(parameters->bits_per_raw_sample,
                PixelFormatBitDepth(pixelFormat));
            if (sourceBitDepth > 0)
                json << ",\"bitDepth\":" << sourceBitDepth;
            const bool rgb = pixelDescriptor != nullptr && (pixelDescriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;
            const auto colorModel = pixelDescriptor == nullptr ? "" :
                (rgb ? (pixelDescriptor->nb_components == 1 ? "灰度" : "RGB") : "YUV");
            std::string chromaSubsampling;
            if (pixelDescriptor != nullptr && !rgb) {
                if (pixelDescriptor->log2_chroma_w == 1 && pixelDescriptor->log2_chroma_h == 1) chromaSubsampling = "4:2:0";
                else if (pixelDescriptor->log2_chroma_w == 1 && pixelDescriptor->log2_chroma_h == 0) chromaSubsampling = "4:2:2";
                else if (pixelDescriptor->log2_chroma_w == 0 && pixelDescriptor->log2_chroma_h == 0) chromaSubsampling = "4:4:4";
            }
            json << ",\"colorModel\":\"" << EscapeJson(colorModel)
                 << "\",\"chromaSubsampling\":\"" << EscapeJson(chromaSubsampling) << "\"";
            if (static_cast<std::int32_t>(index) == videoStream_ && videoDecoder_ != nullptr) {
                const auto decoderFormat = videoDecoder_->pix_fmt;
                const auto decoderSurfaceFormat = videoDecoder_->sw_pix_fmt;
                const auto* decoderFormatName = av_get_pix_fmt_name(decoderFormat);
                const auto* decoderSurfaceName = av_get_pix_fmt_name(decoderSurfaceFormat);
                if (decoderFormatName != nullptr)
                    json << ",\"decoderPixelFormat\":\"" << EscapeJson(decoderFormatName) << "\"";
                if (decoderSurfaceName != nullptr)
                    json << ",\"decoderSurfaceFormat\":\"" << EscapeJson(decoderSurfaceName) << "\"";
                const auto decoderBitDepth = PixelFormatBitDepth(
                    decoderSurfaceFormat != AV_PIX_FMT_NONE ? decoderSurfaceFormat : decoderFormat);
                if (decoderBitDepth > 0) json << ",\"decoderBitDepth\":" << decoderBitDepth;
                const auto acceleration = HardwareAccelerationName(videoDecoder_);
                if (!acceleration.empty())
                    json << ",\"hardwareAcceleration\":\"" << EscapeJson(acceleration) << "\"";
            }
            json << ",\"colorRange\":" << parameters->color_range
                 << ",\"colorSpace\":" << parameters->color_space
                 << ",\"colorPrimaries\":" << parameters->color_primaries
                 << ",\"colorTransfer\":" << parameters->color_trc
                 << ",\"chromaLocation\":" << parameters->chroma_location
                 << ",\"fieldOrder\":" << parameters->field_order
                 << ",\"level\":" << parameters->level;
            const auto* masteringData = av_packet_side_data_get(parameters->coded_side_data,
                parameters->nb_coded_side_data, AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
            const auto* lightData = av_packet_side_data_get(parameters->coded_side_data,
                parameters->nb_coded_side_data, AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
            HdrProcessor streamHdr;
            streamHdr.ConfigureStream(parameters);
            const auto hdr = streamHdr.State();
            const auto formatName = std::string(HdrProcessor::FormatName(hdr.format));
            const auto processingPath = std::string(HdrProcessor::ProcessingPathName(hdr.processingPath));
            json << ",\"hdrFormat\":\"" << EscapeJson(formatName) << "\""
                 << ",\"hdrCompatibility\":\"" << EscapeJson(HdrProcessor::CompatibilityNames(hdr.compatibility)) << "\""
                 << ",\"hdrProcessingPath\":\"" << EscapeJson(processingPath) << "\""
                 << ",\"hdrFallback\":" << (hdr.fallback ? "true" : "false")
                 << ",\"dynamicHdrMetadata\":" << (hdr.dynamicMetadata ? "true" : "false");
            if (masteringData != nullptr && masteringData->size >= sizeof(AVMasteringDisplayMetadata)) {
                const auto* mastering = reinterpret_cast<const AVMasteringDisplayMetadata*>(masteringData->data);
                std::string primaries;
                if (mastering->has_primaries) {
                    const auto redX = av_q2d(mastering->display_primaries[0][0]);
                    const auto redY = av_q2d(mastering->display_primaries[0][1]);
                    const auto greenX = av_q2d(mastering->display_primaries[1][0]);
                    const auto greenY = av_q2d(mastering->display_primaries[1][1]);
                    if (std::abs(redX - 0.68) < 0.015 && std::abs(redY - 0.32) < 0.015 &&
                        std::abs(greenX - 0.265) < 0.015 && std::abs(greenY - 0.69) < 0.015) primaries = "Display P3";
                    else primaries = "自定义";
                }
                json << ",\"masteringPrimaries\":\"" << EscapeJson(primaries) << "\"";
                if (mastering->has_luminance)
                    json << ",\"masteringMinLuminance\":" << std::setprecision(12) << av_q2d(mastering->min_luminance)
                         << ",\"masteringMaxLuminance\":" << std::setprecision(12) << av_q2d(mastering->max_luminance);
            }
            if (lightData != nullptr && lightData->size >= sizeof(AVContentLightMetadata)) {
                const auto* light = reinterpret_cast<const AVContentLightMetadata*>(lightData->data);
                json << ",\"maxCLL\":" << light->MaxCLL << ",\"maxFALL\":" << light->MaxFALL;
            }
            const char* codecConfiguration = nullptr;
            if (parameters->codec_id == AV_CODEC_ID_AV1) codecConfiguration = "av1C";
            else if (parameters->codec_id == AV_CODEC_ID_H264) codecConfiguration = "avcC";
            else if (parameters->codec_id == AV_CODEC_ID_HEVC) codecConfiguration = "hvcC";
            if (codecConfiguration != nullptr)
                json << ",\"codecConfigurationBox\":\"" << codecConfiguration << "\"";
        }
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            const auto* sampleFormat = av_get_sample_fmt_name(static_cast<AVSampleFormat>(parameters->format));
            json << ",\"sampleRate\":" << parameters->sample_rate << ",\"channels\":" << parameters->ch_layout.nb_channels
                 << ",\"channelLayout\":\"" << EscapeJson(ChannelLayoutName(parameters->ch_layout)) << "\""
                 << ",\"sampleFormat\":\"" << EscapeJson(sampleFormat ? sampleFormat : "") << "\""
                 << ",\"bitsPerCodedSample\":" << parameters->bits_per_coded_sample
                 << ",\"frameSize\":" << parameters->frame_size
                 << ",\"initialPadding\":" << parameters->initial_padding
                 << ",\"trailingPadding\":" << parameters->trailing_padding
                 << ",\"seekPreroll\":" << parameters->seek_preroll;
            // IAMF splits one audio element across several dependent elementary
            // streams, so codecpar describes ONE substream (a stereo pair of a 7.1.4
            // element), not the element. Report the mix presentation's real layout
            // alongside it: a host that shows "stereo" for a 7.1.4 file is stating
            // something false, and the mismatch is silent otherwise.
            const auto iamf = ResolveIamfLayout(format_);
            if (iamf.isIamf) {
                json << ",\"iamf\":{\"substreams\":" << iamf.substreamCount
                     << ",\"elementChannels\":" << iamf.elementChannels
                     << ",\"presentationChannels\":" << iamf.presentationChannels
                     << ",\"presentationLayout\":\""
                     << EscapeJson(iamf.presentationLayoutName) << "\"}";
            }
            // Keep effective precision distinct from the coded width (e.g. ADPCM codewords).
            json << ",\"rawSampleBits\":" << parameters->bits_per_raw_sample
                 << ",\"compressionMode\":\"" << (isLossless ? "无损" : "有损") << "\"";
            if (parameters->codec_id == AV_CODEC_ID_FLAC && parameters->extradata != nullptr && parameters->extradata_size >= 16) {
                static constexpr char Hex[] = "0123456789ABCDEF";
                std::string md5;
                md5.reserve(32);
                for (int byte = parameters->extradata_size - 16; byte < parameters->extradata_size; ++byte) {
                    md5.push_back(Hex[parameters->extradata[byte] >> 4]);
                    md5.push_back(Hex[parameters->extradata[byte] & 0x0f]);
                }
                json << ",\"md5\":\"" << md5 << "\"";
            }
            if (static_cast<std::int32_t>(index) == audioStream_ && audioRenderer_ != nullptr) {
                const auto validBits = audioRenderer_->OutputValidBitsPerSample();
                json << ",\"outputSampleRate\":" << audioRenderer_->OutputSampleRate()
                     << ",\"outputChannels\":" << audioRenderer_->OutputChannels()
                     << ",\"outputBitsPerSample\":" << audioRenderer_->OutputBitsPerSample()
                     << ",\"outputValidBitsPerSample\":" << validBits
                     << ",\"outputFloat\":" << (audioRenderer_->OutputIsFloat() ? "true" : "false");
            }
        }
        AVDictionaryEntry* language = av_dict_get(stream->metadata, "language", nullptr, 0);
        AVDictionaryEntry* title = av_dict_get(stream->metadata, "title", nullptr, 0);
        json << ",\"language\":\"" << EscapeJson(language ? language->value : "") << "\",\"title\":\"" << EscapeJson(title ? title->value : "") << "\"}";
    }
    json << "]}"; std::lock_guard lock(mutex_); mediaInfoJson_ = json.str();
}

void PlayerSession::SetState(const FFF3FPState state, const char* operation) noexcept {
    const auto previous = state_.exchange(state);
    snapshot_.state = state;
    PublishSnapshot();
    if (previous == state) return;
    // CPU placement follows the playing state: tier 3 is dropped as soon as playback
    // stops, and tier 1 is (re)applied on every start. Placement is cheap enough to do
    // inline, and doing it here keeps every state transition in one place.
    if (state == FFF3FPState::Playing) placement_.OnPlaybackStart(workerThreadId_);
    else if (previous == FFF3FPState::Playing) placement_.OnPlaybackStop();
    std::string json = "{\"state\":" + std::to_string(static_cast<unsigned>(state));
    if (operation) json += ",\"operation\":\"" + EscapeJson(operation) + "\""; json += '}';
    Emit(FFF3FPEvent::StateChanged, json);
}

void PlayerSession::Fail(const FFFResult result, std::string message, const char* operation) noexcept {
    try {
        // Mirror every failure into the process log sink (FFF3FP_SetLogCallback).
        extern void FFF3FP_KernelLogImpl(const char*) noexcept;
        FFF3FP_KernelLogImpl(("FAIL: " + std::string(operation ? operation : "") + ": " + message).c_str());
    } catch (...) {}
    try {
        SuspendAudioRenderer(true); snapshot_.state = FFF3FPState::Failed; state_.store(FFF3FPState::Failed); PublishSnapshot();
        ReportError(result, std::move(message), operation); }
    catch (...) {}
}

void PlayerSession::ReportError(const FFFResult result, std::string message, const char* operation) noexcept {
    try {
        { std::lock_guard lock(errorMutex_); lastError_ = message; }
        std::string json = "{\"result\":" + std::to_string(static_cast<int>(result)) +
            ",\"message\":\"" + EscapeJson(message) + "\"";
        if (operation) json += ",\"operation\":\"" + EscapeJson(operation) + "\"";
        json += '}';
        Emit(FFF3FPEvent::Error, json);
    } catch (...) {}
}

void PlayerSession::Emit(const FFF3FPEvent event, const std::string& json) const noexcept {
    if (callback_ == nullptr) return; try { callback_(callbackContext_, event, json.c_str()); } catch (...) {}
}

std::int64_t PlayerSession::ClockPosition() const noexcept {
    if (playbackPreroll_) {
        const auto position = clockOriginPosition100ns_.load();
        PublishPlaybackClock(position, position);
        return position;
    }
    if (!audioClockFinished_ && audioRenderer_ && (audioStream_ >= 0 || externalAudioStream_ >= 0)) {
        const auto audioPosition = tempoOrigin_ + static_cast<std::int64_t>((audioRenderer_->Position100ns() - tempoOrigin_) * playbackRate_.load());
        PublishPlaybackClock(audioPosition, tempoOrigin_ + static_cast<std::int64_t>((audioRenderer_->TimelineLimit100ns() - tempoOrigin_) * playbackRate_.load()));
        return audioPosition;
    }
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    const auto origin = clockOriginQpc_.load(); if (origin == 0 || qpcFrequency_ <= 0) return clockOriginPosition100ns_.load();
    const auto position = clockOriginPosition100ns_.load() + static_cast<std::int64_t>((now.QuadPart - origin) * double(TicksPerSecond) * playbackRate_.load() / qpcFrequency_);
    PublishPlaybackClock(position, snapshot_.duration100ns > 0
        ? snapshot_.duration100ns : (std::numeric_limits<std::int64_t>::max)());
    return position;
}

void PlayerSession::PublishPlaybackClock(const std::int64_t position,
    const std::int64_t limit) const noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    playbackClockSequence_.fetch_add(1, std::memory_order_acq_rel);
    playbackPosition100ns_.store(position, std::memory_order_relaxed);
    playbackClockLimit100ns_.store(std::max(position, limit), std::memory_order_relaxed);
    playbackClockSampleQpc_.store(now.QuadPart, std::memory_order_relaxed);
    playbackClockSequence_.fetch_add(1, std::memory_order_release);
}

void PlayerSession::ResetClock(const std::int64_t position) noexcept {
    // Every clock reset (play, seek, keyframe-seek completion) restarts the timing
    // relationship the pacer was balancing, so an engaged pacer steps aside here.
    if (paceEngaged_) ReleasePresentationPace("clock-reset");
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    clockOriginPosition100ns_ = position;
    clockOriginQpc_ = now.QuadPart;
    PublishPlaybackClock(position, position);
}

void PlayerSession::PublishSnapshot() noexcept {
    snapshot_.presentedVideoFrames = videoRenderer_.PresentedVideoFrames();
    snapshot_.coalescedVideoFrames = videoRenderer_.CoalescedVideoFrames();
    snapshot_.swapChainPresents = videoRenderer_.SwapChainPresents();
    snapshot_.presentWait100ns = videoRenderer_.PresentWait100ns();
    snapshot_.deviceLockWait100ns = videoRenderer_.DeviceLockWait100ns();
    snapshot_.softwareConvert100ns = videoRenderer_.SoftwareConvert100ns();
    snapshot_.videoUpload100ns = videoRenderer_.Upload100ns();
    snapshot_.uploadMapWait100ns = videoRenderer_.UploadMapWait100ns();
    snapshot_.uploadCopy100ns = videoRenderer_.UploadCopy100ns();
    snapshot_.downscaleResample100ns = videoRenderer_.DownscaleResample100ns();
    snapshot_.scalePassCount = videoRenderer_.ScalePassCount();
    snapshot_.uploadCount = videoRenderer_.UploadCount();
    snapshot_.uploadCopyBytes = videoRenderer_.UploadCopyBytes();
    snapshot_.uploadPath = videoRenderer_.UploadPath();
    std::lock_guard lock(snapshotMutex_);
    publishedSnapshot_ = snapshot_;
}

bool PlayerSession::NormalizeLocalPath(const char* path, std::string& normalized, std::string& error) noexcept {
    try {
        auto wide = FromUtf8(path); if (wide.empty()) { error = "A non-empty UTF-8 local path is required."; return false; }
        if (wide.rfind(L"\\\\", 0) == 0 || wide.rfind(L"//", 0) == 0 || wide.find(L"://") != std::wstring::npos ||
            wide.rfind(L"\\\\.\\", 0) == 0 || wide.rfind(L"\\\\?\\GLOBALROOT", 0) == 0) { error = "Network, device and pipe paths are disabled."; return false; }
        std::vector<wchar_t> full(32768); const auto length = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length == 0 || length >= full.size()) { error = "The local path is invalid."; return false; }
        const auto attributes = GetFileAttributesW(full.data());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { error = "The path must identify an existing regular local file."; return false; }
        normalized = ToUtf8(full.data()); return !normalized.empty();
    } catch (...) { error = "The local path could not be normalized."; return false; }
}
