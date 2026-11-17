#include "ui/settings_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/config_store.h"
#include "services/geocode.h"
#include "services/network_task.h"
#include "services/radar_location.h"
#include "services/settings.h"
#include "services/wifi_conn.h"
#include "ui/radar_fonts.h"

namespace ui::settings_screen {

namespace {

namespace st = services::settings;
namespace geo = services::geocode;

// --- Layout (320x240 landscape), same metrics as the Wi-Fi screen. ---
constexpr int32_t kScreenW = 320;
constexpr int32_t kScreenH = 240;
constexpr int32_t kHeaderH = 44;
constexpr int32_t kHeaderBtnW = 64;
constexpr int32_t kHeaderSearchBtnW = 80;
constexpr int32_t kRowH = 46;
constexpr int32_t kFieldH = 40;
constexpr int32_t kPad = 4;
constexpr uint32_t kRowColor = 0x1C2630;
constexpr uint32_t kDimColor = 0x9AA4B2;

enum class View : uint8_t { None, Menu, Address, Results };

bool s_open = false;
lv_obj_t* s_screen = nullptr;
View s_view = View::None;
View s_pending_view = View::None;  // switches are deferred to update()
Exit s_exit = Exit::None;

// Address entry / results
char s_query[geo::kQueryMaxLen + 1] = "";
lv_obj_t* s_field = nullptr;
lv_obj_t* s_search_btn = nullptr;
lv_obj_t* s_list = nullptr;
geo::Status s_shown_status = geo::Status::None;
geo::Result s_results[geo::kMaxResults];
size_t s_result_count = 0;

// Menu
lv_obj_t* s_sd_label = nullptr;
services::config_store::SdState s_shown_sd = services::config_store::SdState::NoCard;

void requestView(View v) { s_pending_view = v; }

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

/** Top bar: left button, centered title, optional right button. */
void makeHeader(const char* title, lv_event_cb_t back_cb, const char* right_text,
                lv_event_cb_t right_cb, lv_obj_t** right_out = nullptr) {
  lv_obj_t* bar = lv_obj_create(s_screen);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, kScreenW, kHeaderH);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x101820), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  const int32_t btn_h = kHeaderH - 2 * kPad;
  lv_obj_t* back = makeButton(bar, LV_SYMBOL_LEFT, back_cb);
  lv_obj_set_size(back, kHeaderBtnW, btn_h);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, kPad, 0);

  lv_obj_t* right = nullptr;
  if (right_text != nullptr) {
    right = makeButton(bar, right_text, right_cb);
    lv_obj_set_size(right, kHeaderSearchBtnW, btn_h);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -kPad, 0);
  }
  if (right_out != nullptr) {
    *right_out = right;
  }

  lv_obj_t* label = lv_label_create(bar);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

lv_obj_t* makeBody() {
  lv_obj_t* body = lv_obj_create(s_screen);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, kScreenW, kScreenH - kHeaderH);
  lv_obj_set_pos(body, 0, kHeaderH);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(body, kPad, 0);
  lv_obj_set_style_pad_row(body, kPad, 0);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  return body;
}

/** Plain row container: title on the left, a control on the right. */
lv_obj_t* makeRow(lv_obj_t* parent, const char* title) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), kRowH);
  lv_obj_set_style_bg_color(row, lv_color_hex(kRowColor), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_set_style_pad_hor(row, 10, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, title);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  return row;
}

/** Tappable row: title, a dim value line under it, and a ">" on the right. */
void makeNavRow(lv_obj_t* parent, const char* title, const char* value,
                lv_event_cb_t cb) {
  lv_obj_t* row = lv_button_create(parent);
  lv_obj_set_size(row, lv_pct(100), kRowH);
  lv_obj_set_style_bg_color(row, lv_color_hex(kRowColor), 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_set_style_pad_hor(row, 10, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* t = lv_label_create(row);
  lv_label_set_text(t, title);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 2);

  lv_obj_t* v = lv_label_create(row);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_font(v, radar::fontSmall(), 0);  // Latin-1 (accents)
  lv_obj_set_style_text_color(v, lv_color_hex(kDimColor), 0);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
  lv_obj_set_width(v, kScreenW - 2 * kPad - 50);
  lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, -3);

  lv_obj_t* arrow = lv_label_create(row);
  lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
}

const char* sdText(services::config_store::SdState s) {
  switch (s) {
    case services::config_store::SdState::Ok:
      return "SD card: settings saved to /planeradar.json";
    case services::config_store::SdState::ReadError:
      return "SD card: /planeradar.json unreadable (fixed on next change)";
    case services::config_store::SdState::WriteError:
      return "SD card: write failed";
    case services::config_store::SdState::NoCard:
    default:
      return "SD card: not found (settings in internal memory)";
  }
}

void refreshSdLabel() {
  if (s_sd_label == nullptr) {
    return;
  }
  const auto state = services::config_store::state();
  if (state != s_shown_sd || lv_label_get_text(s_sd_label)[0] == '\0') {
    s_shown_sd = state;
    lv_label_set_text(s_sd_label, sdText(state));
  }
}

// --------------------------------------------------------------------------
// Event handlers
// --------------------------------------------------------------------------

void onBackToRadar(lv_event_t*) { s_exit = Exit::Radar; }
void onToMenu(lv_event_t*) { requestView(View::Menu); }
void onToAddress(lv_event_t*) { requestView(View::Address); }
void onWifi(lv_event_t*) { s_exit = Exit::Wifi; }

void onUnits(lv_event_t* e) {
  lv_obj_t* bm = static_cast<lv_obj_t*>(lv_event_get_target(e));
  st::setUseMiles(lv_buttonmatrix_get_selected_button(bm) == 1);
  refreshSdLabel();
}

void onRunways(lv_event_t* e) {
  lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  st::setShowRunways(lv_obj_has_state(sw, LV_STATE_CHECKED));
  refreshSdLabel();
}

void onGround(lv_event_t* e) {
  lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  st::setShowGroundAircraft(lv_obj_has_state(sw, LV_STATE_CHECKED));
  refreshSdLabel();
}

void onBrightness(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const int32_t pct = lv_slider_get_value(slider);
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    tft.setBrightness(static_cast<uint8_t>(pct * 255 / 100));  // live preview
  } else {
    st::setBrightnessPct(static_cast<uint8_t>(pct));  // save once, on release
    refreshSdLabel();
  }
}

void onFieldChanged(lv_event_t*) {
  if (s_search_btn == nullptr || s_field == nullptr) {
    return;
  }
  if (lv_textarea_get_text(s_field)[0] != '\0') {
    lv_obj_remove_state(s_search_btn, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(s_search_btn, LV_STATE_DISABLED);
  }
}

void onSearch(lv_event_t*) {
  if (s_field == nullptr) {
    return;
  }
  const char* text = lv_textarea_get_text(s_field);
  if (text[0] == '\0') {
    return;
  }
  strncpy(s_query, text, sizeof(s_query) - 1);
  s_query[sizeof(s_query) - 1] = '\0';
  geo::request(s_query);
  requestView(View::Results);
}

void onResult(lv_event_t* e) {
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (i >= s_result_count) {
    return;
  }
  const geo::Result& r = s_results[i];
  if (!services::location::save(r.lat, r.lon, r.name, /*approximate=*/false)) {
    return;
  }
  // Old traffic belongs to the old spot: drop it and fetch around the new one.
  services::adsb::clear();
  services::network_task::fetchNow();
  s_exit = Exit::Radar;
}

// --------------------------------------------------------------------------
// Views
// --------------------------------------------------------------------------

void buildMenu() {
  makeHeader("Settings", onBackToRadar, nullptr, nullptr);
  lv_obj_t* body = makeBody();

  char name[services::location::kNameMaxLen + 16];
  if (services::location::isSet()) {
    char raw[services::location::kNameMaxLen + 1];
    services::location::name(raw, sizeof(raw));
    if (raw[0] == '\0') {
      snprintf(raw, sizeof(raw), "%.4f, %.4f", services::location::lat(),
               services::location::lon());
    }
    snprintf(name, sizeof(name), "%s%s", raw,
             services::location::isApproximate() ? " (approximate)" : "");
  } else {
    snprintf(name, sizeof(name), "Not set - tap to search an address");
  }
  makeNavRow(body, "Location", name, onToAddress);

  char wifi[48];
  if (services::wifi::connected()) {
    snprintf(wifi, sizeof(wifi), "Connected to %s", services::wifi::currentSsid());
  } else {
    snprintf(wifi, sizeof(wifi), "Not connected");
  }
  makeNavRow(body, "Wi-Fi", wifi, onWifi);

  {
    lv_obj_t* row = makeRow(body, "Distance units");
    static const char* kMap[] = {"km", "mi", ""};
    lv_obj_t* bm = lv_buttonmatrix_create(row);
    lv_buttonmatrix_set_map(bm, kMap);
    lv_buttonmatrix_set_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(bm, true);
    lv_buttonmatrix_set_button_ctrl(bm, st::useMiles() ? 1 : 0,
                                    LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(bm, 110, kRowH - 8);
    lv_obj_set_style_pad_all(bm, 2, 0);
    lv_obj_align(bm, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(bm, onUnits, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  struct SwitchRow {
    const char* title;
    bool on;
    lv_event_cb_t cb;
  };
  const SwitchRow switches[] = {
      {"Runways", st::showRunways(), onRunways},
      {"Ground aircraft", st::showGroundAircraft(), onGround},
  };
  for (const SwitchRow& s : switches) {
    lv_obj_t* row = makeRow(body, s.title);
    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (s.on) {
      lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, s.cb, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  {
    lv_obj_t* row = makeRow(body, "Brightness");
    lv_obj_t* slider = lv_slider_create(row);
    lv_slider_set_range(slider, st::kBrightnessMinPct, st::kBrightnessMaxPct);
    lv_slider_set_value(slider, st::brightnessPct(), LV_ANIM_OFF);
    lv_obj_set_width(slider, 150);
    lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -8, 0);
    // A bigger knob hit area: the resistive panel is imprecise.
    lv_obj_set_ext_click_area(slider, 12);
    lv_obj_add_event_cb(slider, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(slider, onBrightness, LV_EVENT_RELEASED, nullptr);
  }

  s_sd_label = lv_label_create(body);
  lv_label_set_text(s_sd_label, "");
  lv_obj_set_width(s_sd_label, lv_pct(100));
  lv_label_set_long_mode(s_sd_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s_sd_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_sd_label, lv_color_hex(kDimColor), 0);
  lv_obj_set_style_pad_ver(s_sd_label, 4, 0);
  refreshSdLabel();
}

void buildAddress() {
  makeHeader("Location", onToMenu, LV_SYMBOL_OK " Search", onSearch,
             &s_search_btn);

  const int32_t field_y = kHeaderH + kPad;
  s_field = lv_textarea_create(s_screen);
  lv_textarea_set_one_line(s_field, true);
  lv_textarea_set_max_length(s_field, geo::kQueryMaxLen);
  lv_textarea_set_placeholder_text(s_field, "Address, e.g. 14565 gilbert mirabel");
  lv_textarea_set_text(s_field, s_query);
  lv_obj_set_size(s_field, kScreenW - 2 * kPad, kFieldH);
  lv_obj_set_pos(s_field, kPad, field_y);
  lv_obj_add_event_cb(s_field, onFieldChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_state(s_field, LV_STATE_FOCUSED);  // show the cursor

  const int32_t kb_y = field_y + kFieldH + kPad;
  lv_obj_t* kb = lv_keyboard_create(s_screen);
  lv_obj_set_size(kb, kScreenW, kScreenH - kb_y);
  // Keep the keyboard's default bottom anchor (see wifi_screen.cpp).
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, s_field);
  lv_keyboard_set_popovers(kb, true);
  lv_obj_add_event_cb(kb, onSearch, LV_EVENT_READY, nullptr);  // check key

  onFieldChanged(nullptr);
}

void fillResults() {
  lv_obj_clean(s_list);
  s_shown_status = geo::status();
  s_result_count = 0;

  if (s_shown_status == geo::Status::Pending) {
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t* spinner = lv_spinner_create(s_list);
    lv_obj_set_size(spinner, 48, 48);
    lv_obj_t* label = lv_label_create(s_list);
    lv_label_set_text(label, "Searching...");
    return;
  }

  lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  if (s_shown_status == geo::Status::Done) {
    s_result_count = geo::results(s_results, geo::kMaxResults);
  }
  if (s_result_count == 0) {
    lv_obj_t* label = lv_label_create(s_list);
    lv_label_set_text(label, s_shown_status == geo::Status::Failed
                                 ? "Search failed - check Wi-Fi and try again"
                                 : "No results - try adding the city");
    lv_obj_set_style_pad_ver(label, 12, 0);
    return;
  }

  for (size_t i = 0; i < s_result_count; ++i) {
    lv_obj_t* row = lv_button_create(s_list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(kRowColor), 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(row, onResult, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

    lv_obj_t* name = lv_label_create(row);
    lv_label_set_text(name, s_results[i].name);
    lv_obj_set_style_text_font(name, radar::fontCardinal(), 0);
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t* full = lv_label_create(row);
    lv_label_set_text(full, s_results[i].full);
    lv_obj_set_style_text_font(full, radar::fontSmall(), 0);
    lv_obj_set_style_text_color(full, lv_color_hex(kDimColor), 0);
    lv_obj_set_width(full, lv_pct(100));
    lv_label_set_long_mode(full, LV_LABEL_LONG_WRAP);
  }
}

void buildResults() {
  makeHeader("Results", onToAddress, nullptr, nullptr);
  s_list = makeBody();
  fillResults();
}

void buildView(View v) {
  s_field = nullptr;
  s_search_btn = nullptr;
  s_list = nullptr;
  s_sd_label = nullptr;
  lv_obj_clean(s_screen);

  s_view = v;
  switch (v) {
    case View::Menu:
      buildMenu();
      break;
    case View::Address:
      buildAddress();
      break;
    case View::Results:
      buildResults();
      break;
    case View::None:
      break;
  }
}

void onScreenDeleted(lv_event_t*) {
  s_screen = nullptr;
  s_field = nullptr;
  s_search_btn = nullptr;
  s_list = nullptr;
  s_sd_label = nullptr;
  s_view = View::None;
  s_open = false;
}

}  // namespace

void open() {
  if (s_open) {
    return;
  }
  s_screen = lv_obj_create(nullptr);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, onScreenDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_t* old = lv_screen_active();
  lv_screen_load(s_screen);
  if (old != nullptr && old != s_screen) {
    lv_obj_delete(old);
  }
  s_open = true;
  s_exit = Exit::None;
  s_pending_view = View::None;
  buildView(View::Menu);
}

bool isOpen() { return s_open; }

void update() {
  if (!s_open) {
    return;
  }
  if (s_pending_view != View::None) {
    const View v = s_pending_view;
    s_pending_view = View::None;
    buildView(v);
    return;
  }
  if (s_view == View::Results && geo::status() != s_shown_status) {
    fillResults();
  }
  if (s_view == View::Menu) {
    refreshSdLabel();
  }
}

Exit takeExit() {
  const Exit e = s_exit;
  s_exit = Exit::None;
  return e;
}

}  // namespace ui::settings_screen
