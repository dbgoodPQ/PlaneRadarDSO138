#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (active LOW) ---
// CYD: the BOOT button is GPIO 0 (removed with on-device Settings in phase 5).
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr unsigned long kBootResetHoldMs = 3000UL;
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
constexpr uint8_t kDisplayBrightness = 255;  // fixed, 0-255

// --- Touch: XPT2046 on its own SPI bus (VSPI) ---
constexpr int kTouchPinSclk = 25;
constexpr int kTouchPinMosi = 32;
constexpr int kTouchPinMiso = 39;
constexpr int kTouchPinCs = 33;
constexpr int kTouchPinIrq = 36;
constexpr uint32_t kTouchSpiHz = 1000000;
/** Hold a finger on the screen this long during power-up to redo calibration. */
constexpr unsigned long kTouchRecalibrateHoldMs = 3000UL;
/** Log every touch point to Serial (bring-up aid; turn off once verified). */
constexpr bool kTouchDebugLog = true;

// --- On-board RGB LED (active LOW) — kept off ---
constexpr int kLedPinRed = 4;
constexpr int kLedPinGreen = 16;
constexpr int kLedPinBlue = 17;

/** Legacy sprite radar: a square 240x240 frame centered on the 320x240 panel.
 *  Temporary until the LVGL radar (phase 3) moves it into the left 2/3. */
constexpr int kRadarFrameSize = 240;
constexpr int kRadarOriginX = (kDisplayWidth - kRadarFrameSize) / 2;
constexpr int kRadarOriginY = (kDisplayHeight - kRadarFrameSize) / 2;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Redraw cadence. Slow updates avoid continuous visual motion/flicker; aircraft
 *  positions jump between updates instead of being smoothly dead-reckoned. */
constexpr unsigned long kRadarRedrawIntervalMs = 2000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
