#pragma once

#include <cstddef>
#include <cstdint>

namespace radio {

// 128x32 monochrome framebuffer in SSD1306 page layout, so it can be sent
// to the panel as-is.
class Framebuffer {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 32;
    // 1 bit per pixel: byte (x + (y / 8) * width), bit (y % 8).
    static constexpr size_t kBufferSize = kWidth * kHeight / 8;

    void clear();
    void setPixel(int x, int y);
    bool getPixel(int x, int y) const;
    void vLine(int x, int y, int h);

    const uint8_t* data() const { return buf_; }

private:
    uint8_t buf_[kBufferSize] = {};
};

}  // namespace radio
