#pragma once

#include <cstdint>

namespace radio {

// Calendar date/time with no time zone: the radio keeps the recipient's
// local time as plain seconds since 1970-01-01 00:00 (see Clock::now()).
struct CivilTime {
    int year = 1970;
    int month = 1;  // 1..12
    int day = 1;    // 1..31
    int hour = 0;
    int minute = 0;
    int second = 0;
    int weekday() const;  // 0 = Monday .. 6 = Sunday
};

int64_t toSeconds(const CivilTime& t);
CivilTime fromSeconds(int64_t seconds);

// "YYYY-MM-DD HH:MM" or "YYYY-MM-DD HH:MM:SS" (also accepts 'T' between).
bool parseCivilTime(const char* text, CivilTime& out);

// Compiler's __DATE__ ("Dec 24 2026") and __TIME__ ("23:58:00").
bool parseBuildTime(const char* date, const char* time, CivilTime& out);

}  // namespace radio
