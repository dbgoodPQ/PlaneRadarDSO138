/**
 * Plane Radar — CYD (ESP32-2432S028R).
 *
 * Boot: touch calibration (first boot) -> Wi-Fi screen -> radar screen.
 * From the radar, the gear button (top left) or holding BOOT opens Settings; Settings
 * can open the Wi-Fi screen, which then returns to Settings.
 * Everything on the panel is LVGL; the loop task is the only one calling it.
 * Network work (HTTP) runs on services::network_task.
 */

#include <Arduino.h>

#include "config.h"
#include "hardware/boot_button.h"
#include "hardware/display.h"
#include "hardware/touch.h"
#include "services/adsb_client.h"
#include "services/config_store.h"
#include "services/geocode.h"
#include "services/network_task.h"
#include "services/radar_location.h"
#include "services/route_lookup.h"
#include "services/settings.h"
#include "services/wifi_conn.h"
#include "ui/lvgl_port.h"
#include "ui/radar_screen.h"
#include "ui/settings_screen.h"
#include "ui/wifi_screen.h"

namespace {

/** The Wi-Fi screen was opened from Settings: go back there when it closes. */
bool s_wifi_from_settings = false;

void openSettings() {
  // A BOOT hold or the gear tap may still be down: don't let it hit a row.
  ui::lvgl_port::ignoreTouchUntilRelease();
  ui::settings_screen::open();  // replaces the radar screen
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

  // Settings: NVS first, then the SD card file (if any) overrides it.
  services::settings::init();
  services::location::init();
  services::config_store::init();
  tft.setBrightness(static_cast<uint8_t>(
      services::settings::brightnessPct() * 255 / 100));
  services::adsb::init();
  services::route::init();
  services::geocode::init();
  services::wifi::init();

  if (services::wifi::hasSaved()) {
    ui::wifi_screen::openConnecting(/*cancelable=*/false);
  } else {
    ui::wifi_screen::openScan(/*cancelable=*/false);
  }

  services::network_task::start();
}

void loop() {
  services::wifi::loop();
  const auto button = hardware::boot_button::poll();

  if (ui::wifi_screen::isOpen()) {
    ui::wifi_screen::update();
    if (!ui::wifi_screen::isOpen()) {
      if (s_wifi_from_settings) {
        s_wifi_from_settings = false;
        ui::settings_screen::open();
      } else {
        ui::radar_screen::open();
      }
    }
  } else if (ui::settings_screen::isOpen()) {
    ui::settings_screen::update();
    switch (ui::settings_screen::takeExit()) {
      case ui::settings_screen::Exit::Radar:
        ui::radar_screen::open();
        break;
      case ui::settings_screen::Exit::Wifi:
        s_wifi_from_settings = true;
        ui::wifi_screen::openScan(/*cancelable=*/true);
        break;
      case ui::settings_screen::Exit::None:
      default:
        break;
    }
  } else if (button == hardware::boot_button::Event::Hold ||
             ui::radar_screen::takeSettingsRequest()) {
    openSettings();
  } else if (button == hardware::boot_button::Event::Tap) {
    ui::radar_screen::nextRange();
  }

  ui::lvgl_port::handle();
  delay(5);
}
