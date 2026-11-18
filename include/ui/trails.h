#pragma once

#include <cstddef>
#include <cstdint>

#include "services/adsb_client.h"

/**
 * Recent positions of each aircraft, for the radar's trails. Only real fixes
 * from the feed are stored (never dead-reckoned ones), one per fetch, as
 * offsets from the radar centre. Loop task only.
 */
namespace ui::trails {

/** Fixes kept per aircraft: 12 fetches of 5 s ≈ 1 minute. */
constexpr int kMaxPoints = 12;
/** Aircraft with a trail at once (only those inside the radar get one). */
constexpr int kMaxTrails = 32;

/** A fix in 10 m units east (e) and north (n) of the radar centre. */
struct Point {
  int16_t e;
  int16_t n;
};

struct Trail {
  char hex[sizeof(services::adsb::Aircraft::hex)];
  uint8_t count;    // valid points, oldest first
  uint8_t version;  // bumped whenever a point is added
  uint32_t last_seen_ms;
  Point points[kMaxPoints];
};

/** Forget every trail (the radar centre moved). */
void clear();

/**
 * Start recording a fetch. Returns true only when `base_ms` (the fetch time)
 * is new; then call add() for each aircraft inside the radar. Trails of
 * aircraft unseen for 60 s are dropped.
 */
bool beginRecord(unsigned long base_ms);

/** Add the aircraft's fix to its trail (no-op when all slots are in use). */
void add(const services::adsb::Aircraft& plane);

/** Trail of the aircraft with ICAO `hex`, or nullptr. */
const Trail* find(const char* hex);

}  // namespace ui::trails
