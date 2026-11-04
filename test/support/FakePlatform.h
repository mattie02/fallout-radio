#pragma once

// In-memory platform for unit tests. Header-only; include from a test suite.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "platform/Platform.h"

namespace radio::test {

// Accepts up to `room` frames per update (0 = audio off) and records them.
struct FakeAudio : AudioOut {
    size_t room = 0;
    std::vector<int16_t> written;
    bool begin(uint32_t) override { return true; }
    size_t availableFrames() override { return room; }
    size_t write(const int16_t* samples, size_t frames) override {
        frames = std::min(frames, room);
        written.insert(written.end(), samples, samples + frames);
        room -= frames;
        return frames;
    }
};

struct FakeInput : Input {
    InputState next;
    bool begin() override { return true; }
    InputState read() override {
        InputState s = next;
        next = {};
        return s;
    }
};

struct FakeDisplay : Display {
    DisplayState state;
    int shows = 0;
    size_t audioSamples = 0;
    int audioPeak = 0;
    bool begin() override { return true; }
    void show(const DisplayState& s) override {
        state = s;
        ++shows;
    }
    void audio(const int16_t* samples, size_t count) override {
        audioSamples += count;
        for (size_t i = 0; i < count; ++i) audioPeak = std::max(audioPeak, std::abs(int(samples[i])));
    }
};

struct FakeClock : Clock {
    int64_t t = 0;
    uint32_t ms = 0;
    bool begin() override { return true; }
    int64_t now() override { return t; }
    uint32_t millis() override { return ms; }
};

class MemoryFile : public File {
public:
    explicit MemoryFile(const std::string& data, int* fileReads = nullptr,
                        const bool* failing = nullptr)
        : data_(data), fileReads_(fileReads), failing_(failing) {}
    size_t read(uint8_t* dst, size_t len) override {
        if (failing_ && *failing_) return 0;  // card dropped out
        const size_t n = pos_ >= data_.size() ? 0 : std::min(len, data_.size() - pos_);
        std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        reads++;
        if (fileReads_) ++*fileReads_;
        return n;
    }
    bool seek(uint32_t pos) override {
        if (pos > data_.size()) return false;
        pos_ = pos;
        return true;
    }
    uint32_t position() override { return static_cast<uint32_t>(pos_); }
    uint32_t size() override { return static_cast<uint32_t>(data_.size()); }
    static inline int reads = 0;

private:
    const std::string& data_;
    int* fileReads_;
    const bool* failing_;
    size_t pos_ = 0;
};

// Files keyed by absolute path, e.g. "/stations/01_a/station.json".
struct MemoryStorage : Storage {
    std::map<std::string, std::string> files;
    std::map<std::string, int> reads;  // read() calls per path
    // Simulated SD dropout: opens and reads fail until recover() succeeds.
    bool failing = false;
    bool recoverable = true;
    int recoveries = 0;

    bool begin() override { return true; }
    bool exists(const char* path) override { return !failing && files.count(path) > 0; }
    std::unique_ptr<File> open(const char* path) override {
        auto it = files.find(path);
        if (failing || it == files.end()) return nullptr;
        return std::make_unique<MemoryFile>(it->second, &reads[path], &failing);
    }
    bool recover() override {
        ++recoveries;
        if (recoverable) failing = false;
        return !failing;
    }
    bool list(const char* dir, std::vector<std::string>& names) override {
        const std::string prefix = std::string(dir) + "/";
        std::set<std::string> found;
        for (const auto& kv : files) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string rest = kv.first.substr(prefix.size());
            found.insert(rest.substr(0, rest.find('/')));
        }
        names.insert(names.end(), found.begin(), found.end());
        return !found.empty();
    }
};

struct FakeSettings : Settings {
    bool hasDial = false;
    int dial = 0;
    int saves = 0;
    bool loadDial(int& d) override {
        if (hasDial) d = dial;
        return hasDial;
    }
    void saveDial(int d) override {
        hasDial = true;
        dial = d;
        ++saves;
    }
};

struct FakePlatform {
    FakeAudio audio;
    FakeInput input;
    FakeDisplay display;
    FakeClock clock;
    MemoryStorage storage;
    FakeSettings settings;
    Platform platform{audio, input, display, clock, storage, settings};
};

// Reads a file next to the calling test source (pass __FILE__).
inline std::string readFixture(const char* testFile, const char* name) {
    std::string path = testFile;
    path = path.substr(0, path.find_last_of('/') + 1) + "fixtures/" + name;
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline double rms(const int16_t* s, size_t n) {
    double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += static_cast<double>(s[i]) * s[i];
    return n ? std::sqrt(sum / n) / 32768.0 : 0;
}

// Builds a schedule.txt exactly as tools/prep writes it.
struct ScheduleBuilder {
    std::vector<std::string> records;
    uint32_t t = 0;

    ScheduleBuilder& add(uint32_t durationMs, const char* path) {
        char rec[80];
        std::snprintf(rec, sizeof(rec), "%09u %08u %-44s\n", t, durationMs, path);
        records.emplace_back(rec);
        t += durationMs;
        return *this;
    }
    std::string build() const { return build(t); }
    std::string build(uint32_t lengthMs) const {
        char header[80];
        std::snprintf(header, sizeof(header), "#VRSCHED1 %06u %09u", static_cast<unsigned>(records.size()),
                      lengthMs);
        std::string out = header;
        out.resize(63, ' ');
        out += '\n';
        for (const auto& r : records) out += r;
        return out;
    }
};

}  // namespace radio::test
