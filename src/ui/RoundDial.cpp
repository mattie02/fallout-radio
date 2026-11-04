#include "ui/RoundDial.h"

#include <LovyanGFX.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace radio {

namespace {

constexpr int kCenter = RoundDial::kSize / 2;
constexpr float kRad = 3.14159265f / 180.0f;

// The scale runs clockwise over 270 degrees, leaving a gap at the bottom.
constexpr float kSweepDeg = 270.0f;
constexpr int kRimInner = 114;
constexpr int kTickOuter = 111;
constexpr int kLabelRadius = 89;
constexpr int kNeedleInner = 79;
constexpr int kNeedleOuter = 113;
constexpr int kEyeY = 60;
constexpr int kEyeRadius = 12;
constexpr int kFreqY = 104;
constexpr int kNameY = 146;
constexpr int kNameMaxWidth = 156;
constexpr int kClockY = 189;
constexpr int kVolumeOuter = 111;
constexpr int kVolumeInner = 106;

struct Rgb {
    float r, g, b;
};
constexpr Rgb kFaceCenter{252, 234, 182};
constexpr Rgb kFaceEdge{198, 148, 80};
constexpr Rgb kBrass{214, 168, 88};

uint32_t rgb(int r, int g, int b) { return lgfx::color888(r, g, b); }

const uint32_t kInk = rgb(56, 32, 16);
const uint32_t kInkSoft = rgb(120, 82, 44);
const uint32_t kNeedle = rgb(196, 36, 20);
const uint32_t kShadow = rgb(150, 108, 58);
const uint32_t kEyeBody = rgb(28, 22, 16);
const uint32_t kEyeGlow = rgb(90, 255, 130);
const uint32_t kEyeShade = rgb(8, 44, 22);
const uint32_t kFlash = rgb(255, 250, 225);

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Degrees clockwise from the top of the face.
float dialAngle(int d, int dialMin, int dialMax) {
    const float span = static_cast<float>(std::max(dialMax - dialMin, 1));
    return -kSweepDeg / 2 + kSweepDeg * (d - dialMin) / span;
}

float polarX(float deg, float r) { return kCenter + r * std::sin(deg * kRad); }
float polarY(float deg, float r) { return kCenter - r * std::cos(deg * kRad); }

}  // namespace

struct RoundDial::Impl {
    lgfx::LGFX_Sprite face;   // everything that never moves
    lgfx::LGFX_Sprite frame;  // face + what does
    int faceMin = -1;
    int faceMax = -1;
    bool ready = false;

    void drawFace(int dialMin, int dialMax);
    void drawFrame(const DisplayState& s);
};

// Slow (a gradient, every label) but only done when the dial range changes,
// which is once.
void RoundDial::Impl::drawFace(int dialMin, int dialMax) {
    faceMin = dialMin;
    faceMax = dialMax;

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = x + 0.5f - kCenter;
            const float dy = y + 0.5f - kCenter;
            const float r = std::sqrt(dx * dx + dy * dy);
            uint32_t c = 0;
            if (r >= kRimInner && r <= kCenter) {
                // Brass rim, lit from above.
                const float shade = 0.6f - 0.4f * dy / r;
                c = rgb(kBrass.r * shade, kBrass.g * shade, kBrass.b * shade);
            } else if (r < kRimInner) {
                // Lamp behind the middle of the face.
                const float t = r / kRimInner;
                const float glow = t * t;
                c = rgb(lerp(kFaceCenter.r, kFaceEdge.r, glow), lerp(kFaceCenter.g, kFaceEdge.g, glow),
                        lerp(kFaceCenter.b, kFaceEdge.b, glow));
            }
            face.drawPixel(x, y, c);
        }
    }

    // Ticks: half-MHz minor, whole MHz taller, labelled ones tallest.
    const int span = std::max(dialMax - dialMin, 1);
    const int labelStep = span >= 150 ? 40 : 20;
    for (int d = dialMin; d <= dialMax; d += 5) {
        const bool labelled = (d - dialMin) % labelStep == 0;
        const bool whole = d % 10 == 0;
        const float deg = dialAngle(d, dialMin, dialMax);
        const float inner = kTickOuter - (labelled ? 11 : whole ? 7 : 3);
        const float halfWidth = labelled ? 0.9f : 0.5f;
        face.drawWideLine(polarX(deg, inner), polarY(deg, inner), polarX(deg, kTickOuter),
                          polarY(deg, kTickOuter), halfWidth, whole ? kInk : kInkSoft);
    }

    face.setFont(&lgfx::fonts::FreeSerifBold9pt7b);
    face.setTextDatum(lgfx::middle_center);
    face.setTextColor(kInk);
    char text[8];
    for (int d = dialMin; d <= dialMax; d += labelStep) {
        const float deg = dialAngle(d, dialMin, dialMax);
        std::snprintf(text, sizeof(text), "%d", d / 10);
        face.drawString(text, polarX(deg, kLabelRadius), polarY(deg, kLabelRadius));
    }

    // Round socket of the magic eye.
    face.fillSmoothCircle(kCenter, kEyeY, kEyeRadius + 3, kEyeBody);
}

void RoundDial::Impl::drawFrame(const DisplayState& s) {
    std::memcpy(frame.getBuffer(), face.getBuffer(), kSize * kSize * sizeof(uint16_t));
    char text[16];

    // Magic eye: the dark wedge at the top closes as the signal comes in.
    frame.fillSmoothCircle(kCenter, kEyeY, kEyeRadius, kEyeGlow);
    const float signal = std::min(std::max(s.signal, 0.0f), 1.0f);
    const float shadeDeg = 4 + (1 - signal) * 86;
    frame.fillArc(kCenter, kEyeY, kEyeRadius, 0, 270 - shadeDeg, 270 + shadeDeg, kEyeShade);
    frame.fillSmoothCircle(kCenter, kEyeY, 3, kEyeBody);

    // Frequency.
    frame.setFont(&lgfx::fonts::FreeSerifBold24pt7b);
    frame.setTextSize(1);
    frame.setTextDatum(lgfx::middle_center);
    frame.setTextColor(kInk);
    std::snprintf(text, sizeof(text), "%d.%d", s.dial / 10, s.dial % 10);
    frame.drawString(text, kCenter, kFreqY);

    // Station name, shrunk to fit; "MHz" keeps the spot when nothing's there.
    if (s.station[0]) {
        frame.setFont(&lgfx::fonts::Satisfy_24);
        const int w = frame.textWidth(s.station);
        frame.setTextSize(w > kNameMaxWidth ? static_cast<float>(kNameMaxWidth) / w : 1.0f);
        frame.drawString(s.station, kCenter, kNameY);
        frame.setTextSize(1);
    } else {
        frame.setFont(&lgfx::fonts::FreeSerif9pt7b);
        frame.setTextColor(kInkSoft);
        frame.drawString("MHz", kCenter, kNameY);
    }

    // Clock.
    frame.setFont(&lgfx::fonts::FreeSerifBold9pt7b);
    frame.setTextColor(kInk);
    std::snprintf(text, sizeof(text), "%02d:%02d", s.hour, s.minute);
    frame.drawString(text, kCenter, kClockY);

    // Volume: a small arc across the gap in the scale, filling left to right.
    // (Arc angles here: 0 = 3 o'clock, clockwise; 90 = bottom.)
    constexpr float kVolLeft = 90 + 34, kVolRight = 90 - 34;
    frame.fillArc(kCenter, kCenter, kVolumeOuter, kVolumeInner, kVolRight, kVolLeft, kShadow);
    if (s.volume > 0) {
        const float filled = kVolLeft - (kVolLeft - kVolRight) * std::min(s.volume, 1.0f);
        frame.fillArc(kCenter, kCenter, kVolumeOuter, kVolumeInner, filled, kVolLeft, kInk);
    }

    // Needle, with a soft shadow so it stands off the face.
    const float deg = dialAngle(std::min(std::max(s.dial, s.dialMin), s.dialMax), s.dialMin, s.dialMax);
    const float x0 = polarX(deg, kNeedleInner), y0 = polarY(deg, kNeedleInner);
    const float x1 = polarX(deg, kNeedleOuter), y1 = polarY(deg, kNeedleOuter);
    frame.drawWedgeLine(x0 + 1.5f, y0 + 1.5f, x1 + 1.5f, y1 + 1.5f, 2.2f, 0.5f, kShadow);
    frame.drawWedgeLine(x0, y0, x1, y1, 2.2f, 0.5f, kNeedle);

    if (s.flash) {
        for (int r = kCenter - 6; r < kCenter; ++r) frame.drawCircle(kCenter, kCenter, r, kFlash);
    }
}

RoundDial::RoundDial() : impl_(new Impl) {}
RoundDial::~RoundDial() = default;

bool RoundDial::begin() {
    for (lgfx::LGFX_Sprite* s : {&impl_->face, &impl_->frame}) {
        s->setPsram(true);
        s->setColorDepth(16);
        if (!s->createSprite(kSize, kSize)) return false;
    }
    impl_->ready = true;
    return true;
}

const uint16_t* RoundDial::draw(const DisplayState& state) {
    if (!impl_->ready) return nullptr;
    if (state.dialMin != impl_->faceMin || state.dialMax != impl_->faceMax)
        impl_->drawFace(state.dialMin, state.dialMax);
    impl_->drawFrame(state);
    return static_cast<const uint16_t*>(impl_->frame.getBuffer());
}

}  // namespace radio
