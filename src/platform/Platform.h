#pragma once

// Hardware interfaces. The core talks only to these; each target
// (platform/esp32, platform/desktop) provides an implementation.
// Plain C++17, no Arduino or SDL headers.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace radio {

constexpr int kDisplayWidth = 128;
constexpr int kDisplayHeight = 32;
// 1 bit per pixel in SSD1306 page order: byte (x + (y / 8) * width), bit (y % 8).
constexpr size_t kDisplayBufferSize = kDisplayWidth * kDisplayHeight / 8;

constexpr uint32_t kSampleRate = 44100;

// Mono 16-bit PCM output. Must never block.
class AudioOut {
public:
    virtual ~AudioOut() = default;
    virtual bool begin(uint32_t sampleRate) = 0;
    // Frames that can be written right now without blocking.
    virtual size_t availableFrames() = 0;
    // Returns the number of frames accepted.
    virtual size_t write(const int16_t* samples, size_t frames) = 0;
};

// Encoder input accumulated since the previous read(), plus the volume knob.
struct InputState {
    int dialSteps = 0;     // + clockwise, - counter-clockwise
    bool pressed = false;  // button was pressed at least once
    float knob = -1;       // volume knob position 0..1; -1 = no new reading
};

class Input {
public:
    virtual ~Input() = default;
    virtual bool begin() = 0;
    virtual InputState read() = 0;
};

class Display {
public:
    virtual ~Display() = default;
    virtual bool begin() = 0;
    // buffer holds kDisplayBufferSize bytes.
    virtual void show(const uint8_t* buffer) = 0;
};

class Clock {
public:
    virtual ~Clock() = default;
    virtual bool begin() = 0;
    // Recipient's local wall-clock time as seconds since 1970-01-01 00:00,
    // with no timezone applied (what the DS3231 stores).
    virtual int64_t now() = 0;
    // Monotonic milliseconds for UI timing. Unaffected by clock changes.
    virtual uint32_t millis() = 0;
};

class File {
public:
    virtual ~File() = default;
    virtual size_t read(uint8_t* dst, size_t len) = 0;
    virtual bool seek(uint32_t pos) = 0;
    virtual uint32_t position() = 0;
    virtual uint32_t size() = 0;
};

// Paths are absolute from the content root, e.g. "/system/config.json".
class Storage {
public:
    virtual ~Storage() = default;
    virtual bool begin() = 0;
    virtual bool exists(const char* path) = 0;
    // Returns nullptr if the file can't be opened.
    virtual std::unique_ptr<File> open(const char* path) = 0;
    // Names (not paths) of the entries in a folder, unsorted, skipping
    // hidden ones. Returns false if the folder can't be read.
    virtual bool list(const char* dir, std::vector<std::string>& names) = 0;
    // After read errors: try to bring the storage back (e.g. remount the SD
    // card). All open Files must be closed first. Returns true if it is
    // usable again. May take a while; called rarely.
    virtual bool recover() { return false; }
};

struct Platform {
    AudioOut& audio;
    Input& input;
    Display& display;
    Clock& clock;
    Storage& storage;
};

}  // namespace radio
