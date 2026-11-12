// Audio: MP3 decoding and seeking, schedule playback, static, effects, mixing.
// fixtures/tone440.mp3 is a 2 s 440 Hz tone made by tools/prep (radio format).

#include <unity.h>

#include <vector>

#include "../support/FakePlatform.h"
#include "core/Effects.h"
#include "core/Mixer.h"
#include "core/Mp3Decoder.h"
#include "core/Radio.h"
#include "core/StationPlayer.h"

using namespace radio;
using namespace radio::test;

namespace {

constexpr size_t kSecond = kSampleRate;
// Encoder delay/padding and frame rounding: allow two frames either way.
constexpr int kSlack = 2 * 1152;

std::string tone;
MemoryStorage storage;

std::vector<int16_t> readAll(Mp3Decoder& d) {
    std::vector<int16_t> out;
    int16_t buf[1000];
    size_t n;
    while ((n = d.read(buf, 1000)) > 0) out.insert(out.end(), buf, buf + n);
    return out;
}

// Frequency from upward zero crossings.
double frequency(const int16_t* s, size_t n) {
    int crossings = 0;
    for (size_t i = 1; i < n; ++i)
        if (s[i - 1] < 0 && s[i] >= 0) ++crossings;
    return crossings * static_cast<double>(kSampleRate) / n;
}

// RMS gain of a filter for a sine at `hz`, after it settles.
double gainAt(VintageEffects& fx, float hz) {
    double in = 0, out = 0;
    for (size_t i = 0; i < kSecond / 2; ++i) {
        const float x = 0.1f * std::sin(2 * 3.14159265f * hz * i / kSampleRate);
        const float y = fx.process(x);
        if (i > kSecond / 4) {
            in += x * x;
            out += y * y;
        }
    }
    return std::sqrt(out / in);
}

}  // namespace

void setUp() {
    storage.files.clear();
    storage.files["/stations/01_t/music/001_tone.mp3"] = tone;
}
void tearDown() {}

void test_decode_whole_file() {
    Mp3Decoder d;
    TEST_ASSERT_TRUE(d.open(storage.open("/stations/01_t/music/001_tone.mp3")));
    const auto pcm = readAll(d);
    TEST_ASSERT_TRUE(d.atEnd());
    TEST_ASSERT_EQUAL(44100, d.sampleRate());
    TEST_ASSERT_INT_WITHIN(kSlack, 2 * kSecond, pcm.size());
    const size_t mid = pcm.size() / 4;
    TEST_ASSERT_FLOAT_WITHIN(5.0, 440.0, frequency(&pcm[mid], kSecond / 2));
    TEST_ASSERT_GREATER_THAN(0.05 * 1000, rms(&pcm[mid], kSecond / 2) * 1000);
}

void test_seek_by_time() {
    Mp3Decoder d;
    TEST_ASSERT_TRUE(d.open(storage.open("/stations/01_t/music/001_tone.mp3"), 1500));
    const auto pcm = readAll(d);
    TEST_ASSERT_INT_WITHIN(kSlack, kSecond / 2, pcm.size());
    TEST_ASSERT_FLOAT_WITHIN(5.0, 440.0, frequency(&pcm[pcm.size() / 4], pcm.size() / 2));
}

void test_open_past_end_or_missing_fails() {
    Mp3Decoder d;
    TEST_ASSERT_FALSE(d.open(storage.open("/stations/01_t/music/001_tone.mp3"), 5000));
    TEST_ASSERT_FALSE(d.open(nullptr));
    int16_t buf[10];
    TEST_ASSERT_EQUAL(0, d.read(buf, 10));
}

void test_player_follows_schedule_across_entries() {
    storage.files["/s"] = ScheduleBuilder()
                              .add(2000, "music/001_tone.mp3")
                              .add(1000, "-")
                              .add(2000, "music/001_tone.mp3")
                              .build();
    Schedule schedule;
    TEST_ASSERT_TRUE(schedule.open(storage, "/s"));
    Schedule::Position pos;
    TEST_ASSERT_TRUE(schedule.lookup(Schedule::kMondayEpochMs + 1800, pos));

    StationPlayer player(storage);
    player.start("01_t", schedule, pos);
    std::vector<int16_t> buf(kSecond / 2);  // 500 ms: 200 ms tone, then silence
    player.read(buf.data(), buf.size());
    const size_t edge = kSecond / 5;
    TEST_ASSERT_GREATER_THAN(100, rms(buf.data(), edge - 2000) * 10000);
    TEST_ASSERT_EQUAL_FLOAT(0.0, rms(&buf[edge], buf.size() - edge));
    TEST_ASSERT_TRUE(player.entry().isSilence());
    TEST_ASSERT_EQUAL_UINT32(1, player.index());
    TEST_ASSERT_EQUAL_UINT32(300, player.offsetMs());

    std::vector<int16_t> more(kSecond);  // 700 ms silence left, then tone again
    player.read(more.data(), more.size());
    TEST_ASSERT_EQUAL_UINT32(2, player.index());
    TEST_ASSERT_EQUAL_UINT32(300, player.offsetMs());
    TEST_ASSERT_GREATER_THAN(100, rms(&more[kSecond * 7 / 10 + 2000], 5000) * 10000);
    TEST_ASSERT_EQUAL_UINT32(3300, player.schedulePositionMs());  // entry 2 starts at 3000
}

void test_player_missing_file_keeps_time() {
    storage.files["/s"] = ScheduleBuilder().add(1000, "music/404.mp3").add(1000, "music/001_tone.mp3").build();
    Schedule schedule;
    TEST_ASSERT_TRUE(schedule.open(storage, "/s"));
    Schedule::Position pos;
    schedule.lookup(Schedule::kMondayEpochMs, pos);
    StationPlayer player(storage);
    player.start("01_t", schedule, pos);
    std::vector<int16_t> buf(kSecond * 3 / 2);
    player.read(buf.data(), buf.size());
    TEST_ASSERT_EQUAL_FLOAT(0.0, rms(buf.data(), kSecond));
    TEST_ASSERT_EQUAL_UINT32(1, player.index());
    TEST_ASSERT_GREATER_THAN(100, rms(&buf[kSecond + 2000], 5000) * 10000);
}

void test_static_is_bounded_and_deterministic() {
    StaticGenerator a(42), b(42);
    a.setCrackle(20);
    b.setCrackle(20);
    double sum = 0;
    float peak = 0;
    for (size_t i = 0; i < kSecond; ++i) {
        const float x = a.next();
        TEST_ASSERT_EQUAL_FLOAT(x, b.next());
        sum += x * x;
        peak = std::fmax(peak, std::fabs(x));
    }
    const double r = std::sqrt(sum / kSecond);
    TEST_ASSERT_TRUE(r > 0.1 && r < 0.8);
    TEST_ASSERT_TRUE(peak < 2.5f);
}

void test_vintage_band_pass() {
    AudioConfig c;
    c.drive = 1.0f;
    c.volume = 1.0f;
    VintageEffects fx;
    fx.configure(c);
    TEST_ASSERT_TRUE(gainAt(fx, 1000) > 0.85);
    fx.configure(c);
    TEST_ASSERT_TRUE(gainAt(fx, 60) < 0.15);
    fx.configure(c);
    TEST_ASSERT_TRUE(gainAt(fx, 10000) < 0.15);
}

void test_soft_clip() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.1f, softClip(0.1f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, softClip(10.0f));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, softClip(-10.0f));
    TEST_ASSERT_TRUE(softClip(2.0f) < 1.0f && softClip(2.0f) > softClip(1.0f));
}

void test_mixer_crossfades_by_signal() {
    AudioConfig c;
    c.hiss = 0;
    Mixer m;
    m.configure(c);
    std::vector<int16_t> silence(kSecond / 2, 0), out(kSecond / 2);

    // No station, no signal: static only.
    m.render(silence.data(), 0.0f, out.data(), out.size());
    const double staticOnly = rms(out.data(), out.size());
    TEST_ASSERT_TRUE(staticOnly > 0.02);

    // Full signal with a silent station and no hiss: near silence, after the
    // ~30 ms ramp.
    m.render(silence.data(), 1.0f, out.data(), out.size());
    TEST_ASSERT_TRUE(rms(&out[kSecond / 5], out.size() - kSecond / 5) < 0.001);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, m.smoothedSignal());
}

void test_mixer_ramps_smoothly() {
    Mixer m;
    m.configure(AudioConfig{});
    std::vector<int16_t> silence(kSampleRate / 100, 0), out(silence.size());  // 10 ms
    m.render(silence.data(), 1.0f, out.data(), out.size());
    TEST_ASSERT_TRUE(m.smoothedSignal() < 0.5f);  // not an instant jump
}

void test_radio_plays_tuned_station() {
    FakePlatform p;
    p.storage.files = storage.files;
    p.storage.files["/stations/01_t/station.json"] = R"({"name": "Tone", "frequency": 98.6})";
    p.storage.files["/stations/01_t/schedule.txt"] = ScheduleBuilder().add(2000, "music/001_tone.mp3").build();
    p.clock.t = 20808LL * 86400;  // a Monday 00:00
    Radio radio(p.platform);
    radio.begin();  // dial starts at 98.6: on the station
    p.audio.room = 2 * kSecond;  // through the warm-up fade
    radio.update();
    p.audio.written.clear();

    p.audio.room = kSecond / 2;
    radio.update();
    TEST_ASSERT_TRUE(radio.status().playing);
    TEST_ASSERT_EQUAL(kSecond / 2 / 256 * 256, p.audio.written.size());
    const auto& w = p.audio.written;
    const size_t from = w.size() / 2;
    TEST_ASSERT_FLOAT_WITHIN(15.0, 440.0, frequency(&w[from], w.size() - from));

    // Tune off-station: static, no tone.
    p.input.next.dialSteps = 30;
    p.audio.written.clear();
    p.audio.room = kSecond / 2;
    radio.update();
    TEST_ASSERT_FALSE(radio.status().playing);
    TEST_ASSERT_TRUE(rms(&w[w.size() / 2], w.size() / 2) > 0.02);
}

void test_song_change_reads_schedule_once() {
    // Crossing an entry boundary must not re-search the schedule: on an SD
    // card each scattered read is slow, and a search stalls the audio.
    FakePlatform p;
    p.storage.files = storage.files;
    p.storage.files["/stations/01_t/station.json"] = R"({"name": "Tone", "frequency": 98.6})";
    ScheduleBuilder b;
    for (int i = 0; i < 200; ++i) b.add(1000, "music/001_tone.mp3");  // a song every second
    p.storage.files["/stations/01_t/schedule.txt"] = b.build();
    p.clock.t = 20808LL * 86400;
    Radio radio(p.platform);
    radio.begin();
    p.audio.room = 2048;
    radio.update();  // starts the station: one search
    const int afterStart = p.storage.reads["/stations/01_t/schedule.txt"];

    for (int ms = 0; ms < 3000; ms += 50) {  // three boundaries, clock in step
        p.clock.ms += 50;
        if (p.clock.ms % 1000 == 0) ++p.clock.t;
        p.audio.room += kSampleRate / 20;  // 50 ms of audio per 50 ms of clock
        radio.update();
    }
    TEST_ASSERT_EQUAL_UINT32(3000, radio.status().entry.startMs);
    // One entry read per boundary, no binary searches.
    TEST_ASSERT_LESS_OR_EQUAL(3, p.storage.reads["/stations/01_t/schedule.txt"] - afterStart);
}

// A radio playing a long schedule of tone entries, clock and audio in step.
struct ToneRadio : FakePlatform {
    Radio radio{platform};
    explicit ToneRadio(const std::string& toneMp3) {
        storage.files["/stations/01_t/music/001_tone.mp3"] = toneMp3;
        storage.files["/stations/01_t/station.json"] = R"({"name": "Tone", "frequency": 98.6})";
        ScheduleBuilder b;
        for (int i = 0; i < 100; ++i) b.add(2000, "music/001_tone.mp3");
        storage.files["/stations/01_t/schedule.txt"] = b.build();
        clock.t = 20808LL * 86400;
        radio.begin();
    }
    // Advances clock and audio together in 50 ms steps.
    void run(int ms) {
        for (int t = 0; t < ms; t += 50) {
            clock.ms += 50;
            if (clock.ms % 1000 == 0) ++clock.t;
            audio.room += kSampleRate / 20;
            radio.update();
        }
    }
    uint32_t clockPositionMs() const {
        return static_cast<uint32_t>((clock.t - 20808LL * 86400) * 1000 + clock.ms % 1000);
    }
};

// --- Warm-up ------------------------------------------------------------------

struct WarmUpRadio : FakePlatform {
    Radio radio{platform};
    std::vector<int16_t>& out = audio.written;
    // Renders up to the given time since power-on.
    void renderUntil(double seconds) {
        const size_t want = static_cast<size_t>(seconds * kSampleRate);
        if (want > out.size()) audio.room = (want - out.size()) / 256 * 256;
        radio.update();
    }
    // Rendering stops on a block boundary, so `to` may be a little past the end.
    size_t lengthBetween(double from, double to) const {
        return std::min(static_cast<size_t>(to * kSampleRate), out.size()) -
               static_cast<size_t>(from * kSampleRate);
    }
    double level(double from, double to) const {
        return rms(out.data() + static_cast<size_t>(from * kSampleRate), lengthBetween(from, to));
    }
    double hz(double from, double to) const {
        return frequency(out.data() + static_cast<size_t>(from * kSampleRate), lengthBetween(from, to));
    }
};

void test_warmup_without_boot_file_just_fades_in() {
    WarmUpRadio r;
    r.radio.begin();
    r.renderUntil(3);
    const double full = r.level(2.5, 3);
    TEST_ASSERT_TRUE(full > 0.01);  // the static, at full level
    TEST_ASSERT_TRUE(r.level(0, 0.1) < full * 0.15);
    const double middle = r.level(0.65, 0.85);
    TEST_ASSERT_TRUE(middle > full * 0.25);
    TEST_ASSERT_TRUE(middle < full * 0.75);
}

void test_warmup_plays_boot_file_then_radio_comes_up() {
    WarmUpRadio r;
    r.storage.files["/system/boot.mp3"] = tone;  // 2 s: the radio comes up from 0.5 s
    r.radio.begin();
    r.renderUntil(0.4);
    // Just the boot sound, with no static mixed in.
    TEST_ASSERT_TRUE(r.level(0.1, 0.4) > 0.02);
    TEST_ASSERT_FLOAT_WITHIN(5, 440, r.hz(0.1, 0.4));

    r.renderUntil(4);
    // It's over and closed: static now, and no more reads.
    TEST_ASSERT_TRUE(r.level(3, 4) > 0.01);
    TEST_ASSERT_TRUE(r.hz(3, 4) > 600);
    const int reads = r.storage.reads["/system/boot.mp3"];
    TEST_ASSERT_GREATER_THAN(0, reads);
    r.renderUntil(5);
    TEST_ASSERT_EQUAL(reads, r.storage.reads["/system/boot.mp3"]);
}

void test_warmup_follows_knob_and_level_setting() {
    WarmUpRadio knobDown;
    knobDown.storage.files["/system/boot.mp3"] = tone;
    knobDown.radio.begin();
    knobDown.input.next.knob = 0;
    knobDown.renderUntil(0.4);
    TEST_ASSERT_TRUE(knobDown.level(0.1, 0.4) < 0.0001);

    WarmUpRadio silenced;
    silenced.storage.files["/system/boot.mp3"] = tone;
    silenced.storage.files["/system/config.json"] = R"({"audio": {"boot_level": 0}})";
    silenced.radio.begin();
    silenced.renderUntil(0.4);
    TEST_ASSERT_TRUE(silenced.level(0.1, 0.4) < 0.0001);
}

void test_warmup_station_stays_live_underneath() {
    WarmUpRadio r;
    r.storage.files["/system/boot.mp3"] = tone;
    r.storage.files["/stations/01_t/music/001_tone.mp3"] = tone;
    r.storage.files["/stations/01_t/station.json"] = R"({"name": "Tone", "frequency": 98.6})";
    r.storage.files["/stations/01_t/schedule.txt"] = ScheduleBuilder().add(2000, "music/001_tone.mp3").build();
    r.clock.t = 20808LL * 86400;  // a Monday 00:00
    r.radio.begin();
    r.renderUntil(0.4);
    const Radio::Status s = r.radio.status();
    TEST_ASSERT_TRUE(s.playing);
    TEST_ASSERT_UINT32_WITHIN(50, 400, s.offsetMs);  // moving, though not yet audible
}

void test_warmup_card_failure_or_bad_file_cant_keep_radio_silent() {
    WarmUpRadio dropped;
    dropped.storage.files["/system/boot.mp3"] = tone;
    dropped.radio.begin();
    dropped.renderUntil(0.1);
    dropped.storage.failing = true;
    dropped.renderUntil(2);
    TEST_ASSERT_TRUE(dropped.level(1.7, 2) > 0.01);

    WarmUpRadio garbage;
    garbage.storage.files["/system/boot.mp3"] = std::string(16000 * 60, 'x');  // "60 s" long
    garbage.radio.begin();
    garbage.renderUntil(2);
    TEST_ASSERT_TRUE(garbage.level(1.7, 2) > 0.01);
}

void test_recovers_from_card_dropout() {
    ToneRadio r(tone);
    r.run(1000);
    TEST_ASSERT_TRUE(r.radio.status().playing);

    r.storage.failing = true;  // card drops out mid-song
    r.run(1500);
    TEST_ASSERT_EQUAL(1, r.storage.recoveries);  // remounted once, not repeatedly
    TEST_ASSERT_FALSE(r.storage.failing);

    r.audio.written.clear();
    r.run(1000);
    const Radio::Status s = r.radio.status();
    TEST_ASSERT_TRUE(s.playing);
    // Resumed where the clock says, not where it stopped.
    TEST_ASSERT_UINT32_WITHIN(300, r.clockPositionMs(), s.entry.startMs + s.offsetMs);
    const auto& w = r.audio.written;
    TEST_ASSERT_FLOAT_WITHIN(15.0, 440.0, frequency(&w[w.size() / 2], w.size() / 2));
    TEST_ASSERT_EQUAL(1, r.radio.audioStats().storageRecoveries);
}

void test_dead_card_retries_with_backoff() {
    ToneRadio r(tone);
    r.run(500);
    r.storage.failing = true;
    r.storage.recoverable = false;
    r.run(20000);
    // Attempts at ~0, 2, 6 and 14 s: backing off, not hammering the card.
    TEST_ASSERT_TRUE(r.storage.recoveries >= 3 && r.storage.recoveries <= 5);
    TEST_ASSERT_FALSE(r.radio.status().playing);

    r.storage.recoverable = true;  // card comes back (e.g. reseated)
    r.run(32000);                  // within the 30 s maximum pause
    TEST_ASSERT_TRUE(r.radio.status().playing);
}

// After a long time off, the card can still be waking up when the radio
// starts. It must come up with the stations once the card answers, not boot
// to static for good.
void test_card_not_ready_at_power_on() {
    FakePlatform p;
    Radio radio{p.platform};
    p.storage.files["/stations/01_t/music/001_tone.mp3"] = tone;
    p.storage.files["/stations/01_t/station.json"] = R"({"name": "Tone", "frequency": 98.6})";
    p.storage.files["/stations/01_t/schedule.txt"] = ScheduleBuilder().add(60000, "music/001_tone.mp3").build();
    p.storage.files["/system/config.json"] = R"({"dial": {"min": 90.0, "max": 100.0}})";
    p.clock.t = 20808LL * 86400;
    p.settings.hasDial = true;
    p.settings.dial = 986;
    p.storage.failing = true;
    radio.begin();
    TEST_ASSERT_EQUAL(0, static_cast<int>(radio.stations().size()));

    p.storage.failing = false;  // card wakes up
    for (int t = 0; t < 3000; t += 50) {
        p.clock.ms += 50;
        p.audio.room += kSampleRate / 20;
        radio.update();
    }
    TEST_ASSERT_EQUAL(1, static_cast<int>(radio.stations().size()));
    TEST_ASSERT_EQUAL(900, radio.dialMin());  // config.json applied late too
    TEST_ASSERT_EQUAL(986, radio.dial());     // saved dial kept
    TEST_ASSERT_TRUE(radio.status().playing);
}

void test_missing_file_is_not_a_card_failure() {
    ToneRadio r(tone);
    r.storage.files.erase("/stations/01_t/music/001_tone.mp3");
    r.run(5000);
    TEST_ASSERT_EQUAL(0, r.storage.recoveries);
    TEST_ASSERT_TRUE(r.radio.status().playing);  // silence in the slot, keeping time
}

int main() {
    tone = readFixture(__FILE__, "tone440.mp3");
    if (tone.size() < 10000) {
        std::printf("missing fixtures/tone440.mp3\n");
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_decode_whole_file);
    RUN_TEST(test_seek_by_time);
    RUN_TEST(test_open_past_end_or_missing_fails);
    RUN_TEST(test_player_follows_schedule_across_entries);
    RUN_TEST(test_player_missing_file_keeps_time);
    RUN_TEST(test_static_is_bounded_and_deterministic);
    RUN_TEST(test_vintage_band_pass);
    RUN_TEST(test_soft_clip);
    RUN_TEST(test_mixer_crossfades_by_signal);
    RUN_TEST(test_mixer_ramps_smoothly);
    RUN_TEST(test_radio_plays_tuned_station);
    RUN_TEST(test_song_change_reads_schedule_once);
    RUN_TEST(test_warmup_without_boot_file_just_fades_in);
    RUN_TEST(test_warmup_plays_boot_file_then_radio_comes_up);
    RUN_TEST(test_warmup_follows_knob_and_level_setting);
    RUN_TEST(test_warmup_station_stays_live_underneath);
    RUN_TEST(test_warmup_card_failure_or_bad_file_cant_keep_radio_silent);
    RUN_TEST(test_recovers_from_card_dropout);
    RUN_TEST(test_dead_card_retries_with_backoff);
    RUN_TEST(test_card_not_ready_at_power_on);
    RUN_TEST(test_missing_file_is_not_a_card_failure);
    return UNITY_END();
}
