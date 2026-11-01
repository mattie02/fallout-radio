#include <Arduino.h>

#include "core/Radio.h"
#include "platform/esp32/Esp32Platform.h"

namespace {

radio::Esp32Audio audio;
radio::Esp32Input input;
radio::Esp32Display display;
radio::Esp32Clock clock_;
radio::Esp32Storage storage;
radio::Platform platform{audio, input, display, clock_, storage};
radio::Radio radioApp(platform);

}  // namespace

void setup() {
    Serial.begin(115200);
    if (!radioApp.begin()) Serial.println("radio: begin failed");
}

void loop() { radioApp.update(); }
