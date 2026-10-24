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
#if defined(PLANE_RADAR_DSO138)
// ESP32-WROOM-32 dev board: BOOT button is GPIO 0.
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
#else
// ESP32-C3 Super Mini: BOOT button is GPIO 9.
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
#endif
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

#if defined(PLANE_RADAR_DSO138)
// --- Display: JYE Tech DSO138 LCD module, ILI9341 320x240, 8-bit parallel ---
// Wiring matches the DSO138 demo project (module header pin in comment).
constexpr int kDisplayPinCs = 27;   // P7  nCS
constexpr int kDisplayPinDc = 14;   // P8  RS
constexpr int kDisplayPinWr = 25;   // P9  nWR
constexpr int kDisplayPinRd = 13;   // P10 nRD
constexpr int kDisplayPinRst = 26;  // P31 nRESET
constexpr int kDisplayPinD0 = 16;   // P23 DB0
constexpr int kDisplayPinD1 = 4;    // P24 DB1
constexpr int kDisplayPinD2 = 23;   // P25 DB2
constexpr int kDisplayPinD3 = 22;   // P26 DB3
constexpr int kDisplayPinD4 = 21;   // P27 DB4
constexpr int kDisplayPinD5 = 19;   // P28 DB5
constexpr int kDisplayPinD6 = 18;   // P29 DB6
constexpr int kDisplayPinD7 = 17;   // P30 DB7

constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
/** LovyanGFX rotation for landscape 320x240. Try 3 if the image is upside down. */
constexpr int kDisplayRotation = 1;

constexpr uint32_t kDisplayBusWriteHz = 20000000;
// If colors look swapped or inverted on your module, flip these.
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = false;
#else
// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;
constexpr int kDisplayRotation = 0;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;
#endif

/** The radar is drawn into a square 240x240 frame; this is where it lands on the
 *  panel. On the 240x240 round display it fills the screen; on 320x240 it is
 *  centered, leaving a 40 px band on each side. */
constexpr int kRadarFrameSize = 240;
constexpr int kRadarOriginX = (kDisplayWidth - kRadarFrameSize) / 2;
constexpr int kRadarOriginY = (kDisplayHeight - kRadarFrameSize) / 2;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Redraw cadence; aircraft are dead-reckoned along their track/speed so motion
 *  is smooth. ~4 Hz = 250 ms (panel scanout caps at ~24 Hz; the full-frame
 *  recompose+present is the practical limit ~10-15 Hz). */
constexpr unsigned long kRadarRedrawIntervalMs = 250;
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
