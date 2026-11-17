#include "ui/radar_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "services/adsb_client.h"
#include "ui/detail_panel.h"
#include "ui/radar_draw.h"
#include "ui/radar_fonts.h"
#include "ui/radar_geometry.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace ui::radar_screen {

namespace {

using radar::draw::HAlign;
using radar::draw::VAlign;

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;

// ---------------------------------------------------------------------------
// Frame model: computed once per refresh (timer / range change), then drawn by
// every partial-render strip so all strips show the same positions.
// ---------------------------------------------------------------------------

struct PlaneItem {
  uint8_t index;  // into s_planes
  int16_t x;
  int16_t y;
  int dist_sq;
  // Speed vector (from the nose tip); x0 == x1 && y0 == y1 means none.
  int16_t vx0;
  int16_t vy0;
  int16_t vx1;
  int16_t vy1;
  // Triangle.
  int16_t tx[3];
  int16_t ty[3];
  // Tag block: left or right anchor, top line y.
  int16_t tag_x;
  int16_t tag_y;
  bool tag_left_aligned;
};

struct DotItem {
  int16_t x;
  int16_t y;
};

services::adsb::Aircraft s_planes[services::adsb::kMaxAircraft];
PlaneItem s_items[services::adsb::kMaxAircraft];
size_t s_item_count = 0;
DotItem s_dots[services::adsb::kMaxAircraft];
size_t s_dot_count = 0;

// Selected plane, tracked by ICAO hex across updates (empty = none).
char s_selected_hex[sizeof(services::adsb::Aircraft::hex)] = "";
services::adsb::Aircraft s_selected_last = {};
float s_selected_dist_km = 0.0f;
float s_selected_bearing_deg = 0.0f;
bool s_selected_seen = false;          // present in the latest snapshot
unsigned long s_selected_lost_ms = 0;  // when it dropped out (0 = present)
int s_selected_item = -1;              // into s_items, -1 if not drawn

lv_obj_t* s_screen = nullptr;
lv_obj_t* s_radar = nullptr;
lv_timer_t* s_timer = nullptr;

bool s_settings_requested = false;

/**
 * Dead-reckon an aircraft's position from its last-fetched fix along its ground
 * track. Uses the same flat 1° ≈ 111 km projection as offsetKmFromCenter().
 */
void extrapolatedLatLon(const services::adsb::Aircraft& plane,
                        unsigned long base_ms, float* lat, float* lon) {
  *lat = plane.lat;
  *lon = plane.lon;
  if (base_ms == 0 || plane.gs_knots <= 0.0f) {
    return;
  }
  // Elapsed since the fix was measured = time since fetch + the fix's own age.
  const unsigned long elapsed_ms = (millis() - base_ms) + plane.pos_age_ms;
  const float elapsed_h = static_cast<float>(elapsed_ms) / 3600000.0f;
  const float dist_km = plane.gs_knots * 1.852f * elapsed_h;  // knots -> km
  if (dist_km <= 0.0f) {
    return;
  }
  const float rad = plane.track_deg * kDegToRad;  // track: 0 = N, 90 = E
  *lat = plane.lat + (dist_km * cosf(rad)) / kKmPerDeg;
  *lon = plane.lon + (dist_km * sinf(rad)) / kKmPerDeg;
}

float innerRingMaxKm() {
  return radar::rangeCurrent().outer_km *
         (static_cast<float>(radar::kGridOuterRadius -
                             radar::kAircraftInsideRingInsetPx) /
          static_cast<float>(radar::kGridOuterRadius));
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }
  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px = gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
                   radar::kAircraftTrackRefOuterKm *
                   radar::kAircraftTrackLengthScale;
  const int len = static_cast<int>(px + 0.5f);
  return std::max(len, radar::kAircraftSpeedLineMinPx);
}

void computeSymbol(PlaneItem* item, const services::adsb::Aircraft& plane) {
  const int cx = item->x;
  const int cy = item->y;
  const float rad = plane.nose_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  const int tip_x = cx + static_cast<int>(lroundf(sin_h * radar::kAircraftNoseLenPx));
  const int tip_y = cy - static_cast<int>(lroundf(cos_h * radar::kAircraftNoseLenPx));
  const int base_x = cx - static_cast<int>(lroundf(sin_h * radar::kAircraftTailLenPx));
  const int base_y = cy + static_cast<int>(lroundf(cos_h * radar::kAircraftTailLenPx));
  const int wing_x = static_cast<int>(lroundf(cos_h * radar::kAircraftTailHalfPx));
  const int wing_y = static_cast<int>(lroundf(sin_h * radar::kAircraftTailHalfPx));

  item->tx[0] = tip_x;
  item->ty[0] = tip_y;
  item->tx[1] = base_x + wing_x;
  item->ty[1] = base_y + wing_y;
  item->tx[2] = base_x - wing_x;
  item->ty[2] = base_y - wing_y;

  item->vx0 = item->vx1 = tip_x;
  item->vy0 = item->vy1 = tip_y;
  const int len = speedLineLengthPx(plane.gs_knots);
  if (len > 0) {
    const float track_rad = plane.track_deg * kDegToRad;
    int ex = tip_x + static_cast<int>(lroundf(sinf(track_rad) * len));
    int ey = tip_y - static_cast<int>(lroundf(cosf(track_rad) * len));
    radar::clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
    item->vx1 = ex;
    item->vy1 = ey;
  }
}

void computeTag(PlaneItem* item, const services::adsb::Aircraft& plane) {
  const lv_font_t* font = radar::fontSmall();
  const int line_h = lv_font_get_line_height(font);
  int block_w = 0;
  for (const char* s : {plane.callsign, plane.type, plane.alt}) {
    if (s[0] != '\0') {
      block_w = std::max(block_w, static_cast<int>(radar::draw::textSize(s, font).x));
    }
  }
  const int block_h = line_h * 3;
  const int symbol_half = radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;

  // West half: tag to the right (toward center); east half: to the left.
  item->tag_left_aligned = item->x < radar::kCenterX;
  int anchor_x = 0;
  if (item->tag_left_aligned) {
    anchor_x = item->x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kAreaWidth - block_w - 1);
  } else {
    anchor_x = item->x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
  }
  int top = item->y - block_h / 2;
  top = std::max(1, std::min(top, radar::kAreaHeight - block_h - 1));
  item->tag_x = anchor_x;
  item->tag_y = top;
}

/** Rim dot on the true bearing for traffic beyond the outer ring. */
bool beyondRingDot(float dx_km, float dy_km, float dist_km, DotItem* out) {
  if (dist_km < 0.01f) {
    return false;
  }
  const float angle_rad = atan2f(dx_km, dy_km);
  const int rim_r = radar::kBeyondRingRimRadiusPx;
  out->x = radar::kCenterX + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  out->y = radar::kCenterY - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void computeAircraft() {
  // Snapshot under the adsb lock (the fetch runs on another task).
  unsigned long base_ms = 0;
  const size_t n = services::adsb::snapshotAircraft(
      s_planes, services::adsb::kMaxAircraft, &base_ms);
  const float inner_max_km = innerRingMaxKm();

  s_item_count = 0;
  s_dot_count = 0;
  s_selected_seen = false;
  s_selected_item = -1;
  int selected_index = -1;

  for (size_t i = 0; i < n; ++i) {
    float lat = 0.0f;
    float lon = 0.0f;
    extrapolatedLatLon(s_planes[i], base_ms, &lat, &lon);

    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    radar::offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

    if (s_selected_hex[0] != '\0' &&
        strcmp(s_planes[i].hex, s_selected_hex) == 0) {
      s_selected_seen = true;
      selected_index = static_cast<int>(i);
      s_selected_last = s_planes[i];
      s_selected_dist_km = dist_km;
      s_selected_bearing_deg = atan2f(dx_km, dy_km) / kDegToRad;
    }

    if (dist_km <= inner_max_km) {
      int x = 0;
      int y = 0;
      radar::latLonToScreen(lat, lon, &x, &y);
      PlaneItem& item = s_items[s_item_count++];
      item.index = static_cast<uint8_t>(i);
      item.x = x;
      item.y = y;
      item.dist_sq = radar::distSqFromCenter(x, y);
      continue;
    }

    DotItem dot;
    if (beyondRingDot(dx_km, dy_km, dist_km, &dot)) {
      s_dots[s_dot_count++] = dot;
    }
  }
  // Rim dots all sit on one circle, so their draw order doesn't matter.

  // Far first, so nearer aircraft draw on top.
  std::sort(s_items, s_items + s_item_count,
            [](const PlaneItem& a, const PlaneItem& b) {
              return a.dist_sq > b.dist_sq;
            });
  for (size_t d = 0; d < s_item_count; ++d) {
    const services::adsb::Aircraft& plane = s_planes[s_items[d].index];
    computeSymbol(&s_items[d], plane);
    computeTag(&s_items[d], plane);
    if (s_items[d].index == selected_index) {
      s_selected_item = static_cast<int>(d);
    }
  }
}

void clearSelection() {
  s_selected_hex[0] = '\0';
  s_selected_seen = false;
  s_selected_lost_ms = 0;
  s_selected_item = -1;
}

void updatePanel() {
  if (s_selected_hex[0] == '\0') {
    detail_panel::showHint();
    return;
  }
  if (s_selected_seen) {
    s_selected_lost_ms = 0;
  } else {
    if (s_selected_lost_ms == 0) {
      s_selected_lost_ms = millis();
    }
    if (millis() - s_selected_lost_ms >= radar::kSelectLostClearMs) {
      clearSelection();
      detail_panel::showHint();
      return;
    }
  }
  detail_panel::show(s_selected_last, s_selected_dist_km,
                     s_selected_bearing_deg, !s_selected_seen);
}

void refresh() {
  runway::update();
  computeAircraft();
  updatePanel();
  if (s_radar != nullptr) {
    lv_obj_invalidate(s_radar);
  }
}

// ---------------------------------------------------------------------------
// Drawing (LV_EVENT_DRAW_MAIN, once per partial-render strip)
// ---------------------------------------------------------------------------

void drawGrid(lv_layer_t* layer, int ox, int oy) {
  const int cx = ox + radar::kCenterX;
  const int cy = oy + radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  for (int i = 1; i <= radar::kRingCount; ++i) {
    radar::draw::ring(layer, cx, cy, (r * i) / radar::kRingCount,
                      radar::kGridStrokeWidth, radar::kColorGrid);
  }
  radar::draw::line(layer, cx, cy - r, cx, cy + r, radar::kGridStrokeWidth,
                    radar::kColorGrid);
  radar::draw::line(layer, cx - r, cy, cx + r, cy, radar::kGridStrokeWidth,
                    radar::kColorGrid);
}

void drawLabels(lv_layer_t* layer, int ox, int oy) {
  const int cx = ox + radar::kCenterX;
  const int cy = oy + radar::kCenterY;

  radar::draw::fillCircle(layer, cx, cy, radar::kCenterDotRadius,
                          radar::kColorCenter);

  const lv_font_t* cardinal = radar::fontCardinal();
  radar::draw::text(layer, "N", cardinal, radar::kColorLabel, cx,
                    oy + radar::kCardinalNorthOffsetY, HAlign::Center, VAlign::Top);
  radar::draw::text(layer, "S", cardinal, radar::kColorLabel, cx,
                    oy + radar::kAreaHeight + radar::kCardinalSouthOffsetY,
                    HAlign::Center, VAlign::Bottom);
  radar::draw::text(layer, "W", cardinal, radar::kColorLabel, ox, cy,
                    HAlign::Left, VAlign::Middle);
  radar::draw::text(layer, "E", cardinal, radar::kColorLabel,
                    ox + radar::kAreaWidth, cy, HAlign::Right, VAlign::Middle);

  // Ring-3 distance on the east spoke, just inside the outer ring.
  char scale[12];
  radar::formatCurrentRing3Label(scale, sizeof(scale));
  radar::draw::text(layer, scale, radar::fontSmall(), radar::kColorGrid,
                    cx + radar::kGridOuterRadius - radar::kScaleGapFromOuterRing,
                    cy, HAlign::Right, VAlign::Middle, /*bg_pad_x=*/3,
                    /*bg_pad_y=*/2, radar::kColorBackground);

  // Range indicator, top right of the radar area.
  const bool miles = radar::useMiles();
  const float ring3_km = radar::rangeCurrent().ring3_km;
  const int distance = static_cast<int>(lroundf(miles ? ring3_km / 1.609344f : ring3_km));
  char range[16];
  snprintf(range, sizeof(range), "%d %s", distance, miles ? "mi" : "km");
  radar::draw::text(layer, range, radar::fontLarge(), radar::kColorLabel,
                    ox + radar::kAreaWidth - 5, oy + 5, HAlign::Right,
                    VAlign::Top, /*bg_pad_x=*/3, /*bg_pad_y=*/2,
                    radar::kColorBackground);
}

void drawAircraft(lv_layer_t* layer, int ox, int oy) {
  for (size_t d = 0; d < s_dot_count; ++d) {
    radar::draw::fillCircle(layer, ox + s_dots[d].x, oy + s_dots[d].y,
                            radar::kBeyondRingDotRadiusPx, radar::kColorAircraft);
  }

  for (size_t d = 0; d < s_item_count; ++d) {
    const PlaneItem& it = s_items[d];
    if (it.vx0 != it.vx1 || it.vy0 != it.vy1) {
      radar::draw::line(layer, ox + it.vx0, oy + it.vy0, ox + it.vx1,
                        oy + it.vy1, radar::kAircraftTrackLineWidth,
                        radar::kColorTrackVector);
    }
    radar::draw::fillTriangle(layer, ox + it.tx[0], oy + it.ty[0], ox + it.tx[1],
                              oy + it.ty[1], ox + it.tx[2], oy + it.ty[2],
                              radar::kColorAircraft);
  }

  if (s_selected_item >= 0) {
    const PlaneItem& it = s_items[s_selected_item];
    radar::draw::ring(layer, ox + it.x, oy + it.y, radar::kSelectRingRadiusPx,
                      radar::kSelectRingWidthPx, radar::kColorSelection);
  }

  const lv_font_t* font = radar::fontSmall();
  const int line_h = lv_font_get_line_height(font);
  for (size_t d = 0; d < s_item_count; ++d) {
    const PlaneItem& it = s_items[d];
    const services::adsb::Aircraft& plane = s_planes[it.index];
    const HAlign h = it.tag_left_aligned ? HAlign::Left : HAlign::Right;
    const int x = ox + it.tag_x;
    int y = oy + it.tag_y;
    if (plane.callsign[0] != '\0') {
      radar::draw::text(layer, plane.callsign, font, radar::kColorLabel, x, y, h,
                        VAlign::Top);
    }
    y += line_h;
    if (plane.type[0] != '\0') {
      radar::draw::text(layer, plane.type, font, radar::kColorTagType, x, y, h,
                        VAlign::Top);
    }
    y += line_h;
    if (plane.alt[0] != '\0') {
      radar::draw::text(layer, plane.alt, font, radar::kColorTagAltitude, x, y, h,
                        VAlign::Top);
    }
  }
}

void onDraw(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t coords;
  lv_obj_get_coords(s_radar, &coords);
  const int ox = coords.x1;
  const int oy = coords.y1;
  drawGrid(layer, ox, oy);
  runway::draw(layer, ox, oy);
  drawLabels(layer, ox, oy);
  drawAircraft(layer, ox, oy);
}

// ---------------------------------------------------------------------------
// Input and lifecycle
// ---------------------------------------------------------------------------

/** Select the plane symbol nearest to the tap, or clear the selection. */
void selectAt(int x, int y) {
  int best = -1;
  int best_d_sq = radar::kSelectHitRadiusPx * radar::kSelectHitRadiusPx;
  for (size_t d = 0; d < s_item_count; ++d) {
    const int dx = s_items[d].x - x;
    const int dy = s_items[d].y - y;
    const int d_sq = dx * dx + dy * dy;
    if (d_sq <= best_d_sq) {
      best_d_sq = d_sq;
      best = static_cast<int>(d);
    }
  }
  if (best < 0) {
    clearSelection();
  } else {
    const services::adsb::Aircraft& plane = s_planes[s_items[best].index];
    strncpy(s_selected_hex, plane.hex, sizeof(s_selected_hex) - 1);
    s_selected_hex[sizeof(s_selected_hex) - 1] = '\0';
    s_selected_lost_ms = 0;
    Serial.printf("Selected %s (%s)\n", plane.callsign, plane.hex);
  }
  refresh();
}

void onRadarClicked(lv_event_t*) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t coords;
  lv_obj_get_coords(s_radar, &coords);
  selectAt(p.x - coords.x1, p.y - coords.y1);
}

void onRangeClicked(lv_event_t*) { nextRange(); }

// Handled from loop(): opening settings deletes this screen.
void onGearClicked(lv_event_t*) { s_settings_requested = true; }

void onTimer(lv_timer_t*) { refresh(); }

void onScreenDeleted(lv_event_t*) {
  if (s_timer != nullptr) {
    lv_timer_delete(s_timer);
  }
  s_timer = nullptr;
  s_radar = nullptr;
  detail_panel::forget();
  s_screen = nullptr;
}

}  // namespace

void open() {
  if (s_screen != nullptr) {
    return;
  }
  s_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s_screen);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(radar::kColorBackground), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, onScreenDeleted, LV_EVENT_DELETE, nullptr);

  s_radar = lv_obj_create(s_screen);
  lv_obj_remove_style_all(s_radar);
  lv_obj_set_pos(s_radar, 0, 0);
  lv_obj_set_size(s_radar, radar::kAreaWidth, radar::kAreaHeight);
  lv_obj_remove_flag(s_radar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_radar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_radar, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(s_radar, onRadarClicked, LV_EVENT_CLICKED, nullptr);

  // Invisible touch target over the range indicator (drawn by onDraw).
  lv_obj_t* range_btn = lv_obj_create(s_radar);
  lv_obj_remove_style_all(range_btn);
  lv_obj_remove_flag(range_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(range_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(range_btn, radar::kRangeButtonWidth, radar::kRangeButtonHeight);
  lv_obj_align(range_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(range_btn, onRangeClicked, LV_EVENT_CLICKED, nullptr);

  detail_panel::create(s_screen);

  // Settings button, top-left corner of the radar area (on top of the radar,
  // so taps on it never select a plane).
  lv_obj_t* gear = lv_button_create(s_screen);
  lv_obj_set_size(gear, radar::kGearButtonSize, radar::kGearButtonSize);
  lv_obj_set_pos(gear, radar::kGearMarginPx, radar::kGearMarginPx);
  lv_obj_set_style_bg_color(gear, lv_color_hex(0x1C2630), 0);
  lv_obj_set_style_bg_opa(gear, LV_OPA_80, 0);
  lv_obj_set_style_shadow_width(gear, 0, 0);
  lv_obj_set_style_radius(gear, 8, 0);
  lv_obj_set_ext_click_area(gear, 6);
  lv_obj_add_event_cb(gear, onGearClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* icon = lv_label_create(gear);
  lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
  lv_obj_center(icon);

  s_settings_requested = false;

  refresh();
  s_timer = lv_timer_create(onTimer, config::kRadarRedrawIntervalMs, nullptr);

  lv_obj_t* old = lv_screen_active();
  lv_screen_load(s_screen);
  if (old != nullptr && old != s_screen) {
    lv_obj_delete(old);
  }
}

bool isOpen() { return s_screen != nullptr; }

void nextRange() {
  radar::rangeNext();
  char label[12];
  radar::formatCurrentRing3Label(label, sizeof(label));
  Serial.printf("Range: %s\n", label);
  refresh();
}

bool takeSettingsRequest() {
  const bool requested = s_settings_requested;
  s_settings_requested = false;
  return requested;
}

}  // namespace ui::radar_screen
