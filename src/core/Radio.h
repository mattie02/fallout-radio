#pragma once

#include <cstdint>

#include "core/Framebuffer.h"
#include "platform/Platform.h"

namespace radio {

// Main state machine. For now: reads the dial and draws it.
// Schedule, Tuner, Mixer and Effects plug in here in later steps.
class Radio {
public:
    // Dial range in tenths of MHz (88.0 – 108.0). Will come from
    // /system/config.json once the Tuner exists.
    static constexpr int kDialMin = 880;
    static constexpr int kDialMax = 1080;

    explicit Radio(Platform& platform) : p_(platform) {}

    bool begin();
    // Call as often as possible. Never blocks.
    void update();

    int dial() const { return dial_; }

private:
    void render();

    Platform& p_;
    Framebuffer fb_;
    int dial_ = 986;
    bool dirty_ = true;
    int64_t shownMinute_ = -1;
    uint32_t flashUntil_ = 0;
    bool flashing_ = false;
};

}  // namespace radio
