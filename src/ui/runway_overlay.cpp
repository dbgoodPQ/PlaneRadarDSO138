#include "ui/runway_overlay.h"

#include <cmath>
#include <cstdint>

#include "data/large_airports.h"
#include "ui/radar_draw.h"
#include "ui/radar_fonts.h"
#include "ui/radar_geometry.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::runway {
namespace {

constexpr size_t kMaxSegments = 64;
constexpr size_t kMaxAirportLabels = 16;

struct Segment {
  int16_t x0;
  int16_t y0;
  int16_t x1;
  int16_t y1;
};

struct Label {
  uint16_t airport_idx;
  int16_t x;
  int16_t y;
};

Segment s_segments[kMaxSegments];
size_t s_segment_count = 0;
Label s_labels[kMaxAirportLabels];
size_t s_label_count = 0;

/** Per-airport scratch flags for update(): bit 0 = in range, bit 1 = labelled,
 *  bit 2 = range checked. */
uint8_t s_airport_flags[data::large_airports::kAirportCount];
constexpr uint8_t kInRange = 1;
constexpr uint8_t kLabelled = 2;
constexpr uint8_t kChecked = 4;

float e7ToDeg(int32_t e7) { return static_cast<float>(e7) * 1e-7f; }

bool computeRunwaySegment(const data::large_airports::Runway& rw, Segment* out) {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  radar::latLonToScreen(e7ToDeg(rw.le_lat_e7), e7ToDeg(rw.le_lon_e7), &x0, &y0);
  radar::latLonToScreen(e7ToDeg(rw.he_lat_e7), e7ToDeg(rw.he_lon_e7), &x1, &y1);
  if (!radar::segmentIntersectsDisc(x0, y0, x1, y1)) {
    return false;
  }
  radar::clipPointToOuterRing(x0, y0, &x1, &y1);
  radar::clipPointToOuterRing(x1, y1, &x0, &y0);
  out->x0 = static_cast<int16_t>(x0);
  out->y0 = static_cast<int16_t>(y0);
  out->x1 = static_cast<int16_t>(x1);
  out->y1 = static_cast<int16_t>(y1);
  return true;
}

/** Label anchor: the airport position (pulled onto the ring), nudged outward. */
void labelAnchor(const data::large_airports::Airport& ap, int* lx, int* ly) {
  int ax = 0;
  int ay = 0;
  radar::latLonToScreen(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &ax, &ay);
  radar::clipPointOntoOuterRing(&ax, &ay);

  const int dx = ax - radar::kCenterX;
  const int dy = ay - radar::kCenterY;
  const float len = sqrtf(static_cast<float>(dx * dx + dy * dy));
  const int gap = radar::kRunwayLabelGapPx;
  if (len < 1.0f) {
    *lx = ax;
    *ly = ay - gap;
    return;
  }
  *lx = ax + static_cast<int>(lroundf(dx / len * static_cast<float>(gap)));
  *ly = ay + static_cast<int>(lroundf(dy / len * static_cast<float>(gap)));
}

}  // namespace

void update() {
  s_segment_count = 0;
  s_label_count = 0;
  if (!radar::showRunways()) {
    return;
  }
  const float radius_km = radar::fetchRadiusKm();

  for (size_t i = 0; i < data::large_airports::kAirportCount; ++i) {
    s_airport_flags[i] = 0;
  }

  for (size_t i = 0; i < data::large_airports::kRunwayCount &&
                     s_segment_count < kMaxSegments;
       ++i) {
    const auto& rw = data::large_airports::kRunways[i];
    const uint16_t ap_idx = rw.airport_idx;
    uint8_t& flags = s_airport_flags[ap_idx];
    if (!(flags & kChecked)) {
      const auto& ap = data::large_airports::kAirports[ap_idx];
      float dx_km = 0.0f;
      float dy_km = 0.0f;
      float dist_km = 0.0f;
      radar::offsetKmFromCenter(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &dx_km,
                                &dy_km, &dist_km);
      flags |= kChecked;
      if (dist_km <= radius_km) {
        flags |= kInRange;
      }
    }
    if (!(flags & kInRange)) {
      continue;
    }
    if (!computeRunwaySegment(rw, &s_segments[s_segment_count])) {
      continue;
    }
    ++s_segment_count;
    if (!(flags & kLabelled) && s_label_count < kMaxAirportLabels) {
      flags |= kLabelled;
      int lx = 0;
      int ly = 0;
      labelAnchor(data::large_airports::kAirports[ap_idx], &lx, &ly);
      s_labels[s_label_count].airport_idx = ap_idx;
      s_labels[s_label_count].x = static_cast<int16_t>(lx);
      s_labels[s_label_count].y = static_cast<int16_t>(ly);
      ++s_label_count;
    }
  }
}

void draw(lv_layer_t* layer, int ox, int oy) {
  for (size_t i = 0; i < s_segment_count; ++i) {
    const Segment& s = s_segments[i];
    radar::draw::line(layer, ox + s.x0, oy + s.y0, ox + s.x1, oy + s.y1,
                      radar::kRunwayLineWidthPx, radar::kColorRunway);
  }
  for (size_t i = 0; i < s_label_count; ++i) {
    const Label& l = s_labels[i];
    radar::draw::text(layer, data::large_airports::kAirports[l.airport_idx].ident,
                      radar::fontSmall(), radar::kColorRunwayLabel, ox + l.x,
                      oy + l.y, radar::draw::HAlign::Center,
                      radar::draw::VAlign::Bottom, /*bg_pad_x=*/2,
                      /*bg_pad_y=*/1, radar::kColorBackground);
  }
}

}  // namespace ui::runway
