#pragma once

#include "3FR/Api/FFF.Native.Api.h"

#include <cstddef>
#include <cstdint>

enum class FFF3FPDecodeMode : std::uint32_t {
    Unspecified = 0,
    Cpu = 1,
    Gpu = 2,
    D3D11 = Gpu,
};

enum class FFF3FPColorMode : std::uint32_t {
    MapToSdr = 0,
    RawHdrAsSdr = 1,
    MapToHdr = 2,
};

enum class FFF3FPVideoScalingMode : std::uint32_t {
    Shader = 0,
    D3D11VideoProcessor = 1,
};

enum class FFF3FPVideoScalingQuality : std::uint32_t {
    Balanced = 0,
    HighQuality = 1,
};

enum class FFF3FPColorTransfer : std::uint32_t {
    SdrBt709 = 0,
    Pq = 1,
    Hlg = 2,
};

enum class FFF3FPHdrFormat : std::uint32_t {
    Sdr = 0,
    Hdr10 = 1,
    Hdr10Plus = 2,
    Hlg = 3,
    HDRdp = 4,
    HdrVivid = 5,
};

enum class FFF3FPHdrCompatibility : std::uint32_t {
    None = 0,
    Hdr10 = 1u << 0,
    Hlg = 1u << 1,
    HDRdp = 1u << 2,
    HdrVivid = 1u << 3,
};

enum class FFF3FPHdrProcessingPath : std::uint32_t {
    None = 0,
    StaticHdr10 = 1,
    Hdr10PlusDynamic = 2,
    HlgDisplayMapped = 3,
    HDRdpHdr10Fallback = 4,
    HDRdpFelFallback = 5,
    HdrVividDynamic = 6,
    ExternalDynamic = 7,
};

// DEPRECATED ABI (values retained, never produced): the external enhancement layer
// is no longer decoded or reported by the player. Kept so snapshot and probe
// layouts stay byte-identical for hosts built against earlier headers.
enum class FFF3FPHDRdpEnhancementLayer : std::uint32_t {
    None = 0,
    Mel = 1,
    Fel = 2,
    Unknown = 3,
};

enum class FFF3FPState : std::uint32_t {
    Idle = 0,
    Opening = 1,
    Ready = 2,
    Playing = 3,
    Paused = 4,
    Ended = 5,
    Failed = 6,
    Closed = 7,
};

enum class FFF3FPEvent : std::uint32_t {
    StateChanged = 1,
    OpenCompleted = 2,
    OperationCompleted = 3,
    PlaybackEnded = 4,
    Error = 5,
    ColorModeChanged = 6,
    DeviceChanged = 7,
};

using FFF3FPEventCallback = void(__cdecl*)(void* context, FFF3FPEvent eventType,
    const char* detailJsonUtf8);

struct FFF3FPConfiguration {
    std::uint32_t size;
    std::uint32_t version;
    void* outputWindow;
    FFF3FPDecodeMode decodeMode;
    FFF3FPColorMode colorMode;
    float sdrPeakNits;
    float hdrPeakNits;
    float sdrPaperWhiteNits;
    const char* audioEndpointIdUtf8;
    FFF3FPEventCallback eventCallback;
    void* eventCallbackContext;
    FFF3FPVideoScalingQuality videoScalingQuality;
    std::uint32_t forceHdrOutput;
    // Preferred DXGI adapter index for the D3D11 device.
    // Use -1 (or any negative value) to keep the built-in policy: pick the adapter
    // that drives the monitor containing the output window. The index matches
    // IDXGIFactory1::EnumAdapters1 — the enumeration the managed side must use so
    // both sides agree on "which adapter is #1".
    // Out-of-range or failed enumeration falls back to the built-in policy.
    // NOTE: 0 is a valid adapter index, NOT "unset". A host that leaves this
    // field zeroed therefore pins playback to adapter #0 and silently overrides
    // the monitor match, so every caller must initialise it to -1 explicitly.
    std::int32_t preferredAdapterIndex = -1;
    // Presentation policy for SDR sources on an Advanced Color display. The output tier
    // ladder is "prefer 16-bit, fall back to 10-bit", and this field is the preference:
    //   1 = Auto (default): a source deeper than 8 bits is presented on the 16-bit scRGB
    //       chain. Without it a 10-bit SDR source sits on a gamma R10G10B10A2 chain that
    //       DWM resamples as an ordinary SDR window, so the extra precision is rewritten
    //       away.
    //   0 = Never: keep the classic 8/10-bit gamma chain for SDR sources. Sources that
    //       already needed scRGB before this field existed - an HDR transfer function or
    //       wide-gamut primaries - still get it; Never does not demote them.
    // scRGB is only *preferred*, never required. Every way of not getting it degrades to
    // the 10-bit tier (8-bit for 8-bit sources): a display Windows does not report as
    // Advanced Color (the OutputSupportsHdr gate), an OS or driver without the ACM
    // colour-space API, or a swap chain that rejects
    // DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 - that rejection rebuilds the chain and
    // then backs off for 10 s before retrying.
    // FFF3FP_SDR_SCRGB=0/1 overrides whatever the caller passes, for measurement only.
    std::uint32_t sdrScRgbMode = 1;
    // Adaptive downscale-before-upload. Software-decoded frames cross PCIe as whole
    // planes -- an 8K P010 frame is ~100 MB -- which caps playback well below the
    // source rate on a bus that cannot carry it. When the session is software decoding
    // AND the renderer is dropping a large share of frames, resampling the frame down
    // to the size actually on screen first cuts that transfer by roughly the square of
    // the ratio (measured 8K AV1: 99,532,800 B -> 19,685,982 B per upload, the CPU copy
    // 14.85 ms -> 2.83 ms).
    //
    // Measured cost, which is why this stays off: the resample itself is sws_scale at
    // about 4.6 GB/s, so it takes **26 ms per 8K frame** -- roughly 2 ms more than the
    // 14.85 ms of copy it removes, and net playback went 30 -> 24.5 fps. The earlier
    // claim on this line, "about a millisecond of CPU work", was never measured; the
    // direction is right (fewer bytes should be moved) but not on the CPU.
    //
    // Off by default. It never engages for hardware decoding (those frames are already
    // GPU-resident, so there is nothing to save) or for still images.
    //   0 = Disabled (default)
    //   1 = Enabled: engage automatically once drops are sustained
    std::uint32_t adaptiveDownscaleBeforeUpload = 0;
    // Sustained drop ratio, in percent, that engages the adaptive downscale. Measured
    // over a rolling window so one hiccup cannot trigger it. A lower value engages
    // sooner, at the cost of resampling even when the pipeline was nearly keeping up.
    std::uint32_t adaptiveDownscaleDropPercent = 20;
    // Adaptive software-decoder thread count. A fixed count is wrong in both
    // directions. Measured here, the decode optimum lands around 6-12 workers
    // depending on codec and resolution (8K HEVC 6, 8K AV1 8, 4K AV1 32), and going
    // past it actively hurts: 8K HEVC at 32 threads is 2.14x slower than at 6, because
    // those frames are memory-bandwidth bound and extra workers only contend. Equally,
    // an easy source does not need the workers at all, so starting low and growing
    // only under pressure keeps the CPU free when playback is comfortable.
    //
    // Bounded to [minSoftwareDecoderThreads, maxSoftwareDecoderThreads] and by the
    // thread budget: decoder workers plus the player's worker, audio and video present
    // threads stay below the CPU count. One step also never crosses the preferred
    // domain boundary (performance class on hybrid, one L3 domain on multi-CCD): the
    // boundary set is tried first, the next class/domain only if it is not enough.
    // Never applied to hardware decoding, whose decode part is a single thread.
    //   0 = Disabled: one fixed count chosen at open time (the previous behaviour)
    //   1 = Enabled: adapt within the bounds below
    std::uint32_t adaptiveDecoderThreads = 0;
    // Ladder bounds. Clamped to [1, 64]; ffmpeg caps the usable count itself.
    std::uint32_t minSoftwareDecoderThreads = 4;
    std::uint32_t maxSoftwareDecoderThreads = 32;
    // Sustained drop ratio, in percent, meaning "decoding cannot keep up", which grows
    // the ladder; below the shrink threshold it shrinks again. Same rolling-window idea
    // as the adaptive downscale but with separate thresholds, so the two policies
    // cannot drive each other into oscillation.
    std::uint32_t decoderGrowDropPercent = 10;
    std::uint32_t decoderShrinkDropPercent = 2;
    // Deliberately NOT extended here. The flip-rate bound is automatic (min of panel rate
    // and content rate) and computed inside the renderer.
    //
    // A field appended to this struct grows sizeof it, and FFF3FP_Create rejects any host
    // whose reported size is below sizeof(FFF3FPConfiguration) (PlayerApi.cpp:94) -- so a
    // config-only knob would take down every host that has not been rebuilt in the same
    // step, including the two managed ones in this repository. Measured, not assumed:
    // the VB host's own comment on 原生播放器配置 records exactly that failure (host at
    // 80 bytes against a 112-byte kernel => Create returns -1, the player cannot open
    // anything). A setter carries no such coupling: a host that never calls it simply has
    // no bound, which is the behaviour it has today.
};

struct FFF3FPSnapshot {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPState state;
    FFF3FPDecodeMode decodeMode;
    FFF3FPColorMode requestedColorMode;
    FFF3FPColorMode actualColorMode;
    std::int64_t position100ns;
    std::int64_t duration100ns;
    std::int64_t frameIndex;
    std::int64_t framePts;
    std::int32_t frameTimeBaseNumerator;
    std::int32_t frameTimeBaseDenominator;
    std::int32_t selectedVideoStream;
    std::int32_t selectedAudioStream;
    std::uint32_t videoWidth;
    std::uint32_t videoHeight;
    std::uint32_t isHdrSource;
    std::uint32_t isExternalAudio;
    std::int64_t externalAudioOffset100ns;
    std::uint64_t decodedVideoFrames;
    std::uint64_t presentedVideoFrames;
    std::uint64_t droppedVideoFrames;
    std::uint32_t queuedVideoFrames;
    std::uint32_t sourcePeakNits;
    // Audio diagnostics use the same 100 ns time base as position100ns. They
    // report renderer state only; the media clock remains the owner of video
    // presentation timing.
    std::uint64_t decodedAudioFrames;
    // (The IAMF fields live at the end of this struct -- see iamfActive there.)
    std::int64_t audioPosition100ns;
    std::int64_t bufferedAudio100ns;
    std::uint64_t audioUnderruns;
    std::uint64_t audioTimestampJitterFrames;
    std::uint64_t audioDiscontinuities;
    std::uint64_t audioInsertedSilenceFrames;
    std::uint64_t audioDroppedOverlapFrames;
    // API v5 diagnostics. `presentedVideoFrames` counts decoded frames accepted
    // by the renderer (including headless/clip-mode sessions); `swapChainPresents`
    // is the count of actual successful DXGI presents.
    std::uint64_t coalescedVideoFrames;
    std::uint64_t audioRejectedFrames;
    std::uint64_t swapChainPresents;
    std::uint64_t presentWait100ns;
    std::uint64_t deviceLockWait100ns;
    std::uint64_t hardwareTransfer100ns;
    std::uint64_t softwareConvert100ns;
    // Rolling packet-rate estimates for the currently selected streams.
    // These are media-time rates, not the container's static bit_rate field.
    std::uint64_t videoBitRate;
    std::uint64_t audioBitRate;
    // Actual swap-chain precision after renderer/device capability fallback.
    // 8 = BGRA8, 10 = RGB10A2, 16 = R16G16B16A16_FLOAT scRGB.
    std::uint32_t videoOutputBitDepth;
    // The path used for the most recently rendered frame. The renderer selects
    // this automatically from the decoded surface and output requirements.
    FFF3FPVideoScalingMode videoScalingMode;
    // Advances only after a real demuxer seek succeeds and the new media
    // position has been published. Overlay producers use it to discard old state.
    std::uint64_t timelineGeneration;
    // API v7 HDR diagnostics. Dynamic metadata is copied into renderer-owned
    // state before the decoded AVFrame is released, so these values always
    // describe the cached/presented frame rather than the next decoded frame.
    FFF3FPHdrFormat hdrFormat;
    std::uint32_t compatibleHdrFormats;
    FFF3FPHdrProcessingPath hdrProcessingPath;
    // DEPRECATED ABI (slots retained, always zero): the stream is no longer
    // classified as carrying external metadata; its base layer is plain HDR10.
    std::uint32_t hdrDpProfile;
    std::uint32_t hdrDpLevel;
    std::uint32_t hasHDRdpRpu;
    std::uint32_t hasHDRdpEnhancementLayer;
    FFF3FPHDRdpEnhancementLayer hdrDpEnhancementLayer;
    std::uint32_t dynamicHdrMetadataActive;
    std::uint32_t hdrFallbackActive;
    std::uint32_t displayMinLuminanceMilliNits;
    std::uint32_t displayPeakNits;
    std::uint32_t displayFullFramePeakNits;
    std::uint32_t effectiveTargetPeakNits;
    // API v17: ST 2094 dynamic metadata diagnostics. These describe what the
    // per-frame HDR10+ / HDR Vivid parse actually produced, so a host can tell
    // "metadata-guided mapping is running" apart from "metadata was seen but
    // could not be used". Appended at the end of the struct on purpose: the
    // fields above keep their offsets, and readers must gate on `version`.
    //
    // dynamicMetadataWindows: validated windows, 0 when none.
    // dynamicMetadataDegrade: HdrMetadataDegrade reason, 0 = none.
    // dynamicMetadataSerial: increments per frame carrying fresh metadata.
    // dynamicMetadataHeldFrames: consecutive frames reusing the last snapshot.
    // dynamicMetadataTargetedNits: the stream's targeted display luminance.
    std::uint32_t dynamicMetadataWindows;
    std::uint32_t dynamicMetadataDegrade;
    std::uint64_t dynamicMetadataSerial;
    std::uint64_t dynamicMetadataHeldFrames;
    std::uint32_t dynamicMetadataTargetedNits;
    // IAMF immersive audio. Appended at the tail like every other field in this
    // struct: hosts and our own probes read it by fixed offset, so a field placed
    // in the middle would silently shift everything after it.
    // iamfActive is set when the AOM reference decoder is rendering the track;
    // iamfChannels is then the *render target* channel count (7.1.4 = 12) rather
    // than one substream's share, and iamfSoundSystem carries the OAR sound system.
    std::uint32_t iamfActive;
    std::uint32_t iamfChannels;
    std::int32_t iamfSoundSystem;

    // Time spent copying decoded planes into the shader-visible textures, in the
    // same 100 ns base as the other diagnostics. Strictly appended at the end:
    // putting a new field earlier would shift every field after it, and hosts
    // (and our own probes) read the struct by fixed offset. The upload path had no
    // metric at all before this, so upload changes could not be measured.
    std::uint64_t videoUpload100ns;
    // IAMF: channels the *content* declares, which is not the same as iamfChannels.
    // The render target is always 7.1.4 (12), and when the content is smaller libiamf
    // fills the speakers it does not have with digital silence rather than upmixing --
    // verified against its own iamfdec: a 7.1 file rendered at -s5 still produces 12
    // channels, with exactly four of them at peak -inf. So iamfChannels answers "what are
    // we rendering into" while this answers "what is actually in the file", and a UI that
    // showed only the former would label a 7.1 track as 7.1.4 even though four speakers
    // will stay silent. 0 when libiamf reports no per-element detail.
    // Appended at the end for the same offset reason as videoUpload100ns above.
    std::uint32_t iamfContentChannels;
    // IAMF rendering mode in force: 0 = the speaker layout (7.1.4) with the
    // WASAPI resampler folding down to the device, 1 = libiamf's binaural
    // (HRTF) renderer produced the stereo signal directly. Binaural keeps the
    // spatial *image* on headphones; the matrix downmix keeps the energy but
    // loses the placement. Chosen automatically from the device's channel
    // count (stereo => binaural); reported so a host can say which one the
    // user is actually hearing. Appended at the end for the offset reason above.
    std::uint32_t iamfBinaural;
    // Generic bed binaural rendering for NON-IAMF multichannel tracks (legacy
    // channel-based surround beds, plain multichannel PCM/FLAC/WAV): when the
    // output device is stereo, those tracks got
    // a plain swr matrix downmix: energy preserved, placement lost. This is
    // 1 while the OBR HRTF renderer is feeding the stereo output instead, 0 when
    // the matrix downmix is in force (multichannel device, unsupported
    // layout, or IAMF owning the track, which renders through its own binaural
    // path reported by iamfBinaural). Appended at the end for the offset
    // reason above.
    std::uint32_t audioBinaural;
    // Audio position offset in 100ns units applied to the internal (muxed) audio
    // track's timestamps before they are queued: positive = audio plays later than
    // video (lip-sync correction). It is the internal-track counterpart of
    // externalAudioOffset100ns, which covers the externally loaded track; the two are
    // mutually exclusive because exactly one owner supplies any given audio frame, so
    // they never add up on the same frame. Set via FFF3FP_SetAudioDelay; survives
    // Close/Open (only Destroy+Create resets it). Appended at the end for the offset
    // reason above.
    std::int64_t audioDelay100ns;
    // Upload-path cost split. videoUpload100ns covers the whole upload step,
    // which turned out to be three different costs sharing one number:
    //   uploadMapWait100ns - the Map() call itself. D3D11 Map() blocks until the
    //                        GPU is finished with the resource being mapped unless
    //                        D3D11_MAP_FLAG_DO_NOT_WAIT is passed, so this is where
    //                        waiting on the previous frame's draw shows up.
    //   uploadCopy100ns    - the memcpy loop (planar rung 0/1) or the
    //                        UpdateSubresource call (packed / half-float arms):
    //                        the bytes actually moving.
    //   the remainder      - Unmap / CopyResource / driver submission, still inside
    //                        videoUpload100ns; not split out here.
    // The packed arms never map, so their whole cost lands in uploadCopy100ns and
    // their map wait is 0. Appended at the end for the offset reason above.
    std::uint64_t uploadMapWait100ns;
    std::uint64_t uploadCopy100ns;
    // CPU resample performed by the downscale-before-upload policy
    // (PrepareAdaptiveDownscale's sws_scale). It runs OUTSIDE videoUpload100ns and
    // had no metric at all, which is what let that policy look like a 90% win on
    // the numbers while making playback slower. Appended at the end for the offset
    // reason above.
    std::uint64_t downscaleResample100ns;
    // Cumulative count of scale-chain passes actually executed. A delta divided by
    // the presented-frame delta gives passes per frame. The chain is built by
    // halving (EnsurePlaneScaleChain), so this is what makes an extreme downscale
    // show up as GPU draw cost instead of hiding inside the upload number.
    // Appended at the end for the offset reason above.
    std::uint64_t scalePassCount;
    // Bytes actually handed to the copy, and how many pictures were uploaded.
    // uploadCopy100ns on its own cannot be turned into a bandwidth: the snapshot
    // recorded no byte count and no upload count, so every "GB/s" quoted from it
    // was computed from a *theoretical* frame size (W*H*1.5*bpp) divided by a
    // per-PRESENTED-frame time. Both operands were guesses -- an 8K arm that drops
    // 16 of its 57 decoded frames per second divides by 41, and nothing in the
    // snapshot says whether those 16 were uploaded at all. With these two,
    // bytes/upload and bytes/copy100ns are both measured.
    // Appended at the end for the offset reason above.
    std::uint64_t uploadCount;
    std::uint64_t uploadCopyBytes;
    // Which upload branch the last picture took, because the branches move
    // different numbers of bytes into different kinds of memory and were
    // previously indistinguishable:
    //   0 none yet - 1 planar STAGING + Map(WRITE) (the >= 4K rung)
    //   2 planar DYNAMIC + Map(WRITE_DISCARD) (the < 4K rung)
    //   3 packed UpdateSubresource inside directYuv
    //   4 half-float UpdateSubresource - 5 RGB layout 0 UpdateSubresource
    //   6 planar/semi-planar via UpdateSubresource -- the pre-#25 large-frame route,
    //     kept as an env-gated A/B arm against path 1. Bytes stay exact here: the
    //     box handed to the driver is the whole frame, so this arm is comparable to
    //     path 1 on bytes-per-upload and on uploadCopy100ns.
    //   7 D3D11VA hardware frames -- GPU-to-GPU CopySubresourceRegion from the
    //     decoder's array slice into the retained source texture. No Map, so
    //     uploadMapWait100ns stays 0; upload100ns == uploadCopy100ns (the copy is
    //     the whole upload). Bytes are the NV12/P010/P016 texture size, so the
    //     bandwidth column stays comparable with the CPU arms. Before 2026-10-20
    //     this branch recorded nothing and hardware runs showed uploads=0/upath=0.
    // uploadCopyBytes is exact for 1 and 2; the UpdateSubresource arms do not know
    // their own byte total here, so they leave it untouched and stay identifiable
    // by this field instead of being silently averaged in.
    // Appended at the end for the offset reason above.
    std::uint32_t uploadPath;
};
// Layout teeth. FFF3FPSnapshot is read by fixed offset from the VB/C# hosts and from
// the probes, which is why it is append-only; that rule is unenforceable until the
// offsets are pinned, so an insertion in the middle breaks the build here instead of
// shifting a host's view of the struct at runtime.
// Values below were measured by compiling a probe against this header (cl /std:c++20,
// offsetof/sizeof printed) -- not derived from a field table.
static_assert(sizeof(FFF3FPSnapshot) == 464,
    "FFF3FPSnapshot size changed: hosts and probes read it by fixed offset");
static_assert(offsetof(FFF3FPSnapshot, externalAudioOffset100ns) == 88, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, iamfActive) == 364, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, iamfChannels) == 368, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, iamfSoundSystem) == 372, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, videoUpload100ns) == 376, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, iamfContentChannels) == 384, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, iamfBinaural) == 388, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, audioBinaural) == 392, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, audioDelay100ns) == 400, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, uploadMapWait100ns) == 408, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, uploadCopy100ns) == 416, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, downscaleResample100ns) == 424, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, scalePassCount) == 432, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, uploadCount) == 440, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, uploadCopyBytes) == 448, "offset pin");
static_assert(offsetof(FFF3FPSnapshot, uploadPath) == 456, "offset pin");


struct FFF3FPAudioPeakLevels {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t channelCount;
    std::uint32_t inputChannelCount;
    float values[8];
    float inputValues[8];
};

// HDR presentation diagnostics. Separate struct rather than new FFF3FPSnapshot fields:
// the snapshot layout is mirrored by every host's Marshal offsets, and this data is
// polled rarely and read by tooling, not by the frame loop.
//
// It exists because two claims about the ST 2094 paths were previously only checkable by
// reading the shader gate:
//   * whether the per-window curve actually shaped the frame just drawn
//     (dynamicCurveFrames* -- under MapToHdr the renderer deliberately does not
//     map the injected curve (see the useDynamicCurve gate in DrawCachedVideo),
//     so "hdrProcessingPath = ...Dynamic" alone cannot tell a host which of the
//     two mechanisms ran);
//   * whether the per-frame display signaling actually reached DXGI, and how often the
//     change gate suppressed it (setHdrMetaData* / lastSignaled*).
struct FFF3FPHdrSignalDiagnostics {
    std::uint32_t size;
    std::uint32_t version;              // 20
    std::uint64_t dynamicCurveFramesApplied;
    std::uint64_t dynamicCurveFramesRefused;   // dynamic metadata present, curve not applied
    std::uint64_t setHdrMetaDataCalls;         // actual IDXGISwapChain::SetHDRMetaData calls
    std::uint64_t setHdrMetaDataSuppressed;    // frames where the change gate said "same"
    // The last block actually handed to DXGI, in the DXGI units themselves:
    // MaxMasteringLuminance / MinMasteringLuminance in 1/10000 cd/m^2, the two light
    // levels in 1 cd/m^2. Zeroes mean nothing has been signaled yet.
    std::uint32_t lastSignaledMaxMasteringLuminance;
    std::uint32_t lastSignaledMinMasteringLuminance;
    std::uint32_t lastSignaledMaxContentLightLevel;
    std::uint32_t lastSignaledMaxFrameAverageLightLevel;
    std::uint32_t swapChainHdrEnabled;         // 1 when the chain is the scRGB HDR chain
    std::uint32_t reserved;
};
// Pinned so a host-side Marshal.SizeOf drift cannot silently produce a struct that passes
// the `size < sizeof` gate while reading fields at the wrong offsets.
static_assert(sizeof(FFF3FPHdrSignalDiagnostics) == 64,
    "FFF3FPHdrSignalDiagnostics layout changed: mirror it in Fff3FpNative.cs and re-pin");

using FFF3FPHandle = void*;
using FFF3FPBitmapSubtitleHandle = void*;
using FFF3FPAssSubtitleHandle = void*;

// Process-wide native log sink. The kernel calls the installed callback with UTF-8 log
// lines from any of its threads (decode / present / recovery); route them wherever the
// host keeps its logs. Install once at startup, before creating sessions.
// Passing nullptr detaches the sink, but detaching is not synchronous with respect to a
// line already in flight: a host whose callback is a managed delegate must keep that
// delegate reachable until the kernel's threads have stopped. Releasing it right after
// detaching can otherwise deliver a call into a torn-down runtime (CLR fatal, exit 127).
// The type is noexcept because the kernel calls it from its decode/present threads and has
// no way to handle anything coming back through it: a host whose callback is managed code
// must catch its own failures, otherwise an exception escaping here terminates the process.
using FFF3FPLogCallback = void(__cdecl*)(void* context, const char* utf8Line) noexcept;

enum class FFF3FPBitmapSubtitleFlags : std::uint32_t {
    None = 0,
    Clear = 1,
    EndOfStream = 2,
    Forced = 4,
    MoreData = 8,
    Unchanged = 16,
};

struct FFF3FPBitmapSubtitleFrame {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPBitmapSubtitleFlags flags;
    std::uint32_t reserved;
    std::int64_t start100ns;
    std::int64_t end100ns;
    std::int32_t canvasWidth;
    std::int32_t canvasHeight;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
    std::int32_t stride;
    std::uint32_t pixelBytes;
    std::int64_t sequence;
};

enum class FFF3FPTimedTextCommandType : std::uint32_t {
    Text = 1,
    Bitmap = 2,
};

enum class FFF3FPTimedTextFlags : std::uint32_t {
    None = 0,
    Bold = 1,
    Italic = 2,
    Underline = 4,
    Strikeout = 8,
    HdrHighlightBitmap = 16,
    // shadowOffsetX/Y carry the Gaussian standard deviation when this flag is set.
    SoftShadow = 32,
};

enum class FFF3FPTimedTextAlignment : std::uint32_t {
    Near = 0,
    Center = 1,
    Far = 2,
};

struct FFF3FPTimedTextCommand {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPTimedTextCommandType type;
    FFF3FPTimedTextFlags flags;
    float x;
    float y;
    float width;
    float height;
    std::uint32_t foregroundArgb;
    std::uint32_t outlineArgb;
    float fontSize;
    float outlineWidth;
    FFF3FPTimedTextAlignment horizontalAlignment;
    FFF3FPTimedTextAlignment verticalAlignment;
    const char* textUtf8;
    const char* fontFamilyUtf8;
    const void* bitmapBgra;
    std::uint32_t bitmapWidth;
    std::uint32_t bitmapHeight;
    std::uint32_t bitmapStride;
    std::uint32_t bitmapBytes;
    std::uint64_t contentId;
    // Optional version-1 tail. outlineWidth is the final visible distance
    // outside the filled glyph; the renderer uses a 2x centered geometry pen.
    std::uint32_t shadowArgb;
    float shadowOffsetX;
    float shadowOffsetY;
    std::uint32_t reserved;
};

struct FFF3FPTimedTextRasterizationProbe {
    std::uint32_t size;
    std::uint32_t version;
    float outlineWidth;
    float shadowOffsetX;
    float shadowOffsetY;
    float geometryStrokeWidth;
    float effectLeft;
    float effectTop;
    float effectRight;
    float effectBottom;
    float shadowAngleDegrees;
    std::uint32_t naturalSymmetricRendering;
    std::uint32_t grayscaleAntialiasing;
    std::uint32_t pixelSnappingDisabled;
    std::uint32_t outlineIsExternal;
};

// DirectWrite metrics used by the managed subtitle layout. The input font size
// is in the same pixel/DIP unit as FFF3FPTimedTextCommand::fontSize.
struct FFF3FPTimedTextMeasurement {
    std::uint32_t size;
    std::uint32_t version;
    float layoutHeight;
    float visibleTop;
    float visibleBottom;
};

struct FFF3FPTimedTextLayer {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t canvasWidth;
    std::uint32_t canvasHeight;
    std::uint32_t commandCount;
    // 0 = subtitle, 1 = danmaku, 2 = player information, 3 = lyrics. Kept in
    // the original reserved field so the version-1 ABI remains stable while
    // the producers remain independent.
    std::uint32_t layerSlot;
    std::uint64_t sequence;
    const FFF3FPTimedTextCommand* commands;
    // Optional version-1 tail. Older callers may pass the legacy size and are
    // treated as 60 Hz; current callers publish the layer's independent pace.
    float targetFrameRate;
    std::uint32_t reserved2;
    // Optional lyrics presentation tail. Values use logical percentages so
    // managed layout and the native cover renderer share one configuration.
    float coverBackdropBlurRadius;
    std::uint32_t coverBackdropBlurPasses;
    std::uint32_t coverBackdropDownsampleFactor;
    std::uint32_t coverBackdropTintArgb;
    float coverRegionWidthPercentage;
    float lyricsRegionWidthPercentage;
    // Legacy horizontal padding now represents the left inset. The optional
    // tail below adds an independent right inset without changing old fields.
    float coverHorizontalPaddingPercentage;
    float coverVerticalPaddingPercentage;
    float coverRightPaddingPercentage;
};

struct FFF3FPTimedTextStatus {
    std::uint32_t size;
    std::uint32_t version;
    std::uint64_t submittedSequence;
    std::uint64_t renderedSequence;
    std::uint32_t commandCount;
    std::uint32_t canvasWidth;
    std::uint32_t canvasHeight;
    std::uint32_t reserved;
    std::uint64_t visiblePixelCount;
    std::uint64_t spriteCacheHits;
    std::uint64_t spriteCacheMisses;
    // D3D11 exposes only logical buffer 0 for flip-model chains. Its physical
    // identity rotates, so this count must advance once per final presentation.
    std::uint64_t backBufferAcquisitionCount;
    std::uint64_t compositePixelShaderInvocations;
};

// Numeric probe for the production color transform.  This is deliberately
// independent of a swap chain so automated tests can verify luminance anchors
// without judging screenshots by eye. HDR/scRGB outputs are linear values and
// may exceed 1.0 (1.0 represents 80 nits).
struct FFF3FPColorTransform {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPColorMode colorMode;
    FFF3FPColorTransfer transfer;
    // Gamut switch: 0 = Rec.709, 1 = Rec.2020, 2 = P3 (DCI/Display).
    std::uint32_t source2020;
    std::uint32_t reserved;
    float inputRed;
    float inputGreen;
    float inputBlue;
    float sdrPeakNits;
    float sourcePeakNits;
    float paperWhiteNits;
    float outputRed;
    float outputGreen;
    float outputBlue;
};

// Deterministic ABI-v1 probe for HDR classification and luminance policy.
// The hdrDp* inputs below are DEPRECATED ABI (slots retained): they are
// validated but no longer influence the result -- a stream carrying external
// classified through its transfer characteristic alone. HdrDp residual:
// 0=unknown/not present, 1=MEL, 2=FEL.
struct FFF3FPHdrProcessingProbe {
    std::uint32_t size;
    std::uint32_t version;
    FFF3FPColorTransfer transfer;
    std::uint32_t hdrDpProfile;
    std::uint32_t hdrDpLevel;
    std::uint32_t hdrDpCompatibilityId;
    std::uint32_t hdrDpRpu;
    std::uint32_t hdrDpEnhancementLayer;
    std::uint32_t hdrDpResidual;
    std::uint32_t hdr10PlusMetadata;
    std::uint32_t hdrVividMetadata;
    float displayPeakNits;
    float displayFullFramePeakNits;
    float targetPeakOverrideNits;
    FFF3FPHdrFormat outputFormat;
    std::uint32_t outputCompatibility;
    FFF3FPHdrProcessingPath outputProcessingPath;
    FFF3FPHDRdpEnhancementLayer outputEnhancementLayer;  // deprecated: always None
    std::uint32_t outputDynamicMetadata;
    std::uint32_t outputFallback;
    std::uint32_t outputTargetPeakNits;
};

// Reads one pixel from the current video-only back buffer before desktop color
// management. Intended for renderer regression tests and diagnostics.
struct FFF3FPVideoPixelProbe {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t x;
    std::uint32_t y;
    float red;
    float green;
    float blue;
    float alpha;
    FFF3FPVideoScalingMode scalingMode;
    std::uint32_t outputBitDepth;
    FFF3FPColorMode colorMode;
    std::uint32_t reserved;
};

// NVIDIA RTX Video Super Resolution (driver-level D3D11 video processor
// extension). This is a switch, NOT a default: Off unless the host asks for it.
//
// Scope and safety: VSR runs an AI model, so the presented pixels stop being a
// reproducible function of the decoded frame. Hosts that compare frames
// pixel-for-pixel must leave it Off, which is also why the renderer refuses to
// enable it while a comparison-style mode is active.
enum class FFF3FPVideoSuperResolution : std::uint32_t {
    // Default. The shader scaling path is used exactly as before.
    Off = 0,
    // Attempt VSR whenever the source satisfies the driver's conditions
    // (upscaling, source height 360p..1440p, NVIDIA adapter).
    Auto = 1,
};

// Why the last presented frame did or did not use VSR. Reported so a host can
// explain the state without guessing; 0 means "used" when active is 1.
enum class FFF3FPVideoSuperResolutionReason : std::uint32_t {
    None = 0,
    NotRequested = 1,
    NotNvidiaAdapter = 2,
    NotUpscaling = 3,
    SourceResolutionOutOfRange = 4,
    UnsupportedSourceFormat = 5,
    DriverRejected = 6,
    // VSR was requested but the frame's video-processor pass belonged to
    // automatic HDR (TrueHDR owns the pass because its output contract is
    // PQ/BT.2020, which the VSR intermediate cannot represent). VSR's PPE
    // extension may still ride along inside that pass. Tail-appended only, so
    // an older host reports it as an unknown value.
    YieldedToAutoHdr = 7,
};

struct FFF3FPVideoSuperResolutionStatus {
    std::uint32_t size;
    std::uint32_t version; // == 20
    FFF3FPVideoSuperResolution requested;
    // The adapter backing the renderer's D3D11 device is NVIDIA.
    std::uint32_t nvidiaAdapter;
    // VSR was requested and VideoProcessorBlt succeeded on the last frame.
    // IMPORTANT: this means "the driver accepted the request and the blit did
    // not fail" -- NOT a guarantee that the image was perceptibly enhanced.
    // The extension has no reliable capability query, so an accepted request is
    // the strongest signal available (NVIDIA's own status indicator exists for
    // the same reason).
    std::uint32_t active;
    // The driver rejected the request or the blit failed; VSR is latched off
    // until the host issues a fresh Auto request (which re-probes).
    std::uint32_t driverRejected;
    FFF3FPVideoSuperResolutionReason reason;
    std::uint32_t sourceWidth;
    std::uint32_t sourceHeight;
    std::uint32_t targetWidth;
    std::uint32_t targetHeight;
};

// NVIDIA RTX Video HDR ("auto HDR", driver-internal name TrueHDR). Like VSR
// this is a vendor-private stream extension on the standard
// ID3D11VideoProcessor, reached through a second NVIDIA GUID; unlike VSR it
// has a documented capability probe (VideoProcessorGetStreamExtension reads
// back 1). GUID, payload and probe appear byte-for-byte identical across
// Chromium and mpv:
//
//   Chromium  ui/gl/swap_chain_presenter.cc   ToggleNvidiaVpTrueHDR() /
//                                             NvidiaDriverSupportsTrueHDR()
//   mpv       video/filter/vf_d3d11vpp.c      enable_nvidia_true_hdr() /
//                                             supports_nvidia_true_hdr()
//
// Scope: the extension converts an SDR stream to HDR10 (BT.2020 primaries, PQ
// transfer) inside VideoProcessorBlt. Verified constraints shared by both
// implementations:
//   * SDR input only -- an HDR source is refused ("This filter only converts
//     SDR to HDR", mpv 5abce15f1).
//   * The output must be re-tagged and carried on a 10-bit RGB surface
//     (R10G10B10A2_UNORM, HDR10). mpv 0.41.0 shipped the trap: it enabled the
//     extension but never retagged, and the frames came out identical
//     (byte-identical screenshots). The renderer therefore only engages this
//     path when it already presents the scRGB/10-bit HDR output contract, and
//     feeds the PQ surface into the normal HDR colour pipeline.
//
// Like VSR: a switch, NOT a default; Off keeps the renderer byte-identical to
// the pre-enhancement behaviour, and the renderer refuses to enable it while a
// comparison-style mode is active.
enum class FFF3FPVideoAutoHdr : std::uint32_t {
    // Default. SDR frames take the classic path exactly as before.
    Off = 0,
    // Attempt the driver's SDR->HDR conversion whenever the source and the
    // output path satisfy the conditions above.
    Auto = 1,
};

// Why the last presented frame did or did not use the driver's auto HDR.
// Reported so a host can explain the state without guessing; 0 means "used"
// when active is 1.
enum class FFF3FPVideoAutoHdrReason : std::uint32_t {
    None = 0,
    NotRequested = 1,
    NotNvidiaAdapter = 2,
    NotSupportedByDriver = 3,
    NotSdrSource = 4,
    OutputPathNotHdr = 5,
    UnsupportedSourceFormat = 6,
    DriverRejected = 7,
};

struct FFF3FPVideoAutoHdrStatus {
    std::uint32_t size;
    std::uint32_t version; // == 20
    FFF3FPVideoAutoHdr requested;
    // The adapter backing the renderer's D3D11 device is NVIDIA.
    std::uint32_t nvidiaAdapter;
    // The extension was requested, the driver's capability probe read back 1,
    // and VideoProcessorBlt succeeded with the HDR output contract in force on
    // the last frame. Stronger than VSR's bit because the probe exists, but it
    // still is not a perceptual guarantee: the generated HDR is the driver's
    // and its quality level belongs to the NVIDIA App.
    std::uint32_t active;
    // The driver rejected the request or the blit failed; latched off until the
    // host issues a fresh Auto request (which re-probes).
    std::uint32_t driverRejected;
    FFF3FPVideoAutoHdrReason reason;
    // Geometry of the last frame's video-processor pass (0 when inactive).
    std::uint32_t sourceWidth;
    std::uint32_t sourceHeight;
    std::uint32_t outputWidth;
    std::uint32_t outputHeight;
};

#ifdef FFFNATIVE_EXPORTS
#define FFF3FP_API extern "C" __declspec(dllexport)
#else
#define FFF3FP_API extern "C" __declspec(dllimport)
#endif

FFF3FP_API std::uint32_t FFF3FP_GetApiVersion() noexcept;
// DEPRECATED ABI (symbols retained, capability removed): the private
// color-extension module these drove no longer exists. They now report
// "absent" / "unsupported" -- see PlayerApi.cpp.
FFF3FP_API std::int32_t FFF3FP_GetColorExtensionStatus() noexcept;
FFF3FP_API const char* FFF3FP_GetColorExtensionStatusText(std::uint32_t state, std::uint32_t variant) noexcept;
FFF3FP_API void FFF3FP_SetColorExtensionAuthorizationPrompt(int (__cdecl* callback)(char*, std::uint32_t)) noexcept;
FFF3FP_API FFFResult FFF3FP_AuthenticateColorExtension(const char* codeUtf8) noexcept;
// Install the process-wide native log sink.
FFF3FP_API void FFF3FP_SetLogCallback(FFF3FPLogCallback callback, void* context) noexcept;
FFF3FP_API FFFResult FFF3FP_Create(const FFF3FPConfiguration* configuration,
    FFF3FPHandle* player) noexcept;
FFF3FP_API FFFResult FFF3FP_Open(FFF3FPHandle player, const char* localPathUtf8) noexcept;
// Configure before Open. Appended export: existing ABI layouts stay unchanged.
// Profiles: 0 balanced, 1 more, 2 stress. Decode and render adapters are independent.
FFF3FP_API FFFResult FFF3FP_ConfigurePlaybackPerformance(FFF3FPHandle player,
    std::int32_t decodeAdapterIndex, std::uint32_t videoBufferPolicy,
    std::uint32_t audioBufferPolicy) noexcept;
FFF3FP_API FFFResult FFF3FP_DiscNavigate(FFF3FPHandle player, int command, int value, int y) noexcept;
FFF3FP_API FFFResult FFF3FP_CopySdrFrame(FFF3FPHandle player, void* pixels, std::uint32_t capacity,
    std::uint32_t* width, std::uint32_t* height, std::uint32_t discOnly) noexcept;
FFF3FP_API FFFResult FFF3FP_GetDiscStatus(FFF3FPHandle player, char* output, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API FFFResult FFF3FP_Play(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Pause(FFF3FPHandle player) noexcept;
// Synchronously stops the current audio renderer and discards already-submitted
// endpoint buffers. Intended for media/session replacement, not normal pause.
FFF3FP_API FFFResult FFF3FP_DiscardAudioOutput(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Stop(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Close(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_Seek(FFF3FPHandle player, std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekKeyframe(FFF3FPHandle player, std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekFrame(FFF3FPHandle player, std::int64_t frameIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_StepFrame(FFF3FPHandle player, std::int32_t direction) noexcept;
FFF3FP_API FFFResult FFF3FP_StepKeyframe(FFF3FPHandle player, std::int32_t direction) noexcept;
FFF3FP_API FFFResult FFF3FP_SelectVideoStream(FFF3FPHandle player,
    std::int32_t streamIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_SelectAudioStream(FFF3FPHandle player,
    std::int32_t streamIndex) noexcept;
FFF3FP_API FFFResult FFF3FP_LoadExternalAudio(FFF3FPHandle player,
    const char* localPathUtf8, std::int32_t streamIndex, std::int64_t offset100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_ClearExternalAudio(FFF3FPHandle player) noexcept;
FFF3FP_API FFFResult FFF3FP_SetExternalAudioOffset(FFF3FPHandle player,
    std::int64_t offset100ns) noexcept;
// Audio delay for the embedded audio track: positive = audio plays later than the
// video (lip-sync correction). Bounded to ±10 minutes. Session-level preference
// (persists across media); see PlayerSession::SetAudioDelay for the mechanics.
FFF3FP_API FFFResult FFF3FP_SetAudioDelay(FFF3FPHandle player,
    std::int64_t delay100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_SetColorMode(FFF3FPHandle player, FFF3FPColorMode mode,
    float sdrPeakNits, float hdrPeakNits, float sdrPaperWhiteNits,
    std::uint32_t forceHdrOutput) noexcept;
// Present pacing (VRR / G-SYNC / FreeSync low-latency path):
// enableTearing = 1 selects Present(0, DXGI_PRESENT_ALLOW_TEARING) so a VRR display
// can scan out on its own schedule; 0 keeps the default vsync-locked Present(1, 0).
// Swap chains are created with the ALLOW_TEARING capability flag whenever the OS
// reports support, so toggling does not require chain recreation. The request is a
// preference: it is remembered across device/chain re-creation, and where the chain
// was created without the ALLOW_TEARING flag (the OS reports no support) the renderer
// simply keeps the vsync-locked path. Only enableTearing > 1 is rejected
// (InvalidArgument); a successful call does not by itself prove tearing will happen.
FFF3FP_API FFFResult FFF3FP_SetPresentConfig(FFF3FPHandle player,
    std::uint32_t enableTearing) noexcept;
// The window's flip-rate bound is automatic and not configurable:
//     bound = min(display refresh rate, video frame rate)
// Expressed as a period that is the longer of the two, so 60 fps content on a 144 Hz panel
// flips at most 60 times a second -- never 144 re-scan-outs of the same frame -- while
// 240 fps content on the same panel flips at most 144 and the surplus is discarded upstream
// by the session's existing lateness rule. There is deliberately no setter: a configurable
// ceiling would be a switch for a behaviour that is always on.
//
// FFF3FP_GetPresentPacingStatus below is the read-only view of what the presenter actually
// paces against. Read-only diagnostics matter here because the cadence lock is invisible to
// the eye when it works and invisible when it does not; the two numbers that decide its
// behaviour (the panel refresh rate, and whether the grid has been calibrated yet) were
// previously only assertable from inside the renderer.
struct FFF3FPPresentPacingStatus {
    std::uint32_t size;
    std::uint32_t version; // == PlayerApiVersion
    // EnumDisplaySettings for the monitor that hosts the output window. Reported here
    // un-clamped; the renderer used to clamp this to 120 before a 2026-10-15 fix, so a
    // 144 Hz panel was described as 120 Hz to whatever consumed it. It is the declared
    // panel rate only: a timed-text layer's targetFrameRate does not raise it.
    float declaredRefreshHz;
    // Panel period recovered from the swap chain's own frame statistics
    // (IDXGISwapChain::GetFrameStatistics: SyncRefreshCount against SyncQPCTime).
    // 0 until two samples have arrived, which is what "not locked yet" means here.
    float measuredRefreshHz;
    // The bound in force: min(panel rate, content rate), raised to a visible timed-text
    // layer's target frame rate but never above the declared panel rate. The adaptive
    // pace floor can lower it further while the pipeline is behind the media clock.
    float frameRateBoundHz;
    std::uint32_t gridLocked;       // 1 = flips are snapped to the refresh grid
    // Why the grid is not in charge when gridLocked is 0:
    //   0 = locked, or there was nothing to pace against
    //   1 = variable-refresh path requested (the display paces itself; snapping to a fixed
    //       grid would only add latency)
    //   2 = the grid is not calibrated yet (needs two frame-statistics samples)
    //   3 = the native window-move loop owns the cadence for this flip
    std::uint32_t pacingBypass;
    // Signed error of the last flip's submit against the instant it was told to submit at
    // (its slot minus the lead), in 100 ns units. Positive = submitted late.
    // Scored against the slot itself instead, this reports the lead as though it were
    // accuracy: measured that way it produced a suspiciously steady "worst 0.35 refresh
    // periods", which is just the lead (0.35 x 6.94 ms), and it said nothing about how well
    // the wait actually landed. Measured on this machine at 60 fps content: signed mean +0.12
    // refresh periods, worst absolute 0.34 -- and the lead is 0.35, so the worst case spends
    // the entire headroom and lands on the slot itself. The lead is therefore the thing to
    // raise if a slipped slot is ever observed; this reading says the margin is thin, not that
    // it is negative.
    std::int64_t lastSlotError100ns;
    // The displayed half, counted from PresentRefreshCount (the vblank on which the chain's
    // image reached the monitor). holdSampled counts flips whose on-screen hold was
    // observable, holdWithinRounding those held for either rounding (floor or ceiling) of the
    // refresh count the cadence asked for -- 2 or 3 for 60 fps on 144 Hz -- and
    // holdWorstDeviationRefreshes the largest miss outside that band. A hold inside the band
    // is what a steady cadence looks like; one outside it (1 refresh, or 4 and up) is what
    // the eye calls a hitch. Submit accuracy alone does not prove either.
    std::uint64_t holdSampled;
    std::uint64_t holdWithinRounding;
    std::uint64_t holdWorstDeviationRefreshes;
    // Frames discarded because the source delivers faster than the panel can scan out
    // (240 fps content on a 144 Hz panel). Deliberately NOT folded into the snapshot's
    // droppedVideoFrames: that reading means "the pipeline fell behind the media clock" and
    // drives the adaptive-downscale policy, and a panel-limited discard is neither of those
    // things -- no amount of resampling lets a 144 Hz panel show 240 frames a second.
    // Report the two numbers side by side; the total is their sum.
    std::uint64_t displayRateDiscardedFrames;
    // Flips that were handed to Present() but whose image never reached the monitor, because
    // a newer flip replaced them before a vblank sampled the chain. Appended after the field
    // above: this struct is size-checked by the kernel (a caller declaring a smaller size gets
    // InvalidArgument, never a short write), so a host built against the older header keeps
    // loading this DLL but loses this one call until it is rebuilt against the new one.
    // It is what makes the two hold readings above interpretable: a hold spanning k flips is
    // legitimate when k-1 of them are in here, and a hitch when this counter says otherwise.
    std::uint64_t neverCompositedPresents;
};
FFF3FP_API FFFResult FFF3FP_GetPresentPacingStatus(FFF3FPHandle player,
    FFF3FPPresentPacingStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_SetOutputWindow(FFF3FPHandle player, void* outputWindow) noexcept;
FFF3FP_API FFFResult FFF3FP_SetInteractiveMove(FFF3FPHandle player, std::uint32_t enabled) noexcept;
// View transform for frame inspection: zoom scales the fitted video box
// (1.0 = fit, >1 = magnify), panX/panY are normalized offsets in [-1,1]
// relative to the unzoomed box.
//
// pan is positive = the picture moves right/down, so it follows the pointer: a drag to
// the right should be sent as increasing panX, and the image travels with the cursor.
// That is the convention scroll views use, and what lakeUI's PixelPictureBox does
// (_scrollX = start - dx). This sign was inverted here until it was measured; a host
// written against the old behaviour should negate its pan values.
FFF3FP_API FFFResult FFF3FP_SetViewTransform(FFF3FPHandle player,
    float zoom, float panX, float panY) noexcept;
// 静态图片的缩放范围为初始拟合尺寸的 1..32 倍。
// Cursor-anchored zoom. Multiplies the current zoom by `factor` (use 1.25 to step in and
// 0.8 to step out, matching lakeUI's PixelPictureBox) while keeping whatever is under the
// given point fixed. anchorX/anchorY are normalised to [0,1] across the client area, so a
// host can pass the wheel position directly without knowing the fit geometry.
// Appended export: hosts that never call it are unaffected.
// 静态图片的 resultingZoom 按源像素计（1.0 为原生尺寸）；其他媒体按拟合区域计。
FFF3FP_API FFFResult FFF3FP_ZoomViewAt(FFF3FPHandle player,
    float factor, float anchorX, float anchorY, float* resultingZoom) noexcept;
// 图片拖动的客户区像素增量，使用内核当前缩放及平移状态。
FFF3FP_API FFFResult FFF3FP_PanView(FFF3FPHandle player, float deltaX, float deltaY) noexcept;
// 返回图片当前倍率及浮动边界，均以实际源像素为基准。
FFF3FP_API FFFResult FFF3FP_GetImageZoom(FFF3FPHandle player,
    float* zoom, float* minimum, float* maximum) noexcept;
// enable == 0 keeps the behavior above. enable == 1 caps the fitted box
// at the source's native size, which turns zoom into an absolute screen:video pixel
// ratio (zoom 1 = pixel-exact 1:1 instead of fit-to-window, and larger windows show
// the whole frame at native size with letterboxing). Returns NotSupported while a
// disc is open, because that renderer owns its own geometry.
FFF3FP_API FFFResult FFF3FP_SetFitLimitToNative(FFF3FPHandle player,
    std::uint32_t enable) noexcept;
// Equirectangular 360-degree video projection. yaw/pitch/fovY use degrees;
// pitch is clamped by the renderer so the horizon never rolls or flips.
FFF3FP_API FFFResult FFF3FP_Set360View(FFF3FPHandle player,
    std::uint32_t enabled, float yaw, float pitch, float fovY) noexcept;
FFF3FP_API FFFResult FFF3FP_SetAudioEndpoint(FFF3FPHandle player,
    const char* endpointIdUtf8) noexcept;
// Recreates only the WASAPI renderer.  The media session and its selected
// streams stay intact; playback resumes at the current media position.
FFF3FP_API FFFResult FFF3FP_SetAudioExclusiveMode(FFF3FPHandle player,
    std::uint32_t exclusive) noexcept;
FFF3FP_API FFFResult FFF3FP_SetVolume(FFF3FPHandle player, float volume, std::uint32_t muted) noexcept;
FFF3FP_API FFFResult FFF3FP_SetTimedTextLayer(FFF3FPHandle player,
    const FFF3FPTimedTextLayer* layer) noexcept;
FFF3FP_API FFFResult FFF3FP_GetSnapshot(FFF3FPHandle player, FFF3FPSnapshot* snapshot) noexcept;
// HDR presentation diagnostics: whether the ST 2094 curve actually shaped the drawn
// frame, and whether/what per-frame HDR metadata reached the swap chain. Counters are
// process-lifetime for this session, not reset per seek.
FFF3FP_API FFFResult FFF3FP_GetHdrSignalDiagnostics(FFF3FPHandle player,
    FFF3FPHdrSignalDiagnostics* diagnostics) noexcept;
FFF3FP_API FFFResult FFF3FP_ReadVideoPixel(FFF3FPHandle player,
    FFF3FPVideoPixelProbe* probe) noexcept;
// Batch pixel readback. Samples a rectangular region
// of the presented frame in ONE staging copy + Map instead of one GPU round
// trip per pixel. dst receives w*h RGBA32F samples (row-major, premultiplied
// order R,G,B,A), normalized exactly like FFF3FPVideoPixelProbe fields.
// Returns the actual output bit depth via outputBitDepth.
FFF3FP_API FFFResult FFF3FP_ReadVideoPixelRegion(FFF3FPHandle player,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    float* dst, std::uint32_t dstFloatCount,
    std::uint32_t* outputBitDepth) noexcept;
FFF3FP_API FFFResult FFF3FP_GetAudioPeakLevels(FFF3FPHandle player,
    FFF3FPAudioPeakLevels* levels) noexcept;
FFF3FP_API FFFResult FFF3FP_GetTimedTextStatus(FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_GetDanmakuStatus(FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_GetLyricsStatus(FFF3FPHandle player,
    FFF3FPTimedTextStatus* status) noexcept;
FFF3FP_API FFFResult FFF3FP_EvaluateColorTransform(FFF3FPColorTransform* transform) noexcept;
FFF3FP_API FFFResult FFF3FP_EvaluateHdrProcessing(
    FFF3FPHdrProcessingProbe* probe) noexcept;
FFF3FP_API FFFResult FFF3FP_EvaluateTimedTextRasterization(
    FFF3FPTimedTextRasterizationProbe* probe) noexcept;
FFF3FP_API FFFResult FFF3FP_MeasureTimedText(const char* textUtf8,
    const char* fontFamilyUtf8, float fontSize, FFF3FPTimedTextFlags flags,
    float maxWidth, float outlineWidth, float shadowOffsetX, float shadowOffsetY,
    std::uint32_t shadowEnabled, FFF3FPTimedTextMeasurement* measurement) noexcept;
// Returns the natural single-line DirectWrite width used by text commands.
FFF3FP_API FFFResult FFF3FP_MeasureTimedTextWidth(const char* textUtf8,
    const char* fontFamilyUtf8, float fontSize, FFF3FPTimedTextFlags flags,
    float* width) noexcept;
FFF3FP_API FFFResult FFF3FP_GetMediaInfo(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;

// Image information for picture viewing. Fields the source does not carry are
// reported as "unknown" (negative, or 0xFFFFFFFF for rotation) rather than 0,
// so callers must handle unknown explicitly instead of reading a default.
struct FFF3FPImageInfo {
    std::uint32_t size;                  // sizeof(FFF3FPImageInfo)
    std::uint32_t version;               // 20
    std::int32_t  frameCount;            // total frames; <0 unknown
    std::int32_t  loopCount;             // 0 = infinite, -1 = not looping, >0 = count; <0 unknown
    std::uint32_t flags;                 // FFF3FP_IMAGE_FLAG_*
    std::uint32_t rotationQuarterTurns;  // 0..3 from EXIF orientation; 0xFFFFFFFF unknown
    std::int32_t  sourcePixelFormat;     // AVPixelFormat as reported by FFmpeg
    std::uint32_t sourceBitDepth;
    std::uint32_t iccProfileSizeBytes;   // 0 when absent
    std::int32_t  colorPrimaries;        // AVColorPrimaries; <0 unknown
    std::int32_t  colorSpace;            // AVColorSpace; <0 unknown
    std::int32_t  colorTransfer;         // AVColorTransfer; <0 unknown
    std::uint32_t reserved[4];
};

#define FFF3FP_IMAGE_FLAG_STATIC       0x1u
#define FFF3FP_IMAGE_FLAG_ANIMATED     0x2u
#define FFF3FP_IMAGE_FLAG_MULTI_FRAME  0x4u
#define FFF3FP_IMAGE_FLAG_HAS_ALPHA    0x8u
#define FFF3FP_IMAGE_FLAG_HAS_ICC      0x10u
#define FFF3FP_IMAGE_FLAG_HAS_ROTATION 0x20u
// Source primaries wider than Rec.709 (BT.2020, DCI-P3, Display P3). Displaying
// these without clipping requires the scRGB (HDR) output path; an SDR swap
// chain cannot hold colours outside Rec.709.
#define FFF3FP_IMAGE_FLAG_WIDE_GAMUT   0x40u

// Report what a still or animated image actually contains: frame count, EXIF
// rotation, pixel format / bit depth and whether an ICC profile is embedded.
// Returns NotSupported on a native build predating this entry point, so hosts
// degrade instead of failing.
FFF3FP_API FFFResult FFF3FP_GetImageInfo(FFF3FPHandle player, FFF3FPImageInfo* info) noexcept;
// Probes the subtitle streams of a container file without opening a playback
// session. The JSON output reuses the stream shape of FFF3FP_GetMediaInfo and
// adds a top-level startTimeSeconds (containers may carry a nonzero start
// time; a standalone .mks has no reference stream to normalize against).
// Success with an empty streams array means the file opened but has no
// subtitle stream.
FFF3FP_API FFFResult FFF3FP_ProbeSubtitleStreams(const char* localPathUtf8,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API FFFResult FFF3FP_GetLastError(FFF3FPHandle player, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
// Render-target diagnostics. RenderTargetInfo reports the
// current swapchain/client/destination sizes (for App-side overlay positioning
// and pixel-probe coordinate mapping).
// Returns InvalidState when no swapchain exists yet (e.g. before the first
// frame is presented); the contents of *info are then unspecified and must not
// be interpreted as a 0x0 target.
struct FFF3FPRenderTargetInfo {
    std::uint32_t size;
    std::uint32_t version; // == 20
    std::uint32_t swapWidth;
    std::uint32_t swapHeight;
    std::uint32_t clientWidth;
    std::uint32_t clientHeight;
    std::int32_t destX;
    std::int32_t destY;
    std::uint32_t destWidth;
    std::uint32_t destHeight;
    std::uint32_t outputBitDepth;
    std::uint32_t hdr;
};
FFF3FP_API FFFResult FFF3FP_GetRenderTargetInfo(FFF3FPHandle player,
    FFF3FPRenderTargetInfo* info) noexcept;
// Re-present the last cached frame on the presenter thread.
// The host calls it after a child HWND resize so flips keep issuing while
// ResizeBuffers stays on the presenter.
FFF3FP_API FFFResult FFF3FP_Redraw(FFF3FPHandle player) noexcept;

// ---- ST 2094-40 dynamic metadata injection ---------------------------------
//
// Why this exists
// ---------------
// HDR10+ metadata normally arrives inside the bitstream, so it can only be
// exercised with content that actually carries it -- and real HDR10+ material
// is scarce, while the FATE test vector is a synthetic gradient whose curve is
// near-identity and therefore barely exercises the mapping path at all. A host
// (or a test harness) that can hand the renderer metadata directly makes the
// dynamic path testable against arbitrary scenes, and lets a tool drive the
// tone mapping for grading/verification work.
//
// Semantics
// ---------
// Injected metadata replaces whatever the bitstream provided for the frames it
// covers. Values use the same units as the parsed ST 2094-40 payload:
//
//   maxScl           linearized RGB maximum, 0..1 (nits = value * 10000)
//   averageMaxRgb    linearized RGB average, 0..1
//   kneePointX/Y     0..1; kneePointX may be 0 for "no linear segment"
//   curveAnchors     numAnchors intermediate anchor values, ascending, 0..1
//   targetedDisplayNits  the content's targeted display peak, in cd/m^2
//
// Each entry carries up to three processing windows (ST 2094-40's range), so a
// multi-window stream can be described as well as a single full-frame one. A
// window whose toneMappingFlag is 0 bounds its own peak but supplies no curve;
// if any window lacks a usable curve the whole entry falls back to the static
// path, because mapping only part of the frame would look worse than not
// mapping it at all.
//
// `frameIndex` selects the range the entry applies to. Entries are matched by
// the frame's own index (FFF3FPSnapshot::frameIndex), and the last entry whose
// frameIndex is <= the current frame wins, so a host can post a sparse list of
// scene changes rather than one entry per frame.
// The key is that published index, never the packet pts; see "Matching key".
// Injection is opt-in and reversible: with no entries active, or after
// FFF3FP_ClearHdrDynamicMetadata, the renderer behaves exactly as if the
// feature did not exist. A malformed entry is rejected rather than applied.

enum class FFF3FPHdrMetadataSource : std::uint32_t {
    // Whatever the bitstream carried (the default).
    Bitstream = 0,
    // An injected entry is driving the mapping for this frame.
    Injected = 1,
};

// One ST 2094-40 processing window. Coordinates are normalised to the picture
// (0,0 = top-left, 1,1 = bottom-right), matching the bitstream representation.
//
// ST 2094-40 allows num_windows in [1,3]; the common shapes are one full-frame
// window, or three side-by-side regions each carrying its own curve. Windows are
// independent: adjacent windows are mastered separately, so the renderer blends
// across a narrow band at their borders rather than switching at a hard edge.
struct FFF3FPHdrMetadataWindow {
    float upperLeftX;         // 0..1
    float upperLeftY;         // 0..1
    float lowerRightX;        // 0..1, > upperLeftX
    float lowerRightY;        // 0..1, > upperLeftY

    float maxScl[3];          // linearized RGB maxima, 0..1
    float averageMaxRgb;      // linearized RGB average, 0..1
    float fractionBrightPixels; // 0..1, informational

    // Tone-mapping function, ST 2094-40 shape. When toneMappingFlag is 0 the
    // curve fields are ignored and the window only bounds its own peak.
    std::uint32_t toneMappingFlag;
    float kneePointX;         // 0..1, 0 = no linear segment
    float kneePointY;         // 0..1
    std::uint32_t numCurveAnchors; // 0..15
    float curveAnchors[15];   // ascending, 0..1
};

struct FFF3FPHdrDynamicMetadataEntry {
    std::uint32_t size;       // sizeof(FFF3FPHdrDynamicMetadataEntry)
    std::uint32_t version;    // must be 1
    // First frame (inclusive) this entry applies to; see the matching rule above.
    std::int64_t frameIndex;

    // Number of windows this entry describes, 1..3 (ST 2094-40's range).
    // windows[0] must span the full frame when numWindows == 1.
    std::uint32_t numWindows;
    std::uint32_t reserved;
    FFF3FPHdrMetadataWindow windows[3];

    // Targeted system display peak in cd/m^2 (0 = derive from the window peaks).
    float targetedDisplayNits;
};

// Replaces the injected table. entries == nullptr with count == 0 clears it.
// Entries are copied; the caller keeps ownership of the array.
// Pinned for the same reason as the payload structs at the
// foot of this header -- hosts mirror these by Marshal offsets (the C# side
// hand-flattens the 28 floats of one window), so a drift must fail at compile
// time instead of reading fields at wrong offsets behind a passing size gate.
// New fields append; a reorder means a new version.
static_assert(sizeof(FFF3FPHdrMetadataWindow) == 112,
    "FFF3FPHdrMetadataWindow layout changed: mirror it in Fff3FpNative.cs and re-pin");
static_assert(sizeof(FFF3FPHdrDynamicMetadataEntry) == 368,
    "FFF3FPHdrDynamicMetadataEntry layout changed: mirror it in Fff3FpNative.cs and re-pin");

FFF3FP_API FFFResult FFF3FP_SetHdrDynamicMetadata(FFF3FPHandle player,
    const FFF3FPHdrDynamicMetadataEntry* entries, std::uint32_t count) noexcept;

// Drops injected metadata, returning the session to bitstream-only behaviour.
FFF3FP_API FFFResult FFF3FP_ClearHdrDynamicMetadata(FFF3FPHandle player) noexcept;

// Reports whether an injected entry owns the dynamic state *now*: a live processor
// flag that flips when the table is installed or cleared, not a record of the last
// rendered frame. It is not by itself proof that the picture changed -- see
// "Visibility of an install or a clear" below for what a host must check instead.
FFF3FP_API FFFResult FFF3FP_GetHdrMetadataSource(FFF3FPHandle player,
    std::uint32_t* source) noexcept;

// ---- Contract details --------------------------------------------------------
//
// Pinned to measured behaviour so the three entry points above have one
// unambiguous reading. Descriptive only: it changes nothing in the kernel.
//
// Matching key
// ............
// An entry's frameIndex is compared against the value the session publishes as
// FFF3FPSnapshot::frameIndex -- the session's own frame counter -- walking a table
// the kernel sorts ascending by frameIndex when the call is accepted. The packet
// pts is never that key (snapshot.framePts is a different number and is not the
// lookup index): at a 1/15360 time base and 60 fps, frame 4 already carries pts
// 1024, so keying on pts resolved every sparse scene list to its last entry.
// Worked example, three tables (targeted 111 / 222 / 333) on one stream: keyed on
// pts every frame reads 333; keyed on frameIndex, a table posted at 10/17/24,
// 30/37/43/50/56 and 60+ reads 111 on frames 10..29, 222 on 30..59 and 333 from 60
// on -- exactly the "last entry whose frameIndex <= current frame" rule. Frames
// below the first entry are not a hole in the contract: they get the first entry.
// A frame the session cannot index at all falls back to pts, which is a diagnostic
// convenience, not something to design against: to get deterministic keys a host
// must read snapshot.frameIndex and post those numbers.
//
// Visibility of an install or a clear
// ...................................
// The calling thread validates the table as a whole (a malformed entry returns
// InvalidArgument and leaves the previous table in charge; entries == nullptr with
// count == 0 clears it; entries are copied, the caller keeps its array) and hands
// it to the HDR processor, which re-evaluates the state it holds for the frame
// already on screen. The diagnostics therefore move with the call even while the
// session is paused or at end-of-stream, with one timing caveat: the snapshot's ST
// 2094 fields are refreshed by work the call queues onto the session thread, so a
// reader that races the call can still see the previous frame's numbers, whereas
// FFF3FP_GetHdrMetadataSource reads the processor live and flips immediately.
// The *picture* is not repainted from the calling thread. The call marks the HDR
// shader inputs stale and asks the presenter to re-present the cached frame; the
// draw path consumes that mark, refreshing the tone-mapping constants and the
// curve LUT texture immediately before it draws. Nothing touches the device on the
// API caller's stack, so the refresh is serialised with the draw by construction
// and cannot race the presentation pump against the same immediate context.
// Two consequences a host must design for:
//   · The visible half needs a cached frame and an output window. A headless
//     session, or one that has never rendered a frame, has nothing to re-present:
//     only the diagnostics move, and the picture changes with the next frame the
//     renderer caches.
//   · Judge "my injection took effect" on pixels, not on the source flag alone.
//     On a paused session with a cached frame, installing a table moves the sampled
//     points and clearing returns them to their pre-install values. Pure black stays
//     black -- no curve moves it -- so "not every sampled point moved" is expected,
//     whereas "the flag says Injected and no
//     sampled pixel moved" means the present path never ran, not that the curve is
//     identity.
//
// Peak ownership when the entry carries no usable maxscl
// ......................................................
// An active entry owns the frame's dynamic state: its windows and curves, and
// with them the processing path (Hdr10PlusDynamic), the dynamicMetadataActive
// flag and the Hdr10 compatibility bit. It deliberately does NOT own the
// reported format: snapshot.hdrFormat keeps reporting the samples' own
// classification (Hdr10 for an HDR10/PQ stream); only the bitstream's own
// HDR10+ classification sets Hdr10Plus, because overwriting the format made
// the decode transfer function and the gamut switch read a signal the pixels
// are not. The source peak follows a strict precedence and never mixes the
// two sources:
//   1. the entry's own peak -- max(maxScl) across windows[0]'s three channels,
//      converted to nits as value * 10000 -- whenever that is a valid peak;
//   2. otherwise the stream-level baseline -- the peak the processor derived from
//      its codec parameters at open time (stream configuration, HDR10+ side data,
//      color_trc) plus the stream's static mastering metadata. For real shipped
//      HDR10/HDR10+ material this is normally the static
//      targeted_system_display_maximum_luminance.
// Never -- in either branch -- the dynamic peak this frame's bitstream parse
// produced. A curve from the injection with a peak from this frame's SEI would map
// one frame with two sources at once. With a stream whose mastering metadata reads
// 1000/1 and an entry carrying no valid maxscl, snapshot
// .sourcePeakNits stays pinned at 1000 for every frame while the bitstream peak it
// would otherwise have followed moves 322/328/308/347/331/350/363, and windows,
// targeted value and curve all follow the entry. The per-frame path then clamps
// whatever the two branches resolved into [max(1 nit, paper white), 10000 nits];
// maxScl is itself validated to [0,1], so an injected peak cannot exceed 10000.
// So: GetHdrMetadataSource == Injected does NOT imply the peak came from the
// injected entry. It implies the curve, the windows, the targeted value and the
// reported processing path do. Peak provenance is guaranteed only when the entry
// carries a valid maxScl; a host that needs to tell the two cases apart compares
// snapshot.sourcePeakNits against maxScl * 10000 of the entry in range (or samples
// the picture and finds the peak it can actually attribute).

// ---- Generic dynamic-metadata injection carrier ------------------------------
//
// A purpose-agnostic extension of the ST 2094-40 injection above: one channel
// that carries ANY per-frame dynamic metadata kind the kernel knows, keyed and
// applied exactly like the HDR10+ table (same matching rule, same rejection
// semantics, same diagnostics), so a host learns one interface instead of one
// per metadata family. The channel itself prescribes nothing: kinds are a
// registry that only grows, and a kernel that does not know a kind refuses it
// cleanly instead of guessing.
//
// Kinds
// .....
// Hdr10Plus (1): payload is one FFF3FPHdrDynamicMetadataEntry (version 1) --
//   byte-identical to what FFF3FP_SetHdrDynamicMetadata takes per entry, so a
//   host can move between the two channels without re-encoding. Validated and
//   applied by exactly the code path the dedicated entry point uses.
// HdrVivid (2): payload is one FFF3FPHdrVividMetadataPayload (version 1) --
//   ST 2094-30 / CUVA 005-style per-window peaks. The kernel applies Vivid
//   with peak-only semantics (the same reading it gives bitstream Vivid),
//   because CUVA 005.1's parametric curve is not publicly documented; the
//   curve fields the ST 2094-40 entry carries have no Vivid counterpart.
//
// Packet and table semantics
// ..........................
// One successful call REPLACES the injected table wholesale (all kinds);
// packets are copied and the caller keeps its memory. Packets are validated
// as a whole before anything is installed: any malformed packet (bad
// size/version, bad payload, unknown kind) fails the entire call --
// InvalidArgument for shape errors, NotSupported when the kind is unknown to
// this kernel -- and leaves the previous table in charge. Frame matching,
// lazy install before the first indexed frame, paused/EOS application and
// the peak-ownership precedence are the HDR10+ injection's own contract
// (above), now per kind: an HdrVivid entry reports processingPath
// HdrVividDynamic and the HdrVivid compatibility bit, an Hdr10Plus entry
// reports Hdr10PlusDynamic and the Hdr10 bit; neither touches the reported
// format. The two channels share one table: setting through either replaces
// what the other installed.
//
// FFF3FP_ClearDynamicMetadata drops injected metadata: kind 0 clears every
// kind, any other value clears only that kind's entries and re-evaluates the
// remaining table against the frame on screen.
enum class FFF3FPDynamicMetadataKind : std::uint32_t {
    None = 0,        // only meaningful to FFF3FP_ClearDynamicMetadata: every kind
    Hdr10Plus = 1,   // payload: FFF3FPHdrDynamicMetadataEntry, version 1
    HdrVivid = 2,    // payload: FFF3FPHdrVividMetadataPayload, version 1
    // Payload: FFF3FPColorTransformPayload, version 1. A caller-supplied
    // colour transform: one sampled 3-D lookup table plus one linear-light
    // matrix. Every field is generic colour-pipeline vocabulary; the kernel
    // never learns what produced the table.
    ExternalColorTransform = 3,
};

// One ST 2094-30 / CUVA 005-style processing window, peak-only reading. Windows
// are full-frame by definition (the same reading the kernel gives bitstream
// Vivid, whose per-window reading never carries geometry); the kernel derives
// each window's peak as max(maxRGB) * 10000 nits.
struct FFF3FPHdrVividMetadataWindow {
    float maxRGB[3];          // normalized content maxima, 0..1
    float averageMaxRGB;      // normalized average, 0..1
};

struct FFF3FPHdrVividMetadataPayload {
    std::uint32_t size;       // sizeof(FFF3FPHdrVividMetadataPayload)
    std::uint32_t version;    // must be 1
    // Number of windows, 1..3 (mirrors the ST 2094-40 entry's rule).
    std::uint32_t numWindows;
    std::uint32_t reserved;
    FFF3FPHdrVividMetadataWindow windows[3];
    // Targeted system display peak in cd/m^2 (0 = derive from the window peaks).
    float targetedDisplayNits;
};

// One packet of the generic channel. The payload is a single versioned struct
// of `payloadSize` bytes (never a smaller prefix: kindVersion gates the exact
// layout), copied by the kernel on success.
struct FFF3FPDynamicMetadataPacket {
    std::uint32_t size;        // >= sizeof(FFF3FPDynamicMetadataPacket): a larger
                             //     packet is accepted, appended fields stay
                             //     forward-compatible
    std::uint32_t version;     // must be 1
    std::uint32_t kind;        // FFF3FPDynamicMetadataKind
    std::uint32_t kindVersion; // per-kind payload version, currently 1
    // First frame (inclusive) this packet applies to; same matching rule and
    // same key (snapshot.frameIndex) as the HDR10+ injection.
    std::int64_t frameIndex;
    std::uint32_t payloadSize; // EXACTLY sizeof(the kind's payload struct), or 0
                             //     with payload == nullptr. A larger payload is
                             //     rejected, never silently truncated.
    std::uint32_t reserved;    // must be 0
    const void* payload;       // kind-specific struct, caller keeps ownership
};

// Replaces the injected table (all kinds) with these packets. See the carrier
// contract above for validation, rejection and matching semantics.
FFF3FP_API FFFResult FFF3FP_SetDynamicMetadata(FFF3FPHandle player,
    const FFF3FPDynamicMetadataPacket* packets, std::uint32_t count) noexcept;

// Drops injected metadata: kind 0 (FFF3FPDynamicMetadataKind::None) clears
// every kind, any other kind clears only that kind's entries.
FFF3FP_API FFFResult FFF3FP_ClearDynamicMetadata(FFF3FPHandle player,
    std::uint32_t kind) noexcept;

// Pinned so a host-side Marshal.SizeOf drift cannot silently produce structs
// that pass the size gate while reading fields at wrong offsets (same policy
// as FFF3FPHdrSignalDiagnostics above). New kinds extend the kind enum, never
// these layouts; a layout change means a new kindVersion.
static_assert(sizeof(FFF3FPHdrVividMetadataPayload) == 68,
    "FFF3FPHdrVividMetadataPayload layout changed: mirror it in Fff3FpNative.cs and re-pin");
static_assert(sizeof(FFF3FPDynamicMetadataPacket) == 40,
    "FFF3FPDynamicMetadataPacket layout changed: mirror it in Fff3FpNative.cs and re-pin");

// --- External colour transform (kind 3) -------------------------------------
//
// A caller-supplied colour transform carried as pure data: one sampled 3-D
// lookup table over the coded source signal plus one 3x3 matrix applied to the
// linear-light signal after transfer decoding. Nothing in the payload names a
// video format; a 3-D LUT and a working-space matrix are the standard
// vocabulary of colour management (creative grades, display calibration,
// format conversion all speak it), so the same slot serves any producer. The
// kernel consumes the table exactly like it consumes an ST 2094 curve: as
// numbers, through the same frame-indexed injection table.
//
// Why a 3-D table and not three 1-D curves: a per-component 1-D table cannot
// express a transform whose components interact (the general case for any
// cross-channel mapping), while a 3-D table can, with trilinear sampling.
// 33^3 texels keep the payload at ~421 KB and the per-frame upload at a
// fraction of the decoded-frame traffic; a finer grid is a future
// kindVersion, not a layout change.
inline constexpr std::uint32_t kColorTransformLutResolution = 33;

struct FFF3FPColorTransformPayload {
    std::uint32_t size;         // sizeof(FFF3FPColorTransformPayload)
    std::uint32_t version;      // must be 1 (== packet kindVersion)
    // Grid resolution of curveLut; must equal kColorTransformLutResolution.
    std::uint32_t lutResolution;
    // 0: the table replaces the source's component->RGB conversion -- its
    //    input axes are the three coded source components (Y/Cb/Cr for YUV
    //    sources, R/G/B for RGB ones), its output is coded RGB.
    // 1: the table applies after the kernel's own conversion -- its input and
    //    output are both coded RGB.
    std::uint32_t lutDomain;
    // Applied to the linear-light signal after transfer decoding, before gamut
    // conversion. All-zero = identity. Row-major (out = M * rgb).
    float outputMatrix[9];
    // kColorTransformLutResolution^3 texels, R32G32B32_FLOAT layout, index
    // (z * res + y) * res + x, each texel a float3. Input axes span the coded
    // [0,1] domain at texel centres; sampling is trilinear.
    float curveLut[kColorTransformLutResolution * kColorTransformLutResolution *
        kColorTransformLutResolution * 3];
};

static_assert(sizeof(FFF3FPColorTransformPayload) == 431296,
    "FFF3FPColorTransformPayload layout changed: mirror it in Fff3FpNative.cs and re-pin");


FFF3FP_API void FFF3FP_Destroy(FFF3FPHandle player) noexcept;

FFF3FP_API FFFResult FFF3FP_OpenBitmapSubtitle(const char* localPathUtf8,
    std::int32_t streamIndex, FFF3FPBitmapSubtitleHandle* decoder) noexcept;
FFF3FP_API FFFResult FFF3FP_ReadBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder,
    FFF3FPBitmapSubtitleFrame* frame) noexcept;
FFF3FP_API FFFResult FFF3FP_CopyBitmapSubtitlePixels(FFF3FPBitmapSubtitleHandle decoder,
    void* output, std::uint32_t outputSize) noexcept;
FFF3FP_API FFFResult FFF3FP_SeekBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder,
    std::int64_t position100ns) noexcept;
FFF3FP_API FFFResult FFF3FP_GetBitmapSubtitleLastError(FFF3FPBitmapSubtitleHandle decoder,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_DestroyBitmapSubtitle(FFF3FPBitmapSubtitleHandle decoder) noexcept;

// Renders ASS/SSA directly from libass image masks. Font directories are
// separated by LF; every TTF/OTF/TTC is loaded into this renderer's library.
FFF3FP_API FFFResult FFF3FP_OpenAssSubtitle(const char* localPathUtf8,
    const char* fontDirectoriesUtf8, std::int32_t streamIndex,
    FFF3FPAssSubtitleHandle* renderer) noexcept;
FFF3FP_API FFFResult FFF3FP_RenderAssSubtitle(FFF3FPAssSubtitleHandle renderer,
    std::int64_t position100ns, std::int32_t canvasWidth, std::int32_t canvasHeight,
    FFF3FPBitmapSubtitleFrame* frame) noexcept;
FFF3FP_API FFFResult FFF3FP_CopyAssSubtitlePixels(FFF3FPAssSubtitleHandle renderer,
    void* output, std::uint32_t outputSize) noexcept;
FFF3FP_API FFFResult FFF3FP_GetAssSubtitleLastError(FFF3FPAssSubtitleHandle renderer,
    char* outputUtf8, std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
FFF3FP_API void FFF3FP_DestroyAssSubtitle(FFF3FPAssSubtitleHandle renderer) noexcept;

// --- RTX Video Super Resolution (opt-in switch; see the enum above) ---
//
// These are appended AFTER the v16 surface and deliberately do not change
// FFF3FPConfiguration or PlayerApiVersion: FFF3FP_Create rejects a version
// mismatch outright, so bumping it would break every existing host (the 3FC
// shell included) until it is rebuilt in lockstep. Hosts are expected to
// resolve these entry points dynamically (GetProcAddress) and degrade when
// they are absent, which is how a single host can drive both a pre-VSR kernel
// and this one.
//
// Takes effect from the next presented frame; safe to call during playback.
// Returns InvalidArgument for an out-of-range enum value, and InvalidState
// when the host asks to enable VSR while a comparison-style mode is active.
FFF3FP_API FFFResult FFF3FP_SetVideoSuperResolution(FFF3FPHandle player,
    FFF3FPVideoSuperResolution mode) noexcept;
FFF3FP_API FFFResult FFF3FP_GetVideoSuperResolutionStatus(FFF3FPHandle player,
    FFF3FPVideoSuperResolutionStatus* status) noexcept;

// RTX Video HDR (driver-level "auto HDR" / TrueHDR). Appended entry points
// like the VSR pair above: additive, so PlayerApiVersion is unchanged and a
// host that never resolves them keeps working against this DLL.
FFF3FP_API FFFResult FFF3FP_SetVideoAutoHdr(FFF3FPHandle player,
    FFF3FPVideoAutoHdr mode) noexcept;
FFF3FP_API FFFResult FFF3FP_GetVideoAutoHdrStatus(FFF3FPHandle player,
    FFF3FPVideoAutoHdrStatus* status) noexcept;

// --- Frame readback for screenshots ---
//
// Appended after the v16 surface like the VSR and rotation exports, so
// PlayerApiVersion is unchanged and an existing host keeps working. Resolve
// dynamically (GetProcAddress) and degrade when absent.
//
// Why this exists: the host's old screenshot path captured the *screen*
// (Graphics.CopyFromScreen). That fails whenever the window is occluded or
// minimised, and an HDR frame comes back as DWM's SDR-mapped 8-bit result. This
// renders off-screen instead, so it is independent of window visibility and can
// carry an HDR / wide-gamut frame at full precision.
enum class FFF3FPCopyFrameLayout : std::uint32_t {
    // Source resolution, honouring the view rotation (a rotated photo comes out
    // portrait). This is the "original picture" a user expects from a screenshot.
    Source = 0,
    // The current swap-chain size: what the window is showing right now.
    Window = 1,
};

enum class FFF3FPCopyFrameFormat : std::uint32_t {
    // 32bpp BGRA, 4 bytes per pixel. SDR code values.
    Bgra8 = 0,
    // 16-bit half-float RGBA, 8 bytes per pixel, in the linear scRGB contract
    // (1.0 = 80 nits) -- i.e. the same values the HDR swap chain receives. A host
    // that writes this to a file must map it to PQ/HLG itself; writing the raw
    // numbers as if they were sRGB will look wrong.
    Rgba16Float = 1,
};

// Two-call contract, like FFF3FP_CopyBitmapSubtitlePixels: pass pixels = null to
// learn *width/*height, then call again with a buffer of width*height*bpp bytes.
// Capacity is measured in bytes. Returns BufferTooSmall when capacity is short,
// and writes the required size into *width/*height in both cases.
FFF3FP_API FFFResult FFF3FP_CopyFrame(FFF3FPHandle player, void* pixels,
    std::uint32_t capacity, std::uint32_t* width, std::uint32_t* height,
    FFF3FPCopyFrameLayout layout, FFF3FPCopyFrameFormat format) noexcept;
// Bit depth of the most recent FFF3FP_CopyFrame result: 8 for Bgra8, 16 for
// Rgba16Float. Lets a host label the file it just wrote without re-deriving it.
FFF3FP_API FFFResult FFF3FP_GetLastCopyFrameBitDepth(FFF3FPHandle player,
    std::uint32_t* bitDepth) noexcept;

// --- View rotation (quarter turns clockwise, 0..3) ---
//
// Appended after the v16 surface, like the VSR entry points above, so
// FFF3FPConfiguration and PlayerApiVersion stay untouched and an existing host
// keeps working without a rebuild. Resolve dynamically (GetProcAddress) and
// degrade when absent.
//
// Rotation is a *view* property: it does not re-decode and does not touch the
// pixel format. The renderer rotates the sampling coordinate and swaps the fit
// box's axes, so a portrait photo fills a portrait-shaped box instead of being
// letterboxed. Values above 3 are InvalidArgument. Disc playback has its own
// geometry path and returns Success without changing anything.
FFF3FP_API FFFResult FFF3FP_SetViewRotation(FFF3FPHandle player,
    std::uint32_t quarterTurnsClockwise) noexcept;
// Reads back the current rotation; useful for hosts that cycle R / Shift+R and
// need to keep their UI in step after a device-loss reset.
FFF3FP_API FFFResult FFF3FP_GetViewRotation(FFF3FPHandle player,
    std::uint32_t* quarterTurnsClockwise) noexcept;

// --- Audio delay for the embedded audio track -------------------------------
//
// The entry point is appended after the v16 surface, like the VSR and view-rotation
// exports above: resolve it dynamically (GetProcAddress) and treat it as absent as a
// missing optional feature, not a broken session.
//
// The snapshot field it reports through is not optional, and that is the part with
// teeth: GetSnapshot rejects a caller whose declared size is smaller than
// sizeof(FFF3FPSnapshot), so growing the struct forces every snapshot consumer to be
// rebuilt against this header in lockstep with PlayerApiVersion -- for the same reason
// the previous snapshot growth took a version bump. In this tree that version is 20;
// the bump itself belongs to the version pass, not to this feature. A host built
// against the older header failing with InvalidArgument is the contract working, not a
// defect.
//
// Positive = audio plays later than the video (lip-sync correction). Bounded to
// +/- 10 minutes; outside that bound, or called outside Ready/Playing/Paused/Ended, the
// call is rejected and nothing changes. Session-level preference: DoClose does
// not clear it, so it survives Close/Open. On success the kernel re-seeks the
// current position so the change takes effect from the next audible frame. Frames
// without a timestamp are not shifted, exactly as with the external offset. See
// PlayerSession::SetAudioDelay for the mechanics.
FFF3FP_API FFFResult FFF3FP_SetAudioDelay(FFF3FPHandle player,
    std::int64_t delay100ns) noexcept;
// --- Frame side-data ring for external colour analysis ---
//
// Appended after the v16 surface like the VSR, rotation and readback entry
// points: PlayerApiVersion is unchanged, hosts resolve this dynamically
// (GetProcAddress) and degrade when it is absent.
//
// Why this exists. The kind-3 payload (FFF3FPColorTransformPayload) is a
// table the caller supplies, and the analysis that turns a stream's per-frame
// colour metadata into that table deliberately lives in an external module --
// the kernel carries no format-specific reader. This call is the neutral
// hand-off in the other direction: the session keeps a bounded copy of the
// side data attached to the frames it has already presented, keyed by the same
// frameIndex the injection rule uses, and gives the bytes back untouched. The
// kernel never parses them and never learns what produced them.
//
// sideDataType is an FFmpeg AVFrameSideDataType value carried as a plain
// number, which is what keeps this interface generic: an analyser asks for 24,
// 25 or 17 by number (those are the per-frame colour-metadata types in the
// bundled libavutil -- sidedata_size_probe prints the whole table), and the
// kernel holds every type the same way and interprets none of them.
//
// Semantics. Finds the cached frame with the LOWEST index >= fromFrameIndex
// that carries a blob of `sideDataType`, writes that blob into buffer,
// reports its length through *writtenSize and the frame it came from through
// *foundFrameIndex. A host iterates by setting fromFrameIndex to
// foundFrameIndex + 1 on the next call.
//
// Returns NotFound when nothing in the ring matches (*writtenSize is then 0
// and *foundFrameIndex -1), BufferTooSmall when capacity is short -- the
// required size is still reported through *writtenSize and the entry stays in
// the ring, so the host can retry with a bigger buffer -- and Success for a
// buffer == nullptr probe call, which just reports the size (same two-call
// contract as FFF3FP_CopyBitmapSubtitlePixels). InvalidArgument for a null
// out pointer.
//
// Bounds: the ring holds at most 120 presented frames and 1 MiB of blobs,
// oldest first, and blobs above 16384 bytes are not cached (see the constants
// in PlayerSession.cpp, sized from the measured side-data structs). A host
// that polls slower than the ring wraps can miss a frame's metadata; the
// colour transform it then derives applies one frame late at worst, which is
// the same lag the injection already has for the frame it is derived from --
// the blob for frame N only becomes readable after frame N is presented, so a
// table keyed to N takes effect at N+1.
FFF3FP_API FFFResult FFF3FP_CopyFrameSideDataAfter(FFF3FPHandle player,
    std::uint32_t sideDataType, std::int64_t fromFrameIndex, void* buffer,
    std::uint32_t capacity, std::uint32_t* writtenSize,
    std::int64_t* foundFrameIndex) noexcept;
// --- Stream-level side data for external colour analysis --------------------
//
// Appended after the frame side-data ring above, in the same additive style:
// PlayerApiVersion is unchanged, hosts resolve this dynamically
// (GetProcAddress) and degrade when it is absent.
//
// Why a second entry point instead of a flag on the ring above. Stream-level
// side data uses its own FFmpeg type enumeration (AVPacketSideDataType), a
// DIFFERENT enum from the per-frame AVFrameSideDataType the ring above speaks:
// the two collide on raw numbers, so folding this into
// FFF3FP_CopyFrameSideDataAfter would make one number mean two things
// depending on the call. This export keeps the namespaces apart by
// construction. Nothing about it is format-specific: sideDataType is a plain
// AVPacketSideDataType number carried through, and the kernel interprets none
// of the bytes it hands back.
//
// Why stream-level. A container configuration record (the kind that decides,
// for example, how a dynamic-metadata stream must be processed) belongs to the
// media stream, not to an individual packet or frame: the demuxer declares it
// once when the stream is bound (AVCodecParameters::coded_side_data) and does
// not re-send it per frame. A per-frame ring therefore can never carry it.
//
// Semantics. Returns the FIRST-SEEN cached blob of `sideDataType` from the
// current video stream's coded side data, copied byte-for-byte. The session
// neither parses it nor knows what produced it.
//
// Lifecycle. The cache is filled when a video stream is bound -- open, and
// video-track re-selection -- with first-seen-wins per type. It is dropped
// when that stream is destroyed or rebound (close, re-open, track switch). A
// seek does NOT clear it: the demuxer declares stream side data once and a
// seek does not re-emit it, so clearing on seek would silently lose the
// record for the rest of the session.
//
// Bounds: at most 32 blobs (64 KiB total) are cached; a single blob above
// 16384 bytes is skipped, the same per-blob bound the frame ring uses. These
// are advisory: a record that does not fit costs the host one NotFound
// answer, never a failed open.
//
// Returns NotFound when no cached blob of that type exists (*writtenSize is
// then 0); BufferTooSmall when capacity is short -- the required size is
// still reported through *writtenSize and the entry stays cached, so the host
// can retry with a bigger buffer -- and Success for a buffer == nullptr probe
// call, which just reports the size without writing (same two-call contract
// as the ring above and FFF3FP_CopyBitmapSubtitlePixels). InvalidArgument
// for a null writtenSize pointer.
FFF3FP_API FFFResult FFF3FP_CopyStreamSideData(FFF3FPHandle player,
    std::uint32_t sideDataType, void* buffer, std::uint32_t capacity,
    std::uint32_t* writtenSize) noexcept;

// --- Optional auxiliary video layer for module-driven composition -----------
//
// Appended after the side-data surface in the same additive style:
// PlayerApiVersion is unchanged, hosts resolve this dynamically and degrade to
// exactly today's behaviour when it is absent.
//
// What it is. Some sources carry a second video stream meant to be combined
// with the base layer sample-by-sample before colour processing. This entry
// point lets the host (which owns the out-of-tree module that understands the
// format) ask for that combination without teaching the kernel anything about
// the format: the request carries plain integers -- a residual centre, a right
// shift and a maximum code value -- and the kernel applies
//   combined = clamp(base + roundHalfAwayFromZero((aux - centre) / 2^shift),
//                    0, max)
// to the coded samples before the kind-3 table is sampled. The module derives
// those numbers; the kernel only does generic integer arithmetic.
//
// Component order. v1 composes (luma, chroma U, chroma V) into the source's
// three coded lanes in that order -- the planar and semi-planar layouts, where
// that is the only sensible pairing. An RGB source has no such lanes, so a
// request against one is treated as unpaired (see below) rather than guessed.
//
// When it is off. No request has ever carried enabled != 0 => the second
// stream is never opened, no packet is routed to it, and every existing path
// is byte-for-byte what it was. A request with enabled == 0 is recorded and
// means "do not compose this frame"; the table is still applied as usual.
//
// Pairing. The kernel decodes the second stream in software, pairs its frames
// with the base frame whose PTS is closest (within one base-frame period), and
// applies the request selected by the same rule the kind-3 table uses: the
// last request whose frameIndex <= the presented index, or the first entry
// when the index precedes all of them. If a request says enabled but no
// auxiliary frame is available for that index, the kernel does NOT apply the
// colour table for that frame: the table may have been baked on the promise
// that the samples were combined, so base-only output is the safe fallback.
//
// Lifecycle. Requests are bounded (the last 256 are kept). Seek, track switch
// and close flush or free the auxiliary decoder together with the base
// pipeline; the host simply sends the next request after playback resumes.
struct FFF3FPAuxiliaryComposition {
    std::uint32_t size;             // sizeof(FFF3FPAuxiliaryComposition)
    std::uint32_t version;          // FFF3FP_AUXILIARY_COMPOSITION_VERSION
    std::int64_t frameIndex;        // presentation index the request belongs to
    std::uint32_t enabled;          // 0 = skip this frame, nonzero = compose
    std::uint32_t residualCenter;   // sample value that means "no residual"
    std::int32_t residualShift;     // >0 shifts right (with rounding), <0 scales up
    std::uint32_t maxValue;         // clamp ceiling in base-layer code units
};
#define FFF3FP_AUXILIARY_COMPOSITION_VERSION 1u
// The host mirrors this struct in managed code; a silent size drift is the one
// failure both sides would otherwise accept.
static_assert(sizeof(FFF3FPAuxiliaryComposition) == 32,
    "FFF3FPAuxiliaryComposition is mirrored by the managed host and must stay 32 bytes");

FFF3FP_API FFFResult FFF3FP_SetAuxiliaryComposition(FFF3FPHandle player,
    const FFF3FPAuxiliaryComposition* composition) noexcept;

// Playback rate range: 0.1 through 16. Audio tempo processing preserves pitch.
FFF3FP_API FFFResult FFF3FP_SetPlaybackRate(FFF3FPHandle player, double rate) noexcept;
// gains points to ten band gains in dB (-12 through 12); wave is in [0, 1].
FFF3FP_API FFFResult FFF3FP_SetAudioEffects(FFF3FPHandle player, bool enabled, const float* gains, float wave) noexcept;
// Algorithm IDs: 0 = Lanczos3, 1 = Jinc anti alias, 2 = Lanczos4, 3 = Hermite.
FFF3FP_API FFFResult FFF3FP_SetScalingMode(FFF3FPHandle player, std::uint32_t mode) noexcept;
