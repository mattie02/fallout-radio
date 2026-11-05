#include "platform/esp32/Esp32Platform.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <string>

#include "core/CivilTime.h"
#include "platform/esp32/Pins.h"

namespace radio {

namespace {

// The OLED and RTC share one I2C bus.
void ensureI2c() {
    static bool started = false;
    if (!started) {
        Wire.begin(pins::kI2cSda, pins::kI2cScl, pins::kI2cHz);
        started = true;
    }
}

uint8_t fromBcd(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t toBcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

}  // namespace

// --- Audio: I2S -> MAX98357A ---------------------------------------------------

bool Esp32Audio::begin(uint32_t sampleRate) {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 512;  // 8 x 512 frames = ~93 ms of DMA buffering
    chan.auto_clear = true;    // underrun plays silence, not stale audio
    if (i2s_new_channel(&chan, &tx_, nullptr) != ESP_OK) {
        tx_ = nullptr;
        return false;
    }
    i2s_std_config_t cfg = {};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(pins::kI2sBclk);
    cfg.gpio_cfg.ws = static_cast<gpio_num_t>(pins::kI2sLrc);
    cfg.gpio_cfg.dout = static_cast<gpio_num_t>(pins::kI2sDout);
    cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_send_q_ovf = &Esp32Audio::onUnderrun;
    if (i2s_channel_init_std_mode(tx_, &cfg) != ESP_OK ||
        i2s_channel_register_event_callback(tx_, &callbacks, this) != ESP_OK ||
        i2s_channel_enable(tx_) != ESP_OK) {
        i2s_del_channel(tx_);
        tx_ = nullptr;
        return false;
    }
    return true;
}

bool IRAM_ATTR Esp32Audio::onUnderrun(i2s_chan_handle_t, i2s_event_data_t*, void* self) {
    static_cast<Esp32Audio*>(self)->underruns_ = static_cast<Esp32Audio*>(self)->underruns_ + 1;
    return false;
}

uint32_t Esp32Audio::takeUnderruns() {
    const uint32_t n = underruns_;
    underruns_ = 0;
    return n;
}

void Esp32Audio::pump() {
    for (;;) {
        if (stageSent_ == stageBytes_) {
            if (count_ == 0) return;
            const size_t n = std::min(count_, kStageFrames);
            size_t tail = (head_ + kRingFrames - count_) % kRingFrames;
            for (size_t i = 0; i < n; ++i) {
                stage_[2 * i] = stage_[2 * i + 1] = ring_[tail];
                tail = (tail + 1) % kRingFrames;
            }
            count_ -= n;
            stageBytes_ = n * 2 * sizeof(int16_t);
            stageSent_ = 0;
        }
        size_t written = 0;
        i2s_channel_write(tx_, reinterpret_cast<uint8_t*>(stage_) + stageSent_,
                          stageBytes_ - stageSent_, &written, 0);
        stageSent_ += written;
        if (written == 0) return;  // DMA full: try again next update
    }
}

size_t Esp32Audio::availableFrames() {
    if (!tx_) return 0;
    pump();
    return kRingFrames - count_;
}

size_t Esp32Audio::write(const int16_t* samples, size_t frames) {
    if (!tx_) return 0;
    const size_t n = std::min(frames, kRingFrames - count_);
    for (size_t i = 0; i < n; ++i) {
        ring_[head_] = samples[i];
        head_ = (head_ + 1) % kRingFrames;
    }
    count_ += n;
    pump();
    return n;
}

// --- Input: encoder on PCNT, button ------------------------------------------

bool Esp32Input::begin() {
    pinMode(pins::kEncoderA, INPUT_PULLUP);
    pinMode(pins::kEncoderB, INPUT_PULLUP);
    pinMode(pins::kEncoderSw, INPUT_PULLUP);
    analogReadResolution(12);
    analogSetPinAttenuation(pins::kVolumePot, ADC_11db);  // full 0..3.3 V range

    // Full quadrature (x4) decoding, as in the ESP-IDF rotary encoder example.
    pcnt_unit_config_t unit = {};
    unit.low_limit = -30000;
    unit.high_limit = 30000;
    unit.flags.accum_count = 1;  // keep counting across the limits
    if (pcnt_new_unit(&unit, &unit_) != ESP_OK) return false;

    pcnt_glitch_filter_config_t filter = {};
    filter.max_glitch_ns = 1000;
    pcnt_unit_set_glitch_filter(unit_, &filter);

    pcnt_chan_config_t a = {};
    a.edge_gpio_num = pins::kEncoderA;
    a.level_gpio_num = pins::kEncoderB;
    pcnt_chan_config_t b = {};
    b.edge_gpio_num = pins::kEncoderB;
    b.level_gpio_num = pins::kEncoderA;
    pcnt_channel_handle_t chA = nullptr, chB = nullptr;
    if (pcnt_new_channel(unit_, &a, &chA) != ESP_OK || pcnt_new_channel(unit_, &b, &chB) != ESP_OK)
        return false;
    pcnt_channel_set_edge_action(chA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(chB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_unit_add_watch_point(unit_, unit.low_limit);
    pcnt_unit_add_watch_point(unit_, unit.high_limit);

    return pcnt_unit_enable(unit_) == ESP_OK && pcnt_unit_clear_count(unit_) == ESP_OK &&
           pcnt_unit_start(unit_) == ESP_OK;
}

InputState Esp32Input::read() {
    InputState s;
    if (unit_) {
        int count = 0;
        pcnt_unit_get_count(unit_, &count);
        int delta = count - lastCount_;
        lastCount_ = count;
        if (pins::kEncoderReverse) delta = -delta;
        remainder_ += delta;
        s.dialSteps = remainder_ / pins::kEncoderCountsPerStep;
        remainder_ -= s.dialSteps * pins::kEncoderCountsPerStep;
    }

    // Button: active low, 25 ms debounce, report the press edge.
    const bool raw = digitalRead(pins::kEncoderSw) == LOW;
    const uint32_t now = ::millis();
    if (raw != rawPressed_) {
        rawPressed_ = raw;
        rawChangedMs_ = now;
    } else if (raw != stablePressed_ && now - rawChangedMs_ >= 25) {
        stablePressed_ = raw;
        s.pressed = raw;
    }

    // Volume pot: an averaged reading every 20 ms (each analogRead takes
    // ~10 us, so this costs well under 1% of the loop).
    if (static_cast<int32_t>(now - nextKnobMs_) >= 0) {
        nextKnobMs_ = now + 20;
        uint32_t sum = 0;
        for (int i = 0; i < 8; ++i) sum += analogRead(pins::kVolumePot);
        s.knob = sum / (8 * 4095.0f);
    }
    return s;
}

// --- Display: SSD1306 128x32 -------------------------------------------------

namespace {

bool oledCommands(std::initializer_list<uint8_t> bytes) {
    Wire.beginTransmission(pins::kOledAddress);
    Wire.write(0x00);  // control byte: commands follow
    for (uint8_t b : bytes) Wire.write(b);
    return Wire.endTransmission() == 0;
}

}  // namespace

bool Esp32Oled::begin() {
    ensureI2c();
    const bool ok = oledCommands({
        0xAE,        // display off
        0xD5, 0x80,  // clock divide
        0xA8, 0x1F,  // multiplex: 32 rows
        0xD3, 0x00,  // display offset
        0x40,        // start line 0
        0x8D, 0x14,  // charge pump on
        0x20, 0x00,  // horizontal addressing
        0xA1,        // segment remap (column 127 = SEG0)
        0xC8,        // COM scan descending
        0xDA, 0x02,  // COM pins for 128x32
        0x81, 0x8F,  // contrast
        0xD9, 0xF1,  // pre-charge
        0xDB, 0x40,  // VCOMH
        0xA4,        // show RAM
        0xA6,        // normal (not inverted)
        0x2E,        // scrolling off
        0xAF,        // display on
    });
    if (!ok) return false;
    // Core 0: the Arduino loop (and with it the audio) runs on core 1.
    return xTaskCreatePinnedToCore(taskEntry, "oled", 4096, this, 1, &task_, 0) == pdPASS;
}

void Esp32Oled::audio(const int16_t* samples, size_t count) {
    if (!task_) return;
    portENTER_CRITICAL(&lock_);
    capture_.push(samples, count);
    portEXIT_CRITICAL(&lock_);
}

Esp32Oled::Stats Esp32Oled::stats() const {
    Stats s;
    s.present = task_ != nullptr;
    s.frames = frames_;
    s.pushUs = pushUs_;
    return s;
}

void Esp32Oled::taskEntry(void* self) {
    static_cast<Esp32Oled*>(self)->run();
}

void Esp32Oled::run() {
    constexpr TickType_t kFrameTicks = pdMS_TO_TICKS(33);  // ~30 fps
    static int16_t samples[WaveformCapture::kSamples];
    static Framebuffer fb;
    static uint8_t sent[Framebuffer::kBufferSize];
    bool sentAny = false;

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, kFrameTicks);
        portENTER_CRITICAL(&lock_);
        capture_.snapshot(samples);
        portEXIT_CRITICAL(&lock_);
        drawWaveform(samples, fb);
        if (sentAny && std::memcmp(sent, fb.data(), sizeof(sent)) == 0) continue;  // e.g. silence
        std::memcpy(sent, fb.data(), sizeof(sent));
        sentAny = true;

        const uint32_t start = micros();
        oledCommands({0x21, 0, 127, 0x22, 0, 3});  // whole screen
        constexpr size_t kChunk = 64;  // fits the Wire buffer
        for (size_t i = 0; i < Framebuffer::kBufferSize; i += kChunk) {
            Wire.beginTransmission(pins::kOledAddress);
            Wire.write(0x40);  // control byte: data follows
            Wire.write(fb.data() + i, kChunk);
            Wire.endTransmission();
        }
        pushUs_ = micros() - start;
        ++frames_;
    }
}

// --- Clock: DS3231 -------------------------------------------------------------

namespace {

constexpr uint8_t kRtcStatus = 0x0F;
constexpr uint8_t kOscillatorStopped = 0x80;

bool rtcRead(uint8_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(pins::kRtcAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<uint8_t>(pins::kRtcAddress), static_cast<uint8_t>(n)) != n)
        return false;
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

bool rtcWrite(uint8_t reg, const uint8_t* data, size_t n) {
    Wire.beginTransmission(pins::kRtcAddress);
    Wire.write(reg);
    Wire.write(data, n);
    return Wire.endTransmission() == 0;
}

int64_t buildTime() {
    CivilTime t;
    parseBuildTime(__DATE__, __TIME__, t);
    return toSeconds(t);
}

}  // namespace

bool Esp32Clock::readRtc(int64_t& seconds) {
    uint8_t r[7];
    if (!rtcRead(0x00, r, sizeof(r))) return false;
    CivilTime t;
    t.second = fromBcd(r[0] & 0x7F);
    t.minute = fromBcd(r[1] & 0x7F);
    if (r[2] & 0x40) {  // 12-hour mode
        t.hour = fromBcd(r[2] & 0x1F) % 12 + ((r[2] & 0x20) ? 12 : 0);
    } else {
        t.hour = fromBcd(r[2] & 0x3F);
    }
    t.day = fromBcd(r[4] & 0x3F);
    t.month = fromBcd(r[5] & 0x1F);
    t.year = 2000 + fromBcd(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    if (t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 || t.minute > 59 ||
        t.second > 59)
        return false;
    seconds = toSeconds(t);
    return true;
}

void Esp32Clock::resync() {
    const uint32_t ms = ::millis();
    int64_t s;
    if (hasRtc_ && readRtc(s)) {
        baseSeconds_ = s;
        baseMillis_ = ms;
    } else {
        // No RTC: just rebase so millis() wrap-around (49 days) can't bite.
        const uint32_t whole = (ms - baseMillis_) / 1000;
        baseSeconds_ += whole;
        baseMillis_ += whole * 1000;
    }
}

bool Esp32Clock::begin() {
    ensureI2c();
    uint8_t status = 0;
    hasRtc_ = rtcRead(kRtcStatus, &status, 1);
    baseSeconds_ = buildTime();
    baseMillis_ = ::millis();
    if (!hasRtc_) return false;
    if (status & kOscillatorStopped) {
        // Battery was flat or removed: the time is garbage. Start from the
        // firmware build time; set the real time over serial.
        wasReset_ = true;
        set(baseSeconds_);
    }
    resync();
    return true;
}

bool Esp32Clock::set(int64_t localSeconds) {
    baseSeconds_ = localSeconds;
    baseMillis_ = ::millis();
    if (!hasRtc_) return false;
    const CivilTime t = fromSeconds(localSeconds);
    const uint8_t regs[7] = {
        toBcd(t.second), toBcd(t.minute), toBcd(t.hour),  // 24-hour mode
        static_cast<uint8_t>(t.weekday() + 1), toBcd(t.day),
        static_cast<uint8_t>(toBcd(t.month) | (t.year >= 2100 ? 0x80 : 0)),
        toBcd(t.year % 100),
    };
    if (!rtcWrite(0x00, regs, sizeof(regs))) return false;
    // Clear the oscillator-stopped flag: the time is valid again.
    uint8_t status = 0;
    if (!rtcRead(kRtcStatus, &status, 1)) return false;
    status &= static_cast<uint8_t>(~kOscillatorStopped);
    return rtcWrite(kRtcStatus, &status, 1);
}

int64_t Esp32Clock::now() {
    if (::millis() - baseMillis_ >= kResyncMs) resync();
    return baseSeconds_ + (::millis() - baseMillis_) / 1000;
}

uint32_t Esp32Clock::millis() { return ::millis(); }

// --- Storage: SD over SPI ------------------------------------------------------

namespace {

SPIClass sdSpi(FSPI);
Esp32Storage::Timing sdTiming;

void noteTime(uint32_t& slowest, uint32_t startUs) {
    const uint32_t took = micros() - startUs;
    if (took > slowest) slowest = took;
}

constexpr char kMount[] = "/sd";  // where SD.begin() mounts the card in the VFS

// Files open at once. Every station holds its schedule.txt open for good, and
// on top of those sit the playing MP3 and the odd short-lived config file. The
// library's default of 5 is used up by five stations, and then every MP3 fails
// to open and plays as silence. `prep.py check` enforces this: keep SD_MAX_FILES
// in tools/prep/radioprep/check.py the same.
constexpr uint8_t kSdMaxFiles = 12;

bool mountCard() {
    return SD.begin(pins::kSdCs, sdSpi, pins::kSdSpiHz, kMount, kSdMaxFiles);
}

// POSIX file descriptors on the mounted card: each read goes to the FAT
// driver in one call. (The Arduino File wrapper buffers 4 KB per read, so a
// 64-byte schedule record cost a full 4 KB card read; unbuffered stdio is
// worse still, as newlib then reads byte by byte.)
class SdFile : public File {
public:
    SdFile(int fd, uint32_t size) : fd_(fd), size_(size) {}
    ~SdFile() override { ::close(fd_); }
    size_t read(uint8_t* dst, size_t len) override {
        const uint32_t start = micros();
        const ssize_t n = ::read(fd_, dst, len);
        noteTime(sdTiming.slowestReadUs, start);
        if (n <= 0) return 0;
        pos_ += static_cast<uint32_t>(n);
        return static_cast<size_t>(n);
    }
    bool seek(uint32_t pos) override {
        if (::lseek(fd_, static_cast<off_t>(pos), SEEK_SET) < 0) return false;
        pos_ = pos;
        return true;
    }
    // Tracked here rather than asked of the card: after a card error lseek()
    // fails too, and a read error must not look like the end of the file.
    uint32_t position() override { return pos_; }
    uint32_t size() override { return size_; }

private:
    int fd_;
    uint32_t size_;
    uint32_t pos_ = 0;
};

}  // namespace

bool Esp32Storage::begin() {
    sdSpi.begin(pins::kSdSck, pins::kSdMiso, pins::kSdMosi, pins::kSdCs);
    ok_ = mountCard();
    return ok_;
}

bool Esp32Storage::exists(const char* path) {
    struct stat st;
    return ok_ && stat((std::string(kMount) + path).c_str(), &st) == 0;
}

std::unique_ptr<File> Esp32Storage::open(const char* path) {
    if (!ok_) return nullptr;
    const uint32_t start = micros();
    const int fd = ::open((std::string(kMount) + path).c_str(), O_RDONLY);
    struct stat st;
    const bool ok = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    noteTime(sdTiming.slowestOpenUs, start);
    ++sdTiming.opens;
    if (!ok) {
        if (fd >= 0) ::close(fd);
        return nullptr;
    }
    return std::unique_ptr<File>(new SdFile(fd, static_cast<uint32_t>(st.st_size)));
}

bool Esp32Storage::recover() {
    SD.end();
    ok_ = mountCard();
    return ok_;
}

Esp32Storage::Timing Esp32Storage::takeTiming() {
    const Timing t = sdTiming;
    sdTiming = {};
    return t;
}

bool Esp32Storage::list(const char* dir, std::vector<std::string>& names) {
    if (!ok_) return false;
    fs::File d = SD.open(dir);
    if (!d || !d.isDirectory()) return false;
    for (fs::File e = d.openNextFile(); e; e = d.openNextFile()) {
        const char* name = e.name();
        if (name && name[0] != '.') names.emplace_back(name);
    }
    return true;
}

// --- Settings: NVS -------------------------------------------------------------

namespace {

Preferences prefs;
constexpr char kDialKey[] = "dial";

}  // namespace

bool Esp32Settings::open() {
    if (!opened_) opened_ = prefs.begin("radio", false);
    return opened_;
}

bool Esp32Settings::loadDial(int& dial) {
    if (!open() || !prefs.isKey(kDialKey)) return false;
    dial = prefs.getInt(kDialKey, dial);
    return true;
}

void Esp32Settings::saveDial(int dial) {
    if (!open()) return;
    const uint32_t start = micros();
    prefs.putInt(kDialKey, dial);
    lastSaveUs_ = micros() - start;
    ++saves_;
}

}  // namespace radio
