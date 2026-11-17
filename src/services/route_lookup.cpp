#include "services/route_lookup.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <freertos/semphr.h>

#include <cctype>
#include <cstring>

namespace services::route {

namespace {

constexpr char kApiBase[] = "https://api.adsbdb.com/v0/callsign/";
constexpr int kTimeoutMs = 6000;
/** Retry a failed (network/server error) lookup after this long. */
constexpr unsigned long kRetryAfterMs = 30000;

SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_network_task = nullptr;

// Guarded by s_mutex.
char s_callsign[9] = "";
Status s_status = Status::None;
Info s_info = {};
unsigned long s_failed_at_ms = 0;

class Lock {
 public:
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

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

void copyAirport(const JsonObject& airport, char* code, size_t code_len,
                 char* city, size_t city_len) {
  const char* iata = airport["iata_code"] | "";
  copyStr(code, code_len, iata[0] != '\0' ? iata : (airport["icao_code"] | ""));
  if (city != nullptr) {
    copyStr(city, city_len, airport["municipality"] | "");
  }
}

/** Blocking HTTPS GET; fills `info`. Returns Found, NotFound or Failed. */
Status fetch(const char* callsign, Info* info) {
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
  copyAirport(route["origin"].as<JsonObject>(), info->origin, sizeof(info->origin), nullptr, 0);
  copyAirport(route["destination"].as<JsonObject>(), info->destination,
              sizeof(info->destination), info->dest_city,
              sizeof(info->dest_city));
  Serial.printf("route %s: %s %s-%s\n", callsign, info->airline, info->origin,
                info->destination);
  return Status::Found;
}

}  // namespace

void init(TaskHandle_t network_task) {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
  s_network_task = network_task;
}

void request(const char* callsign) {
  if (s_mutex == nullptr || !isValidCallsign(callsign)) {
    return;
  }
  {
    const Lock lock;
    if (strcmp(s_callsign, callsign) == 0) {
      const bool retry = s_status == Status::Failed &&
                         millis() - s_failed_at_ms >= kRetryAfterMs;
      if (!retry) {
        return;  // known, not found, or already pending
      }
    } else {
      copyStr(s_callsign, sizeof(s_callsign), callsign);
      s_info = {};
    }
    s_status = Status::Pending;
  }
  if (s_network_task != nullptr) {
    xTaskNotifyGive(s_network_task);
  }
}

Status get(const char* callsign, Info* out) {
  if (s_mutex == nullptr || callsign == nullptr) {
    return Status::None;
  }
  const Lock lock;
  if (strcmp(s_callsign, callsign) != 0) {
    return Status::None;
  }
  if (s_status == Status::Found && out != nullptr) {
    *out = s_info;
  }
  return s_status;
}

bool process() {
  if (s_mutex == nullptr) {
    return false;
  }
  char callsign[sizeof(s_callsign)];
  {
    const Lock lock;
    if (s_status != Status::Pending) {
      return false;
    }
    copyStr(callsign, sizeof(callsign), s_callsign);
  }

  Info info = {};
  const Status result = fetch(callsign, &info);

  const Lock lock;
  // The selection may have moved on while the request ran.
  if (strcmp(s_callsign, callsign) == 0 && s_status == Status::Pending) {
    s_status = result;
    if (result == Status::Found) {
      s_info = info;
    } else if (result == Status::Failed) {
      s_failed_at_ms = millis();
    }
  }
  return true;
}

}  // namespace services::route
