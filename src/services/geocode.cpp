#include "services/geocode.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "services/network_task.h"

namespace services::geocode {

namespace {

constexpr char kApiBase[] =
    "https://nominatim.openstreetmap.org/search?format=jsonv2&addressdetails=1"
    "&limit=5&q=";
/** Nominatim policy: identify the app; no personal details are sent. */
constexpr char kUserAgent[] = "PlaneRadar/1.0 (ESP32)";
constexpr int kTimeoutMs = 8000;
/** Nominatim policy: at most 1 request per second. */
constexpr unsigned long kMinIntervalMs = 1500;

SemaphoreHandle_t s_mutex = nullptr;

// Guarded by s_mutex.
char s_query[kQueryMaxLen + 1] = "";
uint32_t s_generation = 0;  // bumped by every request()
Status s_status = Status::None;
Result s_results[kMaxResults];
size_t s_count = 0;

unsigned long s_last_request_ms = 0;  // network task only

class Lock {
 public:
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

void copyStr(char* out, size_t len, const char* in) {
  strncpy(out, in != nullptr ? in : "", len - 1);
  out[len - 1] = '\0';
}

/** Percent-encode for a URL query value (spaces become "+"). */
String urlEncode(const char* s) {
  String out;
  for (; *s != '\0'; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else if (c == ' ') {
      out += '+';
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

/** "14565 Rue Gilbert, Mirabel" from the address parts, else display_name. */
void shortName(JsonObjectConst place, char* out, size_t len) {
  JsonObjectConst addr = place["address"];
  const char* number = addr["house_number"] | "";
  const char* road = addr["road"] | "";
  const char* city = addr["city"] | (addr["town"] | (addr["village"] |
                     (addr["municipality"] | (addr["hamlet"] | ""))));
  char street[64] = "";
  if (road[0] != '\0') {
    snprintf(street, sizeof(street), "%s%s%s", number,
             number[0] != '\0' ? " " : "", road);
  }
  if (street[0] != '\0' && city[0] != '\0') {
    snprintf(out, len, "%s, %s", street, city);
  } else if (street[0] != '\0' || city[0] != '\0') {
    copyStr(out, len, street[0] != '\0' ? street : city);
  } else {
    // First two comma-separated parts of the full name.
    const char* full = place["display_name"] | "";
    const char* comma = strchr(full, ',');
    if (comma != nullptr) {
      comma = strchr(comma + 1, ',');
    }
    const size_t n = comma != nullptr ? static_cast<size_t>(comma - full)
                                      : strlen(full);
    copyStr(out, n + 1 < len ? n + 1 : len, full);
  }
}

/** Blocking search. Fills `out`, sets `*count`; false on a network error. */
bool fetch(const char* query, Result* out, size_t* count) {
  *count = 0;
  services::network_task::logHeap("geocode");
  String url = kApiBase;
  url += urlEncode(query);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding: parse straight off the stream
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);
  http.setUserAgent(kUserAgent);
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Accept-Language", "en");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("geocode: HTTP %d\n", code);
    return false;
  }

  JsonDocument filter;
  JsonObject f = filter.add<JsonObject>();
  f["lat"] = true;
  f["lon"] = true;
  f["display_name"] = true;
  for (const char* key : {"house_number", "road", "city", "town", "village",
                          "municipality", "hamlet"}) {
    f["address"][key] = true;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("geocode: bad JSON (%s)\n", err.c_str());
    return false;
  }

  for (JsonObjectConst place : doc.as<JsonArrayConst>()) {
    if (*count >= kMaxResults) {
      break;
    }
    // Nominatim sends coordinates as strings.
    const char* lat = place["lat"] | "";
    const char* lon = place["lon"] | "";
    if (lat[0] == '\0' || lon[0] == '\0') {
      continue;
    }
    Result& r = out[(*count)++];
    r.lat = atof(lat);
    r.lon = atof(lon);
    shortName(place, r.name, sizeof(r.name));
    copyStr(r.full, sizeof(r.full), place["display_name"] | "");
  }
  Serial.printf("geocode: %u result(s)\n", static_cast<unsigned>(*count));
  return true;
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void request(const char* query) {
  if (s_mutex == nullptr) {
    return;
  }
  {
    const Lock lock;
    copyStr(s_query, sizeof(s_query), query);
    ++s_generation;
    s_status = Status::Pending;
    s_count = 0;
  }
  services::network_task::wake();
}

Status status() {
  if (s_mutex == nullptr) {
    return Status::None;
  }
  const Lock lock;
  return s_status;
}

size_t results(Result* out, size_t max_out) {
  if (s_mutex == nullptr) {
    return 0;
  }
  const Lock lock;
  const size_t n = s_count < max_out ? s_count : max_out;
  for (size_t i = 0; i < n; ++i) {
    out[i] = s_results[i];
  }
  return n;
}

bool process() {
  if (s_mutex == nullptr) {
    return false;
  }
  char query[kQueryMaxLen + 1];
  uint32_t generation;
  {
    const Lock lock;
    if (s_status != Status::Pending) {
      return false;
    }
    copyStr(query, sizeof(query), s_query);
    generation = s_generation;
  }

  const unsigned long since = millis() - s_last_request_ms;
  if (s_last_request_ms != 0 && since < kMinIntervalMs) {
    delay(kMinIntervalMs - since);
  }
  s_last_request_ms = millis();

  static Result found[kMaxResults];  // network task only
  size_t count = 0;
  const bool ok = fetch(query, found, &count);

  const Lock lock;
  if (generation != s_generation) {
    return true;  // a newer search replaced this one; it stays pending
  }
  s_status = ok ? Status::Done : Status::Failed;
  s_count = count;
  for (size_t i = 0; i < count; ++i) {
    s_results[i] = found[i];
  }
  return true;
}

}  // namespace services::geocode
