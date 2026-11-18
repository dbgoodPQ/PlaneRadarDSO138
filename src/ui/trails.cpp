#include "ui/trails.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ui::trails {

namespace {

constexpr int kMinMoveUnits = 3;       // ignore fixes < 30 m from the last one

Trail s_trails[kMaxTrails];
unsigned long s_last_base_ms = 0;
uint32_t s_record_ms = 0;  // millis() of the fetch being recorded

int16_t toUnits(float km) {
  const float u = km * 100.0f;  // 10 m units
  if (u > 32767.0f) {
    return 32767;
  }
  if (u < -32767.0f) {
    return -32767;
  }
  return static_cast<int16_t>(lroundf(u));
}

Trail* slotFor(const char* hex, uint32_t now) {
  Trail* free_slot = nullptr;
  Trail* oldest = &s_trails[0];
  for (Trail& t : s_trails) {
    if (t.hex[0] != 0 && strcmp(t.hex, hex) == 0) {
      return &t;
    }
    if (t.hex[0] == 0) {
      if (free_slot == nullptr) {
        free_slot = &t;
      }
    } else if (now - t.last_seen_ms > now - oldest->last_seen_ms) {
      oldest = &t;
    }
  }
  if (free_slot == nullptr && oldest->last_seen_ms == now) {
    return nullptr;  // every slot belongs to an aircraft in this fetch
  }
  Trail* t = free_slot != nullptr ? free_slot : oldest;
  memset(t, 0, sizeof(*t));
  strncpy(t->hex, hex, sizeof(t->hex) - 1);
  return t;
}

}  // namespace

void clear() {
  memset(s_trails, 0, sizeof(s_trails));
  s_last_base_ms = 0;
}

bool beginRecord(unsigned long base_ms, uint32_t now_ms) {
  if (base_ms == 0 || base_ms == s_last_base_ms) {
    return false;
  }
  s_last_base_ms = base_ms;
  s_record_ms = now_ms;
  for (Trail& t : s_trails) {
    if (t.hex[0] != 0 && s_record_ms - t.last_seen_ms > kExpireMs) {
      memset(&t, 0, sizeof(t));
    }
  }
  return true;
}

void add(const char* hex, float east_km, float north_km) {
  if (hex == nullptr || hex[0] == 0) {
    return;
  }
  Trail* t = slotFor(hex, s_record_ms);
  if (t == nullptr) {
    return;
  }
  t->last_seen_ms = s_record_ms;
  const Point p = {toUnits(east_km), toUnits(north_km)};

  if (t->count > 0) {
    const Point& last = t->points[t->count - 1];
    if (abs(p.e - last.e) < kMinMoveUnits && abs(p.n - last.n) < kMinMoveUnits) {
      return;  // no new position (or not moving)
    }
  }
  if (t->count == kMaxPoints) {
    memmove(&t->points[0], &t->points[1], sizeof(Point) * (kMaxPoints - 1));
    --t->count;
  }
  t->points[t->count++] = p;
  ++t->version;
}

const Trail* find(const char* hex) {
  if (hex == nullptr || hex[0] == 0) {
    return nullptr;
  }
  for (const Trail& t : s_trails) {
    if (t.count > 0 && strcmp(t.hex, hex) == 0) {
      return &t;
    }
  }
  return nullptr;
}

}  // namespace ui::trails
