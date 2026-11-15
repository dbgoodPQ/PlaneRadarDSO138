/**
 * Plane Radar — CYD (ESP32-2432S028R).
 *
 * Boot: touch calibration (first boot) -> Wi-Fi screen (LVGL) -> radar.
 * Until the LVGL radar exists (phase 3), the legacy sprite radar draws straight
 * to the panel and LVGL only runs while a settings screen is open.
 */

#include <Arduino.h>

#include "config.h"
#include "hardware/boot_button.h"
#include "hardware/display.h"
#include "hardware/touch.h"
#include "services/adsb_client.h"
#include "services/ip_location.h"
#include "services/radar_location.h"
#include "services/wifi_conn.h"
#include "ui/lvgl_port.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/wifi_screen.h"

namespace {

/** Retry the IP location lookup this often until one succeeds. */
constexpr unsigned long kIpLookupRetryMs = 30000;
/** Touch dropouts shorter than this don't break a long press (resistive panel). */
constexpr unsigned long kTouchDropoutMs = 150;

enum class Mode { Settings, Radar };

Mode g_mode = Mode::Settings;
unsigned long g_last_redraw_ms = 0;

bool g_touch_down = false;
unsigned long g_touch_down_since_ms = 0;
unsigned long g_touch_last_seen_ms = 0;

void showRadar() {
  g_mode = Mode::Radar;
  g_touch_down = false;
  ui::radarDisplayDraw();
  g_last_redraw_ms = millis();
}

void openWifiSettings() {
  ui::radarDisplayRelease();  // give the frame buffer's RAM to LVGL
  ui::lvgl_port::ignoreTouchUntilRelease();
  ui::wifi_screen::openScan(/*cancelable=*/true);
  ui::lvgl_port::resume();
  g_mode = Mode::Settings;
}

/** True once a finger has stayed on the radar for kSettingsLongPressMs. */
bool radarLongPress() {
  int16_t x;
  int16_t y;
  const unsigned long now = millis();
  if (hardware::touch::read(&x, &y)) {
    if (!g_touch_down) {
      g_touch_down = true;
      g_touch_down_since_ms = now;
    }
    g_touch_last_seen_ms = now;
    if (now - g_touch_down_since_ms >= config::kSettingsLongPressMs) {
      g_touch_down = false;
      return true;
    }
  } else if (g_touch_down && now - g_touch_last_seen_ms > kTouchDropoutMs) {
    g_touch_down = false;
  }
  return false;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char label[12];
  ui::radar::formatCurrentRing3Label(label, sizeof(label));
  Serial.printf("Range: %s\n", label);
  ui::radarDisplayDraw();
}

// Network work runs on its own task (core 0) so HTTP never blocks the UI:
// the one-time IP location lookup, then ADS-B polling. One connection at a
// time keeps peak RAM down.
void adsbFetchTask(void*) {
  bool ip_tried = false;
  unsigned long ip_last_try_ms = 0;
  for (;;) {
    if (services::wifi::connected()) {
      if (!services::location::isSet() &&
          (!ip_tried || millis() - ip_last_try_ms >= kIpLookupRetryMs)) {
        ip_tried = true;
        ip_last_try_ms = millis();
        double lat;
        double lon;
        if (services::ip_location::lookup(&lat, &lon)) {
          services::location::save(lat, lon);
        }
      }
      services::adsb::fetchUpdate(services::location::lat(),
                                  services::location::lon(),
                                  ui::radar::fetchRadiusKm());
    }
    vTaskDelay(pdMS_TO_TICKS(config::kAdsbFetchIntervalMs));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Plane Radar (CYD)");

  hardware::boot_button::init();
  displayInit();
  hardware::touch::init();
  ui::lvgl_port::init();

  services::location::init();
  ui::radar::rangeInit();
  services::adsb::init();
  services::wifi::init();

  if (services::wifi::hasSaved()) {
    ui::wifi_screen::openConnecting(/*cancelable=*/false);
  } else {
    ui::wifi_screen::openScan(/*cancelable=*/false);
  }
  g_mode = Mode::Settings;

  // Core 0, away from the UI loop on core 1.
  xTaskCreatePinnedToCore(adsbFetchTask, "adsb", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  services::wifi::loop();
  const auto button = hardware::boot_button::poll();

  if (g_mode == Mode::Settings) {
    ui::wifi_screen::update();
    if (!ui::wifi_screen::isOpen()) {
      showRadar();
    } else {
      ui::lvgl_port::handle();
    }
  } else {
    if (button == hardware::boot_button::Event::Hold || radarLongPress()) {
      openWifiSettings();
    } else if (button == hardware::boot_button::Event::Tap) {
      onRangeTap();
      g_last_redraw_ms = millis();
    } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
      g_last_redraw_ms = millis();
      ui::radarDisplayRefreshAircraft();
    }
  }

  delay(5);
}
