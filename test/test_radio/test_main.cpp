// Radio skeleton tests: dial input and display output, against a fake platform.

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/Radio.h"

using namespace radio;

namespace {

struct FakeAudio : AudioOut {
    bool begin(uint32_t) override { return true; }
    size_t availableFrames() override { return 0; }
    size_t write(const int16_t*, size_t) override { return 0; }
};

struct FakeInput : Input {
    InputState next;
    bool begin() override { return true; }
    InputState read() override {
        InputState s = next;
        next = {};
        return s;
    }
};

struct FakeDisplay : Display {
    uint8_t buf[kDisplayBufferSize] = {};
    int shows = 0;
    bool begin() override { return true; }
    void show(const uint8_t* b) override {
        std::memcpy(buf, b, sizeof(buf));
        ++shows;
    }
    bool pixel(int x, int y) const { return buf[x + (y / 8) * kDisplayWidth] & (1u << (y % 8)); }
};

struct FakeClock : Clock {
    int64_t t = 0;
    uint32_t ms = 0;
    bool begin() override { return true; }
    int64_t now() override { return t; }
    uint32_t millis() override { return ms; }
};

struct FakeStorage : Storage {
    bool begin() override { return true; }
    bool exists(const char*) override { return false; }
    std::unique_ptr<File> open(const char*) override { return nullptr; }
};

struct Rig {
    FakeAudio audio;
    FakeInput input;
    FakeDisplay display;
    FakeClock clock;
    FakeStorage storage;
    Platform platform{audio, input, display, clock, storage};
    Radio radio{platform};
};

// Set RADIO_DUMP=1 to print the display as ASCII art.
void dump(const FakeDisplay& d) {
    if (!std::getenv("RADIO_DUMP")) return;
    for (int y = 0; y < kDisplayHeight; ++y) {
        char line[kDisplayWidth + 1];
        for (int x = 0; x < kDisplayWidth; ++x) line[x] = d.pixel(x, y) ? '#' : '.';
        line[kDisplayWidth] = 0;
        std::printf("%s\n", line);
    }
}

int needleX(const FakeDisplay& d) {
    // The needle is the only column lit at row 19 (above the ticks).
    for (int x = 0; x < kDisplayWidth; ++x)
        if (d.pixel(x, 19)) return x;
    return -1;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_dial_moves_and_redraws() {
    Rig r;
    r.clock.t = 23 * 3600 + 58 * 60;  // 23:58
    TEST_ASSERT_TRUE(r.radio.begin());
    r.radio.update();
    TEST_ASSERT_EQUAL(1, r.display.shows);
    dump(r.display);
    const int before = needleX(r.display);

    r.radio.update();  // nothing changed: no redraw
    TEST_ASSERT_EQUAL(1, r.display.shows);

    r.input.next.dialSteps = 50;  // +5.0 MHz
    r.radio.update();
    TEST_ASSERT_EQUAL(2, r.display.shows);
    TEST_ASSERT_EQUAL(1036, r.radio.dial());
    TEST_ASSERT_GREATER_THAN(before, needleX(r.display));
}

void test_dial_clamps_to_range() {
    Rig r;
    r.radio.begin();
    r.input.next.dialSteps = -10000;
    r.radio.update();
    TEST_ASSERT_EQUAL(Radio::kDialMin, r.radio.dial());
    TEST_ASSERT_EQUAL(2, needleX(r.display));
    r.input.next.dialSteps = 10000;
    r.radio.update();
    TEST_ASSERT_EQUAL(Radio::kDialMax, r.radio.dial());
    TEST_ASSERT_EQUAL(kDisplayWidth - 3, needleX(r.display));
}

void test_button_flashes_display() {
    Rig r;
    r.radio.begin();
    r.radio.update();
    const bool corner = r.display.pixel(127, 31);
    r.input.next.pressed = true;
    r.radio.update();
    TEST_ASSERT_NOT_EQUAL(corner, r.display.pixel(127, 31));
    r.clock.ms = 1000;
    r.radio.update();
    TEST_ASSERT_EQUAL(corner, r.display.pixel(127, 31));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dial_moves_and_redraws);
    RUN_TEST(test_dial_clamps_to_range);
    RUN_TEST(test_button_flashes_display);
    return UNITY_END();
}
