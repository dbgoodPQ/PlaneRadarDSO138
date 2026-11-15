#include "hardware/touch.h"

#include <Arduino.h>
#include <Preferences.h>

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>

#include "config.h"
#include "hardware/display.h"

namespace hardware::touch {

namespace {

constexpr char kNvsNamespace[] = "touch";
constexpr char kNvsKeyCal[] = "cal";
constexpr char kNvsKeyRot[] = "rot";
constexpr size_t kCalCount = 8;  // LovyanGFX calibration: 4 corners x (x, y)
constexpr uint16_t kRawMax = 4095;  // XPT2046 is a 12-bit ADC

constexpr uint16_t kColorBg = TFT_BLACK;
constexpr uint16_t kColorFg = TFT_WHITE;
constexpr uint16_t kColorAccent = TFT_GREEN;

/** Reject garbage (erased/corrupt NVS, or a calibration done without touching). */
bool calibrationLooksValid(const uint16_t* cal) {
  uint16_t lo = kRawMax;
  uint16_t hi = 0;
  for (size_t i = 0; i < kCalCount; ++i) {
    if (cal[i] > kRawMax) {
      return false;
    }
    lo = std::min(lo, cal[i]);
    hi = std::max(hi, cal[i]);
  }
  // The four corners must span a real part of the ADC range.
  return hi - lo > 1000;
}

/**
 * Load the calibration. It is only valid for the rotation it was made in, so a
 * changed kDisplayRotation forces a new calibration.
 */
bool loadCalibration(uint16_t* cal) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    return false;  // namespace does not exist yet (first boot)
  }
  const size_t len = prefs.getBytes(kNvsKeyCal, cal, kCalCount * sizeof(uint16_t));
  const int rot = prefs.getInt(kNvsKeyRot, -1);
  prefs.end();
  return len == kCalCount * sizeof(uint16_t) && rot == config::kDisplayRotation &&
         calibrationLooksValid(cal);
}

void saveCalibration(const uint16_t* cal) {
  Preferences prefs;
  prefs.begin(kNvsNamespace, /*readOnly=*/false);
  prefs.putBytes(kNvsKeyCal, cal, kCalCount * sizeof(uint16_t));
  prefs.putInt(kNvsKeyRot, config::kDisplayRotation);
  prefs.end();
}

void drawCenteredLines(const char* line1, const char* line2, uint16_t color) {
  tft.fillScreen(kColorBg);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(color, kColorBg);
  const int cx = tft.width() / 2;
  const int cy = tft.height() / 2;
  tft.drawString(line1, cx, cy - 14);
  if (line2 != nullptr) {
    tft.drawString(line2, cx, cy + 14);
  }
}

/**
 * True when a finger stays on the screen for kTouchRecalibrateHoldMs right
 * after power-up. Uses the uncalibrated reading: only presence matters here.
 */
bool recalibrateRequestedAtBoot() {
  int32_t x;
  int32_t y;
  if (tft.getTouchRaw(&x, &y) == 0) {
    return false;
  }
  drawCenteredLines("Keep holding to", "recalibrate touch", kColorAccent);
  const unsigned long start = millis();
  unsigned long last_seen = start;
  while (millis() - start < config::kTouchRecalibrateHoldMs) {
    if (tft.getTouchRaw(&x, &y) != 0) {
      last_seen = millis();
    } else if (millis() - last_seen > 150) {
      // Released (150 ms grace absorbs resistive-panel dropouts).
      tft.fillScreen(kColorBg);
      return false;
    }
    delay(10);
  }
  // Wait for release so the hold is not taken as the first corner tap.
  drawCenteredLines("Release the screen", nullptr, kColorAccent);
  while (tft.getTouchRaw(&x, &y) != 0) {
    delay(10);
  }
  delay(200);
  return true;
}

}  // namespace

void calibrate() {
  drawCenteredLines("Touch calibration", "Tap each arrow tip", kColorFg);
  delay(1500);
  tft.fillScreen(kColorBg);

  uint16_t cal[kCalCount];
  for (;;) {
    // Draws a marker in each corner in turn and blocks until it is tapped.
    tft.calibrateTouch(cal, kColorAccent, kColorBg, 15);
    if (calibrationLooksValid(cal)) {
      break;
    }
    drawCenteredLines("Calibration failed", "Try again", TFT_RED);
    delay(1500);
    tft.fillScreen(kColorBg);
  }

  tft.setTouchCalibrate(cal);
  saveCalibration(cal);
  Serial.print("Touch calibration saved:");
  for (uint16_t v : cal) {
    Serial.printf(" %u", v);
  }
  Serial.println();

  drawCenteredLines("Calibration saved", nullptr, kColorAccent);
  delay(800);
  tft.fillScreen(kColorBg);
}

void init() {
  if (recalibrateRequestedAtBoot()) {
    calibrate();
    return;
  }
  uint16_t cal[kCalCount];
  if (loadCalibration(cal)) {
    tft.setTouchCalibrate(cal);
    return;
  }
  Serial.println("No touch calibration stored");
  calibrate();
}

void clearCalibration() {
  Preferences prefs;
  prefs.begin(kNvsNamespace, /*readOnly=*/false);
  prefs.clear();
  prefs.end();
}

bool read(int16_t* x, int16_t* y) {
  lgfx::touch_point_t tp;
  if (tft.getTouch(&tp, 1) == 0) {
    return false;
  }
  // Calibration can map slightly outside the panel at the edges; clamp.
  *x = static_cast<int16_t>(constrain(tp.x, 0, tft.width() - 1));
  *y = static_cast<int16_t>(constrain(tp.y, 0, tft.height() - 1));
  return true;
}

}  // namespace hardware::touch
