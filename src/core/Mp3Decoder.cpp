#include "core/Mp3Decoder.h"

#include <algorithm>
#include <cstring>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include <minimp3.h>

namespace radio {

struct Mp3Decoder::State {
    mp3dec_t dec;
};

Mp3Decoder::Mp3Decoder() : state_(new State) {}

Mp3Decoder::~Mp3Decoder() = default;

bool Mp3Decoder::open(std::unique_ptr<File> file, uint32_t offsetMs) {
    close();
    if (!file) return false;
    const uint64_t byte = static_cast<uint64_t>(offsetMs) * kBytesPerSecond / 1000;
    if (byte >= file->size() || !file->seek(static_cast<uint32_t>(byte))) return false;
    file_ = std::move(file);
    mp3dec_init(&state_->dec);
    ended_ = false;
    return true;
}

void Mp3Decoder::close() {
    file_.reset();
    inputLen_ = inputPos_ = 0;
    pcmLen_ = pcmPos_ = 0;
    fileDone_ = false;
    ended_ = true;
    failed_ = false;
}

bool Mp3Decoder::refill() {
    if (fileDone_) return false;
    if (inputPos_ > 0) {
        std::memmove(input_, input_ + inputPos_, inputLen_ - inputPos_);
        inputLen_ -= inputPos_;
        inputPos_ = 0;
    }
    if (inputLen_ == kInputSize) return false;
    const size_t n = file_->read(input_ + inputLen_, kInputSize - inputLen_);
    if (n == 0) {
        fileDone_ = true;
        if (file_->position() < file_->size()) failed_ = true;  // not EOF: read error
    }
    inputLen_ += n;
    return n > 0;
}

// Decodes the next frame into pcm_. Returns false at end of file.
bool Mp3Decoder::decodeFrame() {
    for (;;) {
        // Keep at least a few frames buffered so frame sync is reliable.
        if (inputLen_ - inputPos_ < kInputSize / 2) refill();
        const size_t avail = inputLen_ - inputPos_;
        if (avail == 0) return false;

        mp3dec_frame_info_t info;
        const int samples = mp3dec_decode_frame(&state_->dec, input_ + inputPos_,
                                                static_cast<int>(avail), pcm_, &info);
        if (info.frame_bytes == 0) {
            // Nothing decodable in the buffer: need more data, or it's junk.
            if (!refill()) {
                if (fileDone_) return false;
                inputPos_ = inputLen_;  // full buffer of garbage: drop it
            }
            continue;
        }
        inputPos_ += static_cast<size_t>(info.frame_bytes);
        if (samples == 0) continue;  // skipped data (e.g. resync after seek)

        sampleRate_ = info.hz;
        if (info.channels == 2) {
            for (int i = 0; i < samples; ++i)
                pcm_[i] = static_cast<int16_t>((pcm_[2 * i] + pcm_[2 * i + 1]) / 2);
        }
        pcmLen_ = static_cast<size_t>(samples);
        pcmPos_ = 0;
        return true;
    }
}

size_t Mp3Decoder::read(int16_t* out, size_t count) {
    size_t done = 0;
    while (done < count && !ended_) {
        if (pcmPos_ == pcmLen_ && !decodeFrame()) {
            ended_ = true;
            break;
        }
        const size_t n = std::min(count - done, pcmLen_ - pcmPos_);
        std::memcpy(out + done, pcm_ + pcmPos_, n * sizeof(int16_t));
        pcmPos_ += n;
        done += n;
    }
    return done;
}

}  // namespace radio
