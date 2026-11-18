#pragma once

/**
 * Airline and route for one flight number, from api.adsbdb.com.
 *
 * The UI asks with request(); the network task does the HTTPS call in
 * process(); the UI reads the result with get(). The last 8 answers (found or
 * unknown) are cached, so re-selecting a plane needs no request. Only the
 * newest request is ever pending.
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

/** True when process() has a lookup to run (network task). */
bool pending();

}  // namespace services::route
