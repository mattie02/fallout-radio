// The two screens: DisplayState -> pixels.
//
// Set RADIO_DUMP=DIR to write sample frames of both screens there as PPM images,
// to look at a design without running the simulator.

#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ui/Framebuffer.h"
#include "ui/RoundDial.h"
#include "ui/Waveform.h"

using namespace radio;

namespace {

DisplayState sample(int dial, const char* name, float signal) {
    DisplayState s;
    s.dial = dial;
    s.dialMin = 880;
    s.dialMax = 1080;
    std::snprintf(s.station, sizeof(s.station), "%s", name);
    s.signal = signal;
    s.hour = 23;
    s.minute = 58;
    s.volume = 0.6f;
    return s;
}

// Big-endian RGB565 -> r, g, b
void rgbAt(const uint16_t* f, int x, int y, int& r, int& g, int& b) {
    const uint16_t be = f[y * RoundDial::kSize + x];
    const uint16_t c = static_cast<uint16_t>(be << 8 | be >> 8);
    r = (c >> 11) << 3;
    g = ((c >> 5) & 0x3F) << 2;
    b = (c & 0x1F) << 3;
}

bool isNeedleRed(int r, int g, int b) { return r > 150 && g < 80 && b < 70; }

void dumpRound(const uint16_t* f, const char* name) {
    const char* dir = std::getenv("RADIO_DUMP");
    if (!dir) return;
    const std::string path = std::string(dir) + "/" + name + ".ppm";
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "P6\n%d %d\n255\n", RoundDial::kSize, RoundDial::kSize);
    for (int y = 0; y < RoundDial::kSize; ++y) {
        for (int x = 0; x < RoundDial::kSize; ++x) {
            int r, g, b;
            rgbAt(f, x, y, r, g, b);
            const unsigned char px[3] = {static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                                         static_cast<unsigned char>(b)};
            std::fwrite(px, 1, 3, out);
        }
    }
    std::fclose(out);
}

void dumpMono(const Framebuffer& fb, const char* name) {
    const char* dir = std::getenv("RADIO_DUMP");
    if (!dir) return;
    const std::string path = std::string(dir) + "/" + name + ".ppm";
    std::FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) return;
    std::fprintf(out, "P6\n%d %d\n255\n", Framebuffer::kWidth, Framebuffer::kHeight);
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const unsigned char v = fb.getPixel(x, y) ? 255 : 0;
            const unsigned char px[3] = {v, v, v};
            std::fwrite(px, 1, 3, out);
        }
    }
    std::fclose(out);
}

// Mean position of the needle's red pixels, relative to the center.
bool needleCenter(const uint16_t* f, float& x, float& y) {
    long sumX = 0, sumY = 0, n = 0;
    for (int py = 0; py < RoundDial::kSize; ++py) {
        for (int px = 0; px < RoundDial::kSize; ++px) {
            int r, g, b;
            rgbAt(f, px, py, r, g, b);
            if (!isNeedleRed(r, g, b)) continue;
            sumX += px - RoundDial::kSize / 2;
            sumY += py - RoundDial::kSize / 2;
            ++n;
        }
    }
    if (n == 0) return false;
    x = static_cast<float>(sumX) / n;
    y = static_cast<float>(sumY) / n;
    return true;
}

int differingPixels(const uint16_t* a, const uint16_t* b) {
    int n = 0;
    for (int i = 0; i < RoundDial::kSize * RoundDial::kSize; ++i) n += a[i] != b[i];
    return n;
}

constexpr int kCaptured = static_cast<int>(WaveformCapture::kSamples);

void sine(int16_t* out, float hz, float amplitude, float phase = 0) {
    for (int i = 0; i < kCaptured; ++i) {
        out[i] = static_cast<int16_t>(amplitude * std::sin(phase + 2 * 3.14159265f * hz * i / 44100.0f));
    }
}

int litPixels(const Framebuffer& fb) {
    int n = 0;
    for (int y = 0; y < Framebuffer::kHeight; ++y)
        for (int x = 0; x < Framebuffer::kWidth; ++x) n += fb.getPixel(x, y);
    return n;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_waveform_silence_is_a_flat_line() {
    int16_t samples[kCaptured] = {};
    Framebuffer fb;
    drawWaveform(samples, fb);
    TEST_ASSERT_EQUAL(Framebuffer::kWidth, litPixels(fb));
    for (int x = 0; x < Framebuffer::kWidth; ++x) TEST_ASSERT_TRUE(fb.getPixel(x, 16));
}

void test_waveform_trace_is_unbroken_and_stays_on_screen() {
    int16_t samples[kCaptured];
    sine(samples, 440, 32767);  // louder than the scale: clipped at the edges
    Framebuffer fb;
    drawWaveform(samples, fb);
    bool top = false, bottom = false;
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
        bool lit = false;
        for (int y = 0; y < Framebuffer::kHeight; ++y) lit |= fb.getPixel(x, y);
        TEST_ASSERT_TRUE(lit);
        top |= fb.getPixel(x, 0);
        bottom |= fb.getPixel(x, Framebuffer::kHeight - 1);
    }
    TEST_ASSERT_TRUE(top);
    TEST_ASSERT_TRUE(bottom);
    dumpMono(fb, "oled_clipped_tone");
}

void test_waveform_tone_stands_still() {
    // The same tone caught at different moments must draw (nearly) the same.
    int16_t a[kCaptured], b[kCaptured];
    Framebuffer fa, fb;
    sine(a, 440, 15000);
    drawWaveform(a, fa);
    dumpMono(fa, "oled_tone");
    for (float phase : {0.7f, 2.0f, 4.5f}) {
        sine(b, 440, 15000, phase);
        drawWaveform(b, fb);
        int differing = 0;
        for (int y = 0; y < Framebuffer::kHeight; ++y)
            for (int x = 0; x < Framebuffer::kWidth; ++x) differing += fa.getPixel(x, y) != fb.getPixel(x, y);
        TEST_ASSERT_LESS_THAN(litPixels(fa) / 4, differing);
    }
}

void test_waveform_noise_without_a_trigger_still_draws() {
    int16_t samples[kCaptured];
    for (int i = 0; i < kCaptured; ++i) samples[i] = 5000 + (std::rand() % 2000);  // never below zero
    Framebuffer fb;
    drawWaveform(samples, fb);
    TEST_ASSERT_GREATER_OR_EQUAL(Framebuffer::kWidth, litPixels(fb));

    for (int i = 0; i < kCaptured; ++i) samples[i] = static_cast<int16_t>(std::rand() % 12000 - 6000);
    drawWaveform(samples, fb);
    dumpMono(fb, "oled_static");
}

void test_capture_keeps_the_latest_in_order() {
    WaveformCapture capture;
    int16_t block[256];
    int16_t next = 0;
    for (int b = 0; b < 9; ++b) {  // wraps around the ring
        for (int16_t& s : block) s = next++;
        capture.push(block, 256);
    }
    int16_t out[kCaptured];
    capture.snapshot(out);
    for (int i = 0; i < kCaptured; ++i) TEST_ASSERT_EQUAL(next - kCaptured + i, out[i]);

    // More than it holds at once: only the end is kept.
    int16_t big[kCaptured + 10];
    for (int i = 0; i < kCaptured + 10; ++i) big[i] = static_cast<int16_t>(i);
    capture.push(big, kCaptured + 10);
    capture.snapshot(out);
    TEST_ASSERT_EQUAL(10, out[0]);
    TEST_ASSERT_EQUAL(kCaptured + 9, out[kCaptured - 1]);
}

void test_round_stays_inside_the_circle() {
    RoundDial dial;
    TEST_ASSERT_TRUE(dial.begin());
    const uint16_t* f = dial.draw(sample(915, "Crooner Ballads", 1));
    TEST_ASSERT_NOT_NULL(f);
    const int last = RoundDial::kSize - 1;
    TEST_ASSERT_EQUAL_HEX16(0, f[0]);
    TEST_ASSERT_EQUAL_HEX16(0, f[last]);
    TEST_ASSERT_EQUAL_HEX16(0, f[last * RoundDial::kSize]);
    TEST_ASSERT_EQUAL_HEX16(0, f[last * RoundDial::kSize + last]);
}

void test_round_needle_follows_the_dial() {
    RoundDial dial;
    TEST_ASSERT_TRUE(dial.begin());
    float x = 0, y = 0;

    // Bottom left at the low end, top in the middle, bottom right at the high end.
    TEST_ASSERT_TRUE(needleCenter(dial.draw(sample(880, "", 0)), x, y));
    TEST_ASSERT_LESS_THAN_FLOAT(-40, x);
    TEST_ASSERT_GREATER_THAN_FLOAT(40, y);
    TEST_ASSERT_TRUE(needleCenter(dial.draw(sample(980, "", 0)), x, y));
    TEST_ASSERT_FLOAT_WITHIN(3, 0, x);
    TEST_ASSERT_LESS_THAN_FLOAT(-60, y);
    TEST_ASSERT_TRUE(needleCenter(dial.draw(sample(1080, "", 0)), x, y));
    TEST_ASSERT_GREATER_THAN_FLOAT(40, x);
    TEST_ASSERT_GREATER_THAN_FLOAT(40, y);

    // Out-of-range input doesn't send it off the scale.
    TEST_ASSERT_TRUE(needleCenter(dial.draw(sample(1500, "", 0)), x, y));
    TEST_ASSERT_GREATER_THAN_FLOAT(40, x);
}

void test_round_shows_every_change() {
    RoundDial a, b;
    TEST_ASSERT_TRUE(a.begin());
    TEST_ASSERT_TRUE(b.begin());
    const DisplayState base = sample(915, "Crooner Ballads", 0.5f);
    const uint16_t* fa = a.draw(base);

    DisplayState same = base;
    TEST_ASSERT_EQUAL(0, differingPixels(fa, b.draw(same)));

    DisplayState signal = base;
    signal.signal = 1;
    TEST_ASSERT_GREATER_THAN(0, differingPixels(fa, b.draw(signal)));

    DisplayState volume = base;
    volume.volume = 0.9f;
    TEST_ASSERT_GREATER_THAN(0, differingPixels(fa, b.draw(volume)));

    DisplayState clock = base;
    clock.minute = 59;
    TEST_ASSERT_GREATER_THAN(0, differingPixels(fa, b.draw(clock)));

    DisplayState flash = base;
    flash.flash = true;
    TEST_ASSERT_GREATER_THAN(0, differingPixels(fa, b.draw(flash)));
}

void test_round_handles_other_dial_ranges_and_long_names() {
    RoundDial dial;
    TEST_ASSERT_TRUE(dial.begin());
    DisplayState s = sample(1000, "A Very Long Station Name Indeed!", 1);
    s.dialMin = 5300 / 10;
    s.dialMax = 17000 / 10;
    TEST_ASSERT_NOT_NULL(dial.draw(s));
    s.dialMin = s.dialMax = 1000;  // no span at all
    TEST_ASSERT_NOT_NULL(dial.draw(s));
}

void test_round_samples_for_looking_at() {
    RoundDial dial;
    TEST_ASSERT_TRUE(dial.begin());
    dumpRound(dial.draw(sample(915, "Crooner Ballads", 1)), "round_crooner");
    dumpRound(dial.draw(sample(1031, "Country & Western", 1)), "round_country");
    dumpRound(dial.draw(sample(1079, "Emergency Broadcast", 0.45f)), "round_hidden_fringe");
    DisplayState empty = sample(976, "", 0);
    empty.volume = 0;
    dumpRound(dial.draw(empty), "round_static");
    dumpRound(dial.draw(sample(880, "", 0)), "round_low_end");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_waveform_silence_is_a_flat_line);
    RUN_TEST(test_waveform_trace_is_unbroken_and_stays_on_screen);
    RUN_TEST(test_waveform_tone_stands_still);
    RUN_TEST(test_waveform_noise_without_a_trigger_still_draws);
    RUN_TEST(test_capture_keeps_the_latest_in_order);
    RUN_TEST(test_round_stays_inside_the_circle);
    RUN_TEST(test_round_needle_follows_the_dial);
    RUN_TEST(test_round_shows_every_change);
    RUN_TEST(test_round_handles_other_dial_ranges_and_long_names);
    RUN_TEST(test_round_samples_for_looking_at);
    return UNITY_END();
}
