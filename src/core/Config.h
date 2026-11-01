#pragma once

#include <string>

#include "platform/Platform.h"

namespace radio {

// /system/config.json
struct RadioConfig {
    float dialMinMhz = 88.0f;
    float dialMaxMhz = 108.0f;
    float lockMhz = 0.1f;  // full signal within this distance of a station
    float fadeMhz = 0.4f;  // then fades to nothing over this distance
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
