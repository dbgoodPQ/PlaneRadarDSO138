#include "services/adsb_parse.h"

#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <cstring>

namespace services::adsb::parse {

namespace {

constexpr float kMaxPositionAgeSec = 30.0f;  // stale fixes aren't flung far
constexpr float kMaxVerticalRateFpm = 32000.0f;

/** First of `keys` that holds a number; 0 if none. */
float firstFloat(JsonObjectConst obj, std::initializer_list<const char*> keys) {
  float v = 0.0f;
  for (const char* key : keys) {
    if (readFloat(obj, key, &v)) {
      return v;
    }
  }
  return 0.0f;
}

}  // namespace

bool readFloat(JsonObjectConst obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float noseHeading(JsonObjectConst plane) {
  return firstFloat(plane, {"true_heading", "mag_heading", "track", "dir"});
}

float trackHeading(JsonObjectConst plane) {
  return firstFloat(plane, {"track", "true_heading", "mag_heading", "dir"});
}

float groundSpeed(JsonObjectConst plane) {
  return firstFloat(plane, {"gs", "tas", "ias"});
}

bool isOnGround(JsonObjectConst plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyTrimmed(JsonObjectConst obj, const char* key, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (!obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void altitude(JsonObjectConst plane, Aircraft* ac) {
  ac->alt[0] = 0;
  ac->alt_ft = 0;
  ac->has_alt = false;
  ac->on_ground = false;

  if (isOnGround(plane)) {
    snprintf(ac->alt, sizeof(ac->alt), "GND");
    ac->on_ground = true;
    return;
  }

  float alt = 0.0f;
  if (readFloat(plane, "alt_baro", &alt) || readFloat(plane, "alt_geom", &alt)) {
    ac->alt_ft = static_cast<int32_t>(lroundf(alt));
    ac->has_alt = true;
    snprintf(ac->alt, sizeof(ac->alt), "%d ft", static_cast<int>(ac->alt_ft));
  }
}

void verticalRate(JsonObjectConst plane, Aircraft* ac) {
  float rate = 0.0f;
  ac->has_vrate =
      readFloat(plane, "baro_rate", &rate) || readFloat(plane, "geom_rate", &rate);
  if (rate > kMaxVerticalRateFpm) rate = kMaxVerticalRateFpm;
  if (rate < -kMaxVerticalRateFpm) rate = -kMaxVerticalRateFpm;
  ac->vrate_fpm = ac->has_vrate ? static_cast<int16_t>(lroundf(rate)) : 0;
}

void emergency(JsonObjectConst plane, Aircraft* ac) {
  copyTrimmed(plane, "squawk", ac->squawk, sizeof(ac->squawk));
  ac->emergency = Emergency::None;
  if (strcmp(ac->squawk, "7700") == 0) {
    ac->emergency = Emergency::General;
  } else if (strcmp(ac->squawk, "7600") == 0) {
    ac->emergency = Emergency::Radio;
  } else if (strcmp(ac->squawk, "7500") == 0) {
    ac->emergency = Emergency::Hijack;
  } else if (plane["emergency"].is<const char*>()) {
    const char* e = plane["emergency"].as<const char*>();
    if (strcmp(e, "nordo") == 0) {
      ac->emergency = Emergency::Radio;
    } else if (strcmp(e, "unlawful") == 0) {
      ac->emergency = Emergency::Hijack;
    } else if (strcmp(e, "general") == 0 || strcmp(e, "lifeguard") == 0 ||
               strcmp(e, "minfuel") == 0 || strcmp(e, "downed") == 0) {
      ac->emergency = Emergency::General;
    }
  }
}

uint32_t positionAgeMs(JsonObjectConst plane) {
  float seen_pos = 0.0f;
  readFloat(plane, "seen_pos", &seen_pos);
  if (seen_pos < 0.0f) seen_pos = 0.0f;
  if (seen_pos > kMaxPositionAgeSec) seen_pos = kMaxPositionAgeSec;
  return static_cast<uint32_t>(seen_pos * 1000.0f);
}

bool aircraft(JsonObjectConst plane, Aircraft* ac) {
  if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
    return false;
  }
  ac->lat = plane["lat"].as<float>();
  ac->lon = plane["lon"].as<float>();
  ac->nose_deg = noseHeading(plane);
  ac->track_deg = trackHeading(plane);
  ac->gs_knots = groundSpeed(plane);
  ac->pos_age_ms = positionAgeMs(plane);

  copyTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {  // no flight number: show the ICAO hex
    copyTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }
  copyTrimmed(plane, "t", ac->type, sizeof(ac->type));
  copyTrimmed(plane, "desc", ac->desc, sizeof(ac->desc));
  altitude(plane, ac);
  verticalRate(plane, ac);
  emergency(plane, ac);
  return true;
}

}  // namespace services::adsb::parse
