#pragma once

// GC9A01 240x240 round TFT, driven by LovyanGFX on its own SPI bus.
//
// Drawing and pushing a frame takes tens of milliseconds, far too long for the
// audio loop, so a task on the other core does it. show() only hands over the
// latest state and returns; if states come faster than frames (spinning the
// dial) the ones in between are skipped.

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "platform/Platform.h"

namespace radio {

class Esp32Tft : public Display {
public:
    bool begin() override;
    void show(const DisplayState& state) override;

    struct Stats {
        bool ready = false;  // false if the frames couldn't be allocated
        uint32_t frames = 0;
        uint32_t drawUs = 0;  // last frame
        uint32_t pushUs = 0;
        uint32_t rows = 0;  // rows that changed and were sent
    };
    Stats stats() const;

private:
    static void taskEntry(void* self);
    void run();

    TaskHandle_t task_ = nullptr;
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    DisplayState pending_;
    bool hasPending_ = false;

    std::atomic<bool> ready_{false};
    std::atomic<uint32_t> frames_{0}, drawUs_{0}, pushUs_{0}, rows_{0};
};

}  // namespace radio
