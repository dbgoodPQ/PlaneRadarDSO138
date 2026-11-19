#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace services::adsb {

/** Emergency state, from the squawk (7500/7600/7700) or the feed's field. */
enum class Emergency : uint8_t { None, General, Radio, Hijack };

/** Short tag label: "EMER", "RADIO", "HIJACK" ("" for None). */
inline const char* emergencyShort(Emergency e) {
  switch (e) {
    case Emergency::General:
      return "EMER";
    case Emergency::Radio:
      return "RADIO";
    case Emergency::Hijack:
      return "HIJACK";
    case Emergency::None:
    default:
      return "";
  }
}

/** Panel banner label: "EMERGENCY", "RADIO FAILURE", "HIJACK". */
inline const char* emergencyLong(Emergency e) {
  switch (e) {
    case Emergency::General:
      return "EMERGENCY";
    case Emergency::Radio:
      return "RADIO FAILURE";
    case Emergency::Hijack:
      return "HIJACK";
    case Emergency::None:
    default:
      return "";
  }
}

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  /** Age of the position fix at fetch time (ms), from the feed's seen_pos.
   *  Added to the elapsed time when dead-reckoning so the drawn position
   *  reflects the estimated current location, not the already-stale fix. */
  uint32_t pos_age_ms;
  /** Flight number, or the ICAO hex when the feed has none. */
  char callsign[9];
  /** ICAO 24-bit address, e.g. "4841ad" ("~" prefix for TIS-B). Stable ID. */
  char hex[8];
  char type[5];
  char alt[12];
  /** Numeric altitude (feet) when has_alt; on_ground when the feed says so. */
  int32_t alt_ft;
  bool has_alt;
  bool on_ground;
  /** Vertical rate (ft/min, + = climbing) when has_vrate. */
  int16_t vrate_fpm;
  bool has_vrate;
  /** Transponder code, e.g. "7700" (may be empty). */
  char squawk[5];
  Emergency emergency;
  /** Full aircraft name, e.g. "BOEING 737-800" (may be empty). */
  char desc[34];
};

/** True when `callsign` is a real flight number (not the hex fallback). */
inline bool hasFlightNumber(const Aircraft& a) {
  return a.callsign[0] != 0 && std::strcmp(a.callsign, a.hex) != 0;
}

constexpr size_t kMaxAircraft = 64;

/** Create the internal lock. Call once before any fetch/snapshot. */
void init();

/**
 * Two buffers: the fetch task parses into one while the UI reads the other.
 * takeLatest() hands the UI the newest finished list (swapping buffers when a
 * new one is ready) and reports its fetch time. The returned list stays valid
 * and unchanged until the next takeLatest() call. One caller only (the UI on
 * the loop task).
 */
const Aircraft* takeLatest(size_t* count, unsigned long* out_last_update_ms);

/**
 * millis() timestamp of the last successful fetch (0 before the first). The
 * stored lat/lon are the values as of this time; callers can dead-reckon
 * newer positions from track_deg/gs_knots and the elapsed time.
 */
unsigned long lastUpdateMs();

/** Drop all aircraft (e.g. the radar moved); the next fetch refills. */
void clear();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/**
 * Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi, over
 * a kept-open HTTPS connection (one TLS handshake, then reused). The result
 * is dropped if the radar location changed since `location_generation`.
 */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km,
                 uint32_t location_generation);

/**
 * Close the kept-open connection (network task). Call before any other HTTPS
 * request, so only one TLS session ever holds memory.
 */
void closeConnection();

}  // namespace services::adsb
