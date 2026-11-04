#include "platform/esp32/Esp32Tft.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "platform/esp32/Pins.h"
#include "ui/RoundDial.h"

namespace radio {

namespace {

constexpr int kSize = RoundDial::kSize;
constexpr uint8_t kBrightness = 255;
// The lamp comes up slowly after the first frame, like a warming dial light.
constexpr uint32_t kFadeInMs = 1500;
constexpr TickType_t kFadeTick = pdMS_TO_TICKS(20);

class Panel : public lgfx::LGFX_Device {
public:
    Panel() {
        auto bus = bus_.config();
        bus.spi_host = SPI3_HOST;  // the SD card has SPI2
        bus.spi_mode = 0;
        bus.freq_write = pins::kTftSpiHz;
        bus.freq_read = pins::kTftSpiHz / 2;
        bus.dma_channel = SPI_DMA_CH_AUTO;
        bus.pin_sclk = pins::kTftSck;
        bus.pin_mosi = pins::kTftMosi;
        bus.pin_miso = -1;
        bus.pin_dc = pins::kTftDc;
        bus_.config(bus);
        panel_.setBus(&bus_);

        auto cfg = panel_.config();
        cfg.pin_cs = pins::kTftCs;
        cfg.pin_rst = pins::kTftRst;
        cfg.pin_busy = -1;
        cfg.panel_width = kSize;
        cfg.panel_height = kSize;
        cfg.readable = false;
        cfg.invert = pins::kTftInvert;
        cfg.rgb_order = false;
        cfg.bus_shared = false;
        panel_.config(cfg);

        auto light = light_.config();
        light.pin_bl = pins::kTftBacklight;
        light.freq = 12000;
        light.pwm_channel = 7;
        light_.config(light);
        panel_.setLight(&light_);

        setPanel(&panel_);
    }

private:
    lgfx::Panel_GC9A01 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

uint32_t rowHash(const uint16_t* row) {
    uint32_t h = 2166136261u;
    for (int x = 0; x < kSize; ++x) h = (h ^ row[x]) * 16777619u;
    return h;
}

}  // namespace

bool Esp32Tft::begin() {
    // Core 0: the Arduino loop (and with it the audio) runs on core 1. The panel
    // can't report back over a write-only bus, so there's nothing to check here.
    return xTaskCreatePinnedToCore(taskEntry, "tft", 8192, this, 1, &task_, 0) == pdPASS;
}

void Esp32Tft::show(const DisplayState& state) {
    if (!task_) return;
    portENTER_CRITICAL(&lock_);
    pending_ = state;
    hasPending_ = true;
    portEXIT_CRITICAL(&lock_);
    xTaskNotifyGive(task_);
}

Esp32Tft::Stats Esp32Tft::stats() const {
    Stats s;
    s.ready = ready_;
    s.frames = frames_;
    s.drawUs = drawUs_;
    s.pushUs = pushUs_;
    s.rows = rows_;
    return s;
}

void Esp32Tft::taskEntry(void* self) {
    static_cast<Esp32Tft*>(self)->run();
}

void Esp32Tft::run() {
    static Panel panel;
    static RoundDial dial;
    static uint32_t sentHash[kSize];

    panel.init();  // includes the panel's reset delays, which is why it's here
    panel.setBrightness(0);
    panel.fillScreen(0);
    if (!dial.begin()) {
        Serial.println("round display: not enough memory for its frames");
        vTaskDelete(nullptr);
        return;
    }
    ready_ = true;

    bool first = true;
    uint32_t fadeStart = 0;
    bool fading = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, fading ? kFadeTick : portMAX_DELAY);

        if (fading) {
            const uint32_t t = millis() - fadeStart;
            fading = t < kFadeInMs;
            panel.setBrightness(fading ? kBrightness * t / kFadeInMs : kBrightness);
        }

        DisplayState state;
        bool has;
        portENTER_CRITICAL(&lock_);
        has = hasPending_;
        if (has) state = pending_;
        hasPending_ = false;
        portEXIT_CRITICAL(&lock_);
        if (!has) continue;

        const uint32_t drawStart = micros();
        const uint16_t* pixels = dial.draw(state);
        const uint32_t pushStart = micros();

        // Send only the runs of rows that changed. Most updates touch a small
        // part of the screen, and the rest stays perfectly still on the panel.
        uint32_t rows = 0;
        panel.startWrite();
        for (int y = 0; y < kSize;) {
            const uint32_t h = rowHash(pixels + y * kSize);
            if (!first && h == sentHash[y]) {
                ++y;
                continue;
            }
            sentHash[y] = h;
            int end = y + 1;
            for (; end < kSize; ++end) {
                const uint32_t next = rowHash(pixels + end * kSize);
                if (!first && next == sentHash[end]) break;
                sentHash[end] = next;
            }
            panel.pushImage(0, y, kSize, end - y,
                            reinterpret_cast<const lgfx::swap565_t*>(pixels + y * kSize));
            rows += end - y;
            y = end;
        }
        panel.endWrite();

        drawUs_ = pushStart - drawStart;
        pushUs_ = micros() - pushStart;
        rows_ = rows;
        ++frames_;

        if (first) {
            first = false;
            fading = true;
            fadeStart = millis();
        }
    }
}

}  // namespace radio
