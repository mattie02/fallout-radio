// Desktop simulator entry point.
//
//   program [--content DIR] [--time "YYYY-MM-DD HH:MM[:SS]"]
//
// Dial:   mouse wheel, ← →  (Shift for 1 MHz steps)
// Button: click, Space
// Clock:  P pause/resume, ] faster, [ slower, R back to real time
// Esc quits.

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
                char title[128];
                std::snprintf(title, sizeof(title), "Vintage Radio  |  %d.%d MHz  |  %s  x%g%s",
                              radio.dial() / 10, radio.dial() % 10,
                              radio::DesktopClock::format(clock.now()).c_str(), clock.speed(),
                              clock.paused() ? "  PAUSED" : "");
                display.setTitle(title);
                nextTitle = ms + 200;
            }

            SDL_Delay(2);
        }
    }

    SDL_Quit();
    return 0;
}

#endif  // PIO_UNIT_TESTING
