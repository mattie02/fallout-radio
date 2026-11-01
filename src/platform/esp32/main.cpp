// ESP32-S3 entry point.
//
// Serial (COM/UART port, 115200 baud) commands:
//   time YYYY-MM-DD HH:MM[:SS]   set the RTC (recipient's local time)
//   status                       print time, tuning and what's playing

#include <Arduino.h>

#include <cstring>

#include "core/CivilTime.h"
#include "core/Radio.h"
#include "platform/esp32/Esp32Platform.h"

namespace {

radio::Esp32Audio audio;
radio::Esp32Input input;
radio::Esp32Display display;
radio::Esp32Clock clock_;
radio::Esp32Storage storage;
radio::Platform platform{audio, input, display, clock_, storage};
radio::Radio radioApp(platform);

void printTime() {
    const radio::CivilTime t = radio::fromSeconds(clock_.now());
    static const char* kDays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    Serial.printf("time: %s %04d-%02d-%02d %02d:%02d:%02d%s\n", kDays[t.weekday()], t.year, t.month,
                  t.day, t.hour, t.minute, t.second, clock_.hasRtc() ? "" : " (no RTC found)");
}

void printStatus() {
    printTime();
    const radio::Radio::Status s = radioApp.status();
    Serial.printf("dial: %.1f MHz", s.dialMhz);
    if (s.station >= 0) {
        const auto& st = radioApp.stations()[s.station];
        Serial.printf("  station: %s  signal: %d%%", st.info.name.c_str(),
                      static_cast<int>(s.signal * 100 + 0.5f));
        if (s.playing) {
            Serial.printf("  playing: %s %u:%02u", s.entry.path, s.offsetMs / 60000,
                          s.offsetMs / 1000 % 60);
        }
    }
    Serial.printf("\nfree heap: %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
}

void runCommand(const char* line) {
    if (std::strncmp(line, "time ", 5) == 0) {
        radio::CivilTime t;
        if (!radio::parseCivilTime(line + 5, t)) {
            Serial.println("usage: time YYYY-MM-DD HH:MM[:SS]");
            return;
        }
        if (!clock_.set(radio::toSeconds(t))) Serial.println("warning: no RTC; time will be lost at power-off");
        printTime();
    } else if (std::strcmp(line, "status") == 0) {
        printStatus();
    } else {
        Serial.println("commands: time YYYY-MM-DD HH:MM[:SS] | status");
    }
}

// Non-blocking line reader.
void pollSerial() {
    static char line[64];
    static size_t len = 0;
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            line[len] = 0;
            if (len > 0) runCommand(line);
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const bool ok = radioApp.begin();

    Serial.println("\nVintage Radio");
    if (!clock_.hasRtc()) Serial.println("RTC: not found (check I2C wiring, address 0x68)");
    else if (clock_.wasReset()) Serial.println("RTC: had lost power; set to firmware build time. Use: time YYYY-MM-DD HH:MM");
    printTime();
    Serial.printf("stations: %u\n", static_cast<unsigned>(radioApp.stations().size()));
    for (const auto& s : radioApp.stations()) {
        Serial.printf("  %5.1f  %-24s %s\n", s.info.frequencyMhz, s.info.name.c_str(),
                      s.schedule.isOpen() ? "" : "(no schedule.txt)");
    }
    if (!ok) Serial.println("warning: some devices failed to start (SD card? amp? display?)");
}

void loop() {
    radioApp.update();
    pollSerial();
}
