#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "config.h"
#include "services/http_body_framing.h"
#include "services/settings.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectTimeoutMs = 5000;  // TLS handshake needs room
constexpr unsigned long kRequestTimeoutMs = 6000;

// Double buffer (~7 KB each). s_front is the UI's; the other one is the fetch
// task's. They swap in takeLatest(), only while s_ready is set (no parse is
// writing the back buffer then).
Aircraft s_buffers[2][kMaxAircraft];
uint8_t s_front = 0;
size_t s_front_count = 0;
unsigned long s_front_update_ms = 0;
size_t s_back_count = 0;
unsigned long s_back_update_ms = 0;
bool s_ready = false;        // back buffer holds a finished, untaken list
bool s_clear_pending = false;  // clear(): the UI's next take returns nothing
unsigned long s_last_update_ms = 0;
PollFn s_poll_fn = nullptr;
SemaphoreHandle_t s_mutex = nullptr;

void lock() {
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
}

void unlock() {
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
}

/** Fetch task: claim the back buffer (drops a list the UI never took). */
Aircraft* beginParse() {
  lock();
  s_ready = false;
  Aircraft* back = s_buffers[s_front ^ 1];
  unlock();
  return back;
}

/** Fetch task: the back buffer is complete; offer it to the UI. */
void publish(size_t count) {
  lock();
  s_back_count = count;
  s_back_update_ms = millis();  // base time for dead-reckoning
  s_last_update_ms = s_back_update_ms;
  s_ready = true;
  s_clear_pending = false;
  unlock();
}

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

/**
 * One GET attempt. No retry here: a failed connect is usually a failed TLS
 * setup (heap too fragmented), and hammering it only churns the heap more.
 * The next fetch cycle tries again.
 */
int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectTimeoutMs);
  pollNetwork();
  return http.GET();
}

/**
 * Pumps the socket one byte at a time, in blocks, without buffering the whole
 * response.
 *
 * Each refill runs the network poll callback, which is why HTTPClient's own
 * body readers (getString/writeToStream) can't be used here -- they block
 * without giving the fetch task a chance to poll. That also means they can't
 * de-chunk for us, so the wire image comes out raw and BodyFramer unwraps it.
 *
 * Where the body ends is BodyFramer's business, not this class's: it stops
 * pulling at the right byte. Reading a little past that point into buffer_ is
 * harmless while the connection is torn down after every fetch.
 */
class PollingSocketSource {
 public:
  PollingSocketSource(HTTPClient& http, WiFiClient& stream,
                      unsigned long deadline)
      : http_(&http), stream_(&stream), deadline_(deadline) {}

  /** Next raw byte, or -1 once the socket closes or the deadline passes. */
  int read() {
    if (pos_ >= len_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

 private:
  bool refill() {
    pos_ = 0;
    len_ = 0;
    while (millis() < deadline_) {
      pollNetwork();
      const int available = stream_->available();
      if (available > 0) {
        const int to_read = available > static_cast<int>(sizeof(buffer_))
                                ? static_cast<int>(sizeof(buffer_))
                                : available;
        const int read_bytes = stream_->readBytes(buffer_, to_read);
        if (read_bytes > 0) {
          len_ = static_cast<size_t>(read_bytes);
          return true;
        }
      }
      if (!http_->connected() && stream_->available() <= 0) {
        break;  // server closed and the socket is drained
      }
      delay(1);
    }
    return false;
  }

  HTTPClient* http_;
  WiFiClient* stream_;
  unsigned long deadline_;
  char buffer_[512];
  size_t pos_ = 0;
  size_t len_ = 0;
};

using BodyReader = services::http::BodyFramer<PollingSocketSource>;

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
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

/** Altitude text for the tag ("12345 ft" / "GND") plus the numeric fields. */
void readAltitude(const JsonObject& plane, Aircraft* ac) {
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
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    ac->alt_ft = static_cast<int32_t>(lroundf(alt));
    ac->has_alt = true;
    snprintf(ac->alt, sizeof(ac->alt), "%d ft", static_cast<int>(ac->alt_ft));
  }
}

/** Vertical rate: barometric, else geometric. */
void readVerticalRate(const JsonObject& plane, Aircraft* ac) {
  float rate = 0.0f;
  ac->has_vrate = readJsonFloat(plane, "baro_rate", &rate) ||
                  readJsonFloat(plane, "geom_rate", &rate);
  if (rate > 32000.0f) rate = 32000.0f;
  if (rate < -32000.0f) rate = -32000.0f;
  ac->vrate_fpm = ac->has_vrate ? static_cast<int16_t>(lroundf(rate)) : 0;
}

/** Squawk 7500/7600/7700 wins; otherwise the feed's "emergency" field. */
void readEmergency(const JsonObject& plane, Aircraft* ac) {
  copyJsonStringTrimmed(plane, "squawk", ac->squawk, sizeof(ac->squawk));
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

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  copyJsonStringTrimmed(plane, "desc", ac->desc, sizeof(ac->desc));
  readAltitude(plane, ac);
  readVerticalRate(plane, ac);
  readEmergency(plane, ac);
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void setPollFn(PollFn fn) { s_poll_fn = fn; }

void clear() {
  lock();
  s_ready = false;
  s_clear_pending = true;
  s_last_update_ms = 0;
  unlock();
}

unsigned long lastUpdateMs() { return s_last_update_ms; }

const Aircraft* takeLatest(size_t* count, unsigned long* out_last_update_ms) {
  lock();
  if (s_ready) {
    s_front ^= 1;
    s_front_count = s_back_count;
    s_front_update_ms = s_back_update_ms;
    s_ready = false;
  } else if (s_clear_pending) {
    s_front_count = 0;
    s_front_update_ms = 0;
    s_clear_pending = false;
  }
  *count = s_front_count;
  if (out_last_update_ms != nullptr) {
    *out_last_update_ms = s_front_update_ms;
  }
  const Aircraft* front = s_buffers[s_front];
  unlock();
  return front;
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  // Keep only the fields we render; the rest never reaches RAM.
  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  for (const char* key :
       {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
        "tas", "ias", "alt_baro", "alt_geom", "seen_pos", "flight", "hex", "t",
        "category", "desc", "squawk", "emergency", "baro_rate", "geom_rate"}) {
    f[key] = true;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  // HTTPClient only records Transfer-Encoding in the collected headers when
  // it is asked for up front; _transferEncoding itself is private.
  static const char* kWantedHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kWantedHeaders, 1);

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("adsb: no response stream");
    http.end();
    return false;
  }

  // On HTTP/1.1 the CDN answers with Transfer-Encoding: chunked, and
  // getStreamPtr() hands back the raw socket -- chunk sizes and all. BodyFramer
  // strips that framing back off.
  const services::http::BodyFraming framing =
      http.header("Transfer-Encoding").equalsIgnoreCase("chunked")
          ? services::http::BodyFraming::kChunked
          : services::http::BodyFraming::kIdentity;

  PollingSocketSource source(http, *stream, millis() + kRequestTimeoutMs);
  BodyReader body(source, framing, http.getSize());
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  // Read off the terminating chunk the parser stopped short of, so the socket
  // sits at the end of the message. Not needed while every fetch builds its own
  // connection, but a prerequisite for ever reusing one.
  body.drain();
  http.end();
  if (err) {
    if (body.framingError()) {
      Serial.println("adsb: malformed chunked body");
    } else if (body.bytesRead() == 0) {
      Serial.println("adsb: empty response");
    } else {
      Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    }
    return false;
  }

  // Parse into the back buffer; the UI keeps reading the front one until
  // publish() offers this list and the UI swaps.
  Aircraft* parsed = beginParse();
  size_t n = 0;
  JsonArray ac = doc["ac"].as<JsonArray>();
  if (!ac.isNull()) {
    for (JsonObject plane : ac) {
      if (n >= kMaxAircraft) {
        break;
      }
      if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
        continue;
      }
      if (isOnGround(plane) && !services::settings::showGroundAircraft()) {
        continue;
      }

      parsed[n].lat = plane["lat"].as<float>();
      parsed[n].lon = plane["lon"].as<float>();
      parsed[n].nose_deg = pickNoseHeading(plane);
      parsed[n].track_deg = pickTrackHeading(plane);
      parsed[n].gs_knots = pickGroundSpeed(plane);

      // seen_pos: seconds since this position was measured. Use it as the
      // dead-reckoning age offset, capped so a very stale fix isn't flung far.
      float seen_pos = 0.0f;
      readJsonFloat(plane, "seen_pos", &seen_pos);
      if (seen_pos < 0.0f) seen_pos = 0.0f;
      if (seen_pos > 30.0f) seen_pos = 30.0f;
      parsed[n].pos_age_ms = static_cast<uint32_t>(seen_pos * 1000.0f);

      fillTagFields(&parsed[n], plane);
#ifdef PLANE_RADAR_FAKE_SQUAWK
      // Test build only: show the first aircraft as squawking 7700.
      if (n == 0) {
        snprintf(parsed[n].squawk, sizeof(parsed[n].squawk), "7700");
        parsed[n].emergency = Emergency::General;
      }
#endif
      ++n;
    }
  }

  publish(n);
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
