#pragma once

namespace services::ip_location {

/**
 * Approximate location of the current internet connection (ip-api.com).
 * Blocking (~0.5 s); call from the ADS-B task, not the UI loop.
 * City-level accuracy only: often 5-50 km off.
 */
bool lookup(double* lat, double* lon);

}  // namespace services::ip_location
