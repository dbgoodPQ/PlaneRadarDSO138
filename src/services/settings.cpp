#include "services/settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "services/config_store.h"
#include "ui/radar_range.h"

namespace services::settings {

namespace {

// Same namespace and keys as before phase 6, so saved choices carry over.
constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kKeyRange[] = "rangeIdx";
constexpr char kKeyMiles[] = "useMiles";
constexpr char kKeyRunways[] = "showRwys";
constexpr char kKeyGround[] = "showGnd";
constexpr char kKeyBrightness[] = "bright";
constexpr char kKeyFlip[] = "flip";

constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr uint8_t kDefaultBrightnessPct = 100;

bool s_use_miles = false;
bool s_show_runways = true;
bool s_show_ground = false;
uint8_t s_brightness_pct = kDefaultBrightnessPct;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_flip_screen = false;

template <typename Fn>
void writePrefs(Fn fn) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  fn(prefs);
  prefs.end();
}

uint8_t clampBrightness(int pct) {
  if (pct < kBrightnessMinPct) {
    return kBrightnessMinPct;
  }
  if (pct > kBrightnessMaxPct) {
    return kBrightnessMaxPct;
  }
  return static_cast<uint8_t>(pct);
}

void changed(bool from_sd) {
  if (!from_sd) {
    config_store::save();
  }
}

}  // namespace

void init() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;  // first boot: defaults
  }
  const uint8_t range = prefs.getUChar(kKeyRange, kDefaultRangeIndex);
  s_range_index =
      range < ui::radar::kRangePresetCount ? range : kDefaultRangeIndex;
  s_use_miles = prefs.getBool(kKeyMiles, false);
  s_show_runways = prefs.getBool(kKeyRunways, true);
  s_show_ground = prefs.getBool(kKeyGround, false);
  s_brightness_pct =
      clampBrightness(prefs.getUChar(kKeyBrightness, kDefaultBrightnessPct));
  s_flip_screen = prefs.getBool(kKeyFlip, false);
  prefs.end();
}

bool useMiles() { return s_use_miles; }
bool showRunways() { return s_show_runways; }
bool showGroundAircraft() { return s_show_ground; }
uint8_t brightnessPct() { return s_brightness_pct; }
uint8_t rangeIndex() { return s_range_index; }
bool flipScreen() { return s_flip_screen; }

void setUseMiles(bool v, bool from_sd) {
  s_use_miles = v;
  writePrefs([v](Preferences& p) { p.putBool(kKeyMiles, v); });
  changed(from_sd);
}

void setShowRunways(bool v, bool from_sd) {
  s_show_runways = v;
  writePrefs([v](Preferences& p) { p.putBool(kKeyRunways, v); });
  changed(from_sd);
}

void setShowGroundAircraft(bool v, bool from_sd) {
  s_show_ground = v;
  writePrefs([v](Preferences& p) { p.putBool(kKeyGround, v); });
  changed(from_sd);
}

void setBrightnessPct(uint8_t pct, bool from_sd) {
  s_brightness_pct = clampBrightness(pct);
  const uint8_t v = s_brightness_pct;
  writePrefs([v](Preferences& p) { p.putUChar(kKeyBrightness, v); });
  changed(from_sd);
}

void setRangeIndex(uint8_t index, bool from_sd) {
  if (index >= ui::radar::kRangePresetCount) {
    return;
  }
  s_range_index = index;
  writePrefs([index](Preferences& p) { p.putUChar(kKeyRange, index); });
  changed(from_sd);
}

void setFlipScreen(bool v, bool from_sd) {
  s_flip_screen = v;
  writePrefs([v](Preferences& p) { p.putBool(kKeyFlip, v); });
  changed(from_sd);
}

}  // namespace services::settings
