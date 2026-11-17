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

/** No plane selected: show the "tap a plane" hint. */
void showHint();

/**
 * Show `plane`. `dist_km` / `bearing_deg` are from the radar center; `lost`
 * marks a plane that dropped out of the feed (last known data is kept).
 */
void show(const services::adsb::Aircraft& plane, float dist_km,
          float bearing_deg, bool lost);

}  // namespace ui::detail_panel
