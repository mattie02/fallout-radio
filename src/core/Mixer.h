#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Config.h"
#include "core/Effects.h"

namespace radio {

// Blends station audio with generated static by signal strength, then runs
// the vintage effects. Signal changes are smoothed (~30 ms) so turning the
// dial never clicks. A boot sound can be mixed in, independent of tuning.
class Mixer {
public:
    void configure(const AudioConfig& config);
    // station: `count` mono samples (may be silence); signal: 0..1 target.
    // beforeVolume, if given, receives the same audio without the volume applied.
    // boot, if given, is `count` samples of the boot sound.
    void render(const int16_t* station, float signal, int16_t* out, size_t count,
                int16_t* beforeVolume = nullptr, const int16_t* boot = nullptr);
    // Level of station + static, 0..1, for the warm-up fade. Smoothed like the
    // signal; `immediate` skips that.
    void setRadioLevel(float level, bool immediate = false) {
        radioLevelTarget_ = level;
        if (immediate) radioLevel_ = level;
    }
    // Volume knob gain 0..1; see VintageEffects::setKnobGain.
    void setKnobGain(float gain, bool immediate) { effects_.setKnobGain(gain, immediate); }

    float smoothedSignal() const { return signal_; }

private:
    AudioConfig config_;
    StaticGenerator static_;
    VintageEffects effects_;
    float signal_ = 0;
    float radioLevel_ = 1, radioLevelTarget_ = 1;
};

}  // namespace radio
