#pragma once

// GPIO assignments. Keep in sync with the pin map in CLAUDE.md.

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

}  // namespace radio::pins
