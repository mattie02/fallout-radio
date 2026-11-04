#include "ui/MonoDial.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace radio {

namespace {

constexpr int kNameY = 16;
constexpr int kScaleLeft = 2;
constexpr int kScaleRight = Framebuffer::kWidth - 3;
constexpr int kScaleBottom = Framebuffer::kHeight - 1;
constexpr int kNeedleTop = 25;
constexpr int kMeterBars = 5;

}  // namespace

void drawMonoDial(const DisplayState& s, Framebuffer& fb) {
    fb.clear();
    char text[24];

    // Frequency, large, top left.
    std::snprintf(text, sizeof(text), "%d.%d", s.dial / 10, s.dial % 10);
    const int x = fb.drawText(0, 0, text, 2);
    fb.drawText(x + 1, 7, "MHz");

    // Clock, top right.
    std::snprintf(text, sizeof(text), "%02d:%02d", s.hour, s.minute);
    fb.drawText(Framebuffer::kWidth - Framebuffer::textWidth(text) + 1, 0, text);

    // Signal meter under the clock: bars of rising height.
    const int lit = static_cast<int>(std::lround(s.signal * kMeterBars));
    for (int i = 0; i < kMeterBars; ++i) {
        const int bx = Framebuffer::kWidth - kMeterBars * 4 + i * 4;
        const int h = i + 1;
        if (i < lit) fb.fillRect(bx, 14 - h, 3, h);
        else fb.hLine(bx, 13, 3);
    }

    // Station name.
    if (s.station[0]) {
        std::snprintf(text, sizeof(text), "%.21s", s.station);
        fb.drawText(0, kNameY, text);
    }

    // Dial scale: baseline, a tick per MHz (taller every 5), the needle.
    const int span = std::max(s.dialMax - s.dialMin, 1);
    auto toX = [&](int d) { return kScaleLeft + (d - s.dialMin) * (kScaleRight - kScaleLeft) / span; };
    fb.hLine(kScaleLeft, kScaleBottom, kScaleRight - kScaleLeft + 1);
    for (int d = (s.dialMin + 9) / 10 * 10; d <= s.dialMax; d += 10) {
        const int h = (d % 50 == 0) ? 4 : 2;
        fb.vLine(toX(d), kScaleBottom - h, h);
    }
    fb.vLine(toX(s.dial), kNeedleTop, kScaleBottom - kNeedleTop + 1);

    if (s.flash) fb.invert();
}

}  // namespace radio
