#pragma once

#include <vector>

namespace radio {

// Dial position -> nearest station and its signal strength.
//
// Signal is 1.0 within lockMhz of a station's frequency, then falls along a
// smoothstep curve to 0.0 at lockMhz + fadeMhz. Where two stations overlap,
// the stronger one wins.
class Tuner {
public:
    struct Result {
        int station = -1;  // index into the frequencies, or -1 for none
        float signal = 0;  // 0..1
    };

    void setSignal(float lockMhz, float fadeMhz);
    void setStations(std::vector<float> frequenciesMhz);

    Result tune(float dialMhz) const;

    static float signalAt(float distanceMhz, float lockMhz, float fadeMhz);

private:
    std::vector<float> freqs_;
    float lock_ = 0.1f;
    float fade_ = 0.4f;
};

}  // namespace radio
