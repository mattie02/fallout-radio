#include "ui/Framebuffer.h"

#include <cstring>

namespace radio {

void Framebuffer::clear() { std::memset(buf_, 0, sizeof(buf_)); }

void Framebuffer::setPixel(int x, int y) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    buf_[x + (y / 8) * kWidth] |= static_cast<uint8_t>(1u << (y % 8));
}

bool Framebuffer::getPixel(int x, int y) const {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return false;
    return buf_[x + (y / 8) * kWidth] & (1u << (y % 8));
}

void Framebuffer::vLine(int x, int y, int h) {
    for (int yy = y; yy < y + h; ++yy) setPixel(x, yy);
}

}  // namespace radio
