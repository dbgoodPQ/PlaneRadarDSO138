#include "ui/radar_range.h"

#include <cmath>
#include <cstdio>

#include "services/settings.h"
#include "ui/radar_theme.h"

namespace ui::radar {

namespace {

constexpr float kKmPerMile = 1.609344f;

}  // namespace

void rangeNext() {
  services::settings::setRangeIndex(static_cast<uint8_t>(
      (services::settings::rangeIndex() + 1) % kRangePresetCount));
}

const RangePreset& rangeCurrent() {
  return kRangePresets[services::settings::rangeIndex()];
}

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px = static_cast<float>(kBeyondRingRimRadiusPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return services::settings::useMiles(); }

bool showRunways() { return services::settings::showRunways(); }

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, useMiles());
}

}  // namespace ui::radar
