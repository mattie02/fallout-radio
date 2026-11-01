#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Config.h"
#include "core/Effects.h"

namespace radio {

// Blends station audio with generated static by signal strength, then runs
// the vintage effects. Signal changes are smoothed (~30 ms) so turning the
// dial never clicks.
class Mixer {
public:
    void configure(const AudioConfig& config);
    // station: `count` mono samples (may be silence); signal: 0..1 target.
    void render(const int16_t* station, float signal, int16_t* out, size_t count);

    float smoothedSignal() const { return signal_; }

private:
    AudioConfig config_;
    StaticGenerator static_;
    VintageEffects effects_;
    float signal_ = 0;
};

}  // namespace radio
