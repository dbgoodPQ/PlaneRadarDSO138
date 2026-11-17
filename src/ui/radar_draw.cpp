#include "ui/radar_draw.h"

#include <algorithm>
#include <cstdlib>

namespace ui::radar::draw {

namespace {

/** False when the box can't touch the strip being drawn: skip the draw call
 *  (LVGL would still allocate and queue a task for it). */
bool visible(const lv_layer_t* layer, int x1, int y1, int x2, int y2) {
  return onStrip(layer, lv_area_t{x1, y1, x2, y2});
}

}  // namespace

void line(lv_layer_t* layer, int x0, int y0, int x1, int y1, int width,
          uint32_t rgb) {
  if (!visible(layer, std::min(x0, x1) - width, std::min(y0, y1) - width,
               std::max(x0, x1) + width, std::max(y0, y1) + width)) {
    return;
  }
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.color = lv_color_hex(rgb);
  dsc.width = width;
  dsc.p1.x = x0;
  dsc.p1.y = y0;
  dsc.p2.x = x1;
  dsc.p2.y = y1;
  lv_draw_line(layer, &dsc);
}

void ring(lv_layer_t* layer, int cx, int cy, int radius, int width,
          uint32_t rgb) {
  if (radius <= 0 ||
      !visible(layer, cx - radius, cy - radius, cx + radius, cy + radius)) {
    return;
  }
  // Strip entirely inside the ring's hole: nothing of the ring in it.
  const lv_area_t& clip = layer->_clip_area;
  const int hole = radius - width - 1;
  if (hole > 0) {
    const int dx = std::max(std::abs(clip.x1 - cx), std::abs(clip.x2 - cx));
    const int dy = std::max(std::abs(clip.y1 - cy), std::abs(clip.y2 - cy));
    if (dx * dx + dy * dy < hole * hole) {
      return;
    }
  }
  lv_draw_arc_dsc_t dsc;
  lv_draw_arc_dsc_init(&dsc);
  dsc.color = lv_color_hex(rgb);
  dsc.width = width;
  dsc.center.x = cx;
  dsc.center.y = cy;
  dsc.radius = static_cast<uint16_t>(radius);
  dsc.start_angle = 0;
  dsc.end_angle = 360;
  lv_draw_arc(layer, &dsc);
}

void fillCircle(lv_layer_t* layer, int cx, int cy, int radius, uint32_t rgb) {
  if (!visible(layer, cx - radius, cy - radius, cx + radius, cy + radius)) {
    return;
  }
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_color_hex(rgb);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = LV_RADIUS_CIRCLE;
  const lv_area_t area = {cx - radius, cy - radius, cx + radius, cy + radius};
  lv_draw_rect(layer, &dsc, &area);
}

void fillRect(lv_layer_t* layer, int x, int y, int w, int h, uint32_t rgb) {
  if (w <= 0 || h <= 0 || !visible(layer, x, y, x + w - 1, y + h - 1)) {
    return;
  }
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = lv_color_hex(rgb);
  dsc.bg_opa = LV_OPA_COVER;
  const lv_area_t area = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(layer, &dsc, &area);
}

void fillTriangle(lv_layer_t* layer, int x0, int y0, int x1, int y1, int x2,
                  int y2, uint32_t rgb) {
  if (!visible(layer, std::min({x0, x1, x2}), std::min({y0, y1, y2}),
               std::max({x0, x1, x2}), std::max({y0, y1, y2}))) {
    return;
  }
  lv_draw_triangle_dsc_t dsc;
  lv_draw_triangle_dsc_init(&dsc);
  dsc.bg_color = lv_color_hex(rgb);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.p[0].x = x0;
  dsc.p[0].y = y0;
  dsc.p[1].x = x1;
  dsc.p[1].y = y1;
  dsc.p[2].x = x2;
  dsc.p[2].y = y2;
  lv_draw_triangle(layer, &dsc);
}

lv_point_t textSize(const char* text, const lv_font_t* font) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size;
}

lv_area_t text(lv_layer_t* layer, const char* text, const lv_font_t* font,
               uint32_t rgb, int x, int y, HAlign h, VAlign v, int bg_pad_x,
               int bg_pad_y, uint32_t bg_rgb) {
  const lv_point_t size = textSize(text, font);
  lv_area_t area;
  switch (h) {
    case HAlign::Left:
      area.x1 = x;
      break;
    case HAlign::Center:
      area.x1 = x - size.x / 2;
      break;
    case HAlign::Right:
    default:
      area.x1 = x - size.x;
      break;
  }
  switch (v) {
    case VAlign::Top:
      area.y1 = y;
      break;
    case VAlign::Middle:
      area.y1 = y - size.y / 2;
      break;
    case VAlign::Bottom:
    default:
      area.y1 = y - size.y;
      break;
  }
  area.x2 = area.x1 + size.x - 1;
  area.y2 = area.y1 + size.y - 1;
  const int pad_x = std::max(bg_pad_x, 0);
  const int pad_y = std::max(bg_pad_y, 0);
  if (!visible(layer, area.x1 - pad_x, area.y1 - pad_y, area.x2 + pad_x,
               area.y2 + pad_y)) {
    return area;
  }

  if (bg_pad_x >= 0 && bg_pad_y >= 0) {
    fillRect(layer, area.x1 - bg_pad_x, area.y1 - bg_pad_y,
             size.x + bg_pad_x * 2, size.y + bg_pad_y * 2, bg_rgb);
  }

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = font;
  dsc.color = lv_color_hex(rgb);
  dsc.text = text;
  dsc.text_local = 1;  // callers pass stack buffers; LVGL copies the text
  lv_draw_label(layer, &dsc, &area);
  return area;
}

}  // namespace ui::radar::draw
