#include "ui/radar_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/detail_panel.h"
#include "ui/perf.h"
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
// every partial-render strip so all strips show the same positions. Only the
// areas that changed are invalidated; the whole radar is redrawn when the
// background (range, units, runways, location) changes.
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
  uint32_t color;  // symbol colour (altitude band)
  // Tag block (radar-area coordinates): 3 lines, 1 (callsign only) or 0 (none
  // fits). Text is right-aligned when the tag sits left of the symbol.
  lv_area_t tag;
  uint8_t tag_lines;
  bool tag_right_align;
  // Everything drawn for this plane (symbol, vector, tag, selection ring),
  // radar-area coordinates: the area to redraw when it moves.
  lv_area_t box;
};

struct DotItem {
  int16_t x;
  int16_t y;
  uint32_t color;
  bool selected;
};

services::adsb::Aircraft s_planes[services::adsb::kMaxAircraft];
PlaneItem s_items[services::adsb::kMaxAircraft];
size_t s_item_count = 0;
DotItem s_dots[services::adsb::kMaxAircraft];
size_t s_dot_count = 0;

// What the previous frame drew, to redraw only what changed.
struct PrevPlane {
  char hex[sizeof(services::adsb::Aircraft::hex)];
  lv_area_t box;
};
PrevPlane s_prev_planes[services::adsb::kMaxAircraft];
size_t s_prev_plane_count = 0;
DotItem s_prev_dots[services::adsb::kMaxAircraft];
size_t s_prev_dot_count = 0;

/**
 * Everything the static background (grid, runways, labels) depends on. When it
 * changes, runways are recomputed and the whole radar is redrawn.
 */
struct Background {
  float outer_km;
  bool miles;
  bool runways;
  double lat;
  double lon;
  bool operator==(const Background& o) const {
    return outer_km == o.outer_km && miles == o.miles && runways == o.runways &&
           lat == o.lat && lon == o.lon;
  }
};
Background s_background = {-1.0f, false, false, 0.0, 0.0};
bool s_full_redraw = true;  // next refresh redraws the whole radar

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
bool s_range_pressed = false;  // range label drawn inverted while touched

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

lv_area_t symbolRect(const PlaneItem& it) {
  return {std::min({it.tx[0], it.tx[1], it.tx[2]}),
          std::min({it.ty[0], it.ty[1], it.ty[2]}),
          std::max({it.tx[0], it.tx[1], it.tx[2]}),
          std::max({it.ty[0], it.ty[1], it.ty[2]})};
}

/** Tag spots around the symbol, in order of preference. */
enum class Spot : uint8_t { Right, Left, AboveRight, BelowRight, AboveLeft, BelowLeft };

lv_area_t spotRect(const PlaneItem& it, Spot spot, int w, int h) {
  const int half = radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx +
                   radar::kAircraftLabelGapPx;
  constexpr int kSideOffset = 3;  // above/below spots: shift off the centre line
  int x1 = 0;
  int y1 = 0;
  switch (spot) {
    case Spot::Right:
      x1 = it.x + half;
      y1 = it.y - h / 2;
      break;
    case Spot::Left:
      x1 = it.x - half - w;
      y1 = it.y - h / 2;
      break;
    case Spot::AboveRight:
      x1 = it.x + kSideOffset;
      y1 = it.y - half - h;
      break;
    case Spot::BelowRight:
      x1 = it.x + kSideOffset;
      y1 = it.y + half;
      break;
    case Spot::AboveLeft:
      x1 = it.x - kSideOffset - w;
      y1 = it.y - half - h;
      break;
    case Spot::BelowLeft:
    default:
      x1 = it.x - kSideOffset - w;
      y1 = it.y + half;
      break;
  }
  return {x1, y1, x1 + w - 1, y1 + h - 1};
}

bool rightAligned(Spot spot) {
  return spot == Spot::Left || spot == Spot::AboveLeft || spot == Spot::BelowLeft;
}

/**
 * Place every tag without overlaps: the selected plane first, then nearest the
 * centre. Each tries 6 spots (toward the centre first); a spot must stay in the
 * area and clear of placed tags, other symbols, the gear button and the range
 * label. No spot for the full tag -> callsign only -> no tag. The selected
 * plane always gets its full tag (first spot, pulled inside the area).
 */
void placeTags() {
  const lv_font_t* font = radar::fontSmall();
  const int line_h = lv_font_get_line_height(font);

  static const lv_area_t kObstacles[] = {
      {radar::kGearMarginPx, radar::kGearMarginPx,
       radar::kGearMarginPx + radar::kGearButtonSize - 1,
       radar::kGearMarginPx + radar::kGearButtonSize - 1},
      {radar::kAreaWidth - radar::kRangeButtonWidth, 0, radar::kAreaWidth - 1,
       radar::kRangeButtonHeight - 1},
  };
  static lv_area_t placed[services::adsb::kMaxAircraft];
  size_t placed_n = 0;

  // Priority: selected, then nearest first (s_items is sorted far first).
  uint8_t order[services::adsb::kMaxAircraft];
  size_t order_n = 0;
  if (s_selected_item >= 0) {
    order[order_n++] = static_cast<uint8_t>(s_selected_item);
  }
  for (int d = static_cast<int>(s_item_count) - 1; d >= 0; --d) {
    if (d != s_selected_item) {
      order[order_n++] = static_cast<uint8_t>(d);
    }
  }

  auto fits = [&](const lv_area_t& r, size_t self) {
    if (r.x1 < 1 || r.y1 < 1 || r.x2 > radar::kAreaWidth - 2 ||
        r.y2 > radar::kAreaHeight - 2) {
      return false;
    }
    for (const lv_area_t& o : kObstacles) {
      if (radar::draw::overlaps(r, o)) {
        return false;
      }
    }
    for (size_t i = 0; i < placed_n; ++i) {
      if (radar::draw::overlaps(r, placed[i])) {
        return false;
      }
    }
    for (size_t d = 0; d < s_item_count; ++d) {
      if (d != self && radar::draw::overlaps(r, symbolRect(s_items[d]))) {
        return false;
      }
    }
    return true;
  };

  for (size_t k = 0; k < order_n; ++k) {
    const size_t d = order[k];
    PlaneItem& it = s_items[d];
    const services::adsb::Aircraft& plane = s_planes[it.index];

    int full_w = 0;
    for (const char* s : {plane.callsign, plane.type, plane.alt}) {
      if (s[0] != 0) {
        full_w = std::max(full_w, static_cast<int>(radar::draw::textSize(s, font).x));
      }
    }
    const int short_w =
        static_cast<int>(radar::draw::textSize(plane.callsign, font).x);

    const bool west = it.x < radar::kCenterX;
    const Spot spots[6] = {
        west ? Spot::Right : Spot::Left,
        west ? Spot::Left : Spot::Right,
        west ? Spot::AboveRight : Spot::AboveLeft,
        west ? Spot::BelowRight : Spot::BelowLeft,
        west ? Spot::AboveLeft : Spot::AboveRight,
        west ? Spot::BelowLeft : Spot::BelowRight,
    };

    it.tag_lines = 0;
    const bool selected = static_cast<int>(d) == s_selected_item;
    for (const uint8_t lines : {static_cast<uint8_t>(3), static_cast<uint8_t>(1)}) {
      if (lines == 1 && (selected || plane.callsign[0] == 0)) {
        break;
      }
      const int w = lines == 3 ? full_w : short_w;
      for (const Spot spot : spots) {
        const lv_area_t r = spotRect(it, spot, w, line_h * lines);
        if (fits(r, d)) {
          it.tag = r;
          it.tag_lines = lines;
          it.tag_right_align = rightAligned(spot);
          break;
        }
      }
      if (it.tag_lines != 0) {
        break;
      }
    }

    if (it.tag_lines == 0 && selected) {
      lv_area_t r = spotRect(it, spots[0], full_w, line_h * 3);
      const int dx = std::max(1 - static_cast<int>(r.x1), 0) -
                     std::max(static_cast<int>(r.x2) - (radar::kAreaWidth - 2), 0);
      const int dy = std::max(1 - static_cast<int>(r.y1), 0) -
                     std::max(static_cast<int>(r.y2) - (radar::kAreaHeight - 2), 0);
      lv_area_move(&r, dx, dy);
      it.tag = r;
      it.tag_lines = 3;
      it.tag_right_align = rightAligned(spots[0]);
    }
    if (it.tag_lines != 0) {
      placed[placed_n++] = it.tag;
    }
  }
}

/** Bounding box of everything drawn for the plane (+ antialiasing margin). */
void computeBox(PlaneItem* item) {
  const int ring = radar::kSelectRingRadiusPx + radar::kSelectRingWidthPx;
  int x1 = item->x - ring;
  int y1 = item->y - ring;
  int x2 = item->x + ring;
  int y2 = item->y + ring;
  for (int i = 0; i < 3; ++i) {
    x1 = std::min<int>(x1, item->tx[i]);
    x2 = std::max<int>(x2, item->tx[i]);
    y1 = std::min<int>(y1, item->ty[i]);
    y2 = std::max<int>(y2, item->ty[i]);
  }
  x1 = std::min<int>({x1, item->vx0, item->vx1});
  x2 = std::max<int>({x2, item->vx0, item->vx1});
  y1 = std::min<int>({y1, item->vy0, item->vy1});
  y2 = std::max<int>({y2, item->vy0, item->vy1});
  if (item->tag_lines != 0) {
    x1 = std::min<int>(x1, item->tag.x1);
    x2 = std::max<int>(x2, item->tag.x2);
    y1 = std::min<int>(y1, item->tag.y1);
    y2 = std::max<int>(y2, item->tag.y2);
  }
  // Covers the selected tag's background (2 px) plus antialiasing.
  constexpr int kPad = 3;
  item->box = {x1 - kPad, y1 - kPad, x2 + kPad, y2 + kPad};
}

lv_area_t dotBox(const DotItem& d) {
  const int r = (d.selected ? radar::kSelectDotRingRadiusPx + radar::kSelectRingWidthPx
                            : radar::kBeyondRingDotRadiusPx) +
                2;
  return {d.x - r, d.y - r, d.x + r, d.y + r};
}

uint32_t planeColor(const services::adsb::Aircraft& plane) {
  return radar::altitudeColor(plane.on_ground, plane.has_alt, plane.alt_ft);
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
      item.color = planeColor(s_planes[i]);
      continue;
    }

    DotItem dot;
    dot.color = planeColor(s_planes[i]);
    dot.selected = static_cast<int>(i) == selected_index;
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
    computeSymbol(&s_items[d], s_planes[s_items[d].index]);
    if (s_items[d].index == selected_index) {
      s_selected_item = static_cast<int>(d);
    }
  }
  placeTags();
  for (size_t d = 0; d < s_item_count; ++d) {
    computeBox(&s_items[d]);
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

/** Redraw a radar-area rectangle. */
void invalidateBox(const lv_area_t& box) {
  lv_area_t coords;
  lv_obj_get_coords(s_radar, &coords);
  lv_area_t a = box;
  lv_area_move(&a, coords.x1, coords.y1);
  lv_obj_invalidate_area(s_radar, &a);
}

bool containsDot(const DotItem* dots, size_t n, const DotItem& d) {
  for (size_t i = 0; i < n; ++i) {
    if (dots[i].x == d.x && dots[i].y == d.y && dots[i].color == d.color &&
        dots[i].selected == d.selected) {
      return true;
    }
  }
  return false;
}

/**
 * Redraw only what moved: each plane's old and new box (merged into one area,
 * matched by hex), and the rim dots that appeared or disappeared.
 */
void invalidateChanges() {
  bool prev_used[services::adsb::kMaxAircraft] = {};
  for (size_t d = 0; d < s_item_count; ++d) {
    lv_area_t box = s_items[d].box;
    const char* hex = s_planes[s_items[d].index].hex;
    for (size_t p = 0; p < s_prev_plane_count; ++p) {
      if (!prev_used[p] && hex[0] != 0 &&
          strcmp(s_prev_planes[p].hex, hex) == 0) {
        prev_used[p] = true;
        box = radar::draw::joined(box, s_prev_planes[p].box);
        break;
      }
    }
    invalidateBox(box);
  }
  for (size_t p = 0; p < s_prev_plane_count; ++p) {
    if (!prev_used[p]) {
      invalidateBox(s_prev_planes[p].box);
    }
  }
  for (size_t i = 0; i < s_dot_count; ++i) {
    if (!containsDot(s_prev_dots, s_prev_dot_count, s_dots[i])) {
      invalidateBox(dotBox(s_dots[i]));
    }
  }
  for (size_t i = 0; i < s_prev_dot_count; ++i) {
    if (!containsDot(s_dots, s_dot_count, s_prev_dots[i])) {
      invalidateBox(dotBox(s_prev_dots[i]));
    }
  }
}

void rememberFrame() {
  s_prev_plane_count = s_item_count;
  for (size_t d = 0; d < s_item_count; ++d) {
    memcpy(s_prev_planes[d].hex, s_planes[s_items[d].index].hex,
           sizeof(s_prev_planes[d].hex));
    s_prev_planes[d].box = s_items[d].box;
  }
  s_prev_dot_count = s_dot_count;
  memcpy(s_prev_dots, s_dots, sizeof(DotItem) * s_dot_count);
}

void refresh() {
  const uint32_t t0 = micros();
  const Background bg = {radar::rangeCurrent().outer_km, radar::useMiles(),
                         radar::showRunways(), services::location::lat(),
                         services::location::lon()};
  if (!(bg == s_background)) {
    s_background = bg;
    runway::update();
    s_full_redraw = true;
  }
  computeAircraft();
  updatePanel();
  perf::addCompute(micros() - t0);
  if (s_radar != nullptr) {
    if (s_full_redraw) {
      lv_obj_invalidate(s_radar);
      s_full_redraw = false;
    } else {
      invalidateChanges();
    }
    rememberFrame();
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
  radar::draw::text(
      layer, range, radar::fontLarge(),
      s_range_pressed ? radar::kColorBackground : radar::kColorLabel,
      ox + radar::kAreaWidth - 5, oy + 5, HAlign::Right, VAlign::Top,
      /*bg_pad_x=*/3, /*bg_pad_y=*/2,
      s_range_pressed ? radar::kColorRangePressedBg : radar::kColorBackground);
}

void drawPlane(lv_layer_t* layer, int ox, int oy, const PlaneItem& it) {
  if (it.vx0 != it.vx1 || it.vy0 != it.vy1) {
    radar::draw::line(layer, ox + it.vx0, oy + it.vy0, ox + it.vx1, oy + it.vy1,
                      radar::kAircraftTrackLineWidth, radar::kColorTrackVector);
  }
  radar::draw::fillTriangle(layer, ox + it.tx[0], oy + it.ty[0], ox + it.tx[1],
                            oy + it.ty[1], ox + it.tx[2], oy + it.ty[2],
                            it.color);
}

void drawTag(lv_layer_t* layer, int ox, int oy, const PlaneItem& it,
             bool selected) {
  if (it.tag_lines == 0) {
    return;
  }
  const services::adsb::Aircraft& plane = s_planes[it.index];
  const lv_font_t* font = radar::fontSmall();
  const int line_h = lv_font_get_line_height(font);
  if (selected) {
    constexpr int kBgPad = 2;
    radar::draw::fillRect(layer, ox + it.tag.x1 - kBgPad, oy + it.tag.y1 - kBgPad,
                          lv_area_get_width(&it.tag) + kBgPad * 2,
                          lv_area_get_height(&it.tag) + kBgPad * 2,
                          radar::kColorSelectedTagBg);
  }
  const HAlign h = it.tag_right_align ? HAlign::Right : HAlign::Left;
  const int x = ox + (it.tag_right_align ? it.tag.x2 + 1 : it.tag.x1);
  int y = oy + it.tag.y1;
  radar::draw::text(layer, plane.callsign, font, radar::kColorLabel, x, y, h,
                    VAlign::Top);
  if (it.tag_lines < 3) {
    return;
  }
  y += line_h;
  if (plane.type[0] != 0) {
    radar::draw::text(layer, plane.type, font, radar::kColorTagType, x, y, h,
                      VAlign::Top);
  }
  y += line_h;
  if (plane.alt[0] != 0) {
    radar::draw::text(layer, plane.alt, font, radar::kColorTagAltitude, x, y, h,
                      VAlign::Top);
  }
}

void drawAircraft(lv_layer_t* layer, int ox, int oy) {
  for (size_t d = 0; d < s_dot_count; ++d) {
    const DotItem& dot = s_dots[d];
    if (dot.selected) {
      continue;  // drawn last, on top
    }
    radar::draw::fillCircle(layer, ox + dot.x, oy + dot.y,
                            radar::kBeyondRingDotRadiusPx, dot.color);
  }

  // Planes whose box misses the strip being drawn are skipped whole.
  auto onStrip = [&](const PlaneItem& it) {
    lv_area_t a = it.box;
    lv_area_move(&a, ox, oy);
    return radar::draw::onStrip(layer, a);
  };

  for (size_t d = 0; d < s_item_count; ++d) {
    if (static_cast<int>(d) != s_selected_item && onStrip(s_items[d])) {
      drawPlane(layer, ox, oy, s_items[d]);
    }
  }
  for (size_t d = 0; d < s_item_count; ++d) {
    if (static_cast<int>(d) != s_selected_item && onStrip(s_items[d])) {
      drawTag(layer, ox, oy, s_items[d], false);
    }
  }

  // Selected plane last, on top of everything.
  if (s_selected_item >= 0) {
    const PlaneItem& it = s_items[s_selected_item];
    if (onStrip(it)) {
      drawPlane(layer, ox, oy, it);
      radar::draw::ring(layer, ox + it.x, oy + it.y, radar::kSelectRingRadiusPx,
                        radar::kSelectRingWidthPx, radar::kColorSelection);
      drawTag(layer, ox, oy, it, true);
    }
  }
  for (size_t d = 0; d < s_dot_count; ++d) {
    const DotItem& dot = s_dots[d];
    if (dot.selected) {
      radar::draw::fillCircle(layer, ox + dot.x, oy + dot.y,
                              radar::kBeyondRingDotRadiusPx, radar::kColorSelection);
      radar::draw::ring(layer, ox + dot.x, oy + dot.y,
                        radar::kSelectDotRingRadiusPx, radar::kSelectRingWidthPx,
                        radar::kColorSelection);
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

/** Range label inverted while the finger is on it. */
void onRangePress(lv_event_t* e) {
  const bool pressed = lv_event_get_code(e) == LV_EVENT_PRESSED;
  if (pressed != s_range_pressed) {
    s_range_pressed = pressed;
    lv_obj_invalidate(static_cast<lv_obj_t*>(lv_event_get_target(e)));
  }
}

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
  lv_obj_add_event_cb(range_btn, onRangePress, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(range_btn, onRangePress, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(range_btn, onRangePress, LV_EVENT_PRESS_LOST, nullptr);
  s_range_pressed = false;

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

  s_full_redraw = true;
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
