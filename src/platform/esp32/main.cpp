// ESP32-S3 entry point.
//
// Serial (COM/UART port, 115200 baud) commands:
//   time YYYY-MM-DD HH:MM[:SS]   set the RTC (recipient's local time)
//   tune MHZ                     move the dial, e.g. "tune 91.5"
//   status                       print time, tuning and what's playing
//   display                      print stats for both screens

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "core/CivilTime.h"
#include "core/Radio.h"
#include "platform/esp32/Esp32Platform.h"
#include "platform/esp32/Esp32Tft.h"

// minimp3 keeps ~16 KB of scratch on the stack while decoding a frame; the
// default 8 KB loop task stack overflows (and the board resets) on the first
// decode.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

namespace {

radio::Esp32Audio audio;
radio::Esp32Input input;
radio::Esp32Oled oled;
radio::Esp32Tft tft;

// The round TFT shows the dial, the OLED the audio.
class Screens : public radio::Display {
public:
    bool begin() override {
        const bool hasOled = oled.begin();
        Serial.printf("OLED: %s\n", hasOled ? "found" : "not found");
        return tft.begin();
    }
    void show(const radio::DisplayState& state) override { tft.show(state); }
    void audio(const int16_t* samples, size_t count) override { oled.audio(samples, count); }
};

Screens display;
radio::Esp32Clock clock_;
radio::Esp32Storage storage;
radio::Esp32Settings settings;
radio::Platform platform{audio, input, display, clock_, storage, settings};
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
    if (s.knob >= 0) Serial.printf("\nvolume knob: %d%%", static_cast<int>(s.knob * 100 + 0.5f));
    Serial.printf("\nfree heap: %u bytes, loop stack never used: %u bytes\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

// Loop timing, reset by printAudio().
uint32_t loopCount = 0;
uint32_t slowestLoopUs = 0;
uint32_t statsSinceMs = 0;

void printAudio() {
    const radio::Radio::AudioStats a = radioApp.audioStats();
    const uint32_t ms = millis() - statsSinceMs;
    Serial.printf("audio over %u ms: rendered %u frames (%.0f/s, need 44100/s), underruns %u\n", ms,
                  a.framesRendered, ms ? a.framesRendered * 1000.0 / ms : 0.0, audio.takeUnderruns());
    Serial.printf("  decoder %s, %d Hz, station peak %.3f, output peak %.3f, sd recoveries %u\n",
                  a.decoderOpen ? "open" : "CLOSED", a.sampleRate, a.stationPeak, a.outputPeak,
                  a.storageRecoveries);
    Serial.printf("  loop: %u passes, slowest %u us\n", loopCount, slowestLoopUs);
    const radio::Esp32Storage::Timing sd = storage.takeTiming();
    Serial.printf("  sd: %u opens, slowest open %u us, slowest read %u us\n", sd.opens,
                  sd.slowestOpenUs, sd.slowestReadUs);
    loopCount = slowestLoopUs = 0;
    statsSinceMs = millis();
}

void printDisplay() {
    const radio::Esp32Tft::Stats t = tft.stats();
    Serial.printf("round display: %s, %u frames, last draw %u us, last push %u us (%u rows)\n",
                  t.ready ? "running" : "NOT RUNNING (no memory for frames?)", t.frames, t.drawUs,
                  t.pushUs, t.rows);
    const radio::Esp32Oled::Stats o = oled.stats();
    if (o.present) Serial.printf("waveform: %u frames, last push %u us\n", o.frames, o.pushUs);
    else Serial.println("waveform: no OLED");
    Serial.printf("dial saves: %u, last took %u us\n", settings.saves(), settings.lastSaveUs());
    Serial.printf("psram: %u bytes, %u free\n", static_cast<unsigned>(ESP.getPsramSize()),
                  static_cast<unsigned>(ESP.getFreePsram()));
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
    } else if (std::strcmp(line, "audio") == 0) {
        printAudio();
    } else if (std::strcmp(line, "display") == 0) {
        printDisplay();
    } else if (std::strncmp(line, "volume ", 7) == 0) {
        radioApp.setVolume(static_cast<float>(std::atof(line + 7)));
        Serial.printf("volume %.2f\n", std::atof(line + 7));
    } else if (std::strncmp(line, "tune ", 5) == 0) {
        radioApp.tuneTo(static_cast<float>(std::atof(line + 5)));
        printStatus();
    } else {
        Serial.println("commands: time YYYY-MM-DD HH:MM[:SS] | tune MHZ | volume 0..1 | status | audio | display");
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
    const uint32_t start = micros();
    radioApp.update();
    const uint32_t took = micros() - start;
    ++loopCount;
    if (took > slowestLoopUs) slowestLoopUs = took;
    pollSerial();
}
