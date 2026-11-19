#pragma once

#include <cstddef>
#include <cstdint>

namespace services::location {

/** Longest place name kept (e.g. "14565 Rue Gilbert, Mirabel"). */
constexpr size_t kNameMaxLen = 63;

/** Load saved lat/lon from NVS, or use config defaults. Call once at boot. */
void init();

/** Current radar center (thread-safe; read by the ADS-B task too). */
double lat();
double lon();

/**
 * Centre and its generation, read together (thread-safe). The generation goes
 * up on every change, so a fetch started for an older centre can be dropped.
 */
void get(double* lat, double* lon, uint32_t* generation);
uint32_t generation();

/** True once a location has been saved (otherwise the config default is used). */
bool isSet();

/** Place name for the current location ("" if unknown). Thread-safe. */
void name(char* out, size_t out_len);

/** True for a location from the IP lookup (city-level only). */
bool isApproximate();

/**
 * Validate, persist to NVS and apply. Thread-safe. Unless `from_sd` (values
 * being loaded from the card), the SD config file is written too.
 */
bool save(double lat, double lon, const char* name, bool approximate,
          bool from_sd = false);

/** Clear stored coordinates (back to the config default). */
void clear();

}  // namespace services::location
