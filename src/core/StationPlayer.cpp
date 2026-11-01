#include "core/StationPlayer.h"

#include <algorithm>
#include <cstring>

namespace radio {

namespace {
uint64_t msToSamples(uint64_t ms) { return ms * kSampleRate / 1000; }
}  // namespace

void StationPlayer::start(const std::string& stationDir, Schedule& schedule,
                          const Schedule::Position& pos) {
    dir_ = stationDir;
    schedule_ = &schedule;
    index_ = pos.index;
    entry_ = pos.entry;
    openEntry(pos.offsetMs);
}

void StationPlayer::stop() {
    schedule_ = nullptr;
    decoder_.close();
}

uint32_t StationPlayer::offsetMs() const {
    return static_cast<uint32_t>(played_ * 1000 / kSampleRate);
}

void StationPlayer::openEntry(uint32_t offsetMs) {
    const uint64_t total = msToSamples(entry_.durationMs);
    played_ = std::min(msToSamples(offsetMs), total);
    remaining_ = total - played_;
    decoder_.close();
    if (entry_.isSilence()) return;
    const std::string path = "/stations/" + dir_ + "/" + entry_.path;
    decoder_.open(storage_.open(path.c_str()), offsetMs);  // on failure: silence
}

void StationPlayer::advance() {
    index_ = (index_ + 1) % schedule_->count();
    if (!schedule_->entry(index_, entry_)) {
        stop();
        return;
    }
    openEntry(0);
}

void StationPlayer::read(int16_t* out, size_t count) {
    size_t done = 0;
    while (done < count) {
        if (!active()) {
            std::memset(out + done, 0, (count - done) * sizeof(int16_t));
            return;
        }
        if (remaining_ == 0) {
            advance();
            continue;
        }
        const size_t n = static_cast<size_t>(std::min<uint64_t>(count - done, remaining_));
        size_t got = decoder_.isOpen() ? decoder_.read(out + done, n) : 0;
        if (got < n) std::memset(out + done + got, 0, (n - got) * sizeof(int16_t));
        done += n;
        played_ += n;
        remaining_ -= n;
    }
}

}  // namespace radio
