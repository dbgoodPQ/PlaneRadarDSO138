#pragma once

#include <cstdint>

#include <lvgl.h>

/**
 * Thin wrappers over LVGL's draw API for the radar's custom draw event.
 * Coordinates are absolute (already offset by the radar area's origin).
 */
namespace ui::radar::draw {

enum class HAlign : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

void line(lv_layer_t* layer, int x0, int y0, int x1, int y1, int width,
          uint32_t rgb);

void ring(lv_layer_t* layer, int cx, int cy, int radius, int width,
          uint32_t rgb);

void fillCircle(lv_layer_t* layer, int cx, int cy, int radius, uint32_t rgb);

void fillRect(lv_layer_t* layer, int x, int y, int w, int h, uint32_t rgb);

void fillTriangle(lv_layer_t* layer, int x0, int y0, int x1, int y1, int x2,
                  int y2, uint32_t rgb);

lv_point_t textSize(const char* text, const lv_font_t* font);

/**
 * Draw `text` with its (h, v) anchor at (x, y). If `bg_pad_x/y` >= 0, first
 * fill a background box of `bg_rgb` that far around the text. Returns the
 * text's area.
 */
lv_area_t text(lv_layer_t* layer, const char* text, const lv_font_t* font,
               uint32_t rgb, int x, int y, HAlign h, VAlign v,
               int bg_pad_x = -1, int bg_pad_y = -1, uint32_t bg_rgb = 0);

}  // namespace ui::radar::draw
