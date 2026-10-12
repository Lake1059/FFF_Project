// Gating: the ENTIRE file (including the "oar.h" include below) is wrapped in
// #if FFF_WITH_LIBIAMF. Without this gating a clean checkout that has no
// third_party\libiamf fails with C1083 on this translation unit, because "oar.h"
// is included unconditionally below. The matching
// vcxproj item additionally carries Condition="'$(HaveLibiamf)'=='true'", so an
// IAMF-off build never compiles this file; the #if is the compiler-side half of
// that guarantee (and covers builds that list the file by other means).
#include "pch.h"
#if FFF_WITH_LIBIAMF
#include "3FP/Audio/BedBinauralRenderer.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include "oar.h"
}

#include <algorithm>
#include <array>
#include <vector>

namespace {

// Samples per block. The OBR DSP is block-based (HRTF convolution), so block
// size trades CPU against latency; 2048 is about 42.8 ms at 48 kHz.
constexpr int kBlockSamples = 2048;

// OBR layout slot -> FFmpeg channel position (AVChannel value carried as int).
// The basis is the kernel's own ground truth: the bed-order finding in
// IamfAudioDecoder.cpp (slots 4..7 are BL BR SiL SiR on the libiamf/OBR side).
// LFE slots stay zeroed: the probe saw OBR render LFE into both ears at ~0.21 rms.
constexpr int kPos51[]  = {AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT, AV_CHAN_FRONT_CENTER,
                           AV_CHAN_LOW_FREQUENCY, AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT};
constexpr int kPos71[]  = {AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT, AV_CHAN_FRONT_CENTER,
                           AV_CHAN_LOW_FREQUENCY, AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT,
                           AV_CHAN_SIDE_LEFT, AV_CHAN_SIDE_RIGHT};
constexpr int kPos514[] = {AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT, AV_CHAN_FRONT_CENTER,
                           AV_CHAN_LOW_FREQUENCY, AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT,
                           AV_CHAN_TOP_FRONT_LEFT, AV_CHAN_TOP_FRONT_RIGHT,
                           AV_CHAN_TOP_BACK_LEFT, AV_CHAN_TOP_BACK_RIGHT};
constexpr int kPos512[] = {AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT, AV_CHAN_FRONT_CENTER,
                           AV_CHAN_LOW_FREQUENCY, AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT,
                           AV_CHAN_TOP_FRONT_LEFT, AV_CHAN_TOP_FRONT_RIGHT};
constexpr int kPos714[] = {AV_CHAN_FRONT_LEFT, AV_CHAN_FRONT_RIGHT, AV_CHAN_FRONT_CENTER,
                           AV_CHAN_LOW_FREQUENCY, AV_CHAN_BACK_LEFT, AV_CHAN_BACK_RIGHT,
                           AV_CHAN_SIDE_LEFT, AV_CHAN_SIDE_RIGHT,
                           AV_CHAN_TOP_FRONT_LEFT, AV_CHAN_TOP_FRONT_RIGHT,
                           AV_CHAN_TOP_BACK_LEFT, AV_CHAN_TOP_BACK_RIGHT};

// Order is the tie-break priority for equal scores (larger layouts first).
constexpr BedBinauralLayout kLayouts[] = {
    {ck_oar_layout_714, kPos714, 12},
    {ck_oar_layout_514, kPos514, 10},
    {ck_oar_layout_512, kPos512, 8},
    {ck_oar_layout_71,  kPos71,  8},
    {ck_oar_layout_51,  kPos51,  6},
};

bool HasSlot(const BedBinauralLayout& layout, const int position) {
    for (int i = 0; i < layout.count; ++i)
        if (layout.positions[i] == position) return true;
    return false;
}

// Consume the source layout with one OBR candidate: build the slot -> source gain table.
// Returns false when the candidate cannot consume every source channel.
// exactCount/emptySlots feed the scoring: more exact matches, fewer non-LFE empty slots.
bool BuildFor(const BedBinauralLayout& layout, const AVChannelLayout& source,
    std::vector<std::vector<std::pair<int, float>>>& slotSources,
    int& exactCount, int& emptySlots, int& widePenalty) {
    slotSources.assign(static_cast<std::size_t>(layout.count), {});
    exactCount = 0;
    emptySlots = 0;
    widePenalty = 0;
    const auto append = [&](int slot, int position, float coefficient) {
        if (av_channel_layout_index_from_channel(&source, static_cast<AVChannel>(position)) >= 0)
            slotSources[static_cast<std::size_t>(slot)].emplace_back(position, coefficient);
    };

    for (int slot = 0; slot < layout.count; ++slot) {
        const auto position = layout.positions[slot];
        if (position == AV_CHAN_LOW_FREQUENCY) continue;   // LFE always muted (measured)
        if (av_channel_layout_index_from_channel(&source, static_cast<AVChannel>(position)) >= 0) {
            slotSources[static_cast<std::size_t>(slot)].emplace_back(position, 1.0f);
            ++exactCount;
            continue;
        }
        // Back/surround fallback, used only when the OBR layout has no dedicated slot
        // for it: a 5.1(side) source has no back ring, so the BL slot takes SIDE_LEFT
        // and keeps the surround content; with a real SiL slot BL stays empty instead.
        if (position == AV_CHAN_BACK_LEFT && !HasSlot(layout, AV_CHAN_SIDE_LEFT)) {
            append(slot, AV_CHAN_SIDE_LEFT, 1.0f);
            if (!slotSources[static_cast<std::size_t>(slot)].empty()) continue;
        }
        if (position == AV_CHAN_BACK_RIGHT && !HasSlot(layout, AV_CHAN_SIDE_RIGHT)) {
            append(slot, AV_CHAN_SIDE_RIGHT, 1.0f);
            if (!slotSources[static_cast<std::size_t>(slot)].empty()) continue;
        }
        if (position == AV_CHAN_SIDE_LEFT && !HasSlot(layout, AV_CHAN_BACK_LEFT)) {
            append(slot, AV_CHAN_BACK_LEFT, 1.0f);
            if (!slotSources[static_cast<std::size_t>(slot)].empty()) continue;
        }
        if (position == AV_CHAN_SIDE_RIGHT && !HasSlot(layout, AV_CHAN_BACK_RIGHT)) {
            append(slot, AV_CHAN_BACK_RIGHT, 1.0f);
            if (!slotSources[static_cast<std::size_t>(slot)].empty()) continue;
        }
        ++emptySlots;   // no source for this slot (stays silent)
    }

    // The front-wide pair (FLC/FRC, +/-60 deg) has no slot in any OBR layout, so:
    //   * surround ring present (71/714): split 0.5/0.5 into FL and SiL, the VBAP
    //     nearest-neighbour pair; virtual azimuth about +/-60 deg (ILD roughly -10 dB).
    //   * no surround ring (51/512/514): fold the whole path into FL at 0.75, so
    //     the azimuth stays front (30 deg) and is never smeared onto the back ring.
    const auto indexOfSlot = [&](int position) {
        for (int slot = 0; slot < layout.count; ++slot)
            if (layout.positions[slot] == position) return slot;
        return -1;
    };
    const auto leftSurround = HasSlot(layout, AV_CHAN_SIDE_LEFT)
        ? indexOfSlot(AV_CHAN_SIDE_LEFT) : -1;
    const auto rightSurround = HasSlot(layout, AV_CHAN_SIDE_RIGHT)
        ? indexOfSlot(AV_CHAN_SIDE_RIGHT) : -1;
    const auto flSlot = indexOfSlot(AV_CHAN_FRONT_LEFT);
    const auto frSlot = indexOfSlot(AV_CHAN_FRONT_RIGHT);
    if (flSlot >= 0 &&
        av_channel_layout_index_from_channel(&source, AV_CHAN_FRONT_LEFT_OF_CENTER) >= 0) {
        if (leftSurround >= 0) {
            slotSources[static_cast<std::size_t>(flSlot)].emplace_back(AV_CHAN_FRONT_LEFT_OF_CENTER, 0.5f);
            slotSources[static_cast<std::size_t>(leftSurround)].emplace_back(AV_CHAN_FRONT_LEFT_OF_CENTER, 0.5f);
        } else {
            // No surround ring: front-wide folds into FL at 0.75, leaving level headroom.
            slotSources[static_cast<std::size_t>(flSlot)].emplace_back(AV_CHAN_FRONT_LEFT_OF_CENTER, 0.75f);
        }
    }
    if (frSlot >= 0 &&
        av_channel_layout_index_from_channel(&source, AV_CHAN_FRONT_RIGHT_OF_CENTER) >= 0) {
        if (rightSurround >= 0) {
            slotSources[static_cast<std::size_t>(frSlot)].emplace_back(AV_CHAN_FRONT_RIGHT_OF_CENTER, 0.5f);
            slotSources[static_cast<std::size_t>(rightSurround)].emplace_back(AV_CHAN_FRONT_RIGHT_OF_CENTER, 0.5f);
        } else {
            slotSources[static_cast<std::size_t>(frSlot)].emplace_back(AV_CHAN_FRONT_RIGHT_OF_CENTER, 0.75f);
        }
    }

    // Scoring penalty: front-wide content with no surround ring to go to can only
    // fold into the front, which costs azimuth, so each such channel adds one penalty.
    // 7.1(wide) therefore prefers 71 (the +/-60 deg pair) over 51, which only folds front.
    if (!HasSlot(layout, AV_CHAN_SIDE_LEFT) &&
        av_channel_layout_index_from_channel(&source, AV_CHAN_FRONT_LEFT_OF_CENTER) >= 0)
        ++widePenalty;
    if (!HasSlot(layout, AV_CHAN_SIDE_RIGHT) &&
        av_channel_layout_index_from_channel(&source, AV_CHAN_FRONT_RIGHT_OF_CENTER) >= 0)
        ++widePenalty;
    // Coverage check: every non-LFE source channel must be consumed by some slot, or
    // the candidate would drop content silently (7.1(wide) against 512: nothing fills
    // the top-front slots and FLC/FRC had nowhere to go -- that old path is what this stops).
    for (int c = 0; c < source.nb_channels; ++c) {
        const auto position = av_channel_layout_channel_from_index(&source, c);
        if (position == AV_CHAN_LOW_FREQUENCY) continue;   // LFE dropped by policy, counts as consumed
        bool consumed = false;
        for (const auto& sources : slotSources)
            for (const auto& [sourcePosition, coefficient] : sources)
                if (sourcePosition == position) { consumed = true; break; }
        if (!consumed) return false;
    }
    return true;
}

} // namespace

BedBinauralRenderer::BedBinauralRenderer() = default;

BedBinauralRenderer::~BedBinauralRenderer() {
    DestroyPipeline();
    if (inputLayout_ != nullptr) {
        av_channel_layout_uninit(inputLayout_);
        delete inputLayout_;
        inputLayout_ = nullptr;
    }
}

const BedBinauralLayout* BedBinauralRenderer::SelectLayout(
    const AVChannelLayout& layout,
    std::vector<std::vector<std::pair<int, float>>>& slotSources) {
    slotSources.clear();
    if (layout.order != AV_CHANNEL_ORDER_NATIVE) return nullptr;
    if (layout.nb_channels < 5) return nullptr;   // stereo/quad go straight to swr
    const BedBinauralLayout* best = nullptr;
    auto bestSources = std::vector<std::vector<std::pair<int, float>>>{};
    auto bestExact = -1;
    auto bestEmpty = 1 << 30;
    auto bestWide = 1 << 30;
    for (const auto& candidate : kLayouts) {
        auto sources = std::vector<std::vector<std::pair<int, float>>>{};
        int exact = 0;
        int empty = 0;
        int wide = 0;
        if (!BuildFor(candidate, layout, sources, exact, empty, wide)) continue;
        // Scoring order: most exact matches first > smallest front-wide penalty >
        // fewest non-LFE empty slots last. For a 5.1 source both 51 and 71 match exactly,
        // but 51 wins on the last rule because it leaves no SiL/SiR slot idle.
        const auto better = best == nullptr || exact > bestExact ||
            (exact == bestExact && wide < bestWide) ||
            (exact == bestExact && wide == bestWide && empty < bestEmpty);
        if (better) {
            best = &candidate;
            bestSources = std::move(sources);
            bestExact = exact;
            bestEmpty = empty;
            bestWide = wide;
        }
    }
    if (best != nullptr) slotSources = std::move(bestSources);
    return best;
}

bool BedBinauralRenderer::SupportsLayout(const AVChannelLayout& layout) noexcept {
    auto scratch = std::vector<std::vector<std::pair<int, float>>>{};
    return SelectLayout(layout, scratch) != nullptr;
}

void BedBinauralRenderer::DestroyPipeline() noexcept {
    if (oar_ != nullptr) {
        oar_destroy(static_cast<oar_t*>(oar_));
        oar_ = nullptr;
    }
    if (toPlanarFloat_ != nullptr) {
        swr_free(&toPlanarFloat_);
        toPlanarFloat_ = nullptr;
    }
    planes_.clear();
    slotSources_.clear();
    staged_ = 0;
    blockSamples_ = 0;
    layout_ = nullptr;
    active_ = false;
}

void BedBinauralRenderer::Reset() noexcept {
    staged_ = 0;
    active_ = false;
    // OBR handle is kept: after a seek the next whole block starts clean and the
    // half-filled one is dropped, matching the swr path's discontinuity rule.
}

bool BedBinauralRenderer::EnsurePipeline(const AVFrame* frame) {
    const auto format = static_cast<AVSampleFormat>(frame->format);
    const auto planar = av_sample_fmt_is_planar(format) != 0;
    if (oar_ != nullptr && inputRate_ == frame->sample_rate &&
        inputFormat_ == static_cast<int>(format) && inputIsPlanar_ == planar &&
        inputLayout_ != nullptr &&
        av_channel_layout_compare(inputLayout_, &frame->ch_layout) == 0)
        return true;

    DestroyPipeline();

    // ---- pick the OBR layout (and build the slot -> source gain table) ----
    const auto* layout = SelectLayout(frame->ch_layout, slotSources_);
    if (layout == nullptr) return false;
    layout_ = layout;

    // ---- create the OAR ----
    oar_config_t config{};
    config.target_layout = ck_oar_layout_binaural;
    config.samples_per_channel = kBlockSamples;
    config.sampling_rate = static_cast<std::uint32_t>(frame->sample_rate);
    oar_ = oar_create(&config);
    if (oar_ == nullptr) { DestroyPipeline(); return false; }
    const auto gid = oar_add_audio_group(static_cast<oar_t*>(oar_));
    if (gid < 0) { DestroyPipeline(); return false; }
    oar_audio_element_config_t element{};
    element.type = ck_channel_based;
    element.cbc.layout = static_cast<oar_layout_t>(layout->oarLayout);
    if (oar_add_audio_element(static_cast<oar_t*>(oar_), static_cast<std::uint32_t>(gid),
            elementId_, &element) != ck_oar_ok) {
        DestroyPipeline();
        return false;
    }
    // Limiter: every channel at full scale after the HRTF sum pushes the output past
    // 0 dBFS, which is what OBR's built-in look-ahead limiter is for. Enabling it may fail.
    oar_enable_limiter(static_cast<oar_t*>(oar_), 1);
    oar_set_limiter_threshold(static_cast<oar_t*>(oar_), -1.0f);
    limiterDelay_ = oar_get_limiter_delay(static_cast<oar_t*>(oar_));
    blockSamples_ = kBlockSamples;

    // ---- accumulation buffers (one plane per OBR slot, planar float) ----
    planes_.assign(static_cast<std::size_t>(layout->count),
        std::vector<float>(static_cast<std::size_t>(kBlockSamples), 0.0f));
    staged_ = 0;

    // ---- internal swr: any input format -> planar float, same layout and rate ----
    if (inputLayout_ == nullptr) inputLayout_ = new AVChannelLayout{};
    else av_channel_layout_uninit(inputLayout_);
    if (av_channel_layout_copy(inputLayout_, &frame->ch_layout) < 0) { DestroyPipeline(); return false; }
    AVChannelLayout outLayout{};
    if (av_channel_layout_copy(&outLayout, &frame->ch_layout) < 0 ||
        swr_alloc_set_opts2(&toPlanarFloat_, &outLayout, AV_SAMPLE_FMT_FLTP, frame->sample_rate,
            &frame->ch_layout, format, frame->sample_rate, 0, nullptr) < 0 ||
        toPlanarFloat_ == nullptr || swr_init(toPlanarFloat_) < 0) {
        av_channel_layout_uninit(&outLayout);
        DestroyPipeline();
        return false;
    }
    av_channel_layout_uninit(&outLayout);
    inputRate_ = frame->sample_rate;
    inputFormat_ = static_cast<int>(format);
    inputIsPlanar_ = planar;
    active_ = true;
    return true;
}

bool BedBinauralRenderer::StageSlice(const AVFrame* frame, const int firstSample,
    const int sliceSamples) {
    // Convert one slice of the frame to planar float and accumulate it, weighted by
    // the slot -> source gain table, into [staged_, staged_ + got); the caller must
    // keep the slice inside that range.
    const auto channels = frame->ch_layout.nb_channels;
    if (channels <= 0 || sliceSamples <= 0) return true;
    std::vector<float> converted(static_cast<std::size_t>(channels) * sliceSamples, 0.0f);
    std::vector<std::uint8_t*> outPlanes(static_cast<std::size_t>(channels));
    for (int c = 0; c < channels; ++c)
        outPlanes[static_cast<std::size_t>(c)] = reinterpret_cast<std::uint8_t*>(
            converted.data() + static_cast<std::size_t>(c) * sliceSamples);
    // Input pointers are offset to the slice start, per sample format.
    // Planar: plane c lives in extended_data[c], offset by firstSample*bytesPerSample.
    // Interleaved: everything is in extended_data[0] and swr reads only in[0], so the
    // slice starts at firstSample*channels*bytes; the other entries may alias it, swr ignores them.
    std::vector<const std::uint8_t*> inPlanes(static_cast<std::size_t>(channels));
    const auto planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format)) != 0;
    const auto bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
    const auto* interleavedBase = frame->extended_data[0];
    for (int c = 0; c < channels; ++c) {
        inPlanes[static_cast<std::size_t>(c)] = planar
            ? frame->extended_data[c] +
                  static_cast<std::ptrdiff_t>(firstSample) * bytesPerSample
            : interleavedBase +
                  static_cast<std::ptrdiff_t>(firstSample) * channels * bytesPerSample;
    }
    const auto got = swr_convert(toPlanarFloat_, outPlanes.data(), sliceSamples,
        const_cast<const std::uint8_t**>(inPlanes.data()), sliceSamples);
    if (got <= 0) return true;   // conversion failed: skip this slice, keep the audio chain alive

    for (int slot = 0; slot < layout_->count; ++slot) {
        auto& plane = planes_[static_cast<std::size_t>(slot)];
        std::fill(plane.begin() + staged_, plane.begin() + staged_ + got, 0.0f);
        for (const auto& [position, coefficient] : slotSources_[static_cast<std::size_t>(slot)]) {
            const auto source = av_channel_layout_index_from_channel(&frame->ch_layout,
                static_cast<AVChannel>(position));
            if (source < 0) continue;
            const auto* samples = converted.data() +
                static_cast<std::size_t>(source) * static_cast<std::size_t>(sliceSamples);
            for (int i = 0; i < got; ++i)
                plane[static_cast<std::size_t>(staged_ + i)] += coefficient * samples[i];
        }
    }
    return true;
}

bool BedBinauralRenderer::EmitBlock(const std::int64_t position100ns,
    std::vector<Block>& out) {
    // Accumulated planes -> OBR (plane c sits at data + c*blockSamples_).
    std::vector<float> planar(static_cast<std::size_t>(layout_->count) * blockSamples_);
    for (int c = 0; c < layout_->count; ++c)
        std::copy(planes_[static_cast<std::size_t>(c)].begin(),
            planes_[static_cast<std::size_t>(c)].begin() + blockSamples_,
            planar.begin() + static_cast<std::size_t>(c) * blockSamples_);
    oar_audio_block_t in{};
    in.data = planar.data();
    in.channels = static_cast<std::uint32_t>(layout_->count);
    in.samples_per_channel = static_cast<std::uint32_t>(blockSamples_);
    if (oar_update_audio_element_data(static_cast<oar_t*>(oar_), elementId_, &in) != ck_oar_ok)
        return false;

    std::vector<float> rendered(2u * static_cast<std::size_t>(blockSamples_), 0.0f);
    oar_audio_block_t outBlock{};
    outBlock.data = rendered.data();
    outBlock.channels = 2;
    outBlock.samples_per_channel = static_cast<std::uint32_t>(blockSamples_);
    if (oar_render(static_cast<oar_t*>(oar_), &outBlock) != ck_oar_ok) return false;

    auto* frame = av_frame_alloc();
    if (frame == nullptr) return false;
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = inputRate_;
    frame->nb_samples = blockSamples_;
    av_channel_layout_default(&frame->ch_layout, 2);   // newer avutil returns void
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return false;
    }
    for (int c = 0; c < 2; ++c)
        std::copy(rendered.begin() + static_cast<std::size_t>(c) * blockSamples_,
            rendered.begin() + static_cast<std::size_t>(c + 1) * blockSamples_,
            reinterpret_cast<float*>(frame->extended_data[c]));
    frame->pts = AV_NOPTS_VALUE;   // position travels as position100ns, same contract as Enqueue
    frame->best_effort_timestamp = AV_NOPTS_VALUE;
    out.push_back(Block{frame, position100ns});
    return true;
}

bool BedBinauralRenderer::Process(const AVFrame* frame, const std::int64_t position100ns,
    std::vector<Block>& out) {
    if (frame == nullptr || frame->nb_samples <= 0) return true;
    if (!EnsurePipeline(frame)) return false;   // caller falls back to the swr downmix

    // Each output block sits at the frame start plus the block's offset inside the frame.
    const auto blockPosition = [&](const int sampleOffsetInFrame) -> std::int64_t {
        if (position100ns == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
        return position100ns + av_rescale_q(sampleOffsetInFrame,
            AVRational{1, inputRate_}, AVRational{1, 10000000});
    };

    // Staging is slice-wise because a decoded frame can far exceed one block (4096 or
    // 8192 samples): fill piecewise and emit one block whenever the buffer is full.
    int consumed = 0;
    while (consumed < frame->nb_samples) {
        const auto room = blockSamples_ - staged_;
        auto slice = std::min(room, frame->nb_samples - consumed);
        if (!StageSlice(frame, consumed, slice)) return true;   // slice failed: drop this slice
        staged_ += slice;
        consumed += slice;
        if (staged_ < blockSamples_) break;                     // buffer not full: wait for the next frame
        if (!EmitBlock(blockPosition(consumed - blockSamples_), out)) return false;
        // Move the remainder to the head, then zero the tail -- never the other way round.
        const auto remainder = staged_ - blockSamples_;
        if (remainder > 0)
            for (auto& plane : planes_)
                std::move(plane.begin() + blockSamples_, plane.begin() + blockSamples_ + remainder,
                    plane.begin());
        if (remainder < blockSamples_)
            for (auto& plane : planes_)
                std::fill(plane.begin() + remainder, plane.begin() + blockSamples_, 0.0f);
        staged_ = remainder;
    }
    return true;
}
#endif // FFF_WITH_LIBIAMF
