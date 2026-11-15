#include "services/radar_location.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";

// Doubles are not written atomically on the ESP32: guard them, since the
// ADS-B task reads them while the loop task (or the IP lookup) may write.
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
bool s_is_set = false;

void apply(double lat, double lon, bool is_set) {
  portENTER_CRITICAL(&s_mux);
  s_lat = lat;
  s_lon = lon;
  s_is_set = is_set;
  portEXIT_CRITICAL(&s_mux);
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void persist(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.end();
  apply(lat, lon, true);
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat, lon)) {
      apply(lat, lon, true);
    }
  }
  prefs.end();
}

double lat() {
  portENTER_CRITICAL(&s_mux);
  const double v = s_lat;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

double lon() {
  portENTER_CRITICAL(&s_mux);
  const double v = s_lon;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

bool isSet() {
  portENTER_CRITICAL(&s_mux);
  const bool v = s_is_set;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

bool save(double lat, double lon) {
  if (!validLatLon(lat, lon)) {
    return false;
  }
  persist(lat, lon);
  Serial.printf("Radar location saved: %.6f, %.6f\n", lat, lon);
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  apply(config::kDefaultRadarLat, config::kDefaultRadarLon, false);
}

}  // namespace services::location
