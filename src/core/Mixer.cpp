#include "core/Mixer.h"

#include <cmath>

#include "platform/Platform.h"

namespace radio {

namespace {
constexpr float kSmoothing = 1.0f / (0.03f * kSampleRate);  // ~30 ms

int16_t toPcm(float x) {
    return static_cast<int16_t>(std::lround(std::fmax(-1.0f, std::fmin(1.0f, x)) * 32767.0f));
}
}  // namespace

void Mixer::configure(const AudioConfig& config) {
    config_ = config;
    static_.setCrackle(config.crackle);
    effects_.configure(config);
}

void Mixer::render(const int16_t* station, float signal, int16_t* out, size_t count,
                   int16_t* beforeVolume) {
    for (size_t i = 0; i < count; ++i) {
        signal_ += kSmoothing * (signal - signal_);
        const float stationGain = signal_;
        const float staticGain = config_.staticLevel * (1.0f - signal_) + config_.hiss;
        float x = station[i] / 32768.0f * stationGain + static_.next() * staticGain;
        float unscaled = 0;
        x = effects_.process(x, &unscaled);
        out[i] = toPcm(x);
        if (beforeVolume) beforeVolume[i] = toPcm(unscaled);
    }
}

}  // namespace radio
