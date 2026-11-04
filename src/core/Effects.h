#pragma once

#include <cstdint>

#include "core/Config.h"

namespace radio {

// Second-order IIR filter (RBJ cookbook), direct form I.
class Biquad {
public:
    void setLowpass(float sampleRate, float hz, float q = 0.7071f);
    void setHighpass(float sampleRate, float hz, float q = 0.7071f);
    void reset() { x1_ = x2_ = y1_ = y2_ = 0; }
    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    void set(float b0, float b1, float b2, float a0, float a1, float a2);
    float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// Filtered noise for between stations: hiss with a slow fade wobble and
// random crackle pops. Deterministic for a given seed.
class StaticGenerator {
public:
    explicit StaticGenerator(uint32_t seed = 0x1234567u) : rng_(seed ? seed : 1) {}
    void setCrackle(float popsPerSecond);
    float next();  // roughly -1..1

private:
    float uniform();  // -1..1

    uint32_t rng_;
    float lp_ = 0;
    float wobble_ = 1, wobbleTarget_ = 1;
    uint32_t wobbleCountdown_ = 0;
    float pop_ = 0;
    float popChance_ = 0;
};

// The "old speaker" sound: band-pass, soft saturation, volume.
class VintageEffects {
public:
    void configure(const AudioConfig& config);
    // Volume knob gain 0..1, on top of the master volume. Changes are
    // smoothed (~50 ms) so knob steps never click; `immediate` skips that
    // (the first reading at boot).
    void setKnobGain(float gain, bool immediate);
    // beforeVolume, if given, receives the sample before any volume is applied.
    float process(float x, float* beforeVolume = nullptr);

private:
    Biquad highpass_, lowpass1_, lowpass2_;
    float drive_ = 1;
    float makeup_ = 1;
    float volume_ = 1;
    float knob_ = 1, knobTarget_ = 1;
};

// Saturates smoothly towards +-1 (rational tanh approximation).
float softClip(float x);

}  // namespace radio
