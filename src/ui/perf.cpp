#include "ui/perf.h"

#include <Arduino.h>
#include <lvgl.h>

namespace ui::perf {

namespace {

constexpr uint32_t kLogIntervalMs = 30000;

uint32_t s_last_log_ms = 0;

uint32_t s_refr_start_us = 0;
uint32_t s_frame_px = 0;  // pixels flushed in the current refresh

uint32_t s_frames = 0;  // refreshes that flushed at least one pixel
uint64_t s_render_us = 0;
uint32_t s_render_max_us = 0;
uint64_t s_px = 0;
uint64_t s_flush_us = 0;

uint32_t s_computes = 0;
uint64_t s_compute_us = 0;
uint32_t s_compute_max_us = 0;

void onRefrStart(lv_event_t*) {
  s_refr_start_us = micros();
  s_frame_px = 0;
}

void onRefrReady(lv_event_t*) {
  if (s_frame_px == 0) {
    return;  // nothing was dirty
  }
  const uint32_t us = micros() - s_refr_start_us;
  ++s_frames;
  s_render_us += us;
  if (us > s_render_max_us) {
    s_render_max_us = us;
  }
  s_px += s_frame_px;
}

float ms(uint64_t total_us, uint32_t n) {
  return n == 0 ? 0.0f : static_cast<float>(total_us) / n / 1000.0f;
}

}  // namespace

void attach(void* display) {
  auto* disp = static_cast<lv_display_t*>(display);
  lv_display_add_event_cb(disp, onRefrStart, LV_EVENT_REFR_START, nullptr);
  lv_display_add_event_cb(disp, onRefrReady, LV_EVENT_REFR_READY, nullptr);
}

void addFlush(uint32_t pixels, uint32_t us) {
  s_frame_px += pixels;
  s_flush_us += us;
}

void addCompute(uint32_t us) {
  ++s_computes;
  s_compute_us += us;
  if (us > s_compute_max_us) {
    s_compute_max_us = us;
  }
}

void logIfDue() {
  const uint32_t now = millis();
  if (now - s_last_log_ms < kLogIntervalMs) {
    return;
  }
  s_last_log_ms = now;
  Serial.printf(
      "perf frames=%u render_avg=%.1fms render_max=%.1fms px_avg=%u "
      "flush_avg=%.1fms compute_n=%u compute_avg=%.1fms compute_max=%.1fms\n",
      static_cast<unsigned>(s_frames), ms(s_render_us, s_frames),
      s_render_max_us / 1000.0f,
      static_cast<unsigned>(s_frames == 0 ? 0 : s_px / s_frames),
      ms(s_flush_us, s_frames), static_cast<unsigned>(s_computes),
      ms(s_compute_us, s_computes), s_compute_max_us / 1000.0f);
  s_frames = 0;
  s_render_us = 0;
  s_render_max_us = 0;
  s_px = 0;
  s_flush_us = 0;
  s_computes = 0;
  s_compute_us = 0;
  s_compute_max_us = 0;
}

}  // namespace ui::perf
