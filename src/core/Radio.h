#pragma once

#include <cstdint>
#include <vector>

#include "core/Config.h"
#include "core/Framebuffer.h"
#include "core/Mixer.h"
#include "core/Schedule.h"
#include "core/StationPlayer.h"
#include "core/Tuner.h"
#include "platform/Platform.h"

namespace radio {

// Main state machine: reads the dial, tunes, plays the tuned station's
// schedule mixed with static, draws the display.
class Radio {
public:
    struct Station {
        StationInfo info;
        Schedule schedule;  // closed if schedule.txt is missing or invalid
    };

    // Snapshot for debugging / the simulator's window title.
    struct Status {
        float dialMhz = 0;
        int station = -1;  // index into stations(), -1 if nothing in range
        float signal = 0;
        bool playing = false;  // a schedule entry is known for the station
        Schedule::Entry entry;
        uint32_t offsetMs = 0;
    };

    explicit Radio(Platform& platform) : p_(platform), player_(platform.storage) {}

    bool begin();
    // Call as often as possible. Never blocks.
    void update();
    // Re-reads /system/config.json (signal and audio settings).
    void reloadConfig();

    // Dial position in tenths of MHz.
    int dial() const { return dial_; }
    int dialMin() const { return dialMin_; }
    int dialMax() const { return dialMax_; }
    float dialMhz() const { return dial_ / 10.0f; }

    const std::vector<Station>& stations() const { return stations_; }
    Status status() const;

private:
    static constexpr size_t kAudioBlock = 256;
    // Re-seek the player if it drifts this far from the clock.
    static constexpr int64_t kMaxDriftMs = 2000;

    void loadStations();
    void retune();
    int64_t localMs();
    void trackSchedule(int64_t nowMs);
    void syncPlayer(int64_t nowMs);
    void renderAudio();
    void render();

    Platform& p_;
    StationPlayer player_;
    Mixer mixer_;
    int playerStation_ = -1;
    int16_t stationBuf_[kAudioBlock] = {};
    int16_t outBuf_[kAudioBlock] = {};
    // Sub-second wall clock: Clock::now() has 1 s resolution.
    int64_t lastSec_ = INT64_MIN;
    uint32_t secStartMillis_ = 0;

    Framebuffer fb_;
    RadioConfig config_;
    std::vector<Station> stations_;
    Tuner tuner_;
    Tuner::Result tuned_;

    int dial_ = 986;
    int dialMin_ = 880;
    int dialMax_ = 1080;

    // Schedule entry currently on air for the tuned station.
    int entryStation_ = -1;
    bool haveEntry_ = false;
    Schedule::Position entry_;
    int64_t entryStartMs_ = 0;  // local wall-clock ms
    int64_t entryEndMs_ = 0;

    bool dirty_ = true;
    int64_t shownMinute_ = -1;
    uint32_t flashUntil_ = 0;
    bool flashing_ = false;
};

}  // namespace radio
