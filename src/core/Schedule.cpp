#include "core/Schedule.h"

#include <cstring>

namespace radio {

namespace {

constexpr char kMagic[] = "#VRSCHED1 ";

bool parseDigits(const char* s, int n, uint32_t& out) {
    out = 0;
    for (int i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        out = out * 10 + static_cast<uint32_t>(s[i] - '0');
    }
    return true;
}

}  // namespace

bool Schedule::open(Storage& storage, const char* path) {
    close();
    file_ = storage.open(path);
    if (!file_) return false;

    char rec[kRecordSize];
    const bool ok = readRecord(0, rec) && std::memcmp(rec, kMagic, sizeof(kMagic) - 1) == 0 &&
                    parseDigits(rec + 10, 6, count_) && rec[16] == ' ' &&
                    parseDigits(rec + 17, 9, lengthMs_) && rec[kRecordSize - 1] == '\n' &&
                    count_ > 0 && lengthMs_ > 0 &&
                    file_->size() == (count_ + 1) * kRecordSize;
    if (!ok) close();
    return ok;
}

void Schedule::close() {
    file_.reset();
    count_ = 0;
    lengthMs_ = 0;
}

bool Schedule::readRecord(uint32_t record, char* buf) {
    return file_ && file_->seek(record * kRecordSize) &&
           file_->read(reinterpret_cast<uint8_t*>(buf), kRecordSize) == kRecordSize;
}

bool Schedule::entry(uint32_t index, Entry& out) {
    char rec[kRecordSize];
    if (index >= count_ || !readRecord(index + 1, rec)) return false;
    if (!parseDigits(rec, 9, out.startMs) || rec[9] != ' ' ||
        !parseDigits(rec + 10, 8, out.durationMs) || rec[18] != ' ' ||
        rec[kRecordSize - 1] != '\n')
        return false;
    int len = kPathWidth;
    while (len > 0 && rec[19 + len - 1] == ' ') --len;
    if (len == 0) return false;
    std::memcpy(out.path, rec + 19, len);
    out.path[len] = 0;
    return true;
}

uint32_t Schedule::positionMs(int64_t localMs, uint32_t lengthMs) {
    int64_t week = (localMs - kMondayEpochMs) % kWeekMs;
    if (week < 0) week += kWeekMs;
    return static_cast<uint32_t>(week % lengthMs);
}

bool Schedule::lookup(int64_t localMs, Position& out) {
    if (!isOpen()) return false;
    const uint32_t pos = positionMs(localMs, lengthMs_);

    // Last entry with startMs <= pos.
    uint32_t lo = 0, hi = count_ - 1;
    Entry e;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo + 1) / 2;
        if (!entry(mid, e)) return false;
        if (e.startMs <= pos) lo = mid;
        else hi = mid - 1;
    }
    if (!entry(lo, e) || e.startMs > pos || pos - e.startMs >= e.durationMs) return false;
    out.index = lo;
    out.entry = e;
    out.offsetMs = pos - e.startMs;
    return true;
}

}  // namespace radio
