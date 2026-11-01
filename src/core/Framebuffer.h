#pragma once

#include <cstdint>

#include "platform/Platform.h"

namespace radio {

// 128x32 monochrome framebuffer in SSD1306 page layout, so it can be sent
// to the panel as-is.
class Framebuffer {
public:
    static constexpr int kWidth = kDisplayWidth;
    static constexpr int kHeight = kDisplayHeight;
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

    uint8_t buf_[kDisplayBufferSize] = {};
};

}  // namespace radio
