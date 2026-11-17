#include "services/radar_location.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

#include "config.h"
#include "services/config_store.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
constexpr char kKeyName[] = "name";
constexpr char kKeyApprox[] = "approx";

// Doubles are not written atomically on the ESP32: guard them, since the
// ADS-B task reads them while the loop task (or the IP lookup) may write.
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
bool s_is_set = false;
char s_name[kNameMaxLen + 1] = "";
bool s_approximate = false;

void apply(double lat, double lon, bool is_set, const char* name,
           bool approximate) {
  portENTER_CRITICAL(&s_mux);
  s_lat = lat;
  s_lon = lon;
  s_is_set = is_set;
  strncpy(s_name, name != nullptr ? name : "", kNameMaxLen);
  s_name[kNameMaxLen] = '\0';
  s_approximate = approximate;
  portEXIT_CRITICAL(&s_mux);
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void persist(double lat, double lon, const char* name, bool approximate) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.putString(kKeyName, name != nullptr ? name : "");
  prefs.putBool(kKeyApprox, approximate);
  prefs.end();
  apply(lat, lon, true, name, approximate);
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat, lon)) {
      char name[kNameMaxLen + 1] = "";
      if (prefs.isKey(kKeyName)) {  // saved before place names existed
        prefs.getString(kKeyName, name, sizeof(name));
      }
      apply(lat, lon, true, name, prefs.getBool(kKeyApprox, false));
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

void name(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  portENTER_CRITICAL(&s_mux);
  strncpy(out, s_name, out_len - 1);
  portEXIT_CRITICAL(&s_mux);
  out[out_len - 1] = '\0';
}

bool isApproximate() {
  portENTER_CRITICAL(&s_mux);
  const bool v = s_approximate;
  portEXIT_CRITICAL(&s_mux);
  return v;
}

bool save(double lat, double lon, const char* name, bool approximate,
          bool from_sd) {
  if (!validLatLon(lat, lon)) {
    return false;
  }
  persist(lat, lon, name, approximate);
  Serial.printf("Radar location saved: %.6f, %.6f  %s%s\n", lat, lon,
                name != nullptr ? name : "", approximate ? " (approximate)" : "");
  if (!from_sd) {
    config_store::save();
  }
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.remove(kKeyName);
  prefs.remove(kKeyApprox);
  prefs.end();
  apply(config::kDefaultRadarLat, config::kDefaultRadarLon, false, "", false);
}

}  // namespace services::location
