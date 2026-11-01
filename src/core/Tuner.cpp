#include "core/Tuner.h"

#include <cmath>
#include <utility>

namespace radio {

namespace {
// Dial positions are tenths of MHz held in floats; absorb rounding so that
// "exactly at the edge" behaves as written in config.json.
constexpr float kEpsilon = 1e-4f;
}  // namespace

void Tuner::setSignal(float lockMhz, float fadeMhz) {
    lock_ = lockMhz < 0 ? 0 : lockMhz;
    fade_ = fadeMhz < 0 ? 0 : fadeMhz;
}

void Tuner::setStations(std::vector<float> frequenciesMhz) { freqs_ = std::move(frequenciesMhz); }

float Tuner::signalAt(float distanceMhz, float lockMhz, float fadeMhz) {
    const float d = std::fabs(distanceMhz);
    if (d <= lockMhz + kEpsilon) return 1.0f;
    if (fadeMhz <= 0 || d >= lockMhz + fadeMhz - kEpsilon) return 0.0f;
    const float x = 1.0f - (d - lockMhz) / fadeMhz;  // 1 at lock edge, 0 at fade edge
    return x * x * (3.0f - 2.0f * x);
}

Tuner::Result Tuner::tune(float dialMhz) const {
    Result best;
    for (size_t i = 0; i < freqs_.size(); ++i) {
        const float s = signalAt(dialMhz - freqs_[i], lock_, fade_);
        if (s > best.signal) {
            best.station = static_cast<int>(i);
            best.signal = s;
        }
    }
    return best;
}

}  // namespace radio
