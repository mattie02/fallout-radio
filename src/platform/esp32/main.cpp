// ESP32-S3 entry point.
//
// Serial commands, on either USB port: COM/UART (115200 baud) or native USB.
// Replies go back to the port the command came in on; the boot report goes to
// COM only.
//   time YYYY-MM-DD HH:MM[:SS]   set the RTC (recipient's local time)
//   tune MHZ                     move the dial, e.g. "tune 91.5"
//   status                       print time, tuning and what's playing
//   display                      print stats for both screens
//   bootlog                      how far this and the previous power-on got

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include <esp_task_wdt.h>

#include "core/CivilTime.h"
#include "core/Radio.h"
#include "platform/esp32/BootLog.h"
#include "platform/esp32/Esp32Platform.h"
#include "platform/esp32/Esp32Tft.h"

// minimp3 keeps ~16 KB of scratch on the stack while decoding a frame; the
// default 8 KB loop task stack overflows (and the board resets) on the first
// decode.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

namespace {

// If startup or the loop stalls this long, the task watchdog restarts the
// board, as pressing RST would. Startup takes ~2 s, more with a slow SD card.
constexpr uint32_t kHangTimeoutMs = 15000;

radio::BootLog bootLog;

// The platform devices, each noting in the boot log when it starts, so a boot
// that hangs shows where.
struct LoggedAudio : radio::Esp32Audio {
    bool begin(uint32_t rate) override {
        bootLog.mark(radio::BootLog::kAudio);
        return Esp32Audio::begin(rate);
    }
};
struct LoggedInput : radio::Esp32Input {
    bool begin() override {
        bootLog.mark(radio::BootLog::kInput);
        return Esp32Input::begin();
    }
};
struct LoggedClock : radio::Esp32Clock {
    bool begin() override {
        bootLog.mark(radio::BootLog::kClock);
        return Esp32Clock::begin();
    }
};
struct LoggedStorage : radio::Esp32Storage {
    bool begin() override {
        bootLog.mark(radio::BootLog::kStorage);
        return Esp32Storage::begin();
    }
};

LoggedAudio audio;
LoggedInput input;
radio::Esp32Oled oled;
radio::Esp32Tft tft;

// The round TFT shows the dial, the OLED the audio.
class Screens : public radio::Display {
public:
    bool begin() override {
        bootLog.mark(radio::BootLog::kDisplay);
        const bool hasOled = oled.begin();
        Serial.printf("OLED: %s\n", hasOled ? "found" : "not found");
        return tft.begin();
    }
    void show(const radio::DisplayState& state) override { tft.show(state); }
    void audio(const int16_t* samples, size_t count) override { oled.audio(samples, count); }
};

Screens display;
LoggedClock clock_;
LoggedStorage storage;
radio::Esp32Settings settings;
radio::Platform platform{audio, input, display, clock_, storage, settings};
radio::Radio radioApp(platform);

// The native USB port. The core only creates this object when that port is the
// main console (ARDUINO_USB_CDC_ON_BOOT), so make our own. Its writes are
// skipped while no host is connected, so they never stall the audio loop.
HWCDC usbSerial;

// Where command replies go: the port the current command came in on.
Print* out = &Serial;

void printTime() {
    const radio::CivilTime t = radio::fromSeconds(clock_.now());
    static const char* kDays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    out->printf("time: %s %04d-%02d-%02d %02d:%02d:%02d%s\n", kDays[t.weekday()], t.year, t.month,
                  t.day, t.hour, t.minute, t.second, clock_.hasRtc() ? "" : " (no RTC found)");
}

void printStatus() {
    printTime();
    const radio::Radio::Status s = radioApp.status();
    out->printf("dial: %.1f MHz", s.dialMhz);
    if (s.station >= 0) {
        const auto& st = radioApp.stations()[s.station];
        out->printf("  station: %s  signal: %d%%", st.info.name.c_str(),
                      static_cast<int>(s.signal * 100 + 0.5f));
        if (s.playing) {
            out->printf("  playing: %s %u:%02u", s.entry.path, s.offsetMs / 60000,
                          s.offsetMs / 1000 % 60);
        }
    }
    if (s.knob >= 0) out->printf("\nvolume knob: %d%%", static_cast<int>(s.knob * 100 + 0.5f));
    out->printf("\nfree heap: %u bytes, loop stack never used: %u bytes\n",
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
    out->printf("audio over %u ms: rendered %u frames (%.0f/s, need 44100/s), underruns %u\n", ms,
                  a.framesRendered, ms ? a.framesRendered * 1000.0 / ms : 0.0, audio.takeUnderruns());
    out->printf("  decoder %s, %d Hz, station peak %.3f, output peak %.3f, sd recoveries %u\n",
                  a.decoderOpen ? "open" : "CLOSED", a.sampleRate, a.stationPeak, a.outputPeak,
                  a.storageRecoveries);
    out->printf("  loop: %u passes, slowest %u us\n", loopCount, slowestLoopUs);
    const radio::Esp32Storage::Timing sd = storage.takeTiming();
    out->printf("  sd: %u opens, slowest open %u us, slowest read %u us\n", sd.opens,
                  sd.slowestOpenUs, sd.slowestReadUs);
    loopCount = slowestLoopUs = 0;
    statsSinceMs = millis();
}

void printDisplay() {
    const radio::Esp32Tft::Stats t = tft.stats();
    out->printf("round display: %s, %u frames, last draw %u us, last push %u us (%u rows)\n",
                  t.ready ? "running" : "NOT RUNNING (no memory for frames?)", t.frames, t.drawUs,
                  t.pushUs, t.rows);
    const radio::Esp32Oled::Stats o = oled.stats();
    if (o.present) out->printf("waveform: %u frames, last push %u us\n", o.frames, o.pushUs);
    else out->println("waveform: no OLED");
    out->printf("dial saves: %u, last took %u us\n", settings.saves(), settings.lastSaveUs());
    out->printf("psram: %u bytes, %u free\n", static_cast<unsigned>(ESP.getPsramSize()),
                  static_cast<unsigned>(ESP.getFreePsram()));
}

void runCommand(const char* line) {
    if (std::strncmp(line, "time ", 5) == 0) {
        radio::CivilTime t;
        if (!radio::parseCivilTime(line + 5, t)) {
            out->println("usage: time YYYY-MM-DD HH:MM[:SS]");
            return;
        }
        if (!clock_.set(radio::toSeconds(t))) out->println("warning: no RTC; time will be lost at power-off");
        printTime();
    } else if (std::strcmp(line, "status") == 0) {
        printStatus();
    } else if (std::strcmp(line, "audio") == 0) {
        printAudio();
    } else if (std::strcmp(line, "display") == 0) {
        printDisplay();
    } else if (std::strcmp(line, "bootlog") == 0) {
        bootLog.report(*out);
    } else if (std::strncmp(line, "volume ", 7) == 0) {
        radioApp.setVolume(static_cast<float>(std::atof(line + 7)));
        out->printf("volume %.2f\n", std::atof(line + 7));
    } else if (std::strncmp(line, "tune ", 5) == 0) {
        radioApp.tuneTo(static_cast<float>(std::atof(line + 5)));
        printStatus();
    } else {
        out->println("commands: time YYYY-MM-DD HH:MM[:SS] | tune MHZ | volume 0..1 | status | audio | display | bootlog");
    }
}

// Non-blocking line reader, one per port.
class LineReader {
public:
    explicit LineReader(Stream& port) : port_(port) {}
    void poll() {
        while (port_.available() > 0) {
            const char c = static_cast<char>(port_.read());
            if (c == '\n' || c == '\r') {
                line_[len_] = 0;
                if (len_ > 0) {
                    out = &port_;
                    runCommand(line_);
                    out = &Serial;
                }
                len_ = 0;
            } else if (len_ < sizeof(line_) - 1) {
                line_[len_++] = c;
            }
        }
    }

private:
    Stream& port_;
    char line_[64];
    size_t len_ = 0;
};

LineReader comReader(Serial);     // COM/UART port
LineReader usbReader(usbSerial);  // native USB port (USB Serial/JTAG)

void pollSerial() {
    comReader.poll();
    usbReader.poll();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    usbSerial.begin();
    // Watch this task (setup, then loop): a hang restarts the board.
    esp_task_wdt_config_t wdt = {};
    wdt.timeout_ms = kHangTimeoutMs;
    wdt.idle_core_mask = 1 << 0;  // as configured: core 0's idle task
    wdt.trigger_panic = true;
    if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
    esp_task_wdt_add(nullptr);
    bootLog.begin();
    const bool ok = radioApp.begin();
    const uint32_t bootMs = millis();

    Serial.printf("\nVintage Radio (ready %u ms after power-on)\n", bootMs);
    if (!clock_.hasRtc()) Serial.println("RTC: not found (check I2C wiring, address 0x68)");
    else if (clock_.wasReset()) Serial.println("RTC: had lost power; set to firmware build time. Use: time YYYY-MM-DD HH:MM");
    printTime();
    Serial.printf("stations: %u\n", static_cast<unsigned>(radioApp.stations().size()));
    for (const auto& s : radioApp.stations()) {
        Serial.printf("  %5.1f  %-24s %s\n", s.info.frequencyMhz, s.info.name.c_str(),
                      s.schedule.isOpen() ? "" : "(no schedule.txt)");
    }
    if (!ok) Serial.println("warning: some devices failed to start (SD card? amp? display?)");
    bootLog.report(Serial);
}

void loop() {
    esp_task_wdt_reset();
    bootLog.mark(radio::BootLog::kLoop);
    static bool drawn = false;
    if (!drawn && tft.stats().frames > 0) {
        drawn = true;
        bootLog.mark(radio::BootLog::kFirstFrame);
    }
    const uint32_t start = micros();
    radioApp.update();
    const uint32_t took = micros() - start;
    ++loopCount;
    if (took > slowestLoopUs) slowestLoopUs = took;
    pollSerial();
}
