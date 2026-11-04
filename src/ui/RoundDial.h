#pragma once

#include <cstdint>
#include <memory>

#include "platform/Platform.h"

namespace radio {

// The dial on the 240x240 round color TFT: a backlit cream dial face with a
// brass rim, a needle sweeping the scale, a magic-eye tuning indicator, the
// clock, and a small volume arc in the gap at the bottom.
//
// The same code draws on the ESP32 and in the simulator, so what the simulator
// shows is what the panel gets, pixel for pixel. No hardware access here.
class RoundDial {
public:
    static constexpr int kSize = 240;

    RoundDial();
    ~RoundDial();

    // Allocates the frames (in PSRAM where there is any). False if out of memory.
    bool begin();
    // Draws a frame and returns its kSize x kSize pixels, valid until the next
    // draw: RGB565 with the high byte first in memory, as the panel wants it.
    // Returns nullptr if begin() hasn't succeeded.
    const uint16_t* draw(const DisplayState& state);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace radio
