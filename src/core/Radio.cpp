#include "core/Radio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace radio {

namespace {

constexpr uint32_t kPressFlashMs = 120;
// If a lookup fails (bad file, card error), wait this long before trying again.
constexpr int64_t kLookupRetryMs = 5000;
constexpr int64_t kRecoveryMinMs = 2000;
constexpr int64_t kRecoveryMaxMs = 30000;
constexpr int kForceRestart = -2;  // playerStation_ value that no target matches

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
        s.scheduled = s.schedule.open(p_.storage, schedulePath(s).c_str());
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

void Radio::tuneTo(float mhz) {
    dial_ = std::min(std::max(tenths(mhz), dialMin_), dialMax_);
    dirty_ = true;
    retune();
}

void Radio::setVolume(float volume) {
    config_.audio.volume = volume;
    mixer_.configure(config_.audio);
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

// The player moves from entry to entry by itself; the schedule is only
// searched (storage reads) when starting a station or when the player has
// drifted from the clock, e.g. after a clock change.
void Radio::syncPlayer(int64_t nowMs) {
    int target = -1;
    if (tuned_.station >= 0 && tuned_.signal > 0 && stations_[tuned_.station].schedule.isOpen())
        target = tuned_.station;

    bool restart = target != playerStation_;
    if (!restart && target >= 0) {
        if (player_.active()) {
            // Cheap check, no storage access: is the player where the clock says?
            const int64_t length = stations_[target].schedule.lengthMs();
            int64_t drift = (static_cast<int64_t>(player_.schedulePositionMs()) -
                             Schedule::positionMs(nowMs, stations_[target].schedule.lengthMs())) %
                            length;
            if (drift > length / 2) drift -= length;
            if (drift < -length / 2) drift += length;
            restart = drift > kMaxDriftMs || drift < -kMaxDriftMs;
        } else {
            restart = nowMs - lastLookupFailMs_ >= kLookupRetryMs;  // previous lookup failed
        }
    }
    if (!restart) return;

    playerStation_ = target;
    if (target < 0) {
        player_.stop();
        return;
    }
    Schedule::Position pos;
    lookupFailed_ = !stations_[target].schedule.lookup(nowMs, pos);
    if (!lookupFailed_) {
        player_.start(stations_[target].info.dir, stations_[target].schedule, pos);
    } else {
        player_.stop();
        lastLookupFailMs_ = nowMs;
    }
}

std::string Radio::schedulePath(const Station& s) {
    return "/stations/" + s.info.dir + "/schedule.txt";
}

bool Radio::storageTrouble() const {
    if (player_.failed() || lookupFailed_) return true;
    for (const auto& s : stations_)
        if (s.scheduled && !s.schedule.isOpen()) return true;  // lost in a failed recovery
    return false;
}

// SD cards can drop out (power dips, worn contacts). Rather than going silent
// until a reboot: close everything, remount, reopen the schedules and resume
// the station where the clock says. Each attempt can stall the audio briefly,
// so retries back off from 2 s to 30 s while the trouble persists.
void Radio::recoverStorage(int64_t nowMs) {
    if (!storageTrouble()) {
        recoveryBackoffMs_ = kRecoveryMinMs;
        return;
    }
    if (nowMs < nextRecoveryMs_) return;

    player_.stop();
    for (auto& s : stations_) s.schedule.close();
    p_.storage.recover();
    for (auto& s : stations_)
        if (s.scheduled) s.schedule.open(p_.storage, schedulePath(s).c_str());
    lookupFailed_ = false;
    playerStation_ = kForceRestart;
    ++statRecoveries_;

    nextRecoveryMs_ = nowMs + recoveryBackoffMs_;
    recoveryBackoffMs_ = std::min(recoveryBackoffMs_ * 2, kRecoveryMaxMs);
}

void Radio::renderAudio() {
    size_t avail = p_.audio.availableFrames();
    while (avail >= kAudioBlock) {
        player_.read(stationBuf_, kAudioBlock);
        mixer_.render(stationBuf_, tuned_.signal, outBuf_, kAudioBlock);
        for (size_t i = 0; i < kAudioBlock; ++i) {
            statStationPeak_ = std::max(statStationPeak_, std::abs(static_cast<int>(stationBuf_[i])));
            statOutputPeak_ = std::max(statOutputPeak_, std::abs(static_cast<int>(outBuf_[i])));
        }
        statFrames_ += kAudioBlock;
        const size_t written = p_.audio.write(outBuf_, kAudioBlock);
        if (written < kAudioBlock) break;
        avail -= kAudioBlock;
    }
}

Radio::AudioStats Radio::audioStats() {
    AudioStats s;
    s.decoderOpen = player_.active() && player_.decoderOpen();
    s.sampleRate = player_.decodedSampleRate();
    s.framesRendered = statFrames_;
    s.stationPeak = statStationPeak_ / 32768.0f;
    s.outputPeak = statOutputPeak_ / 32768.0f;
    s.storageRecoveries = statRecoveries_;
    statRecoveries_ = 0;
    statFrames_ = 0;
    statStationPeak_ = statOutputPeak_ = 0;
    return s;
}

void Radio::update() {
    const InputState in = p_.input.read();
    if (in.knob >= 0) {
        const bool first = knob_.level() < 0;
        if (knob_.update(in.knob)) mixer_.setKnobGain(knob_.gain(), first);
    }
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
    syncPlayer(nowMs);
    renderAudio();
    recoverStorage(nowMs);

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
    s.knob = knob_.level();
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
