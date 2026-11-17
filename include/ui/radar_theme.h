#pragma once

#include <cstdint>

#include "config.h"

namespace ui::radar {

/** Radar area: the left 2/3 of the 320x240 panel (the right 1/3 is free). */
constexpr int kAreaWidth = (config::kDisplayWidth * 2 + 2) / 3;  // 214
constexpr int kAreaHeight = config::kDisplayHeight;              // 240
constexpr int kCenterX = kAreaWidth / 2;
constexpr int kCenterY = kAreaHeight / 2;

/** Outermost grid ring (inside the E/W labels). */
constexpr int kGridOuterRadius = 94;

/** N: offset from top edge (top_center, negative = up). */
constexpr int kCardinalNorthOffsetY = -1;
/** S: offset from bottom edge (bottom_center, positive = down). */
constexpr int kCardinalSouthOffsetY = 1;

/** Gap between scale label right edge and outer ring on the east spoke (px). */
constexpr int kScaleGapFromOuterRing = 6;

constexpr int kRingCount = 4;

/** Grid stroke width (rings and crosshair), px. */
constexpr int kGridStrokeWidth = 2;

constexpr int kCenterDotRadius = 2;

/** Filled aircraft symbol (nose triangle). */
constexpr int kAircraftNoseLenPx = 8;
constexpr int kAircraftTailLenPx = 3;
constexpr int kAircraftTailHalfPx = 4;
/** Track vector: ground distance covered in this many seconds at current gs. */
constexpr float kAircraftTrackHorizonSec = 60.0f;
/** Minimum visible vector when gs > 0 (px). */
constexpr int kAircraftSpeedLineMinPx = 2;
/** Track line length uses this outer_km, not the active range preset. */
constexpr float kAircraftTrackRefOuterKm = 13.3f;
/** Shorter than full 60 s horizon at ref scale; ×1.5 length boost applied. */
constexpr float kAircraftTrackLengthScale = 1.5f / 5.0f;
/** Speed vector stroke width (px). */
constexpr int kAircraftTrackLineWidth = 2;

constexpr int kRunwayLineWidthPx = 2;
constexpr int kRunwayLabelGapPx = 3;
/** Gap from triangle edge to tag block (px). */
constexpr int kAircraftLabelGapPx = 1;
/** Keep symbol centroid inside outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx =
    kAircraftNoseLenPx + kAircraftTailHalfPx + 1;

/** Beyond-ring traffic: bearing cues on a circle near the area edge. */
constexpr int kBeyondRingDotRadiusPx = 4;
constexpr int kBeyondRingScreenMarginPx = 2;
/** Radius of the circle the beyond-ring dots sit on. */
constexpr int kBeyondRingRimRadiusPx =
    (kCenterX < kCenterY ? kCenterX : kCenterY) - kBeyondRingScreenMarginPx;

/** Range indicator (top right of the radar area) doubles as the range button:
 *  touch area size, larger than the text for the resistive panel. */
constexpr int kRangeButtonWidth = 64;
constexpr int kRangeButtonHeight = 36;

/** Plane selection: a tap within this distance of a symbol selects it. */
constexpr int kSelectHitRadiusPx = 20;
/** Ring drawn around the selected plane. */
constexpr int kSelectRingRadiusPx = 12;
constexpr int kSelectRingWidthPx = 2;
/** Once the selected plane is gone from the feed, clear it after this long. */
constexpr unsigned long kSelectLostClearMs = 10000;

/** Detail panel: the right 1/3 of the panel. */
constexpr int kPanelX = kAreaWidth;
constexpr int kPanelWidth = config::kDisplayWidth - kAreaWidth;  // 106
/** Settings (gear) button, top-left corner of the radar area. */
constexpr int kGearButtonSize = 40;
constexpr int kGearMarginPx = 4;

/** Palette (24-bit RGB; LVGL converts to RGB565). */
constexpr uint32_t kColorBackground = 0x040A1C;
constexpr uint32_t kColorGrid = 0x106420;
constexpr uint32_t kColorLabel = 0xFFFFFF;
constexpr uint32_t kColorCenter = 0xFFFFFF;
constexpr uint32_t kColorAircraft = 0xFF0000;
constexpr uint32_t kColorTrackVector = 0xFF00FF;
constexpr uint32_t kColorTagType = 0xFFC800;
constexpr uint32_t kColorTagAltitude = 0x5AC8FF;
constexpr uint32_t kColorRunway = 0x3896AA;
/** Lighter teal for ICAO labels (vs runway lines). */
constexpr uint32_t kColorRunwayLabel = 0x6ED2E6;
constexpr uint32_t kColorSelection = 0xFFFFFF;
/** Detail panel: field names, secondary text, warnings. */
constexpr uint32_t kColorPanelDim = 0x8A96A8;
constexpr uint32_t kColorPanelText = 0xE6EAF0;
constexpr uint32_t kColorPanelWarning = 0xFFA040;

/** Selected plane's tag background; range label while pressed. */
constexpr uint32_t kColorSelectedTagBg = 0x1C2630;
constexpr uint32_t kColorRangePressedBg = 0x30D050;

/** Selected plane beyond the outer ring: ring around its rim dot. */
constexpr int kSelectDotRingRadiusPx = 8;

/** Aircraft symbol colour by altitude band (feet). Red stays free for
 *  emergencies. */
constexpr int32_t kAltLowFt = 3000;
constexpr int32_t kAltMidFt = 10000;
constexpr int32_t kAltHighFt = 25000;
constexpr uint32_t kColorAltGround = 0x9AA0A6;
constexpr uint32_t kColorAltLow = 0xFF8C1A;
constexpr uint32_t kColorAltMid = 0xFFD21A;
constexpr uint32_t kColorAltHigh = 0x3CE65A;
constexpr uint32_t kColorAltCruise = 0x3CC8FF;
constexpr uint32_t kColorAltUnknown = 0xFFFFFF;

inline uint32_t altitudeColor(bool on_ground, bool has_alt, int32_t alt_ft) {
  if (on_ground) {
    return kColorAltGround;
  }
  if (!has_alt) {
    return kColorAltUnknown;
  }
  if (alt_ft < kAltLowFt) {
    return kColorAltLow;
  }
  if (alt_ft < kAltMidFt) {
    return kColorAltMid;
  }
  if (alt_ft < kAltHighFt) {
    return kColorAltHigh;
  }
  return kColorAltCruise;
}

}  // namespace ui::radar
