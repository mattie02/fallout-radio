#include "core/WarmUp.h"

#include <algorithm>
#include <cstring>

namespace radio {

namespace {
constexpr char kBootPath[] = "/system/boot.mp3";
}  // namespace

void WarmUp::begin(Storage& storage) {
    auto file = storage.open(kBootPath);
    if (!file) return;
    const uint64_t total = static_cast<uint64_t>(file->size()) * kSampleRate / Mp3Decoder::kBytesPerSecond;
    if (!decoder_.open(std::move(file))) return;
    fadeStart_ = std::min(total > kFadeSamples ? total - kFadeSamples : 0, kMaxSilentSamples);
}

void WarmUp::stop() {
    decoder_.close();
    fadeStart_ = std::min(fadeStart_, rendered_);
}

void WarmUp::read(int16_t* out, size_t count) {
    size_t got = 0;
    if (decoder_.isOpen()) {
        got = decoder_.read(out, count);
        if (got < count || decoder_.failed()) stop();  // ended, maybe sooner than its size said
    }
    if (got < count) std::memset(out + got, 0, (count - got) * sizeof(int16_t));
    rendered_ += count;
}

float WarmUp::radioLevel() const {
    if (rendered_ <= fadeStart_) return 0;
    const uint64_t into = rendered_ - fadeStart_;
    return into >= kFadeSamples ? 1.0f : static_cast<float>(into) / kFadeSamples;
}

}  // namespace radio
