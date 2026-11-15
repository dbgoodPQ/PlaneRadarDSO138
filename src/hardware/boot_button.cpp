#include "hardware/boot_button.h"

#include <Arduino.h>

#include "config.h"

namespace hardware::boot_button {

namespace {

bool s_down = false;
bool s_hold_fired = false;
unsigned long s_down_since_ms = 0;

}  // namespace

void init() { pinMode(config::kBootPin, INPUT_PULLUP); }

Event poll() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();

  if (down && !s_down) {
    s_down = true;
    s_hold_fired = false;
    s_down_since_ms = now;
    return Event::None;
  }
  if (down) {
    if (!s_hold_fired && now - s_down_since_ms >= config::kBootHoldMs) {
      s_hold_fired = true;
      return Event::Hold;
    }
    return Event::None;
  }
  if (s_down) {  // released
    s_down = false;
    const unsigned long held = now - s_down_since_ms;
    if (!s_hold_fired && held >= config::kBootTapMinMs) {
      return Event::Tap;
    }
  }
  return Event::None;
}

}  // namespace hardware::boot_button
