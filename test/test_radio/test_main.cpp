// Radio: dial input, station loading, schedule tracking and display output,
// against an in-memory platform.

#include <unity.h>

#include <cstdio>
#include <cstdlib>

#include "../support/FakePlatform.h"
#include "core/Radio.h"

using namespace radio;
using namespace radio::test;

namespace {

constexpr int64_t kMondaySec = 20808LL * 86400;  // Mon 2026-12-21 00:00

struct Rig : FakePlatform {
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
    // The needle is the only thing lit on row 25 (ticks start lower).
    for (int x = 0; x < kDisplayWidth; ++x)
        if (d.pixel(x, 25)) return x;
    return -1;
}

void addStation(MemoryStorage& s, const char* dir, const char* json, const std::string& schedule) {
    s.files[std::string("/stations/") + dir + "/station.json"] = json;
    if (!schedule.empty()) s.files[std::string("/stations/") + dir + "/schedule.txt"] = schedule;
}

void addTwoStations(MemoryStorage& s) {
    s.files["/system/config.json"] =
        R"({"dial": {"min": 88.0, "max": 108.0}, "signal": {"lock_mhz": 0.1, "fade_mhz": 0.4}})";
    // Listed in folder order, but 04 is lower on the dial: stations sort by frequency.
    addStation(s, "04_country", R"({"name": "Country", "frequency": 91.5})",
               ScheduleBuilder().add(60000, "music/001_a.mp3").add(30000, "dj/001_b.mp3").build());
    addStation(s, "01_crooner", R"({"name": "Crooner", "frequency": 95.3})",
               ScheduleBuilder().add(120000, "music/007_c.mp3").build());
    addStation(s, "02_broken", R"({"name": "No frequency"})", "");
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
    TEST_ASSERT_EQUAL(r.radio.dialMin(), r.radio.dial());
    TEST_ASSERT_EQUAL(2, needleX(r.display));
    r.input.next.dialSteps = 10000;
    r.radio.update();
    TEST_ASSERT_EQUAL(r.radio.dialMax(), r.radio.dial());
    TEST_ASSERT_EQUAL(kDisplayWidth - 3, needleX(r.display));
}

void test_dial_range_from_config() {
    Rig r;
    r.storage.files["/system/config.json"] = R"({"dial": {"min": 530, "max": 1700}})";
    r.radio.begin();
    TEST_ASSERT_EQUAL(5300, r.radio.dialMin());
    TEST_ASSERT_EQUAL(17000, r.radio.dialMax());
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

void test_loads_valid_stations_sorted_by_frequency() {
    Rig r;
    addTwoStations(r.storage);
    r.radio.begin();
    TEST_ASSERT_EQUAL(2, r.radio.stations().size());
    TEST_ASSERT_EQUAL_STRING("Country", r.radio.stations()[0].info.name.c_str());
    TEST_ASSERT_EQUAL_STRING("Crooner", r.radio.stations()[1].info.name.c_str());
    TEST_ASSERT_TRUE(r.radio.stations()[0].schedule.isOpen());
}

void test_tuning_and_now_playing() {
    Rig r;
    addTwoStations(r.storage);
    r.clock.t = kMondaySec + 75;  // 75 s into the week
    r.radio.begin();

    r.input.next.dialSteps = 915 - r.radio.dial();  // to 91.5: Country
    r.radio.update();
    Radio::Status s = r.radio.status();
    TEST_ASSERT_EQUAL(0, s.station);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.signal);
    TEST_ASSERT_TRUE(s.playing);
    TEST_ASSERT_EQUAL_STRING("dj/001_b.mp3", s.entry.path);
    TEST_ASSERT_EQUAL_UINT32(15000, s.offsetMs);
    dump(r.display);

    // Time passes into the next entry (the 90 s schedule wraps).
    r.clock.t = kMondaySec + 95;
    r.radio.update();
    s = r.radio.status();
    TEST_ASSERT_EQUAL_STRING("music/001_a.mp3", s.entry.path);
    TEST_ASSERT_EQUAL_UINT32(5000, s.offsetMs);

    // Off station: static. Partly tuned: weaker signal, same programme.
    r.input.next.dialSteps = 40;  // 95.5
    r.radio.update();
    s = r.radio.status();
    TEST_ASSERT_EQUAL(1, s.station);
    TEST_ASSERT_EQUAL_STRING("music/007_c.mp3", s.entry.path);
    TEST_ASSERT_EQUAL_UINT32(95000, s.offsetMs);
    r.input.next.dialSteps = 20;  // 97.5
    r.radio.update();
    s = r.radio.status();
    TEST_ASSERT_EQUAL(-1, s.station);
    TEST_ASSERT_FALSE(s.playing);
}

void test_station_without_schedule() {
    Rig r;
    addStation(r.storage, "01_x", R"({"name": "X", "frequency": 98.6})", "");
    r.radio.begin();
    r.radio.update();
    const Radio::Status s = r.radio.status();
    TEST_ASSERT_EQUAL(0, s.station);
    TEST_ASSERT_FALSE(s.playing);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dial_moves_and_redraws);
    RUN_TEST(test_dial_clamps_to_range);
    RUN_TEST(test_dial_range_from_config);
    RUN_TEST(test_button_flashes_display);
    RUN_TEST(test_loads_valid_stations_sorted_by_frequency);
    RUN_TEST(test_tuning_and_now_playing);
    RUN_TEST(test_station_without_schedule);
    return UNITY_END();
}
