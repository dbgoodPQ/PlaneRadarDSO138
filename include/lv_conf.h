/**
 * LVGL 9.2 configuration for Plane Radar (CYD, 320x240, no PSRAM).
 * Anything not set here takes LVGL's default (see lv_conf_internal.h).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ---- Color ---- */
#define LV_COLOR_DEPTH 16

/* ---- Memory: use the ESP32 heap instead of a fixed static pool, so LVGL
 *      only takes what the current screen needs. ---- */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/* ---- Timing ---- */
#define LV_DEF_REFR_PERIOD 20 /* ms */
#define LV_DPI_DEF 130

/* ---- OS: all LVGL calls happen on the Arduino loop task ---- */
#define LV_USE_OS LV_OS_NONE

/* ---- Logging / asserts ---- */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* ---- Fonts ---- */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ---- Theme ---- */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0 /* no "grow on press": avoids redraw cost */

/* ---- Text area: show each typed password character briefly ---- */
#define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1000

/* ---- Not used ---- */
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_FILE_EXPLORER 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_MENU 0
#define LV_USE_SPAN 0
#define LV_USE_LOTTIE 0
#define LV_USE_ANIMIMG 0

#endif /* LV_CONF_H */
