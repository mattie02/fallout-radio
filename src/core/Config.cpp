#include "core/Config.h"

// ArduinoJson is portable C++ (no Arduino headers needed), so it is allowed in core/.
#include <ArduinoJson.h>

namespace radio {

namespace {

bool readJson(Storage& storage, const std::string& path, JsonDocument& doc) {
    auto file = storage.open(path.c_str());
    if (!file) return false;
    std::string text(file->size(), '\0');
    if (file->read(reinterpret_cast<uint8_t*>(&text[0]), text.size()) != text.size()) return false;
    return !deserializeJson(doc, text.data(), text.size());
}

}  // namespace

bool loadRadioConfig(Storage& storage, RadioConfig& config) {
    JsonDocument doc;
    if (!readJson(storage, "/system/config.json", doc)) return false;
    config.dialMinMhz = doc["dial"]["min"] | config.dialMinMhz;
    config.dialMaxMhz = doc["dial"]["max"] | config.dialMaxMhz;
    config.lockMhz = doc["signal"]["lock_mhz"] | config.lockMhz;
    config.fadeMhz = doc["signal"]["fade_mhz"] | config.fadeMhz;
    return true;
}

bool loadStationInfo(Storage& storage, const std::string& dir, StationInfo& out) {
    JsonDocument doc;
    if (!readJson(storage, "/stations/" + dir + "/station.json", doc)) return false;
    out.dir = dir;
    out.name = doc["name"] | dir.c_str();
    out.frequencyMhz = doc["frequency"] | 0.0f;
    return out.frequencyMhz > 0;
}

}  // namespace radio
