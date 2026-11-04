#include "ui/Waveform.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace radio {

namespace {

constexpr int kSamplesPerColumn = 4;
constexpr int kSpan = Framebuffer::kWidth * kSamplesPerColumn;
// Level that reaches the top and bottom of the screen. The effects chain's soft
// clipping tops out near 0.7 of full scale (at the default drive).
constexpr float kFullScale = 22000.0f;
// Must dip this far below zero before a rise counts, so hiss doesn't trigger.
constexpr int kTriggerArm = -400;

int findTrigger(const int16_t* s) {
    const int searchEnd = static_cast<int>(WaveformCapture::kSamples) - kSpan;
    bool armed = false;
    for (int i = 0; i < searchEnd; ++i) {
        if (s[i] < kTriggerArm) armed = true;
        else if (armed && s[i] >= 0) return i;
    }
    return 0;
}

int toY(int sample) {
    const float mid = (Framebuffer::kHeight - 1) / 2.0f;
    const float y = mid - sample / kFullScale * mid;
    return std::min(std::max(static_cast<int>(std::lround(y)), 0), Framebuffer::kHeight - 1);
}

}  // namespace

void WaveformCapture::push(const int16_t* samples, size_t count) {
    if (count > kSamples) {
        samples += count - kSamples;
        count = kSamples;
    }
    const size_t first = std::min(count, kSamples - head_);
    std::memcpy(ring_ + head_, samples, first * sizeof(int16_t));
    std::memcpy(ring_, samples + first, (count - first) * sizeof(int16_t));
    head_ = (head_ + count) % kSamples;
}

void WaveformCapture::snapshot(int16_t* out) const {
    const size_t newest = kSamples - head_;
    std::memcpy(out, ring_ + head_, newest * sizeof(int16_t));
    std::memcpy(out + newest, ring_, head_ * sizeof(int16_t));
}

void drawWaveform(const int16_t* samples, Framebuffer& fb) {
    fb.clear();
    const int16_t* s = samples + findTrigger(samples);
    int prevY = toY(s[0]);
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        // Cover every sample in the column, so short peaks aren't lost, and join
        // up with the previous column, so the trace stays one unbroken line.
        int top = prevY, bottom = prevY;
        for (int i = 0; i < kSamplesPerColumn; ++i) {
            const int y = toY(s[x * kSamplesPerColumn + i]);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            prevY = y;
        }
        fb.vLine(x, top, bottom - top + 1);
    }
}

}  // namespace radio
