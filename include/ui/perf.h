#pragma once

#include <cstdint>

/**
 * Render performance counters (loop task only). logIfDue() prints one line
 * every 30 s and resets the counters:
 *   perf frames=N render_avg/max (ms, LVGL refresh incl. flush)
 *        px_avg (pixels flushed per frame) flush_avg (ms)
 *        compute_avg/max (ms, radar model update)
 */
namespace ui::perf {

/** Hook LVGL's refresh start/end events on the display. */
void attach(void* display);

/** Called by the flush callback: pixels sent and time spent sending. */
void addFlush(uint32_t pixels, uint32_t us);

/** Time spent recomputing the radar model (runways, aircraft, panel). */
void addCompute(uint32_t us);

/** Print and reset the counters when 30 s have passed. */
void logIfDue();

}  // namespace ui::perf
