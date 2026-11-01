// CivilTime: calendar <-> seconds, parsing (used by the ESP32 RTC and serial).

#include <unity.h>

#include <ctime>

#include "core/CivilTime.h"

using namespace radio;

void setUp() {}
void tearDown() {}

void test_known_dates() {
    CivilTime t;
    t.year = 2026, t.month = 12, t.day = 24, t.hour = 23, t.minute = 58;
    TEST_ASSERT_EQUAL_INT64(1798156680LL, toSeconds(t));
    TEST_ASSERT_EQUAL(3, t.weekday());  // Thursday
    TEST_ASSERT_EQUAL_INT64(0, toSeconds(CivilTime{}));
    TEST_ASSERT_EQUAL(3, CivilTime{}.weekday());  // 1970-01-01 Thursday
}

void test_round_trip_matches_libc() {
    // Every ~37 hours from 1970 to 2100, against the C library's gmtime.
    for (int64_t s = 0; s < 4102444800LL; s += 133337) {
        const CivilTime t = fromSeconds(s);
        TEST_ASSERT_EQUAL_INT64(s, toSeconds(t));
        const std::time_t tt = static_cast<std::time_t>(s);
        std::tm tm{};
        gmtime_r(&tt, &tm);
        TEST_ASSERT_EQUAL(tm.tm_year + 1900, t.year);
        TEST_ASSERT_EQUAL(tm.tm_mon + 1, t.month);
        TEST_ASSERT_EQUAL(tm.tm_mday, t.day);
        TEST_ASSERT_EQUAL(tm.tm_hour, t.hour);
        TEST_ASSERT_EQUAL(tm.tm_min, t.minute);
        TEST_ASSERT_EQUAL(tm.tm_sec, t.second);
        TEST_ASSERT_EQUAL((tm.tm_wday + 6) % 7, t.weekday());
    }
}

void test_parse() {
    CivilTime t;
    TEST_ASSERT_TRUE(parseCivilTime("2026-12-24 23:58", t));
    TEST_ASSERT_EQUAL(23, t.hour);
    TEST_ASSERT_EQUAL(0, t.second);
    TEST_ASSERT_TRUE(parseCivilTime("2026-12-24T23:58:07\r\n", t));
    TEST_ASSERT_EQUAL(7, t.second);
    TEST_ASSERT_TRUE(parseCivilTime("2028-02-29 00:00", t));  // leap day
    TEST_ASSERT_FALSE(parseCivilTime("2027-02-29 00:00", t));
    TEST_ASSERT_FALSE(parseCivilTime("2026-13-01 00:00", t));
    TEST_ASSERT_FALSE(parseCivilTime("2026-12-24 24:00", t));
    TEST_ASSERT_FALSE(parseCivilTime("2026-12-24", t));
    TEST_ASSERT_FALSE(parseCivilTime("2026-12-24 23:58 extra", t));
}

void test_build_time() {
    CivilTime t;
    TEST_ASSERT_TRUE(parseBuildTime("Dec 24 2026", "23:58:01", t));
    TEST_ASSERT_EQUAL(12, t.month);
    TEST_ASSERT_EQUAL(24, t.day);
    TEST_ASSERT_EQUAL(1, t.second);
    TEST_ASSERT_TRUE(parseBuildTime("Nov  1 2026", "09:05:00", t));
    TEST_ASSERT_EQUAL(11, t.month);
    TEST_ASSERT_EQUAL(1, t.day);
    TEST_ASSERT_FALSE(parseBuildTime("Foo 1 2026", "09:05:00", t));
    // The real compiler macros parse too.
    TEST_ASSERT_TRUE(parseBuildTime(__DATE__, __TIME__, t));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_known_dates);
    RUN_TEST(test_round_trip_matches_libc);
    RUN_TEST(test_parse);
    RUN_TEST(test_build_time);
    return UNITY_END();
}
