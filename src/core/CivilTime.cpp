#include "core/CivilTime.h"

#include <cstdio>
#include <cstring>

namespace radio {

namespace {

// Howard Hinnant's days_from_civil / civil_from_days.
int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int daysInMonth(int y, int m) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : kDays[m - 1];
}

bool valid(const CivilTime& t) {
    return t.year >= 1970 && t.year <= 2199 && t.month >= 1 && t.month <= 12 && t.day >= 1 &&
           t.day <= daysInMonth(t.year, t.month) && t.hour >= 0 && t.hour <= 23 &&
           t.minute >= 0 && t.minute <= 59 && t.second >= 0 && t.second <= 59;
}

}  // namespace

int CivilTime::weekday() const {
    const int64_t days = daysFromCivil(year, month, day);
    return static_cast<int>(((days + 3) % 7 + 7) % 7);  // 1970-01-01 was a Thursday
}

int64_t toSeconds(const CivilTime& t) {
    return daysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

CivilTime fromSeconds(int64_t seconds) {
    int64_t days = seconds / 86400;
    int64_t rem = seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    CivilTime t;
    t.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    t.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    t.year = static_cast<int>(yoe + era * 400 + (t.month <= 2));
    t.hour = static_cast<int>(rem / 3600);
    t.minute = static_cast<int>(rem / 60 % 60);
    t.second = static_cast<int>(rem % 60);
    return t;
}

bool parseCivilTime(const char* text, CivilTime& out) {
    CivilTime t;
    char sep = 0;
    int consumed = 0;
    const int n = std::sscanf(text, "%4d-%2d-%2d%c%2d:%2d%n", &t.year, &t.month, &t.day, &sep,
                              &t.hour, &t.minute, &consumed);
    if (n < 6 || (sep != ' ' && sep != 'T')) return false;
    const char* rest = text + consumed;
    if (*rest == ':') {
        int len = 0;
        if (std::sscanf(rest, ":%2d%n", &t.second, &len) != 1) return false;
        rest += len;
    }
    while (*rest == ' ' || *rest == '\r' || *rest == '\n') ++rest;
    if (*rest || !valid(t)) return false;
    out = t;
    return true;
}

bool parseBuildTime(const char* date, const char* time, CivilTime& out) {
    static const char* kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {};
    CivilTime t;
    if (std::sscanf(date, "%3s %d %d", mon, &t.day, &t.year) != 3 ||
        std::sscanf(time, "%d:%d:%d", &t.hour, &t.minute, &t.second) != 3)
        return false;
    const char* p = std::strstr(kMonths, mon);
    if (!p || (p - kMonths) % 3) return false;
    t.month = static_cast<int>((p - kMonths) / 3) + 1;
    if (!valid(t)) return false;
    out = t;
    return true;
}

}  // namespace radio
