#pragma once

/**
 * Airline and route for one flight number, from api.adsbdb.com.
 *
 * The UI asks with request(); the network task does the HTTPS call in
 * process(); the UI reads the result with get(). Only the last requested
 * callsign is kept (the selected plane).
 */
namespace services::route {

struct Info {
  char airline[40];      // "KLM Royal Dutch Airlines"
  char origin[5];        // IATA "NRT" (ICAO if no IATA)
  char destination[5];   // IATA "AMS"
  char dest_city[24];    // "Amsterdam"
};

enum class Status { None, Pending, Found, NotFound, Failed };

/** Create the lock. Call once at boot, before the network task starts. */
void init();

/**
 * Ask for the route of `callsign` (UI task). No-op if it is already known or
 * in flight; a failed lookup is retried after a while.
 */
void request(const char* callsign);

/** Status (and, when Found, the info) for `callsign` (UI task). */
Status get(const char* callsign, Info* out);

/** Run a pending lookup, if any (network task). True if it made a request. */
bool process();

}  // namespace services::route
