#pragma once

#include <cstdint>
#include <string>

#include "core/Mp3Decoder.h"
#include "core/Schedule.h"
#include "platform/Platform.h"

namespace radio {

// Plays one station's schedule: decodes the current entry from its offset and
// moves to the next entry when the entry's scheduled duration has been played
// (counted in samples, so transitions are seamless). Silence entries, missing
// files and files shorter than their slot all play silence, keeping time.
class StationPlayer {
public:
    explicit StationPlayer(Storage& storage) : storage_(storage) {}

    // stationDir: folder name under /stations. The schedule must outlive
    // playback.
    void start(const std::string& stationDir, Schedule& schedule, const Schedule::Position& pos);
    void stop();
    bool active() const { return schedule_ != nullptr; }

    // Always fills `count` samples (silence when inactive).
    void read(int16_t* out, size_t count);

    const Schedule::Entry& entry() const { return entry_; }
    uint32_t index() const { return index_; }
    uint32_t offsetMs() const;
    // Position within the whole schedule.
    uint32_t schedulePositionMs() const { return entry_.startMs + offsetMs(); }

    // Diagnostics.
    bool decoderOpen() const { return decoder_.isOpen() && !decoder_.atEnd(); }
    int decodedSampleRate() const { return decoder_.sampleRate(); }

private:
    void openEntry(uint32_t offsetMs);
    void advance();

    Storage& storage_;
    std::string dir_;
    Schedule* schedule_ = nullptr;
    uint32_t index_ = 0;
    Schedule::Entry entry_;
    uint64_t played_ = 0;     // samples into the entry, including the start offset
    uint64_t remaining_ = 0;  // samples left in the entry's slot
    Mp3Decoder decoder_;
};

}  // namespace radio
