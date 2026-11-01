#pragma once

// GPIO assignments. Keep in sync with the pin map in CLAUDE.md.

#include <cstdint>

namespace radio::pins {

constexpr int kI2cSda = 8;  // OLED + RTC
constexpr int kI2cScl = 9;

constexpr int kEncoderA = 11;   // CLK
constexpr int kEncoderB = 12;   // DT
constexpr int kEncoderSw = 13;  // active low, INPUT_PULLUP

constexpr int kSdCs = 15;
constexpr int kSdSck = 16;
constexpr int kSdMosi = 17;
constexpr int kSdMiso = 18;

constexpr int kI2sBclk = 5;
constexpr int kI2sLrc = 6;
constexpr int kI2sDout = 7;

constexpr int kOledAddress = 0x3C;
constexpr int kRtcAddress = 0x68;

// KY-040: one detent = one full quadrature cycle = 4 counts. If the dial
// moves two steps per click, try 2; if it turns the wrong way, flip kReverse.
constexpr int kEncoderCountsPerStep = 4;
constexpr bool kEncoderReverse = false;

constexpr uint32_t kI2cHz = 400000;
// 20 MHz failed mid-stream ("Card Failed! cmd: 0x0d") through the 5 V SD
// module's level shifter and jumper wires. Playback needs only 16 KB/s;
// 4 MHz gives ~500 KB/s.
constexpr uint32_t kSdSpiHz = 4000000;

}  // namespace radio::pins
