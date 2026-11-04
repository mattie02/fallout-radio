#pragma once

// macOS simulator platform layer, built on SDL2.

#include <SDL.h>

#include <string>

#include "platform/Platform.h"
#include "ui/Framebuffer.h"
#include "ui/RoundDial.h"

namespace radio {

class DesktopAudio : public AudioOut {
public:
    ~DesktopAudio() override;
    bool begin(uint32_t sampleRate) override;
    size_t availableFrames() override;
    size_t write(const int16_t* samples, size_t frames) override;

private:
    SDL_AudioDeviceID dev_ = 0;
    uint32_t targetQueueFrames_ = 0;
};

// Mouse wheel / left-right arrows turn the dial; click / Space press the
// button. Shift + arrow turns ten steps at a time. Up / down turn the
// volume knob.
class DesktopInput : public Input {
public:
    bool begin() override { return true; }
    InputState read() override;
    // Returns true if the event was an input event.
    bool handleEvent(const SDL_Event& e);

private:
    InputState pending_;
    float knob_ = 1;
};

// Shows either screen, scaled up, inside a bezel: the round TFT dial (masked to
// its circle) or the white-on-black 128x32 OLED. Both are drawn by the same code
// as on the radio.
class DesktopDisplay : public Display {
public:
    enum class Screen { Round, Oled };

    explicit DesktopDisplay(Screen screen) : screen_(screen) {}
    ~DesktopDisplay() override;
    bool begin() override;
    void show(const DisplayState& state) override;
    // Re-presents the last frame (e.g. after the window was exposed).
    void refresh();
    void setTitle(const std::string& title);
    void toggleScreen();

private:
    bool applyScreen();
    void drawRound(uint8_t* pixels, int pitch);
    void drawOled(uint8_t* pixels, int pitch);

    Screen screen_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    DisplayState state_;
    RoundDial round_;
    Framebuffer oled_;
};

// Simulated wall clock with pause, speed-up and jump, for testing schedules.
class DesktopClock : public Clock {
public:
    bool begin() override;
    int64_t now() override;
    uint32_t millis() override;

    void setTime(int64_t localSeconds);
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    void setSpeed(double speed);
    double speed() const { return speed_; }
    // Back to the computer's real local time at 1x.
    void resetToRealTime();

    // Current computer local time as naive seconds since 1970.
    static int64_t realLocalTime();
    // Parses "YYYY-MM-DD HH:MM[:SS]" into naive local seconds.
    static bool parseTime(const char* text, int64_t& out);
    static std::string format(int64_t localSeconds);

private:
    void reanchor();

    double anchorTime_ = 0;  // simulated seconds at anchorTicks_
    uint64_t anchorTicks_ = 0;
    double speed_ = 1.0;
    bool paused_ = false;
};

// Reads files from a local folder standing in for the SD card.
class DesktopStorage : public Storage {
public:
    explicit DesktopStorage(std::string root) : root_(std::move(root)) {}
    bool begin() override;
    bool exists(const char* path) override;
    std::unique_ptr<File> open(const char* path) override;
    bool list(const char* dir, std::vector<std::string>& names) override;

private:
    std::string resolve(const char* path) const;
    std::string root_;
};

}  // namespace radio
