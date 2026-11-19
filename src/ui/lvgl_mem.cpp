#include "ui/lvgl_mem.h"

#include <esp_heap_caps.h>
#include <lvgl.h>

#include <cstdlib>

// LVGL is only called from the loop task, so the counters need no lock; the
// network task reads them for logging (32-bit reads, a stale value is fine).
namespace {

volatile size_t s_used = 0;
volatile size_t s_peak = 0;
volatile uint32_t s_failures = 0;

void track(void* p, bool add) {
  if (p == nullptr) {
    return;
  }
  const size_t n = heap_caps_get_allocated_size(p);
  if (add) {
    s_used = s_used + n;
    if (s_used > s_peak) {
      s_peak = s_used;
    }
  } else {
    s_used = s_used - n;
  }
}

}  // namespace

namespace ui::lvgl_mem {

Stats stats() { return Stats{s_used, s_peak, s_failures}; }

}  // namespace ui::lvgl_mem

// --- LVGL custom stdlib hooks (see lv_mem.h / lv_mem_core_clib.c) ---
extern "C" {

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return nullptr;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) {
  void* p = malloc(size);
  if (p == nullptr) {
    s_failures = s_failures + 1;
  }
  track(p, true);
  return p;
}

void* lv_realloc_core(void* p, size_t new_size) {
  const size_t old = p != nullptr ? heap_caps_get_allocated_size(p) : 0;
  void* q = realloc(p, new_size);
  if (q == nullptr) {
    if (new_size == 0) {
      s_used = s_used - old;  // realloc(p, 0) freed p
    } else {
      s_failures = s_failures + 1;  // p is unchanged
    }
    return nullptr;
  }
  s_used = s_used - old;
  track(q, true);
  return q;
}

void lv_free_core(void* p) {
  track(p, false);
  free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) { LV_UNUSED(mon_p); }

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

}  // extern "C"
