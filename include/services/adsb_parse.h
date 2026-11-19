#pragma once

#include <ArduinoJson.h>

#include "services/adsb_client.h"

/**
 * Turns one entry of the adsb.fi "ac" array into an Aircraft. Pure: depends
 * only on ArduinoJson, so the native unit tests can run it on a PC.
 */
namespace services::adsb::parse {

/** Number at `key` (float, double or int) into `out`; false if absent. */
bool readFloat(JsonObjectConst obj, const char* key, float* out);

/** Nose heading: true_heading, else mag_heading, track, dir; 0 if none. */
float noseHeading(JsonObjectConst plane);

/** Ground track: track, else true_heading, mag_heading, dir; 0 if none. */
float trackHeading(JsonObjectConst plane);

/** Speed (knots): gs, else tas, ias; 0 if none. */
float groundSpeed(JsonObjectConst plane);

/** True when alt_baro is the string "ground". */
bool isOnGround(JsonObjectConst plane);

/** String at `key` with trailing spaces removed, cut to fit; "" if absent. */
void copyTrimmed(JsonObjectConst obj, const char* key, char* out, size_t out_len);

/** Altitude text ("12345 ft" / "GND") plus alt_ft, has_alt and on_ground. */
void altitude(JsonObjectConst plane, Aircraft* ac);

/** Vertical rate (ft/min): baro_rate, else geom_rate; clamped to ±32000. */
void verticalRate(JsonObjectConst plane, Aircraft* ac);

/** Squawk 7500/7600/7700 wins; otherwise the feed's "emergency" field. */
void emergency(JsonObjectConst plane, Aircraft* ac);

/** Seconds since the position was measured (seen_pos), as ms, capped at 30 s. */
uint32_t positionAgeMs(JsonObjectConst plane);

/**
 * Fill `ac` from one aircraft entry. Returns false (and leaves `ac`
 * unspecified) when the entry has no position.
 */
bool aircraft(JsonObjectConst plane, Aircraft* ac);

}  // namespace services::adsb::parse
