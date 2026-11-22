#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>

// Streaming PCM normalizer. K-weighted loudness is measured before user volume.
// Control follows the signal in 10 ms intervals without holding audio or changing timestamps.
class RealtimeLoudness final {
public:
    static constexpr std::size_t MaxChannels = 32;
    using Weights = std::array<double, MaxChannels>;

    void SetTarget(const bool enabled, const int targetLufs, const int rangeLu) noexcept {
        if (enabled_ != enabled) Reset();
        enabled_ = enabled;
        targetLufs_ = targetLufs;
        rangeLu_ = rangeLu;
    }

    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
    [[nodiscard]] bool HasFormat(const unsigned rate, const unsigned channels) const noexcept {
        return rate == sampleRate_ && channels == channels_;
    }

    void SetFormat(const unsigned rate, const unsigned channels, const Weights& weights) noexcept {
        Reset();
        sampleRate_ = rate;
        channels_ = channels;
        weights_ = weights;
        interval_ = std::max(1u, rate / 100);
        shortBlend_ = 1.0 - std::exp(-static_cast<double>(interval_) / (0.4 * rate));
        referenceBlend_ = 1.0 - std::exp(-static_cast<double>(interval_) / (4.0 * rate));
        gainBlend_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.25 * rate)));
        limiterBlend_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.06 * rate)));
        const auto kShelf = std::tan(std::numbers::pi * 1681.974450955533 / rate);
        const auto shelfDamping = kShelf / 0.7071752369554196;
        const auto shelfSquared = kShelf * kShelf;
        const auto shelfDenom = 1.0 + shelfDamping + shelfSquared;
        const auto high = std::pow(10.0, 3.999843853973347 / 20.0);
        const auto middle = std::pow(high, 0.4996667741545416);
        shelf_ = {(high + middle * shelfDamping + shelfSquared) / shelfDenom,
            2.0 * (shelfSquared - high) / shelfDenom,
            (high - middle * shelfDamping + shelfSquared) / shelfDenom,
            2.0 * (shelfSquared - 1.0) / shelfDenom, (1.0 - shelfDamping + shelfSquared) / shelfDenom};
        const auto kPass = std::tan(std::numbers::pi * 38.13547087602444 / rate);
        const auto passDamping = kPass / 0.5003270373238773;
        const auto passSquared = kPass * kPass;
        const auto passDenom = 1.0 + passDamping + passSquared;
        highPass_ = {1.0, -2.0, 1.0, 2.0 * (passSquared - 1.0) / passDenom,
            (1.0 - passDamping + passSquared) / passDenom};
    }

    void Reset() noexcept {
        states_ = {};
        samples_ = 0;
        powerSum_ = shortPower_ = referencePower_ = 0.0;
        gain_ = desiredGain_ = limiter_ = 1.0f;
    }

    template<typename Sample>
    void Process(Sample* samples, const std::size_t frames) noexcept {
        if (!enabled_ || !sampleRate_ || !channels_ || channels_ > MaxChannels) return;
        constexpr double pcmScale = std::is_floating_point_v<Sample> ? 1.0 :
            static_cast<double>(std::uint64_t{1} << (sizeof(Sample) * 8 - 1));
        for (std::size_t frame = 0; frame < frames; ++frame, samples += channels_) {
            float peak = 0.0f;
            for (unsigned channel = 0; channel < channels_; ++channel) {
                const float input = static_cast<float>(samples[channel] / pcmScale);
                if (!std::isfinite(input)) { samples[channel] = 0; continue; }
                peak = std::max(peak, std::abs(input));
                if (weights_[channel] == 0.0) continue;
                auto& state = states_[channel];
                const auto weighted = highPass_.Filter(shelf_.Filter(input, state.shelf), state.highPass);
                powerSum_ += weights_[channel] * weighted * weighted;
            }
            if (++samples_ == interval_) UpdateTarget();
            gain_ += (desiredGain_ - gain_) * gainBlend_;
            // One gain for all channels preserves the spatial image. Limit sample peaks to -1 dBFS.
            const auto ceiling = peak > 0.0f ? std::min(1.0f, 0.8912509f / (peak * gain_)) : 1.0f;
            limiter_ = std::min(ceiling, limiter_ + (1.0f - limiter_) * limiterBlend_);
            const auto factor = static_cast<double>(gain_ * limiter_);
            for (unsigned channel = 0; channel < channels_; ++channel) {
                if constexpr (std::is_floating_point_v<Sample>)
                    samples[channel] = static_cast<Sample>(samples[channel] * factor);
                else
                    samples[channel] = static_cast<Sample>(std::llround(samples[channel] * factor));
            }
        }
    }

private:
    struct Delay { double one = 0.0; double two = 0.0; };
    struct ChannelState { Delay shelf; Delay highPass; };
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double Filter(const double input, Delay& state) const noexcept {
            const auto output = input * b0 + state.one;
            state.one = input * b1 - output * a1 + state.two;
            state.two = input * b2 - output * a2;
            return output;
        }
    };

    void UpdateTarget() noexcept {
        const auto power = powerSum_ / interval_;
        powerSum_ = 0.0;
        samples_ = 0;
        shortPower_ += (power - shortPower_) * shortBlend_;
        // Gate silence and low-level noise so gaps do not drive the gain up.
        if (power < 1.0e-7) return;
        if (referencePower_ == 0.0) shortPower_ = referencePower_ = power;
        else if (shortPower_ >= referencePower_ * 0.1) referencePower_ += (shortPower_ - referencePower_) * referenceBlend_;
        const auto loudness = -0.691 + 10.0 * std::log10(shortPower_);
        const auto reference = -0.691 + 10.0 * std::log10(referencePower_);
        const auto halfRange = rangeLu_ * 0.5;
        const auto preserved = std::clamp(loudness - reference, -halfRange, halfRange);
        const auto gainDb = std::clamp(targetLufs_ + preserved - loudness, -36.0, 18.0);
        desiredGain_ = static_cast<float>(std::pow(10.0, gainDb / 20.0));
    }

    bool enabled_ = false;
    int targetLufs_ = -18;
    int rangeLu_ = 7;
    unsigned sampleRate_ = 0, channels_ = 0, interval_ = 1, samples_ = 0;
    Weights weights_{};
    std::array<ChannelState, MaxChannels> states_{};
    Biquad shelf_, highPass_;
    double powerSum_ = 0.0, shortPower_ = 0.0, referencePower_ = 0.0;
    double shortBlend_ = 0.0, referenceBlend_ = 0.0;
    float gainBlend_ = 0.0f, limiterBlend_ = 0.0f;
    float gain_ = 1.0f, desiredGain_ = 1.0f, limiter_ = 1.0f;
};
