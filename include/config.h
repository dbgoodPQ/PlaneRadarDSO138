#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi ---
/** Give up on a connection attempt after this long. */
constexpr unsigned long kWifiConnectTimeoutMs = 15000;
/** After losing a saved network, retry it at this interval (background). */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;
/** Most networks listed on the Wi-Fi screen (strongest first). */
constexpr int kWifiMaxListedNetworks = 15;

// --- Touch ---
/** Touch dropouts shorter than this don't count as a release (resistive panel). */
constexpr unsigned long kTouchDropoutMs = 150UL;

// --- BOOT button (active LOW) ---
// CYD: GPIO 0. Tap = next range, hold = open Settings.
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr unsigned long kBootHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: ESP32-2432S028R (CYD), ILI9341 320x240 on HSPI ---
constexpr int kDisplayPinSclk = 14;
constexpr int kDisplayPinMosi = 13;
constexpr int kDisplayPinMiso = 12;
constexpr int kDisplayPinCs = 15;
constexpr int kDisplayPinDc = 2;
constexpr int kDisplayPinRst = -1;        // tied to EN on the CYD
constexpr int kDisplayPinBacklight = 21;  // PWM, active HIGH

constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
/** LovyanGFX rotation for landscape (USB connector on the right). Use 3 if the
 *  image is upside down; touch follows the rotation automatically. */
constexpr int kDisplayRotation = 1;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
constexpr uint32_t kDisplaySpiReadHz = 16000000;
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = false;
/** Backlight at boot, before the saved brightness setting is applied. */
constexpr uint8_t kDisplayBrightness = 255;

// --- Touch: XPT2046 over software SPI (bit-banged) ---
constexpr int kTouchPinSclk = 25;
constexpr int kTouchPinMosi = 32;
constexpr int kTouchPinMiso = 39;
constexpr int kTouchPinCs = 33;
constexpr int kTouchPinIrq = 36;
/** Hold a finger on the screen this long during power-up to redo calibration. */
constexpr unsigned long kTouchRecalibrateHoldMs = 3000UL;

// --- microSD slot: VSPI (touch is bit-banged, so VSPI is free) ---
constexpr int kSdPinSclk = 18;
constexpr int kSdPinMiso = 19;
constexpr int kSdPinMosi = 23;
constexpr int kSdPinCs = 5;
constexpr uint32_t kSdSpiHz = 20000000;

// --- On-board RGB LED (active LOW) — kept off ---
constexpr int kLedPinRed = 4;
constexpr int kLedPinGreen = 16;
constexpr int kLedPinBlue = 17;

// --- Radar center defaults (until a location is saved) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Redraw cadence. Slow updates avoid continuous visual motion/flicker; aircraft
 *  positions jump between updates instead of being smoothly dead-reckoned. */
constexpr unsigned long kRadarRedrawIntervalMs = 2000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;

}  // namespace config
