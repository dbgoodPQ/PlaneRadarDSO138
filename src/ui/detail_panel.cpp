#include "ui/detail_panel.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "services/route_lookup.h"
#include "ui/radar_draw.h"
#include "ui/radar_fonts.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::detail_panel {

namespace {

constexpr int kPadX = 5;
constexpr int kPadY = 4;
constexpr float kKmPerMile = 1.609344f;

enum Row { kRowAlt, kRowVs, kRowSpd, kRowTrk, kRowDst, kRowBrg, kRowCount };
constexpr const char* kRowNames[kRowCount] = {"ALT", "V/S", "SPD",
                                              "TRK", "DST", "BRG"};

lv_obj_t* s_panel = nullptr;
lv_obj_t* s_status = nullptr;    // "Tap a plane" / "Signal lost"
lv_obj_t* s_legend = nullptr;    // altitude colours (idle panel)
lv_obj_t* s_summary = nullptr;   // traffic summary (idle panel)
Summary s_sum = {};
lv_obj_t* s_details = nullptr;   // everything below, hidden when no selection
lv_obj_t* s_callsign = nullptr;
lv_obj_t* s_type = nullptr;
lv_obj_t* s_desc = nullptr;
lv_obj_t* s_airline = nullptr;
lv_obj_t* s_from = nullptr;
lv_obj_t* s_to = nullptr;
lv_obj_t* s_values[kRowCount] = {};

lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t rgb,
                    bool wrap) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
  lv_label_set_text(label, "");
  if (wrap) {
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  }
  return label;
}

/** Cap a wrapping label at `lines` lines, ending in "..." when cut. */
void limitLines(lv_obj_t* label, int lines) {
  const lv_font_t* font = lv_obj_get_style_text_font(label, 0);
  lv_obj_set_style_max_height(label, lv_font_get_line_height(font) * lines, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

lv_obj_t* makeColumn(lv_obj_t* parent) {
  lv_obj_t* col = lv_obj_create(parent);
  lv_obj_remove_style_all(col);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(col, lv_pct(100));
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 1, 0);
  return col;
}

/** Update a label only when its text changes (avoids needless redraws). */
void setText(lv_obj_t* label, const char* text) {
  if (strcmp(lv_label_get_text(label), text) != 0) {
    lv_label_set_text(label, text);
  }
}

void setHidden(lv_obj_t* obj, bool hidden) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden) {
    if (hidden) {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

/** Text, or hide the label when empty. */
void setOptional(lv_obj_t* label, const char* text) {
  setText(label, text);
  setHidden(label, text[0] == '\0');
}

// --- Altitude legend ---
struct LegendEntry {
  uint32_t rgb;
  const char* text;
};
constexpr LegendEntry kLegend[] = {
    {radar::kColorAltCruise, "Above 25k ft"},
    {radar::kColorAltHigh, "10k - 25k ft"},
    {radar::kColorAltMid, "3k - 10k ft"},
    {radar::kColorAltLow, "Below 3k ft"},
    {radar::kColorAltGround, "Ground"},
    {radar::kColorAltUnknown, "Unknown"},
};
constexpr int kLegendCount = sizeof(kLegend) / sizeof(kLegend[0]);
constexpr int kLegendTopPad = 6;
constexpr int kLegendSwatchPx = 9;
constexpr int kLegendTextX = kLegendSwatchPx + 6;

int legendRowHeight() { return lv_font_get_line_height(radar::fontSmall()) + 3; }

void onLegendDraw(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &c);
  const lv_font_t* font = radar::fontSmall();
  const int row_h = legendRowHeight();
  int y = c.y1 + kLegendTopPad;
  radar::draw::text(layer, "Altitude", font, radar::kColorPanelDim, c.x1, y,
                    radar::draw::HAlign::Left, radar::draw::VAlign::Top);
  for (const LegendEntry& entry : kLegend) {
    y += row_h;
    const int mid = y + lv_font_get_line_height(font) / 2;
    radar::draw::fillRect(layer, c.x1, mid - kLegendSwatchPx / 2, kLegendSwatchPx,
                          kLegendSwatchPx, entry.rgb);
    radar::draw::text(layer, entry.text, font, radar::kColorPanelText,
                      c.x1 + kLegendTextX, y, radar::draw::HAlign::Left,
                      radar::draw::VAlign::Top);
  }
}

// --- Traffic summary (idle panel) ---
constexpr int kSummaryLines = 5;

int summaryRowHeight() { return lv_font_get_line_height(radar::fontSmall()) + 2; }

bool sameSummary(const Summary& a, const Summary& b) {
  return a.in_range == b.in_range && a.beyond == b.beyond &&
         a.emergencies == b.emergencies && a.has_nearest == b.has_nearest &&
         strcmp(a.nearest, b.nearest) == 0 &&
         lroundf(a.nearest_km * 10.0f) == lroundf(b.nearest_km * 10.0f);
}

void onSummaryDraw(lv_event_t* e) {
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &c);
  const lv_font_t* font = radar::fontSmall();
  const int row_h = summaryRowHeight();
  int y = c.y1;
  char buf[32];
  auto line = [&](const char* text, uint32_t rgb) {
    radar::draw::text(layer, text, font, rgb, c.x1, y, radar::draw::HAlign::Left,
                      radar::draw::VAlign::Top);
    y += row_h;
  };

  if (s_sum.in_range == 0 && s_sum.beyond == 0) {
    line("No aircraft", radar::kColorPanelDim);
    return;
  }
  snprintf(buf, sizeof(buf), "%u in range", static_cast<unsigned>(s_sum.in_range));
  line(buf, radar::kColorPanelText);
  snprintf(buf, sizeof(buf), "%u beyond", static_cast<unsigned>(s_sum.beyond));
  line(buf, radar::kColorPanelDim);
  if (s_sum.has_nearest) {
    line("Nearest", radar::kColorPanelDim);
    const bool miles = radar::useMiles();
    const float dist = miles ? s_sum.nearest_km / kKmPerMile : s_sum.nearest_km;
    snprintf(buf, sizeof(buf), "%s %.1f %s", s_sum.nearest, dist, miles ? "mi" : "km");
    line(buf, radar::kColorPanelText);
  }
  if (s_sum.emergencies > 0) {
    snprintf(buf, sizeof(buf), "! %u emergenc%s",
             static_cast<unsigned>(s_sum.emergencies),
             s_sum.emergencies == 1 ? "y" : "ies");
    line(buf, radar::kColorEmergency);
  }
}

int roundDeg(float deg) {
  int d = static_cast<int>(lroundf(deg)) % 360;
  return d < 0 ? d + 360 : d;
}

void showRoute(const services::adsb::Aircraft& plane) {
  services::route::Info info = {};
  services::route::Status status = services::route::Status::None;
  if (services::adsb::hasFlightNumber(plane)) {
    services::route::request(plane.callsign);
    status = services::route::get(plane.callsign, &info);
  }

  char from[16] = "";
  char to[40] = "";
  const char* airline = "";
  switch (status) {
    case services::route::Status::Pending:
      airline = "Looking up route...";
      break;
    case services::route::Status::Found:
      airline = info.airline;
      if (info.origin[0] != '\0') {
        snprintf(from, sizeof(from), "From %s", info.origin);
      }
      if (info.destination[0] != '\0') {
        snprintf(to, sizeof(to), "To %s %s", info.destination, info.dest_city);
      }
      break;
    case services::route::Status::NotFound:
      airline = "No route info";
      break;
    case services::route::Status::Failed:
      airline = "Route unavailable";
      break;
    case services::route::Status::None:
    default:
      break;
  }
  setOptional(s_airline, airline);
  lv_obj_set_style_text_color(
      s_airline,
      lv_color_hex(status == services::route::Status::Found
                       ? radar::kColorPanelText
                       : radar::kColorPanelDim),
      0);
  setOptional(s_from, from);
  setOptional(s_to, to);
}

}  // namespace

void create(lv_obj_t* screen) {
  s_panel = lv_obj_create(screen);
  lv_obj_remove_style_all(s_panel);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_panel, radar::kPanelX, 0);
  lv_obj_set_size(s_panel, radar::kPanelWidth, radar::kAreaHeight);
  lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(s_panel, 1, 0);
  lv_obj_set_style_border_color(s_panel, lv_color_hex(radar::kColorGrid), 0);
  lv_obj_set_style_border_opa(s_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(s_panel, kPadX, 0);
  lv_obj_set_style_pad_ver(s_panel, kPadY, 0);
  lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_panel, 4, 0);

  s_status = makeLabel(s_panel, radar::fontSmall(), radar::kColorPanelDim, true);
  lv_obj_set_style_pad_all(s_status, 2, 0);
  lv_obj_set_style_radius(s_status, 3, 0);

  // Traffic summary (idle panel only), drawn by one object.
  s_summary = lv_obj_create(s_panel);
  lv_obj_remove_style_all(s_summary);
  lv_obj_remove_flag(s_summary, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_summary, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_summary, lv_pct(100), summaryRowHeight() * kSummaryLines);
  lv_obj_add_event_cb(s_summary, onSummaryDraw, LV_EVENT_DRAW_MAIN, nullptr);

  // Altitude colour legend (idle panel only): one object that draws itself,
  // instead of ~20 widgets (saves ~2.5 KB of heap).
  s_legend = lv_obj_create(s_panel);
  lv_obj_remove_style_all(s_legend);
  lv_obj_remove_flag(s_legend, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_legend, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_legend, lv_pct(100),
                  kLegendTopPad + legendRowHeight() * (1 + kLegendCount));
  lv_obj_add_event_cb(s_legend, onLegendDraw, LV_EVENT_DRAW_MAIN, nullptr);

  s_details = makeColumn(s_panel);
  s_callsign = makeLabel(s_details, radar::fontLarge(), radar::kColorLabel, false);
  s_type = makeLabel(s_details, radar::fontCardinal(), radar::kColorTagType, false);
  s_desc = makeLabel(s_details, radar::fontSmall(), radar::kColorPanelDim, true);
  s_airline = makeLabel(s_details, radar::fontSmall(), radar::kColorPanelText, true);
  s_from = makeLabel(s_details, radar::fontSmall(), radar::kColorRunwayLabel, true);
  s_to = makeLabel(s_details, radar::fontSmall(), radar::kColorRunwayLabel, true);
  // Long names stop at two lines ("...") so the value rows always fit.
  for (lv_obj_t* l : {s_desc, s_airline, s_to}) {
    limitLines(l, 2);
  }

  lv_obj_t* sep = lv_obj_create(s_details);
  lv_obj_remove_style_all(sep);
  lv_obj_set_size(sep, lv_pct(100), 1);
  lv_obj_set_style_bg_color(sep, lv_color_hex(radar::kColorGrid), 0);
  lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
  lv_obj_set_style_margin_ver(sep, 3, 0);

  for (int r = 0; r < kRowCount; ++r) {
    lv_obj_t* row = lv_obj_create(s_details);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t* name = makeLabel(row, radar::fontSmall(), radar::kColorPanelDim, false);
    lv_label_set_text_static(name, kRowNames[r]);
    s_values[r] = makeLabel(row, radar::fontSmall(), radar::kColorPanelText, false);
  }

  s_sum = Summary{};
  showIdle(s_sum);
}

void forget() {
  s_panel = nullptr;
  s_status = nullptr;
  s_legend = nullptr;
  s_summary = nullptr;
  s_details = nullptr;
  s_callsign = nullptr;
  s_type = nullptr;
  s_desc = nullptr;
  s_airline = nullptr;
  s_from = nullptr;
  s_to = nullptr;
  for (lv_obj_t*& v : s_values) {
    v = nullptr;
  }
}

void showIdle(const Summary& summary) {
  if (s_panel == nullptr) {
    return;
  }
  if (!sameSummary(summary, s_sum)) {
    s_sum = summary;
    lv_obj_invalidate(s_summary);
  }
  setHidden(s_status, true);
  setHidden(s_summary, false);
  setHidden(s_legend, false);
  setHidden(s_details, true);
}

void show(const services::adsb::Aircraft& plane, float dist_km,
          float bearing_deg, bool lost) {
  if (s_panel == nullptr) {
    return;
  }
  // Status line: emergency banner (red box) and/or "Signal lost".
  const bool emergency = plane.emergency != services::adsb::Emergency::None;
  char status[40] = "";
  if (emergency) {
    snprintf(status, sizeof(status), "%s %s%s",
             services::adsb::emergencyLong(plane.emergency), plane.squawk,
             lost ? "\nSignal lost" : "");
  } else if (lost) {
    snprintf(status, sizeof(status), "Signal lost");
  }
  setText(s_status, status);
  lv_obj_set_style_text_color(
      s_status,
      lv_color_hex(emergency ? radar::kColorLabel : radar::kColorPanelWarning), 0);
  lv_obj_set_style_bg_color(s_status, lv_color_hex(radar::kColorEmergency), 0);
  lv_obj_set_style_bg_opa(s_status, emergency ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  setHidden(s_status, status[0] == 0);
  setHidden(s_summary, true);
  setHidden(s_legend, true);
  setHidden(s_details, false);

  setText(s_callsign, plane.callsign[0] != '\0' ? plane.callsign : plane.hex);
  setOptional(s_type, plane.type);
  // The banner takes a line: drop the aircraft name so the rows still fit.
  setOptional(s_desc, emergency ? "" : plane.desc);
  showRoute(plane);

  char buf[24];
  setText(s_values[kRowAlt], plane.alt[0] != '\0' ? plane.alt : "-");

  if (!plane.has_vrate) {
    snprintf(buf, sizeof(buf), "-");
  } else if (std::abs(plane.vrate_fpm) < radar::kVrateLevelFpm) {
    snprintf(buf, sizeof(buf), "level");
  } else {
    snprintf(buf, sizeof(buf), "%+d fpm", static_cast<int>(plane.vrate_fpm));
  }
  setText(s_values[kRowVs], buf);

  if (plane.gs_knots > 0.0f) {
    snprintf(buf, sizeof(buf), "%d kt", static_cast<int>(lroundf(plane.gs_knots)));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  setText(s_values[kRowSpd], buf);

  snprintf(buf, sizeof(buf), "%03d\xC2\xB0", roundDeg(plane.track_deg));
  setText(s_values[kRowTrk], buf);

  const bool miles = radar::useMiles();
  const float dist = miles ? dist_km / kKmPerMile : dist_km;
  snprintf(buf, sizeof(buf), dist < 100.0f ? "%.1f %s" : "%.0f %s", dist,
           miles ? "mi" : "km");
  setText(s_values[kRowDst], buf);

  snprintf(buf, sizeof(buf), "%03d\xC2\xB0", roundDeg(bearing_deg));
  setText(s_values[kRowBrg], buf);
}

}  // namespace ui::detail_panel
