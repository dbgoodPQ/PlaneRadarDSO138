#pragma once

#include <cstddef>
#include <cstdint>

/**
 * LVGL's allocator (LV_STDLIB_CUSTOM in lv_conf.h): plain malloc/free on the
 * ESP32 heap, with byte counting so LVGL's share of the heap can be measured
 * (used to size a dedicated LVGL pool later).
 */
namespace ui::lvgl_mem {

struct Stats {
  size_t used;        // bytes LVGL holds now
  size_t peak;        // most LVGL ever held at once
  uint32_t failures;  // allocations that returned NULL
};

Stats stats();

}  // namespace ui::lvgl_mem
