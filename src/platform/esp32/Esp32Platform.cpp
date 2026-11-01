#include "platform/esp32/Esp32Platform.h"

#include <Arduino.h>

#include "platform/esp32/Pins.h"

namespace radio {

// --- Audio (TODO: I2S to MAX98357A) -----------------------------------------

bool Esp32Audio::begin(uint32_t) { return true; }

size_t Esp32Audio::availableFrames() { return 0; }

size_t Esp32Audio::write(const int16_t*, size_t) { return 0; }

// --- Input (TODO: ESP32Encoder on PCNT) -------------------------------------

bool Esp32Input::begin() {
    pinMode(pins::kEncoderSw, INPUT_PULLUP);
    return true;
}

InputState Esp32Input::read() { return {}; }

// --- Display (TODO: SSD1306 over I2C) ---------------------------------------

bool Esp32Display::begin() { return true; }

void Esp32Display::show(const uint8_t*) {}

// --- Clock (TODO: DS3231 via RTClib) ----------------------------------------

bool Esp32Clock::begin() { return true; }

int64_t Esp32Clock::now() { return ::millis() / 1000; }

uint32_t Esp32Clock::millis() { return ::millis(); }

// --- Storage (TODO: SD over SPI) --------------------------------------------

bool Esp32Storage::begin() { return true; }

bool Esp32Storage::exists(const char*) { return false; }

std::unique_ptr<File> Esp32Storage::open(const char*) { return nullptr; }

bool Esp32Storage::list(const char*, std::vector<std::string>&) { return false; }

}  // namespace radio
