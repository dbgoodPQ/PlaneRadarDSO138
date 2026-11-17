#include "ui/detail_panel.h"

#include <cmath>
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

enum Row { kRowAlt, kRowSpd, kRowTrk, kRowDst, kRowBrg, kRowCount };
constexpr const char* kRowNames[kRowCount] = {"ALT", "SPD", "TRK", "DST", "BRG"};

lv_obj_t* s_panel = nullptr;
lv_obj_t* s_status = nullptr;    // "Tap a plane" / "Signal lost"
lv_obj_t* s_legend = nullptr;    // altitude colours, shown with the hint
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

  showHint();
}

void forget() {
  s_panel = nullptr;
  s_status = nullptr;
  s_legend = nullptr;
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

void showHint() {
  if (s_panel == nullptr) {
    return;
  }
  setText(s_status, "Tap a plane to see its details.\n\nTap the range to zoom.");
  lv_obj_set_style_text_color(s_status, lv_color_hex(radar::kColorPanelDim), 0);
  setHidden(s_status, false);
  setHidden(s_legend, false);
  setHidden(s_details, true);
}

void show(const services::adsb::Aircraft& plane, float dist_km,
          float bearing_deg, bool lost) {
  if (s_panel == nullptr) {
    return;
  }
  setText(s_status, "Signal lost");
  lv_obj_set_style_text_color(s_status, lv_color_hex(radar::kColorPanelWarning), 0);
  setHidden(s_status, !lost);
  setHidden(s_legend, true);
  setHidden(s_details, false);

  setText(s_callsign, plane.callsign[0] != '\0' ? plane.callsign : plane.hex);
  setOptional(s_type, plane.type);
  setOptional(s_desc, plane.desc);
  showRoute(plane);

  char buf[24];
  setText(s_values[kRowAlt], plane.alt[0] != '\0' ? plane.alt : "-");

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
