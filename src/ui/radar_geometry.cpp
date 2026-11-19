#include "ui/radar_geometry.h"

#include <cmath>

#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::radar {

namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;

}  // namespace

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  // Longitude degrees shrink toward the poles; scale by cos(latitude) so
  // east-west distance isn't overstated away from the equator.
  const float center_lat_rad =
      static_cast<float>(services::location::lat()) * kDegToRad;
  *dx_km = static_cast<float>(lon - services::location::lon()) * kKmPerDeg *
           cosf(center_lat_rad);
  *dy_km = static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

int distSqFromCenter(int x, int y) {
  const int dx = x - kCenterX;
  const int dy = y - kCenterY;
  return dx * dx + dy * dy;
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r_sq = kGridOuterRadius * kGridOuterRadius;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      break;
    }
  }
  *x1 = x0;
  *y1 = y0;
}

void clipPointOntoOuterRing(int* x, int* y) {
  const int dx = *x - kCenterX;
  const int dy = *y - kCenterY;
  const int d_sq = dx * dx + dy * dy;
  if (d_sq <= kGridOuterRadius * kGridOuterRadius || d_sq == 0) {
    return;
  }
  const float scale =
      static_cast<float>(kGridOuterRadius) / sqrtf(static_cast<float>(d_sq));
  *x = kCenterX + static_cast<int>(lroundf(static_cast<float>(dx) * scale));
  *y = kCenterY + static_cast<int>(lroundf(static_cast<float>(dy) * scale));
}

bool segmentIntersectsDisc(int x0, int y0, int x1, int y1) {
  const int r_sq = kGridOuterRadius * kGridOuterRadius;
  if (distSqFromCenter(x0, y0) <= r_sq || distSqFromCenter(x1, y1) <= r_sq) {
    return true;
  }

  // Floats: runway endpoints far outside the disc overflow int maths.
  const float dx = static_cast<float>(x1 - x0);
  const float dy = static_cast<float>(y1 - y0);
  const float fx = static_cast<float>(x0 - kCenterX);
  const float fy = static_cast<float>(y0 - kCenterY);
  const float a = dx * dx + dy * dy;
  if (a == 0.0f) {
    return false;
  }
  const float b = 2.0f * (fx * dx + fy * dy);
  const float c = fx * fx + fy * fy - static_cast<float>(r_sq);
  const float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) {
    return false;
  }
  const float root = sqrtf(disc);
  const float t0 = (-b - root) / (2.0f * a);
  const float t1 = (-b + root) / (2.0f * a);
  return (t0 >= 0.0f && t0 <= 1.0f) || (t1 >= 0.0f && t1 <= 1.0f);
}

}  // namespace ui::radar
