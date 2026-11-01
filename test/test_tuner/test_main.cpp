// Tuner: dial position -> station + signal strength.

#include <unity.h>

#include "core/Tuner.h"

using namespace radio;

void setUp() {}
void tearDown() {}

void test_signal_curve() {
    const float lock = 0.1f, fade = 0.4f;
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Tuner::signalAt(0.0f, lock, fade));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Tuner::signalAt(0.1f, lock, fade));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Tuner::signalAt(-0.1f, lock, fade));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, Tuner::signalAt(0.3f, lock, fade));  // midpoint of fade
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Tuner::signalAt(0.5f, lock, fade));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Tuner::signalAt(3.0f, lock, fade));

    // Falls steadily from lock edge to fade edge.
    float prev = 1.0f;
    for (int i = 11; i <= 50; ++i) {
        const float s = Tuner::signalAt(i / 100.0f, lock, fade);
        TEST_ASSERT_TRUE(s <= prev);
        prev = s;
    }
}

void test_no_fade_is_a_hard_edge() {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Tuner::signalAt(0.1f, 0.1f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Tuner::signalAt(0.2f, 0.1f, 0.0f));
}

void test_tune_picks_station_in_range() {
    Tuner t;
    t.setSignal(0.1f, 0.4f);
    t.setStations({91.5f, 95.3f, 107.9f});

    Tuner::Result r = t.tune(91.5f);
    TEST_ASSERT_EQUAL_INT(0, r.station);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.signal);

    // Dial steps are tenths, held in floats: 95.4 must count as within lock.
    r = t.tune(954 / 10.0f);
    TEST_ASSERT_EQUAL_INT(1, r.station);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, r.signal);

    r = t.tune(107.6f);
    TEST_ASSERT_EQUAL_INT(2, r.station);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, r.signal);

    r = t.tune(100.0f);  // between stations: static
    TEST_ASSERT_EQUAL_INT(-1, r.station);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, r.signal);
}

void test_overlap_picks_stronger() {
    Tuner t;
    t.setSignal(0.1f, 0.4f);
    t.setStations({100.0f, 100.6f});
    TEST_ASSERT_EQUAL_INT(0, t.tune(100.2f).station);
    TEST_ASSERT_EQUAL_INT(1, t.tune(100.4f).station);
}

void test_no_stations() {
    Tuner t;
    TEST_ASSERT_EQUAL_INT(-1, t.tune(98.6f).station);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_signal_curve);
    RUN_TEST(test_no_fade_is_a_hard_edge);
    RUN_TEST(test_tune_picks_station_in_range);
    RUN_TEST(test_overlap_picks_stronger);
    RUN_TEST(test_no_stations);
    return UNITY_END();
}
