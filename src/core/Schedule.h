#pragma once

#include <cstdint>
#include <memory>

#include "platform/Platform.h"

namespace radio {

// A station's schedule.txt, read in place from storage: fixed-width 64-byte
// records, binary-searched so the file is never loaded whole. Format is
// documented in tools/prep/README.md (the prep tool writes it).
class Schedule {
public:
    static constexpr uint32_t kRecordSize = 64;
    static constexpr uint32_t kPathWidth = 44;
    static constexpr int64_t kWeekMs = 7LL * 24 * 3600 * 1000;
    // 1970-01-05 00:00 was a Monday; schedules start at Monday 00:00.
    static constexpr int64_t kMondayEpochMs = 4LL * 24 * 3600 * 1000;

    struct Entry {
        uint32_t startMs = 0;  // from the start of the schedule
        uint32_t durationMs = 0;
        char path[kPathWidth + 1] = {};  // relative to the station folder
        bool isSilence() const { return path[0] == '-' && path[1] == 0; }
    };

    struct Position {
        uint32_t index = 0;
        Entry entry;
        uint32_t offsetMs = 0;  // into the entry
    };

    // path: e.g. "/stations/01_crooner/schedule.txt". Validates the header
    // and file size.
    bool open(Storage& storage, const char* path);
    void close();
    bool isOpen() const { return file_ != nullptr; }

    uint32_t count() const { return count_; }
    uint32_t lengthMs() const { return lengthMs_; }

    bool entry(uint32_t index, Entry& out);
    // What is playing at a local wall-clock time (ms since 1970, no zone).
    bool lookup(int64_t localMs, Position& out);

    // Local time -> position within a schedule of the given length, counted
    // from the most recent Monday 00:00 and wrapped.
    static uint32_t positionMs(int64_t localMs, uint32_t lengthMs);

private:
    bool readRecord(uint32_t record, char* buf);

    std::unique_ptr<File> file_;
    uint32_t count_ = 0;
    uint32_t lengthMs_ = 0;
};

}  // namespace radio
