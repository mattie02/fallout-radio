// Desktop simulator entry point.
//
//   program [--content DIR] [--time "YYYY-MM-DD HH:MM[:SS]"]
//
// Dial:   mouse wheel, ← →  (Shift for 1 MHz steps)
// Button: click, Space
// Clock:  P pause/resume, ] faster, [ slower, R back to real time
// C reloads system/config.json (tune the sound by ear). Esc quits.

// Excluded from `pio test` builds, which bring their own main().
#ifndef PIO_UNIT_TESTING

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "core/Radio.h"
#include "platform/desktop/DesktopPlatform.h"

namespace {

constexpr double kSpeeds[] = {1, 10, 60, 600, 3600, 86400};
constexpr int kSpeedCount = sizeof(kSpeeds) / sizeof(kSpeeds[0]);

std::string mmss(uint32_t ms) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u:%02u", ms / 60000, ms / 1000 % 60);
    return buf;
}

// "91.5 MHz | Crooner Ballads 100% | music/012_x.mp3 1:23/3:05 | Thu 2026-12-24 23:58:00 x1"
std::string statusLine(const radio::Radio& radio, radio::DesktopClock& clock) {
    const radio::Radio::Status s = radio.status();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f MHz", s.dialMhz);
    std::string line = buf;
    if (s.station >= 0) {
        const auto& station = radio.stations()[s.station];
        std::snprintf(buf, sizeof(buf), " %d%%", static_cast<int>(s.signal * 100 + 0.5f));
        line += "  |  " + station.info.name + buf + "  |  ";
        if (s.playing) {
            line += s.entry.isSilence() ? std::string("(silence)") : std::string(s.entry.path);
            line += " " + mmss(s.offsetMs) + "/" + mmss(s.entry.durationMs);
        } else {
            line += station.schedule.isOpen() ? "(lookup failed)" : "(no schedule.txt)";
        }
    } else {
        line += "  |  static";
    }
    std::snprintf(buf, sizeof(buf), " x%g", clock.speed());
    line += "  |  " + radio::DesktopClock::format(clock.now()) + buf;
    if (clock.paused()) line += " PAUSED";
    return line;
}

void usage() {
    std::fprintf(stderr, "usage: program [--content DIR] [--time \"YYYY-MM-DD HH:MM[:SS]\"]\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string contentDir = "content";
    const char* startTime = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--content") && i + 1 < argc) {
            contentDir = argv[++i];
        } else if (!std::strcmp(argv[i], "--time") && i + 1 < argc) {
            startTime = argv[++i];
        } else {
            usage();
            return 2;
        }
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    {
        radio::DesktopAudio audio;
        radio::DesktopInput input;
        radio::DesktopDisplay display;
        radio::DesktopClock clock;
        radio::DesktopStorage storage(contentDir);
        radio::Platform platform{audio, input, display, clock, storage};

        clock.resetToRealTime();
        if (startTime) {
            int64_t t = 0;
            if (!radio::DesktopClock::parseTime(startTime, t)) {
                usage();
                return 2;
            }
            clock.setTime(t);
        }

        radio::Radio radio(platform);
        if (!radio.begin()) std::fprintf(stderr, "radio: some devices failed to start\n");

        int speedIndex = 0;
        uint32_t nextTitle = 0;
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    running = false;
                } else if (input.handleEvent(e)) {
                    // handled
                } else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE: running = false; break;
                        case SDLK_p: clock.setPaused(!clock.paused()); break;
                        case SDLK_RIGHTBRACKET:
                            if (speedIndex + 1 < kSpeedCount) clock.setSpeed(kSpeeds[++speedIndex]);
                            break;
                        case SDLK_LEFTBRACKET:
                            if (speedIndex > 0) clock.setSpeed(kSpeeds[--speedIndex]);
                            break;
                        case SDLK_r:
                            speedIndex = 0;
                            clock.resetToRealTime();
                            break;
                        case SDLK_c:
                            radio.reloadConfig();
                            std::printf("reloaded %s/system/config.json\n", contentDir.c_str());
                            break;
                        default: break;
                    }
                    nextTitle = 0;
                } else if (e.type == SDL_WINDOWEVENT &&
                           e.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    display.refresh();
                }
            }

            radio.update();

            const uint32_t ms = clock.millis();
            if (static_cast<int32_t>(ms - nextTitle) >= 0) {
                display.setTitle(statusLine(radio, clock));
                nextTitle = ms + 200;
            }

            SDL_Delay(2);
        }
    }

    SDL_Quit();
    return 0;
}

#endif  // PIO_UNIT_TESTING
