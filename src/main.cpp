/**
 * Plane Radar — CYD (ESP32-2432S028R).
 *
 * Boot: touch calibration (first boot) -> Wi-Fi screen -> radar screen.
 * Everything on the panel is LVGL; the loop task is the only one calling it.
 */

#include <Arduino.h>

#include "config.h"
#include "hardware/boot_button.h"
#include "hardware/display.h"
#include "hardware/touch.h"
#include "services/adsb_client.h"
#include "services/ip_location.h"
#include "services/radar_location.h"
#include "services/route_lookup.h"
#include "services/wifi_conn.h"
#include "ui/lvgl_port.h"
#include "ui/radar_range.h"
#include "ui/radar_screen.h"
#include "ui/wifi_screen.h"

namespace {

/** Retry the IP location lookup this often until one succeeds. */
constexpr unsigned long kIpLookupRetryMs = 30000;

void openWifiSettings() {
  // The finger that held the radar is still down: don't let it tap the list.
  ui::lvgl_port::ignoreTouchUntilRelease();
  ui::wifi_screen::openScan(/*cancelable=*/true);  // replaces the radar screen
}

// Network work runs on its own task (core 0) so HTTP never blocks the UI:
// the one-time IP location lookup, ADS-B polling, and route lookups for the
// selected plane. One connection at a time keeps peak RAM down. A route
// request wakes the task early (task notification) so it is answered quickly.
void networkTask(void*) {
  bool ip_tried = false;
  unsigned long ip_last_try_ms = 0;
  unsigned long last_fetch_ms = 0;
  bool fetched_once = false;
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
      services::route::process();
      if (!fetched_once ||
          millis() - last_fetch_ms >= config::kAdsbFetchIntervalMs) {
        fetched_once = true;
        last_fetch_ms = millis();
        services::adsb::fetchUpdate(services::location::lat(),
                                    services::location::lon(),
                                    ui::radar::fetchRadiusKm());
      }
    }
    // Sleep until the next ADS-B fetch is due (or a route request arrives).
    // Before Wi-Fi is up, just check back every second.
    const unsigned long since = millis() - last_fetch_ms;
    unsigned long wait_ms = 1000;
    if (fetched_once) {
      wait_ms = since >= config::kAdsbFetchIntervalMs
                    ? 1
                    : config::kAdsbFetchIntervalMs - since;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
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

  // Core 0, away from the UI loop on core 1.
  TaskHandle_t network_task = nullptr;
  xTaskCreatePinnedToCore(networkTask, "net", 16384, nullptr, 1, &network_task, 0);
  services::route::init(network_task);
}

void loop() {
  services::wifi::loop();
  const auto button = hardware::boot_button::poll();

  if (ui::wifi_screen::isOpen()) {
    ui::wifi_screen::update();
    if (!ui::wifi_screen::isOpen()) {
      ui::radar_screen::open();
    }
  } else if (button == hardware::boot_button::Event::Hold ||
             ui::radar_screen::takeSettingsRequest()) {
    openWifiSettings();
  } else if (button == hardware::boot_button::Event::Tap) {
    ui::radar_screen::nextRange();
  }

  ui::lvgl_port::handle();
  delay(5);
}
