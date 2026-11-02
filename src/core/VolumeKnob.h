#pragma once

namespace radio {

// Volume pot: raw position readings -> a steady knob level -> loudness.
// Smooths ADC noise, ignores tiny changes (hysteresis), treats the last bit
// of travel at each end as hard 0% / 100%, and maps level to gain on a
// logarithmic curve so equal turns sound like equal steps.
class VolumeKnob {
public:
    static constexpr float kSmoothing = 0.25f;  // per reading (~50 Hz)
    static constexpr float kEnd = 0.02f;        // dead zone at each end
    static constexpr float kHysteresis = 0.01f;
    static constexpr float kRangeDb = 40.0f;    // gain just above 0% is -40 dB

    // raw: knob position 0..1. Returns true if level() changed.
    bool update(float raw);
    // 0..1, or -1 before the first reading.
    float level() const { return level_; }
    // 0..1 gain for level(); 1 before the first reading.
    float gain() const { return level_ < 0 ? 1.0f : curve(level_); }

    static float curve(float level);

private:
    float filtered_ = -1;
    float level_ = -1;
};

}  // namespace radio
