#pragma once

#include "platform/Platform.h"
#include "ui/Framebuffer.h"

namespace radio {

// The dial on the 128x32 monochrome OLED.
void drawMonoDial(const DisplayState& state, Framebuffer& fb);

}  // namespace radio
