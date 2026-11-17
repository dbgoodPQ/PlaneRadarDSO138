#pragma once

#include <cstddef>

/**
 * Address search with OpenStreetMap Nominatim. The UI calls request(); the
 * network task runs the HTTPS call in process(); the UI reads status() and
 * results(). One search at a time; a new request replaces the old one.
 */
namespace services::geocode {

constexpr size_t kMaxResults = 5;
constexpr size_t kQueryMaxLen = 100;

struct Result {
  double lat;
  double lon;
  /** Short name for the radar, e.g. "14565 Rue Gilbert, Mirabel". */
  char name[64];
  /** Full address as Nominatim gives it (UTF-8, may be cut). */
  char full[128];
};

enum class Status { None, Pending, Done, Failed };

/** Create the lock. Call once at boot, before the network task starts. */
void init();

/** Start a search (UI task). Wakes the network task. */
void request(const char* query);

Status status();

/** Copy the results of the last finished search; returns how many. */
size_t results(Result* out, size_t max_out);

/** Run a pending search, if any (network task). True if it made a request. */
bool process();

}  // namespace services::geocode
