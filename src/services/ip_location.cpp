#include "services/ip_location.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include <cstring>

namespace services::ip_location {

namespace {

// ip-api.com's free endpoint is plain HTTP (HTTPS is paid). Nothing secret
// is sent, and plain HTTP avoids a ~40 KB TLS handshake on a no-PSRAM ESP32.
constexpr char kUrl[] = "http://ip-api.com/json/?fields=status,lat,lon,city";
constexpr int kTimeoutMs = 5000;

}  // namespace

bool lookup(double* lat, double* lon, char* city, size_t city_len) {
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);
  if (!http.begin(client, kUrl)) {
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("IP location: HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("IP location: bad JSON (%s)\n", err.c_str());
    return false;
  }
  if (doc["status"] != "success" || !doc["lat"].is<double>() ||
      !doc["lon"].is<double>()) {
    Serial.println("IP location: no result");
    return false;
  }
  *lat = doc["lat"].as<double>();
  *lon = doc["lon"].as<double>();
  if (city_len > 0) {
    strncpy(city, doc["city"] | "", city_len - 1);
    city[city_len - 1] = '\0';
  }
  Serial.printf("IP location: %s (%.4f, %.4f)\n", doc["city"] | "?", *lat, *lon);
  return true;
}

}  // namespace services::ip_location
