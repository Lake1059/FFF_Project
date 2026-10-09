#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "3FP/Api/FFF.Player.Api.h"

struct AVFrame;
struct AVFormatContext;

// IAMF (Immersive Audio Model and Formats) decoding, via the Alliance for Open
// Media reference library `libiamf`.
//
// Why not FFmpeg: libavformat splits one IAMF audio element into several
// *dependent* elementary streams and libavcodec has no iamf decoder, so the step
// that turns those substreams into a target layout lives inside the ffmpeg CLI's
// filter graph and is not reachable through the libraries. Tellingly, the CLI
// reaches "7.1.4" by running an ordinary channel remap: measured on
// live_7_1_4.iamf, `ffmpeg -ac 12` yields 12 channels of which only **2 carry
// signal** -- the other ten are silent. libiamf renders the same file with 11 of
// 12 channels carrying signal. Only libiamf actually decodes IAMF.
//
// The wrapper is deliberately small: open, configure from the container's
// descriptor bytes, then push access units and receive interleaved PCM, which is
// wrapped into an AVFrame so the existing renderer path (peak metering, seek
// anchoring, WASAPI downmix) keeps working unchanged.
class IamfAudioDecoder {
public:
    IamfAudioDecoder();
    ~IamfAudioDecoder();
    IamfAudioDecoder(const IamfAudioDecoder&) = delete;
    IamfAudioDecoder& operator=(const IamfAudioDecoder&) = delete;

    /// True when the build linked libiamf. When false, callers must keep using
    /// the FFmpeg path; every other method then fails cleanly.
    static bool Available() noexcept;

    /// Channels the content declares, from libiamf's per-element detail (0 = unknown).
    /// Distinct from Channels(), which is the render target we asked for. Must be read on
    /// every configure path -- it was once only read in Open(), so file playback through
    /// RestartFile() reported 0.
    /// Takes the element array rather than IAMF_StreamInfo so this header stays free of
    /// libiamf types (it must compile in builds without libiamf).
    static int ContentChannelsFromElements(
        const void* elements, std::uint32_t count) noexcept;

    /// Opens a decoder and configures it from the whole IAMF bitstream prefix.
    /// `soundSystem` is the caller's rendering target (0..14, -1 = leave default).
    /// Returns false and fills `error` when libiamf cannot handle this file, which
    /// happens for real content (unimplemented FLAC carriage, some HOA layouts);
    /// the caller is expected to fall back to FFmpeg rather than fail the track.
    bool Open(const std::uint8_t* data, std::size_t size, int soundSystem,
        std::string& error);

    /// Pushes one access unit (a packet's payload) and, for every frame libiamf
    /// produces, hands a packed AVFrame to `sink`. Returns false on a hard error.
    /// `position100ns` is attached to each produced frame.
    bool Decode(const std::uint8_t* data, std::size_t size, std::int64_t position100ns,
        const std::function<void(AVFrame*)>& sink, std::string& error);

    /// Drives the decoder from the raw OBU stream of a file, producing roughly
    /// `framesWanted` frames per call and remembering where it stopped.
    ///
    /// This exists because FFmpeg's IAMF demuxer hands out **one packet per
    /// substream**, while libiamf expects a whole access unit -- every substream's
    /// OBUs together. Feeding it per-substream packets makes it reject the frames
    /// ("Substream ID N already has an audio frame OBU"). Streaming the raw OBUs is
    /// what its own reference tool does and the only arrangement in which the
    /// substreams reassemble.
    ///
    /// Deliberately incremental: the corpus contains a 328 MB fixture, so decoding
    /// a whole file up front is not an option.
    bool OpenFile(const std::string& pathUtf8, std::string& error);
    /// Produces up to `maxFrames` frames, appending (frame, position100ns) pairs to
    /// `out`. `atEnd` is set once the file and the decoder's delay line are drained.
    bool PumpFile(std::size_t maxFrames,
        std::vector<std::pair<AVFrame*, std::int64_t>>& out, bool& atEnd,
        std::string& error);
    /// Restarts the stream from the beginning (seeking is done by re-decoding,
    /// because the decoder has no random-access entry point).
    bool RestartFile(std::string& error);

    /// Restarts the stream and labels the frames it produces as starting at
    /// `position100ns` in the file. Use this for seeks: the decoder always replays from
    /// byte 0, so without the base its timestamps would name the wrong position and the
    /// caller's "drop audio before the seek target" filter would discard every frame
    /// until playback caught up, silencing the track. Pass 0 to anchor at the start.
    bool RestartFileAt(std::int64_t position100ns, std::string& error);

    /// Flushes any delayed output (libiamf synthesises tail frames).
    bool Flush(const std::function<void(AVFrame*)>& sink, std::string& error);

    int Channels() const noexcept { return channels_; }
    /// Channels the *content* declares, which can be fewer than Channels().
    /// The render target is fixed at 7.1.4; when the content is smaller, libiamf fills the
    /// speakers it does not have with digital silence (verified against iamfdec: 7.1 leaves
    /// 4 silent, 7.1.2 leaves 2, 7.1.4 leaves none). So the two numbers answer different
    /// questions -- "what are we rendering into" versus "what is actually in the file" --
    /// and the UI should show the content, not the target. 0 when libiamf did not report
    /// per-element information.
    int ContentChannels() const noexcept { return contentChannels_; }
    int SampleRate() const noexcept { return sampleRate_; }
    int BitDepth() const noexcept { return bitDepth_; }
    /// The sound system actually in effect, for diagnostics.
    int SoundSystem() const noexcept { return soundSystem_; }

private:
    /// Wraps `producedSamples` of interleaved S16 PCM from pcm_ into an AVFrame
    /// and hands it to `sink`. Shared by Decode and the flush path.
    bool Deliver(int producedSamples, std::int64_t pts100ns,
        const std::function<void(AVFrame*)>& sink, std::string& error);
    bool Receive(bool draining, const std::function<void(AVFrame*)>& sink,
        std::string& error);
    /// Allocates a frame, copies the freshly decoded PCM into it and appends it.
    bool AppendFrame(int samples, std::int64_t position100ns,
        std::vector<std::pair<AVFrame*, std::int64_t>>& out, std::string& error);
    /// Advances the sliding buffer past `bytes` already consumed by libiamf.
    void Consume(std::uint32_t bytes);
    void Close() noexcept;

    void* handle_ = nullptr;
    // Interleaved output buffer, sized from libiamf's own max_frame_size.
    std::vector<std::uint8_t> pcm_;
    int channels_ = 0;
    // Channels the content declares; may be fewer than channels_ (see ContentChannels).
    int contentChannels_ = 0;
    int sampleRate_ = 0;
    int bitDepth_ = 0;
    int soundSystem_ = -1;
    bool configured_ = false;
    AVFrame* frame_ = nullptr;

    // ── Streaming file state (OpenFile / PumpFile / RestartFile) ──
    std::string filePath_;
    void* file_ = nullptr;             // HANDLE; void* keeps windows.h out of the header
    std::vector<std::uint8_t> block_;  // sliding OBU buffer
    std::size_t used_ = 0;             // read cursor into block_
    std::size_t filled_ = 0;           // bytes of block_ holding valid data
    bool fileEnd_ = false;             // the file has been read to the end
    bool endOfStream_ = false;         // the delay line has been drained too
    std::int64_t nextPosition100ns_ = 0;
    // File position that a restarted stream's zero corresponds to; see RestartFileAt.
    std::int64_t seekBase100ns_ = 0;
    // Consecutive decode calls that neither produced nor consumed anything.
    int stalledRounds_ = 0;
};
