// Schedule: reading schedule.txt and mapping wall-clock time to file + offset.

#include <unity.h>

#include <cstring>

#include "../support/FakePlatform.h"
#include "core/Schedule.h"

using namespace radio;
using namespace radio::test;

namespace {

constexpr int64_t kDayMs = 24LL * 3600 * 1000;
constexpr int64_t kWeekMs = Schedule::kWeekMs;
// Mon 2026-12-21 00:00 as naive local ms (days since 1970: 20808).
constexpr int64_t kMonday = 20808LL * kDayMs;

const char* kPath = "/s.txt";

MemoryStorage storage;

Schedule openWith(const std::string& text) {
    storage.files[kPath] = text;
    Schedule s;
    TEST_ASSERT_TRUE(s.open(storage, kPath));
    return s;
}

}  // namespace

void setUp() { storage.files.clear(); }
void tearDown() {}

void test_monday_reference_is_a_monday() {
    // 2026-12-21 is a Monday; check it lands on schedule position 0.
    TEST_ASSERT_EQUAL_UINT32(0, Schedule::positionMs(kMonday, kWeekMs));
    TEST_ASSERT_EQUAL_UINT32(0, Schedule::positionMs(kMonday - 7 * kDayMs, kWeekMs));
    // Christmas Eve 23:58 is Thursday: 3 days 23 h 58 min in.
    const int64_t eve = kMonday + 3 * kDayMs + (23 * 60 + 58) * 60000LL;
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(eve - kMonday), Schedule::positionMs(eve, kWeekMs));
    // Sunday 23:59:59.999 is the last ms of the week.
    TEST_ASSERT_EQUAL_UINT32(kWeekMs - 1, Schedule::positionMs(kMonday - 1, kWeekMs));
    // Times before the reference Monday wrap correctly too.
    TEST_ASSERT_EQUAL_UINT32(kWeekMs - 1, Schedule::positionMs(Schedule::kMondayEpochMs - 1, kWeekMs));
}

void test_short_schedule_wraps() {
    TEST_ASSERT_EQUAL_UINT32(500, Schedule::positionMs(kMonday + 2500, 1000));
}

void test_open_reads_header() {
    Schedule s = openWith(ScheduleBuilder().add(1000, "music/001_a.mp3").add(2000, "-").build());
    TEST_ASSERT_EQUAL_UINT32(2, s.count());
    TEST_ASSERT_EQUAL_UINT32(3000, s.lengthMs());
    Schedule::Entry e;
    TEST_ASSERT_TRUE(s.entry(1, e));
    TEST_ASSERT_EQUAL_UINT32(1000, e.startMs);
    TEST_ASSERT_EQUAL_UINT32(2000, e.durationMs);
    TEST_ASSERT_TRUE(e.isSilence());
    TEST_ASSERT_FALSE(s.entry(2, e));
}

void test_open_rejects_bad_files() {
    Schedule s;
    TEST_ASSERT_FALSE(s.open(storage, "/missing.txt"));

    std::string text = ScheduleBuilder().add(1000, "a.mp3").build();
    storage.files[kPath] = text.substr(0, text.size() - 1);  // truncated
    TEST_ASSERT_FALSE(s.open(storage, kPath));

    storage.files[kPath] = "X" + text.substr(1);  // bad magic
    TEST_ASSERT_FALSE(s.open(storage, kPath));

    text[10] = '9';  // count says more entries than the file holds
    storage.files[kPath] = text;
    TEST_ASSERT_FALSE(s.open(storage, kPath));
    TEST_ASSERT_FALSE(s.isOpen());
}

void test_lookup_finds_entry_and_offset() {
    ScheduleBuilder b;
    for (int i = 0; i < 100; ++i) {
        char path[32];
        std::snprintf(path, sizeof(path), "music/%03d_x.mp3", i);
        b.add(1000 + i, path);  // varying lengths
    }
    Schedule s = openWith(b.build());

    Schedule::Position p;
    TEST_ASSERT_TRUE(s.lookup(kMonday, p));
    TEST_ASSERT_EQUAL_UINT32(0, p.index);
    TEST_ASSERT_EQUAL_UINT32(0, p.offsetMs);
    TEST_ASSERT_EQUAL_STRING("music/000_x.mp3", p.entry.path);

    // Every boundary: last ms of entry i, first ms of entry i + 1.
    uint32_t start = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        const uint32_t dur = 1000 + i;
        TEST_ASSERT_TRUE(s.lookup(kMonday + start, p));
        TEST_ASSERT_EQUAL_UINT32(i, p.index);
        TEST_ASSERT_EQUAL_UINT32(0, p.offsetMs);
        TEST_ASSERT_TRUE(s.lookup(kMonday + start + dur - 1, p));
        TEST_ASSERT_EQUAL_UINT32(i, p.index);
        TEST_ASSERT_EQUAL_UINT32(dur - 1, p.offsetMs);
        start += dur;
    }
    // Past the end wraps to the start.
    TEST_ASSERT_TRUE(s.lookup(kMonday + start + 5, p));
    TEST_ASSERT_EQUAL_UINT32(0, p.index);
    TEST_ASSERT_EQUAL_UINT32(5, p.offsetMs);
}

void test_lookup_reads_few_records() {
    // A real week: ~10,000 entries of about a minute. Binary search should
    // touch only ~log2(n) records, not scan the file.
    ScheduleBuilder b;
    while (b.t < kWeekMs) b.add(static_cast<uint32_t>(std::min<int64_t>(60000, kWeekMs - b.t)), "music/001_a.mp3");
    Schedule s = openWith(b.build(kWeekMs));
    MemoryFile::reads = 0;
    Schedule::Position p;
    TEST_ASSERT_TRUE(s.lookup(kMonday + 5 * kDayMs + 12345, p));
    TEST_ASSERT_LESS_THAN(20, MemoryFile::reads);
    TEST_ASSERT_EQUAL_UINT32((5 * kDayMs + 12345) / 60000, p.index);
    TEST_ASSERT_EQUAL_UINT32(12345 % 60000, p.offsetMs);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_monday_reference_is_a_monday);
    RUN_TEST(test_short_schedule_wraps);
    RUN_TEST(test_open_reads_header);
    RUN_TEST(test_open_rejects_bad_files);
    RUN_TEST(test_lookup_finds_entry_and_offset);
    RUN_TEST(test_lookup_reads_few_records);
    return UNITY_END();
}
