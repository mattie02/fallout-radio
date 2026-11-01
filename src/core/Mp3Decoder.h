#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "platform/Platform.h"

namespace radio {

// Streams mono 16-bit PCM from an MP3 file using minimp3 (same decoder on
// both targets). Files are expected in the prep tool's format: CBR 128 kbps,
// 44.1 kHz, no tags, so time t ms starts near byte t * 16. Stereo input is
// downmixed; other sample rates play at the wrong pitch.
class Mp3Decoder {
public:
    static constexpr uint32_t kBytesPerSecond = 16000;  // 128 kbps

    Mp3Decoder();
    ~Mp3Decoder();
    Mp3Decoder(const Mp3Decoder&) = delete;
    Mp3Decoder& operator=(const Mp3Decoder&) = delete;

    // Takes ownership of the file and starts at offsetMs. The decoder
    // resyncs to the next frame after the seek point.
    bool open(std::unique_ptr<File> file, uint32_t offsetMs = 0);
    void close();
    bool isOpen() const { return file_ != nullptr; }

    // Fills up to `count` samples; returns fewer only at end of file.
    size_t read(int16_t* out, size_t count);
    bool atEnd() const { return ended_; }
    // Sample rate reported by the last decoded frame (0 before the first).
    int sampleRate() const { return sampleRate_; }

private:
    bool decodeFrame();
    bool refill();

    struct State;  // mp3dec_t, kept out of the header
    std::unique_ptr<State> state_;
    std::unique_ptr<File> file_;

    static constexpr size_t kInputSize = 8192;
    static constexpr size_t kMaxFrameSamples = 1152 * 2;
    uint8_t input_[kInputSize];
    size_t inputLen_ = 0;
    size_t inputPos_ = 0;
    bool fileDone_ = false;

    int16_t pcm_[kMaxFrameSamples];
    size_t pcmLen_ = 0;
    size_t pcmPos_ = 0;
    bool ended_ = true;
    int sampleRate_ = 0;
};

}  // namespace radio
