#include "core/Effects.h"

#include <cmath>

#include "platform/Platform.h"

namespace radio {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kKnobSmoothing = 1.0f / (0.05f * kSampleRate);  // ~50 ms
}  // namespace

// --- Biquad ------------------------------------------------------------------

void Biquad::set(float b0, float b1, float b2, float a0, float a1, float a2) {
    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
}

void Biquad::setLowpass(float sampleRate, float hz, float q) {
    const float w = 2 * kPi * hz / sampleRate;
    const float alpha = std::sin(w) / (2 * q);
    const float c = std::cos(w);
    set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

void Biquad::setHighpass(float sampleRate, float hz, float q) {
    const float w = 2 * kPi * hz / sampleRate;
    const float alpha = std::sin(w) / (2 * q);
    const float c = std::cos(w);
    set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

// --- Static ------------------------------------------------------------------

void StaticGenerator::setCrackle(float popsPerSecond) {
    popChance_ = popsPerSecond > 0 ? popsPerSecond / kSampleRate : 0;
}

float StaticGenerator::uniform() {
    // xorshift32
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_) / 2147483648.0f - 1.0f;
}

float StaticGenerator::next() {
    const float white = uniform();
    lp_ += 0.25f * (white - lp_);  // darker component
    float s = 0.35f * white + 0.9f * lp_;

    // Slow random swell, as if the signal were drifting.
    if (wobbleCountdown_-- == 0) {
        wobbleCountdown_ = kSampleRate / 8;
        wobbleTarget_ = 0.65f + 0.35f * (uniform() * 0.5f + 0.5f);
    }
    wobble_ += 0.0005f * (wobbleTarget_ - wobble_);
    s *= wobble_;

    // Crackle: occasional sharp, fast-decaying pops.
    if (popChance_ > 0 && (uniform() * 0.5f + 0.5f) < popChance_) {
        pop_ = (uniform() < 0 ? -1.0f : 1.0f) * (0.5f + 0.5f * (uniform() * 0.5f + 0.5f));
    }
    s += pop_;
    pop_ *= 0.55f;
    return s;
}

// --- Effects -----------------------------------------------------------------

float softClip(float x) {
    if (x > 3) return 1;
    if (x < -3) return -1;
    return x * (27 + x * x) / (27 + 9 * x * x);
}

void VintageEffects::configure(const AudioConfig& config) {
    const float fs = static_cast<float>(kSampleRate);
    const float nyquistSafe = fs * 0.45f;
    highpass_.setHighpass(fs, std::fmin(std::fmax(config.lowHz, 20.0f), nyquistSafe));
    lowpass1_.setLowpass(fs, std::fmin(std::fmax(config.highHz, 200.0f), nyquistSafe));
    lowpass2_.setLowpass(fs, std::fmin(std::fmax(config.highHz, 200.0f), nyquistSafe));
    drive_ = std::fmax(config.drive, 0.1f);
    // Unity gain for quiet signals: drive only shapes the peaks.
    makeup_ = 1.0f / drive_;
    volume_ = std::fmin(std::fmax(config.volume, 0.0f), 1.0f);
}

void VintageEffects::setKnobGain(float gain, bool immediate) {
    knobTarget_ = std::fmin(std::fmax(gain, 0.0f), 1.0f);
    if (immediate) knob_ = knobTarget_;
}

float VintageEffects::process(float x) {
    x = highpass_.process(x);
    x = lowpass2_.process(lowpass1_.process(x));
    x = softClip(x * drive_) * makeup_;
    knob_ += kKnobSmoothing * (knobTarget_ - knob_);
    return x * volume_ * knob_;
}

}  // namespace radio
