#pragma once

#include <string>

#include "platform/Platform.h"

namespace radio {

// "audio" in /system/config.json. Tunable by ear: the simulator reloads
// the file when you press C.
struct AudioConfig {
    float volume = 0.7f;        // master, 0..1
    float staticLevel = 0.5f;   // static loudness with no station
    float hiss = 0.03f;         // static left over when fully tuned
    float crackle = 6.0f;       // pops per second in the static
    float lowHz = 250.0f;       // vintage band-pass: high-pass corner
    float highHz = 3800.0f;     //   and low-pass corner
    float drive = 1.4f;         // soft saturation; 1 = clean
    float bootLevel = 0.7f;     // loudness of /system/boot.mp3
};

// /system/config.json
struct RadioConfig {
    float dialMinMhz = 88.0f;
    float dialMaxMhz = 108.0f;
    float lockMhz = 0.1f;  // full signal within this distance of a station
    float fadeMhz = 0.4f;  // then fades to nothing over this distance
    AudioConfig audio;
};

// /stations/<dir>/station.json (only what the firmware needs; the schedule
// rules in it are for the prep tool)
struct StationInfo {
    std::string dir;  // folder name, e.g. "01_crooner"
    std::string name;
    float frequencyMhz = 0;
};

// Missing keys keep their defaults. Returns false if the file is missing or
// isn't valid JSON (config is then left at defaults).
bool loadRadioConfig(Storage& storage, RadioConfig& config);

// Returns false if station.json is missing, invalid or has no frequency.
bool loadStationInfo(Storage& storage, const std::string& dir, StationInfo& out);

}  // namespace radio
