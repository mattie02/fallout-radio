#pragma once

// ESP32-S3 platform layer. Pin assignments live in Pins.h.
//   Audio:   I2S (ESP-IDF 5 std driver) -> MAX98357A
//   Input:   KY-040 encoder on the PCNT hardware counter + debounced button
//   Display: SSD1306 128x32 over I2C (minimal driver, no library)
//   Clock:   DS3231 over I2C, read occasionally and extrapolated with millis()
//   Storage: SD card over SPI

#include <driver/i2s_std.h>
#include <driver/pulse_cnt.h>

#include "platform/Platform.h"

namespace radio {

// Non-blocking: write() fills a ring buffer that is pumped into the I2S DMA
// buffers with zero timeout. If the loop stalls, the DMA plays silence
// (auto_clear) rather than repeating old audio.
class Esp32Audio : public AudioOut {
public:
    bool begin(uint32_t sampleRate) override;
    size_t availableFrames() override;
    size_t write(const int16_t* samples, size_t frames) override;

    // Times the DMA ran dry since the last call (diagnostics).
    uint32_t takeUnderruns();

private:
    void pump();
    static bool onUnderrun(i2s_chan_handle_t, i2s_event_data_t*, void* self);

    volatile uint32_t underruns_ = 0;

    // ~46 ms on top of ~93 ms DMA. Kept small: every frame buffered here is
    // delay between turning the dial and hearing it.
    static constexpr size_t kRingFrames = 2048;
    static constexpr size_t kStageFrames = 256;
    i2s_chan_handle_t tx_ = nullptr;
    int16_t ring_[kRingFrames] = {};
    size_t head_ = 0, count_ = 0;
    int16_t stage_[kStageFrames * 2] = {};  // stereo: the amp mixes L+R
    size_t stageBytes_ = 0, stageSent_ = 0;
};

class Esp32Input : public Input {
public:
    bool begin() override;
    InputState read() override;

private:
    pcnt_unit_handle_t unit_ = nullptr;
    int lastCount_ = 0;
    int remainder_ = 0;
    bool rawPressed_ = false, stablePressed_ = false;
    uint32_t rawChangedMs_ = 0;
};

class Esp32Display : public Display {
public:
    bool begin() override;
    void show(const uint8_t* buffer) override;

private:
    bool ok_ = false;
};

class Esp32Clock : public Clock {
public:
    bool begin() override;
    int64_t now() override;
    uint32_t millis() override;

    // Sets the DS3231 (local time, naive seconds). Returns false if no RTC.
    bool set(int64_t localSeconds);
    bool hasRtc() const { return hasRtc_; }
    // True if the RTC had lost its time and was set from the build time.
    bool wasReset() const { return wasReset_; }

private:
    bool readRtc(int64_t& seconds);
    void resync();

    static constexpr uint32_t kResyncMs = 10 * 60 * 1000;
    bool hasRtc_ = false;
    bool wasReset_ = false;
    int64_t baseSeconds_ = 0;
    uint32_t baseMillis_ = 0;
};

class Esp32Storage : public Storage {
public:
    bool begin() override;
    bool exists(const char* path) override;
    std::unique_ptr<File> open(const char* path) override;
    bool list(const char* dir, std::vector<std::string>& names) override;
    bool recover() override;  // unmount and remount the card

    // Diagnostics: slowest open() and file read since the last call, in us.
    struct Timing {
        uint32_t slowestOpenUs = 0;
        uint32_t slowestReadUs = 0;
        uint32_t opens = 0;
    };
    Timing takeTiming();

private:
    bool ok_ = false;
};

}  // namespace radio
