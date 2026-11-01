#include "core/Radio.h"

#include <cstdio>

namespace radio {

namespace {

constexpr uint32_t kPressFlashMs = 120;

// Dial scale geometry on the 128x32 panel.
constexpr int kScaleLeft = 2;
constexpr int kScaleRight = Framebuffer::kWidth - 3;
constexpr int kScaleBottom = Framebuffer::kHeight - 1;
constexpr int kNeedleTop = 18;

int dialToX(int dial) {
    return kScaleLeft + (dial - Radio::kDialMin) * (kScaleRight - kScaleLeft) /
                            (Radio::kDialMax - Radio::kDialMin);
}

}  // namespace

bool Radio::begin() {
    bool ok = true;
    ok &= p_.display.begin();
    ok &= p_.input.begin();
    ok &= p_.clock.begin();
    ok &= p_.storage.begin();
    ok &= p_.audio.begin(kSampleRate);
    dirty_ = true;
    return ok;
}

void Radio::update() {
    const InputState in = p_.input.read();
    if (in.dialSteps != 0) {
        int d = dial_ + in.dialSteps;
        if (d < kDialMin) d = kDialMin;
        if (d > kDialMax) d = kDialMax;
        if (d != dial_) {
            dial_ = d;
            dirty_ = true;
        }
    }

    const uint32_t ms = p_.clock.millis();
    if (in.pressed) {
        flashing_ = true;
        flashUntil_ = ms + kPressFlashMs;
        dirty_ = true;
    } else if (flashing_ && static_cast<int32_t>(ms - flashUntil_) >= 0) {
        flashing_ = false;
        dirty_ = true;
    }

    const int64_t minute = p_.clock.now() / 60;
    if (minute != shownMinute_) {
        shownMinute_ = minute;
        dirty_ = true;
    }

    if (dirty_) {
        render();
        dirty_ = false;
    }
}

void Radio::render() {
    fb_.clear();
    char text[16];

    // Frequency, large, top left.
    std::snprintf(text, sizeof(text), "%d.%d", dial_ / 10, dial_ % 10);
    const int x = fb_.drawText(0, 0, text, 2);
    fb_.drawText(x + 1, 7, "MHz");

    // Clock, top right.
    const int64_t secOfDay = ((shownMinute_ * 60) % 86400 + 86400) % 86400;
    std::snprintf(text, sizeof(text), "%02d:%02d",
                  static_cast<int>(secOfDay / 3600),
                  static_cast<int>(secOfDay / 60 % 60));
    fb_.drawText(Framebuffer::kWidth - Framebuffer::textWidth(text) + 1, 0, text);

    // Dial scale: baseline, a tick per MHz (taller every 5), and the needle.
    fb_.hLine(kScaleLeft, kScaleBottom, kScaleRight - kScaleLeft + 1);
    for (int mhz = kDialMin; mhz <= kDialMax; mhz += 10) {
        const int h = (mhz % 50 == 0) ? 5 : 3;
        fb_.vLine(dialToX(mhz), kScaleBottom - h, h);
    }
    fb_.vLine(dialToX(dial_), kNeedleTop, kScaleBottom - kNeedleTop + 1);

    if (flashing_) fb_.invert();
    p_.display.show(fb_.data());
}

}  // namespace radio
