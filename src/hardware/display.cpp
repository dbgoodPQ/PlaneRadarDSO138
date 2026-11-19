#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"
#include "services/settings.h"

LGFX tft;

namespace {

int s_rotation = config::kDisplayRotation;

/** The CYD's RGB LED is active LOW and glows faintly when its pins float. */
void ledOff() {
  for (int pin : {config::kLedPinRed, config::kLedPinGreen, config::kLedPinBlue}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

}  // namespace

void displayInit() {
  ledOff();
  tft.init();
  s_rotation = services::settings::flipScreen() ? config::kDisplayRotationFlipped
                                                : config::kDisplayRotation;
  tft.setRotation(s_rotation);
  tft.setBrightness(config::kDisplayBrightness);
}

int displayRotation() { return s_rotation; }

bool displayFlipped() { return s_rotation == config::kDisplayRotationFlipped; }
