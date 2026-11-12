#pragma once

// Remembers, in NVS, how far each power-on got, so a boot that hung can be
// diagnosed afterwards over serial ("bootlog"). A boot that never reaches
// setup() leaves no trace except a boot count that didn't go up.

#include <Arduino.h>

#include <cstdint>

namespace radio {

class BootLog {
public:
    enum Stage : uint8_t {
        kNone = 0,
        kSetup,       // setup() entered
        kDisplay,     // starting the screens
        kInput,       // starting encoder and pot
        kClock,       // reading the RTC
        kStorage,     // mounting the SD card
        kAudio,       // starting I2S
        kLoop,        // loop() running
        kFirstFrame,  // the round display has drawn a frame
    };

    // Reads what the previous boot reached, then starts this one's record.
    void begin();
    // Records progress. Stages only move forward; each costs a flash write.
    void mark(Stage s);
    void report(Print& out) const;

    static const char* name(Stage s);

private:
    bool open_ = false;
    Stage stage_ = kNone;
    Stage prevStage_ = kNone;
    uint8_t prevReason_ = 0;
    uint8_t reason_ = 0;
    uint32_t boots_ = 0;
    uint32_t watchdogResets_ = 0;
};

}  // namespace radio
