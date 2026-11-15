#pragma once

namespace services::location {

/** Load saved lat/lon from NVS, or use config defaults. Call once at boot. */
void init();

/** Current radar center (thread-safe; read by the ADS-B task too). */
double lat();
double lon();

/** True once a location has been saved (otherwise the config default is used). */
bool isSet();

/** Validate, persist to NVS and apply. Thread-safe. */
bool save(double lat, double lon);

/** Clear stored coordinates (back to the config default). */
void clear();

}  // namespace services::location
