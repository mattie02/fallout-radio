#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Mp3Decoder.h"
#include "platform/Platform.h"

namespace radio {

// The start of every power-on: the optional /system/boot.mp3 plays while the
// radio itself (station and static) stays silent, then the radio fades up
// through the end of it, like tubes warming. With no file, it's just the fade.
//
// The station keeps playing underneath, muted, so it's still "live" when it
// comes up. Timing counts samples rendered, not the clock, so a clock jump can't
// disturb it.
class WarmUp {
public:
    static constexpr uint64_t kFadeSamples = kSampleRate * 3 / 2;  // 1.5 s
    // However long a boot file is, the radio starts coming up by this point.
    static constexpr uint64_t kMaxSilentSamples = kSampleRate * 10ull;

    // Opens the boot file, if there is one.
    void begin(Storage& storage);
    // Closes the boot file (it must be, before storage recovery) and moves on to
    // the fade.
    void stop();
    // True while the boot file is playing.
    bool playing() const { return decoder_.isOpen(); }

    // Fills `count` samples of the boot sound, silence once it's over.
    void read(int16_t* out, size_t count);
    // How far up the radio should be now, 0..1.
    float radioLevel() const;

private:
    Mp3Decoder decoder_;
    uint64_t rendered_ = 0;
    uint64_t fadeStart_ = 0;
};

}  // namespace radio
