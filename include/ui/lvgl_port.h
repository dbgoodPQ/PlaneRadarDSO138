#pragma once

namespace ui::lvgl_port {

/**
 * Create the LVGL display (flushing through LovyanGFX) and the touch input
 * device. Call once after displayInit() and touch init.
 */
void init();

/**
 * Run LVGL timers/redraws. Call often from loop(). Only call LVGL from the
 * loop task: LVGL is not thread-safe.
 */
void handle();

/**
 * Ignore the touch panel until the finger is lifted once. Use when opening a
 * screen with a long press, so the press is not taken as a tap on it.
 */
void ignoreTouchUntilRelease();

}  // namespace ui::lvgl_port
