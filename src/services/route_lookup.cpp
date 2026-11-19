#include "services/route_lookup.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <freertos/semphr.h>

#include <cctype>
#include <cstring>

#include "services/network_task.h"

namespace services::route {

namespace {

constexpr char kApiBase[] = "https://api.adsbdb.com/v0/callsign/";
constexpr int kTimeoutMs = 6000;
/** Retry a failed (network/server error) lookup after this long. */
constexpr unsigned long kRetryAfterMs = 30000;

constexpr size_t kCacheSize = 8;

SemaphoreHandle_t s_mutex = nullptr;

struct Entry {
  char callsign[9];  // "" = free slot
  Status status;     // Pending, Found, NotFound or Failed
  Info info;
  uint32_t used;     // LRU stamp
  unsigned long failed_at_ms;
};

// Guarded by s_mutex.
Entry s_cache[kCacheSize] = {};
uint32_t s_use_counter = 0;

class Lock {
 public:
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

/** Cache entry for `callsign`, or nullptr. Lock held. */
Entry* findEntry(const char* callsign) {
  for (Entry& e : s_cache) {
    if (e.callsign[0] != '\0' && strcmp(e.callsign, callsign) == 0) {
      return &e;
    }
  }
  return nullptr;
}

/** The newest pending entry, or nullptr. Lock held. */
Entry* pendingEntry() {
  for (Entry& e : s_cache) {
    if (e.callsign[0] != '\0' && e.status == Status::Pending) {
      return &e;
    }
  }
  return nullptr;
}

void copyStr(char* out, size_t len, const char* in) {
  if (in == nullptr) {
    in = "";
  }
  strncpy(out, in, len - 1);
  out[len - 1] = '\0';
}

/** Flight numbers are letters and digits only; anything else is not sent. */
bool isValidCallsign(const char* s) {
  if (s == nullptr || s[0] == '\0' || strlen(s) > 8) {
    return false;
  }
  for (; *s != '\0'; ++s) {
    if (!isalnum(static_cast<unsigned char>(*s))) {
      return false;
    }
  }
  return true;
}

/** Drop a UTF-8 character cut in half at the end of `s` (after truncation). */
void trimPartialUtf8(char* s) {
  size_t len = strlen(s);
  size_t start = len;  // start of the last character
  while (start > 0 && (static_cast<unsigned char>(s[start - 1]) & 0xC0) == 0x80) {
    --start;
  }
  if (start == 0) {
    return;
  }
  --start;
  const unsigned char lead = static_cast<unsigned char>(s[start]);
  const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (len - start < need) {
    s[start] = '\0';
  }
}

/**
 * Airport code (IATA, else ICAO) and place name for the panel:
 * "City, Country", or just the city or the country when one is missing, or
 * the code when both are.
 */
void copyAirport(const JsonObject& airport, char* code, size_t code_len,
                 char* place, size_t place_len) {
  const char* iata = airport["iata_code"] | "";
  copyStr(code, code_len, iata[0] != '\0' ? iata : (airport["icao_code"] | ""));

  const char* city = airport["municipality"] | "";
  const char* country = airport["country_name"] | "";
  if (city[0] != '\0' && country[0] != '\0') {
    snprintf(place, place_len, "%s, %s", city, country);
  } else if (city[0] != '\0' || country[0] != '\0') {
    copyStr(place, place_len, city[0] != '\0' ? city : country);
  } else {
    copyStr(place, place_len, code);
  }
  trimPartialUtf8(place);
}

/** Blocking HTTPS GET; fills `info`. Returns Found, NotFound or Failed. */
Status fetch(const char* callsign, Info* info) {
  services::network_task::logHeap("route");
  String url = kApiBase;
  url += callsign;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding: parse straight off the stream
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);
  if (!http.begin(client, url)) {
    return Status::Failed;
  }
  const int code = http.GET();
  if (code == HTTP_CODE_NOT_FOUND) {
    http.end();
    Serial.printf("route %s: unknown\n", callsign);
    return Status::NotFound;
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("route %s: HTTP %d\n", callsign, code);
    return Status::Failed;
  }

  JsonDocument filter;
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["airline"]["name"] = true;
  for (const char* end : {"origin", "destination"}) {
    fr[end]["iata_code"] = true;
    fr[end]["icao_code"] = true;
    fr[end]["municipality"] = true;
    fr[end]["country_name"] = true;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("route %s: bad JSON (%s)\n", callsign, err.c_str());
    return Status::Failed;
  }

  JsonObject route = doc["response"]["flightroute"];
  if (route.isNull()) {
    return Status::NotFound;
  }
  copyStr(info->airline, sizeof(info->airline), route["airline"]["name"] | "");
  copyAirport(route["origin"].as<JsonObject>(), info->origin,
              sizeof(info->origin), info->origin_place,
              sizeof(info->origin_place));
  copyAirport(route["destination"].as<JsonObject>(), info->destination,
              sizeof(info->destination), info->dest_place,
              sizeof(info->dest_place));
  Serial.printf("route %s: %s %s-%s (%s - %s)\n", callsign, info->airline,
                info->origin, info->destination, info->origin_place,
                info->dest_place);
  return Status::Found;
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void request(const char* callsign) {
  if (s_mutex == nullptr || !isValidCallsign(callsign)) {
    return;
  }
  {
    const Lock lock;
    Entry* e = findEntry(callsign);
    if (e != nullptr) {
      e->used = ++s_use_counter;
      const bool ask = e->status == Status::None ||
                       (e->status == Status::Failed &&
                        millis() - e->failed_at_ms >= kRetryAfterMs);
      if (!ask) {
        return;  // cached, or already pending
      }
    }
    // Only the newest request stays pending: older unanswered ones go back
    // to None (a lookup already in flight still fills its entry).
    for (Entry& other : s_cache) {
      if (&other != e && other.callsign[0] != '\0' &&
          other.status == Status::Pending) {
        other.status = Status::None;
      }
    }
    if (e == nullptr) {
      e = &s_cache[0];
      for (Entry& slot : s_cache) {
        if (slot.callsign[0] == '\0') {
          e = &slot;
          break;
        }
        if (slot.used < e->used) {
          e = &slot;
        }
      }
      *e = Entry{};
      copyStr(e->callsign, sizeof(e->callsign), callsign);
      e->used = ++s_use_counter;
    }
    e->status = Status::Pending;
  }
  services::network_task::wake();
}

Status get(const char* callsign, Info* out) {
  if (s_mutex == nullptr || callsign == nullptr) {
    return Status::None;
  }
  const Lock lock;
  const Entry* e = findEntry(callsign);
  if (e == nullptr) {
    return Status::None;
  }
  if (e->status == Status::Found && out != nullptr) {
    *out = e->info;
  }
  return e->status;
}

bool pending() {
  if (s_mutex == nullptr) {
    return false;
  }
  const Lock lock;
  return pendingEntry() != nullptr;
}

bool process() {
  if (s_mutex == nullptr) {
    return false;
  }
  char callsign[sizeof(Entry::callsign)];
  {
    const Lock lock;
    const Entry* e = pendingEntry();
    if (e == nullptr) {
      return false;
    }
    copyStr(callsign, sizeof(callsign), e->callsign);
  }

  Info info = {};
  const Status result = fetch(callsign, &info);

  const Lock lock;
  // Kept even if the selection moved on (status back to None): it is cached
  // for next time. The entry may have been evicted meanwhile.
  Entry* e = findEntry(callsign);
  if (e != nullptr &&
      (e->status == Status::Pending || e->status == Status::None)) {
    e->status = result;
    if (result == Status::Found) {
      e->info = info;
    } else if (result == Status::Failed) {
      e->failed_at_ms = millis();
    }
  }
  return true;
}

}  // namespace services::route
