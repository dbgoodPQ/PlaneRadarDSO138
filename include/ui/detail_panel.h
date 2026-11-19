#pragma once

#include <lvgl.h>

#include "services/adsb_client.h"

/**
 * Selected-plane details in the right 1/3 of the radar screen: callsign,
 * type, aircraft name, airline and destination (looked up online), altitude,
 * speed, track, distance and bearing from the radar center.
 */
namespace ui::detail_panel {

/** Build the panel on `screen`. Call from the radar screen's open(). */
void create(lv_obj_t* screen);

/** The radar screen was deleted: drop the (now dangling) object pointers. */
void forget();

/** Traffic summary shown when no plane is selected. */
struct Summary {
  uint16_t in_range;     // inside the outer ring
  uint16_t beyond;       // rim dots
  uint16_t emergencies;  // anywhere in the feed
  bool has_nearest;
  char nearest[9];       // callsign
  float nearest_km;
};

/** No plane selected: traffic summary + altitude legend. */
void showIdle(const Summary& summary);

/**
 * Show `plane`. `dist_km` is from the radar center; `lost` marks a plane
 * that dropped out of the feed (last known data is kept). Names are shown in
 * full; value rows that no longer fit are hidden (V/S, then SPD, then DST).
 */
void show(const services::adsb::Aircraft& plane, float dist_km, bool lost);

}  // namespace ui::detail_panel
