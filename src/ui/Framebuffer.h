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
    static constexpr int kCharWidth = 6;  // 5px glyph + 1px gap, at scale 1
    static constexpr int kCharHeight = 8;

    void clear();
    void invert();
    void setPixel(int x, int y, bool on = true);
    bool getPixel(int x, int y) const;
    void hLine(int x, int y, int w);
    void vLine(int x, int y, int h);
    void fillRect(int x, int y, int w, int h, bool on = true);
    // Draws ASCII text with the built-in 5x7 font. Lowercase is drawn as
    // uppercase. Returns the x just past the last character.
    int drawText(int x, int y, const char* text, int scale = 1);
    static int textWidth(const char* text, int scale = 1);

    const uint8_t* data() const { return buf_; }

private:
    void drawChar(int x, int y, char c, int scale);

    uint8_t buf_[kBufferSize] = {};
};

}  // namespace radio
