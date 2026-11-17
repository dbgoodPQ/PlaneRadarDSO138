#include "ui/lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/touch.h"

namespace ui::lvgl_port {

namespace {

/** Partial render buffer: 20 lines (12.8 KB), DMA-capable internal RAM.
 *  Halved from 40 to leave the heap room for TLS. */
constexpr int kBufferLines = 20;

lv_display_t* s_display = nullptr;
lv_indev_t* s_touch = nullptr;
bool s_ignore_touch = false;
int16_t s_last_x = 0;
int16_t s_last_y = 0;
uint32_t s_last_pressed_ms = 0;
bool s_was_pressed = false;

uint32_t tickMs() { return millis(); }

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  const int32_t w = lv_area_get_width(area);
  const int32_t h = lv_area_get_height(area);
  // LVGL renders native little-endian RGB565; rgb565_t tells LovyanGFX so it
  // byte-swaps for the ILI9341.
  tft.startWrite();
  tft.pushImageDMA(area->x1, area->y1, w, h,
                   reinterpret_cast<const lgfx::rgb565_t*>(px_map));
  tft.waitDMA();  // LVGL reuses the buffer as soon as flush_ready is called
  tft.endWrite();
  lv_display_flush_ready(display);
}

void readTouch(lv_indev_t*, lv_indev_data_t* data) {
  int16_t x;
  int16_t y;
  bool pressed = hardware::touch::read(&x, &y);
  const uint32_t now = millis();
  if (pressed) {
    s_last_x = x;
    s_last_y = y;
    s_last_pressed_ms = now;
    s_was_pressed = true;
  } else if (s_was_pressed &&
             now - s_last_pressed_ms < config::kTouchDropoutMs) {
    // Resistive panels drop out briefly under a steady finger: bridge the gap
    // at the last point so a long press isn't broken into several taps.
    pressed = true;
    x = s_last_x;
    y = s_last_y;
  } else {
    s_was_pressed = false;
  }
  if (s_ignore_touch) {
    if (!pressed) {
      s_ignore_touch = false;
    }
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (pressed) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;  // LVGL keeps the last point
  }
}

}  // namespace

void init() {
  lv_init();
  lv_tick_set_cb(tickMs);

  s_display = lv_display_create(config::kDisplayWidth, config::kDisplayHeight);
  const size_t buf_bytes =
      config::kDisplayWidth * kBufferLines * sizeof(uint16_t);
  void* buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (buf == nullptr) {
    Serial.println("lvgl: draw buffer alloc failed");
    return;
  }
  lv_display_set_buffers(s_display, buf, nullptr, buf_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_display, flush);

  s_touch = lv_indev_create();
  lv_indev_set_type(s_touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s_touch, readTouch);
  // Resistive panels jitter: a slightly larger drag threshold keeps taps on
  // list rows from turning into tiny scrolls.
  lv_indev_set_scroll_limit(s_touch, 12);

  lv_theme_t* theme = lv_theme_default_init(
      s_display, lv_palette_main(LV_PALETTE_GREEN),
      lv_palette_main(LV_PALETTE_LIGHT_BLUE), /*dark=*/true,
      &lv_font_montserrat_14);
  lv_display_set_theme(s_display, theme);
}

void handle() { lv_timer_handler(); }

void ignoreTouchUntilRelease() { s_ignore_touch = true; }

}  // namespace ui::lvgl_port
