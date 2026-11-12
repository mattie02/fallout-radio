#include "platform/esp32/BootLog.h"

#include <Preferences.h>
#include <esp_system.h>

namespace radio {

namespace {

Preferences bootPrefs;

const char* reasonName(uint8_t r) {
    switch (static_cast<esp_reset_reason_t>(r)) {
        case ESP_RST_POWERON: return "power-on or RST button";
        case ESP_RST_EXT: return "external reset";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog (hang)";
        case ESP_RST_WDT: return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT: return "brownout (supply dipped)";
        case ESP_RST_SDIO: return "SDIO";
        case ESP_RST_USB: return "USB";
        case ESP_RST_JTAG: return "JTAG";
        default: return "unknown";
    }
}

}  // namespace

const char* BootLog::name(Stage s) {
    switch (s) {
        case kNone: return "nothing recorded";
        case kSetup: return "setup started";
        case kDisplay: return "starting screens";
        case kInput: return "starting encoder/pot";
        case kClock: return "reading RTC";
        case kStorage: return "mounting SD card";
        case kAudio: return "starting audio";
        case kLoop: return "running";
        case kFirstFrame: return "running, dial drawn";
    }
    return "?";
}

void BootLog::begin() {
    reason_ = static_cast<uint8_t>(esp_reset_reason());
    open_ = bootPrefs.begin("boot", false);
    if (!open_) return;
    prevStage_ = static_cast<Stage>(bootPrefs.getUChar("stage", kNone));
    prevReason_ = bootPrefs.getUChar("reason", 0);
    boots_ = bootPrefs.getUInt("boots", 0) + 1;
    watchdogResets_ = bootPrefs.getUInt("wdt", 0);
    if (reason_ == ESP_RST_TASK_WDT || reason_ == ESP_RST_INT_WDT || reason_ == ESP_RST_WDT)
        bootPrefs.putUInt("wdt", ++watchdogResets_);
    bootPrefs.putUInt("boots", boots_);
    bootPrefs.putUChar("reason", reason_);
    mark(kSetup);
}

void BootLog::mark(Stage s) {
    if (s <= stage_) return;
    stage_ = s;
    if (open_) bootPrefs.putUChar("stage", s);
}

void BootLog::report(Print& out) const {
    out.printf("boot #%u: reset by %s; now %s\n", static_cast<unsigned>(boots_), reasonName(reason_),
               name(stage_));
    out.printf("previous boot: reset by %s; got as far as: %s\n", reasonName(prevReason_),
               name(prevStage_));
    out.printf("watchdog restarts (hangs caught) so far: %u\n", static_cast<unsigned>(watchdogResets_));
}

}  // namespace radio
