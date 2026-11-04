#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/Framebuffer.h"

namespace radio {

// Keeps the most recent audio for drawing. Not thread-safe: on the ESP32 the
// owner guards it, since audio arrives on one core and is drawn on the other.
class WaveformCapture {
public:
    static constexpr size_t kSamples = 1024;  // ~23 ms

    void push(const int16_t* samples, size_t count);
    // Copies out the last kSamples, oldest first.
    void snapshot(int16_t* out) const;

private:
    int16_t ring_[kSamples] = {};
    size_t head_ = 0;
};

// Draws kSamples of audio as an oscilloscope trace, about 12 ms across. The
// trace starts at a rising zero crossing, so a steady tone stands still rather
// than sliding sideways.
void drawWaveform(const int16_t* samples, Framebuffer& fb);

}  // namespace radio
