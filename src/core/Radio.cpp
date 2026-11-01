#include "core/Radio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace radio {

namespace {

constexpr uint32_t kPressFlashMs = 120;
// If a lookup fails (bad file), wait this long before trying again.
constexpr int64_t kLookupRetryMs = 1000;

// Display layout on the 128x32 panel.
constexpr int kNameY = 16;
constexpr int kScaleLeft = 2;
constexpr int kScaleRight = Framebuffer::kWidth - 3;
constexpr int kScaleBottom = Framebuffer::kHeight - 1;
constexpr int kNeedleTop = 25;
constexpr int kMeterBars = 5;

int tenths(float mhz) { return static_cast<int>(std::lround(mhz * 10.0f)); }

}  // namespace

bool Radio::begin() {
    bool ok = true;
    ok &= p_.display.begin();
    ok &= p_.input.begin();
    ok &= p_.clock.begin();
    const bool storageOk = p_.storage.begin();
    ok &= storageOk;
    ok &= p_.audio.begin(kSampleRate);

    if (storageOk) {
        loadRadioConfig(p_.storage, config_);
        loadStations();
    }
    dialMin_ = tenths(config_.dialMinMhz);
    dialMax_ = std::max(dialMin_, tenths(config_.dialMaxMhz));
    dial_ = std::min(std::max(dial_, dialMin_), dialMax_);
    tuner_.setSignal(config_.lockMhz, config_.fadeMhz);
    mixer_.configure(config_.audio);
    retune();
    dirty_ = true;
    return ok;
}

void Radio::reloadConfig() {
    RadioConfig fresh;  // keys removed from the file go back to defaults
    loadRadioConfig(p_.storage, fresh);
    fresh.dialMinMhz = config_.dialMinMhz;  // dial range needs a restart
    fresh.dialMaxMhz = config_.dialMaxMhz;
    config_ = fresh;
    tuner_.setSignal(config_.lockMhz, config_.fadeMhz);
    mixer_.configure(config_.audio);
    retune();
}

void Radio::loadStations() {
    std::vector<std::string> dirs;
    if (!p_.storage.list("/stations", dirs)) return;
    std::sort(dirs.begin(), dirs.end());
    for (const auto& dir : dirs) {
        Station s;
        if (!loadStationInfo(p_.storage, dir, s.info)) continue;
        s.schedule.open(p_.storage, ("/stations/" + dir + "/schedule.txt").c_str());
        stations_.push_back(std::move(s));
    }
    std::stable_sort(stations_.begin(), stations_.end(), [](const Station& a, const Station& b) {
        return a.info.frequencyMhz < b.info.frequencyMhz;
    });
    std::vector<float> freqs;
    for (const auto& s : stations_) freqs.push_back(s.info.frequencyMhz);
    tuner_.setStations(std::move(freqs));
}

void Radio::retune() {
    const Tuner::Result r = tuner_.tune(dialMhz());
    if (r.station != tuned_.station || r.signal != tuned_.signal) dirty_ = true;
    tuned_ = r;
}

int64_t Radio::localMs() {
    const int64_t sec = p_.clock.now();
    const uint32_t ms = p_.clock.millis();
    if (sec != lastSec_) {
        lastSec_ = sec;
        secStartMillis_ = ms;
    }
    return sec * 1000 + std::min<uint32_t>(ms - secStartMillis_, 999);
}

void Radio::trackSchedule(int64_t nowMs) {
    if (tuned_.station < 0) {
        haveEntry_ = false;
        entryStation_ = -1;
        return;
    }
    const bool stale = entryStation_ != tuned_.station || nowMs < entryStartMs_ ||
                       nowMs >= entryEndMs_;
    if (!stale) return;

    entryStation_ = tuned_.station;
    Schedule& schedule = stations_[tuned_.station].schedule;
    haveEntry_ = schedule.isOpen() && schedule.lookup(nowMs, entry_);
    if (haveEntry_) {
        entryStartMs_ = nowMs - entry_.offsetMs;
        entryEndMs_ = entryStartMs_ + entry_.entry.durationMs;
    } else {
        entryStartMs_ = nowMs;
        entryEndMs_ = nowMs + kLookupRetryMs;
    }
}

void Radio::syncPlayer(int64_t nowMs) {
    const int target = (tuned_.station >= 0 && tuned_.signal > 0 && haveEntry_) ? tuned_.station : -1;
    bool restart = target != playerStation_;
    if (!restart && target >= 0 && player_.active()) {
        // Cheap check, no storage access: is the player where the clock says?
        const Schedule& schedule = stations_[target].schedule;
        const int64_t length = schedule.lengthMs();
        int64_t drift = (static_cast<int64_t>(player_.schedulePositionMs()) -
                         Schedule::positionMs(nowMs, schedule.lengthMs())) % length;
        if (drift > length / 2) drift -= length;
        if (drift < -length / 2) drift += length;
        restart = drift > kMaxDriftMs || drift < -kMaxDriftMs;
    }
    if (!restart) return;

    playerStation_ = target;
    if (target < 0) {
        player_.stop();
        return;
    }
    Schedule::Position pos = entry_;
    pos.offsetMs = static_cast<uint32_t>(nowMs - entryStartMs_);
    player_.start(stations_[target].info.dir, stations_[target].schedule, pos);
}

void Radio::renderAudio() {
    size_t avail = p_.audio.availableFrames();
    while (avail >= kAudioBlock) {
        player_.read(stationBuf_, kAudioBlock);
        mixer_.render(stationBuf_, tuned_.signal, outBuf_, kAudioBlock);
        const size_t written = p_.audio.write(outBuf_, kAudioBlock);
        if (written < kAudioBlock) break;
        avail -= kAudioBlock;
    }
}

void Radio::update() {
    const InputState in = p_.input.read();
    if (in.dialSteps != 0) {
        const int d = std::min(std::max(dial_ + in.dialSteps, dialMin_), dialMax_);
        if (d != dial_) {
            dial_ = d;
            dirty_ = true;
            retune();
        }
    }

    const uint32_t ms = p_.clock.millis();
    if (in.pressed) {
        flashing_ = true;
        flashUntil_ = ms + kPressFlashMs;
        dirty_ = true;
    } else if (flashing_ && static_cast<int32_t>(ms - flashUntil_) >= 0) {
        flashing_ = false;
        dirty_ = true;
    }

    const int64_t nowMs = localMs();
    trackSchedule(nowMs);
    syncPlayer(nowMs);
    renderAudio();

    if (lastSec_ / 60 != shownMinute_) {
        shownMinute_ = lastSec_ / 60;
        dirty_ = true;
    }

    if (dirty_) {
        render();
        dirty_ = false;
    }
}

Radio::Status Radio::status() const {
    Status s;
    s.dialMhz = dialMhz();
    s.station = tuned_.station;
    s.signal = tuned_.signal;
    if (player_.active() && playerStation_ == tuned_.station) {
        s.playing = true;
        s.entry = player_.entry();
        s.offsetMs = player_.offsetMs();
    }
    return s;
}

void Radio::render() {
    fb_.clear();
    char text[24];

    // Frequency, large, top left.
    std::snprintf(text, sizeof(text), "%d.%d", dial_ / 10, dial_ % 10);
    const int x = fb_.drawText(0, 0, text, 2);
    fb_.drawText(x + 1, 7, "MHz");

    // Clock, top right.
    const int64_t secOfDay = ((shownMinute_ * 60) % 86400 + 86400) % 86400;
    std::snprintf(text, sizeof(text), "%02d:%02d", static_cast<int>(secOfDay / 3600),
                  static_cast<int>(secOfDay / 60 % 60));
    fb_.drawText(Framebuffer::kWidth - Framebuffer::textWidth(text) + 1, 0, text);

    // Signal meter under the clock: bars of rising height.
    const int lit = static_cast<int>(std::lround(tuned_.signal * kMeterBars));
    for (int i = 0; i < kMeterBars; ++i) {
        const int bx = Framebuffer::kWidth - kMeterBars * 4 + i * 4;
        const int h = i + 1;
        if (i < lit) fb_.fillRect(bx, 14 - h, 3, h);
        else fb_.hLine(bx, 13, 3);
    }

    // Station name.
    if (tuned_.station >= 0) {
        std::snprintf(text, sizeof(text), "%.21s", stations_[tuned_.station].info.name.c_str());
        fb_.drawText(0, kNameY, text);
    }

    // Dial scale: baseline, a tick per MHz (taller every 5), the needle.
    const int span = std::max(dialMax_ - dialMin_, 1);
    auto toX = [&](int d) { return kScaleLeft + (d - dialMin_) * (kScaleRight - kScaleLeft) / span; };
    fb_.hLine(kScaleLeft, kScaleBottom, kScaleRight - kScaleLeft + 1);
    for (int d = (dialMin_ + 9) / 10 * 10; d <= dialMax_; d += 10) {
        const int h = (d % 50 == 0) ? 4 : 2;
        fb_.vLine(toX(d), kScaleBottom - h, h);
    }
    fb_.vLine(toX(dial_), kNeedleTop, kScaleBottom - kNeedleTop + 1);

    if (flashing_) fb_.invert();
    p_.display.show(fb_.data());
}

}  // namespace radio
