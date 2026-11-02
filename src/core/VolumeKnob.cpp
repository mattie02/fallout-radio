#include "core/VolumeKnob.h"

#include <cmath>

namespace radio {

bool VolumeKnob::update(float raw) {
    raw = std::fmin(std::fmax(raw, 0.0f), 1.0f);
    filtered_ = filtered_ < 0 ? raw : filtered_ + kSmoothing * (raw - filtered_);

    const float p = std::fmin(std::fmax((filtered_ - kEnd) / (1.0f - 2 * kEnd), 0.0f), 1.0f);
    // Always land exactly on the ends, even when the step there is tiny.
    const bool snap = (p == 0.0f || p == 1.0f) && p != level_;
    if (level_ >= 0 && !snap && std::fabs(p - level_) < kHysteresis) return false;
    level_ = p;
    return true;
}

float VolumeKnob::curve(float level) {
    if (level <= 0) return 0;
    if (level >= 1) return 1;
    return std::pow(10.0f, kRangeDb * (level - 1.0f) / 20.0f);
}

}  // namespace radio
