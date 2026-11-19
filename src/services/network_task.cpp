#include "services/network_task.h"

#include <Arduino.h>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "services/adsb_client.h"
#include "services/geocode.h"
#include "services/ip_location.h"
#include "services/radar_location.h"
#include "services/route_lookup.h"
#include "services/wifi_conn.h"
#include "ui/lvgl_mem.h"
#include "ui/radar_range.h"

namespace services::network_task {

namespace {

/** Retry the IP location lookup this often until one succeeds. */
constexpr unsigned long kIpLookupRetryMs = 30000;
/** Before Wi-Fi is up, check back this often. */
constexpr unsigned long kIdlePollMs = 1000;
constexpr uint32_t kStackBytes = 10240;
/** Periodic heap log interval. */
constexpr unsigned long kHeapLogIntervalMs = 30000;

TaskHandle_t s_task = nullptr;
volatile bool s_fetch_now = false;

void ipLocationIfNeeded(bool* tried, unsigned long* last_try_ms) {
  if (services::location::isSet() ||
      (*tried && millis() - *last_try_ms < kIpLookupRetryMs)) {
    return;
  }
  *tried = true;
  *last_try_ms = millis();
  double lat;
  double lon;
  char city[services::location::kNameMaxLen + 1];
  if (services::ip_location::lookup(&lat, &lon, city, sizeof(city))) {
    services::location::save(lat, lon, city, /*approximate=*/true);
  }
}

void run(void*) {
  bool ip_tried = false;
  unsigned long ip_last_try_ms = 0;
  unsigned long last_fetch_ms = 0;
  unsigned long last_heap_log_ms = 0;
  bool fetched_once = false;
  for (;;) {
    if (millis() - last_heap_log_ms >= kHeapLogIntervalMs) {
      last_heap_log_ms = millis();
      logHeap("tick");
    }
    if (services::wifi::connected()) {
      traceHeap("net:idle");
      // Another HTTPS request is about to run: free the ADS-B connection
      // first, so two TLS sessions never hold memory at once.
      if (!services::location::isSet() ||
          services::geocode::status() == services::geocode::Status::Pending ||
          services::route::pending()) {
        services::adsb::closeConnection();
      }
      ipLocationIfNeeded(&ip_tried, &ip_last_try_ms);
      traceHeap("net:ip_location");
      // User-initiated lookups first: someone is waiting on them.
      services::geocode::process();
      traceHeap("net:geocode");
      services::route::process();
      traceHeap("net:route");
      if (s_fetch_now || !fetched_once ||
          millis() - last_fetch_ms >= config::kAdsbFetchIntervalMs) {
        s_fetch_now = false;
        fetched_once = true;
        last_fetch_ms = millis();
        double lat = 0.0;
        double lon = 0.0;
        uint32_t generation = 0;
        services::location::get(&lat, &lon, &generation);
        services::adsb::fetchUpdate(lat, lon, ui::radar::fetchRadiusKm(),
                                    generation);
      }
    }
    // Sleep until the next ADS-B fetch is due, or until woken.
    unsigned long wait_ms = kIdlePollMs;
    if (fetched_once) {
      const unsigned long since = millis() - last_fetch_ms;
      wait_ms = since >= config::kAdsbFetchIntervalMs
                    ? 1
                    : config::kAdsbFetchIntervalMs - since;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
  }
}

}  // namespace

void start() {
  // Core 0, away from the UI loop on core 1.
  // ~6 KB used at most (measured, TLS included); 10 KB leaves margin.
  xTaskCreatePinnedToCore(run, "net", kStackBytes, nullptr, 1, &s_task, 0);
}

void wake() {
  if (s_task != nullptr) {
    xTaskNotifyGive(s_task);
  }
}

void logHeap(const char* what) {
  const size_t free_b = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  const UBaseType_t stack_left =
      s_task != nullptr ? uxTaskGetStackHighWaterMark(s_task) : 0;
  const ui::lvgl_mem::Stats lv = ui::lvgl_mem::stats();
  Serial.printf(
      "heap[%s] free=%u largest=%u min=%u net_stack_left=%u lvgl=%u "
      "lvgl_peak=%u lvgl_fail=%u\n",
      what, static_cast<unsigned>(free_b), static_cast<unsigned>(largest),
      static_cast<unsigned>(min_free), static_cast<unsigned>(stack_left),
      static_cast<unsigned>(lv.used), static_cast<unsigned>(lv.peak),
      static_cast<unsigned>(lv.failures));
}

#ifdef PLANE_RADAR_HEAP_TRACE
void traceHeap(const char* stage) {
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  static size_t last_min = SIZE_MAX;
  const size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  portENTER_CRITICAL(&mux);
  const size_t prev = last_min;
  last_min = min_free;
  portEXIT_CRITICAL(&mux);
  if (min_free < prev) {
    Serial.printf("heapmin %u (-%u) during %s [core %d] free=%u largest=%u\n",
                  static_cast<unsigned>(min_free),
                  static_cast<unsigned>(prev == SIZE_MAX ? 0 : prev - min_free),
                  stage, xPortGetCoreID(),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  }
}
#endif

void fetchNow() {
  s_fetch_now = true;
  wake();
}

}  // namespace services::network_task
