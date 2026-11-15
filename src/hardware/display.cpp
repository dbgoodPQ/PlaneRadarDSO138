#include "hardware/display.h"

#include <Arduino.h>

#include "hardware/display_font.h"

LGFX tft;

namespace {

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
  tft.setRotation(config::kDisplayRotation);
  tft.setBrightness(config::kDisplayBrightness);
  tft.setTextWrap(false);
  displayFontInit();
}
