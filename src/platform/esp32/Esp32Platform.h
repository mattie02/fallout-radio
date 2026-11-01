#pragma once

// ESP32-S3 platform layer. Stubs for now; real drivers arrive in build-plan
// step 5 (I2S, ESP32Encoder/PCNT, SSD1306, DS3231 via RTClib, SD over SPI).
// Pin assignments live in Pins.h.

#include "platform/Platform.h"

namespace radio {

class Esp32Audio : public AudioOut {
public:
    bool begin(uint32_t sampleRate) override;
    size_t availableFrames() override;
    size_t write(const int16_t* samples, size_t frames) override;
};

class Esp32Input : public Input {
public:
    bool begin() override;
    InputState read() override;
};

class Esp32Display : public Display {
public:
    bool begin() override;
    void show(const uint8_t* buffer) override;
};

class Esp32Clock : public Clock {
public:
    bool begin() override;
    int64_t now() override;
    uint32_t millis() override;
};

class Esp32Storage : public Storage {
public:
    bool begin() override;
    bool exists(const char* path) override;
    std::unique_ptr<File> open(const char* path) override;
    bool list(const char* dir, std::vector<std::string>& names) override;
};

}  // namespace radio
