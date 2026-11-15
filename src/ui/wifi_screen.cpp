#include "ui/wifi_screen.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include <cstring>

#include "config.h"
#include "services/wifi_conn.h"

namespace ui::wifi_screen {

namespace {

namespace wifi = services::wifi;

// --- Layout (320x240 landscape). Touch targets are >= 40 px tall. ---
constexpr int32_t kScreenW = 320;
constexpr int32_t kScreenH = 240;
constexpr int32_t kHeaderH = 44;
constexpr int32_t kHeaderBtnW = 64;
constexpr int32_t kRowH = 46;
constexpr int32_t kFieldH = 40;
constexpr int32_t kEyeBtnW = 48;
constexpr int32_t kBottomBtnH = 44;
constexpr int32_t kPad = 4;
constexpr unsigned long kConnectedCloseMs = 1500;
constexpr size_t kMinWpaPassLen = 8;

enum class View : uint8_t { None, Scan, Name, Password, Status };

// --- Screen state (all on the loop task) ---
bool s_open = false;
bool s_cancelable = false;
lv_obj_t* s_screen = nullptr;
View s_view = View::None;
View s_pending_view = View::None;  // view switches are deferred to update()
bool s_pending_close = false;

// The network being joined.
char s_ssid[wifi::kSsidMaxLen + 1] = "";
char s_pass[wifi::kPassMaxLen + 1] = "";
bool s_secured = true;
bool s_hidden = false;          // typed by name via "Other network..."
bool s_attempt_saved = false;   // retrying the saved network, not new creds

// Scan view
wifi::Network s_networks[config::kWifiMaxListedNetworks];
size_t s_network_count = 0;
lv_obj_t* s_list = nullptr;
bool s_list_shows_results = false;

// Name / password view
lv_obj_t* s_field = nullptr;
lv_obj_t* s_join_btn = nullptr;
lv_obj_t* s_eye_label = nullptr;
lv_obj_t* s_keyboard = nullptr;

// Status view
wifi::State s_shown_state = wifi::State::Idle;
unsigned long s_connected_at_ms = 0;

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

void copyStr(char* dst, size_t dst_size, const char* src) {
  strncpy(dst, src != nullptr ? src : "", dst_size - 1);
  dst[dst_size - 1] = '\0';
}

void requestView(View v) { s_pending_view = v; }

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb,
                     void* user_data = nullptr) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

/** Top bar: optional left button, centered title, optional right button. */
lv_obj_t* makeHeader(const char* title, const char* left_text, lv_event_cb_t left_cb,
                     const char* right_text, lv_event_cb_t right_cb,
                     lv_obj_t** right_btn_out = nullptr) {
  lv_obj_t* bar = lv_obj_create(s_screen);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, kScreenW, kHeaderH);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x101820), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  const int32_t btn_h = kHeaderH - 2 * kPad;
  if (left_text != nullptr) {
    lv_obj_t* btn = makeButton(bar, left_text, left_cb);
    lv_obj_set_size(btn, kHeaderBtnW, btn_h);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, kPad, 0);
  }
  lv_obj_t* right = nullptr;
  if (right_text != nullptr) {
    right = makeButton(bar, right_text, right_cb);
    lv_obj_set_size(right, kHeaderBtnW, btn_h);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -kPad, 0);
  }
  if (right_btn_out != nullptr) {
    *right_btn_out = right;
  }

  lv_obj_t* label = lv_label_create(bar);
  lv_label_set_text(label, title);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, kScreenW - 2 * (kHeaderBtnW + 2 * kPad));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  return bar;
}

const char* signalWord(int8_t rssi) {
  if (rssi >= -60) return "Strong";
  if (rssi >= -70) return "Good";
  if (rssi >= -80) return "Fair";
  return "Weak";
}

lv_color_t signalColor(int8_t rssi) {
  if (rssi >= -60) return lv_palette_main(LV_PALETTE_GREEN);
  if (rssi >= -70) return lv_palette_main(LV_PALETTE_LIGHT_GREEN);
  if (rssi >= -80) return lv_palette_main(LV_PALETTE_AMBER);
  return lv_palette_main(LV_PALETTE_RED);
}

bool isConnectedTo(const char* ssid) {
  return wifi::state() == wifi::State::Connected &&
         strcmp(wifi::currentSsid(), ssid) == 0;
}

void startAttempt() {
  s_attempt_saved = false;
  wifi::connect(s_ssid, s_pass);
  requestView(View::Status);
}

// --------------------------------------------------------------------------
// Event handlers (only set state; the screen is rebuilt in update())
// --------------------------------------------------------------------------

void onBack(lv_event_t*) { s_pending_close = true; }

void onRescan(lv_event_t*) { requestView(View::Scan); }

void onToNetworks(lv_event_t*) { requestView(View::Scan); }

void onNetworkRow(lv_event_t* e) {
  const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (i >= s_network_count) {
    return;
  }
  copyStr(s_ssid, sizeof(s_ssid), s_networks[i].ssid);
  s_secured = s_networks[i].secured;
  s_hidden = false;
  s_pass[0] = '\0';
  if (s_secured) {
    requestView(View::Password);
  } else {
    startAttempt();  // open network: nothing to type
  }
}

void onOtherRow(lv_event_t*) {
  s_ssid[0] = '\0';
  s_pass[0] = '\0';
  s_hidden = true;
  s_secured = false;  // unknown: an empty password means an open network
  requestView(View::Name);
}

void updateJoinEnabled() {
  if (s_join_btn == nullptr || s_field == nullptr) {
    return;
  }
  const size_t len = strlen(lv_textarea_get_text(s_field));
  bool ok;
  if (s_view == View::Name) {
    ok = len > 0;
  } else {
    // WPA/WPA2 needs 8+ characters; a hidden network may also be open.
    ok = (len >= kMinWpaPassLen) || (!s_secured && len == 0);
  }
  if (ok) {
    lv_obj_remove_state(s_join_btn, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(s_join_btn, LV_STATE_DISABLED);
  }
}

void onFieldChanged(lv_event_t*) { updateJoinEnabled(); }

void onJoin(lv_event_t*) {
  if (s_join_btn == nullptr || lv_obj_has_state(s_join_btn, LV_STATE_DISABLED)) {
    return;
  }
  const char* text = lv_textarea_get_text(s_field);
  if (s_view == View::Name) {
    copyStr(s_ssid, sizeof(s_ssid), text);
    requestView(View::Password);
    return;
  }
  copyStr(s_pass, sizeof(s_pass), text);
  startAttempt();
}

void onPasswordBack(lv_event_t*) {
  requestView(s_hidden ? View::Name : View::Scan);
}

void onToggleEye(lv_event_t*) {
  const bool hidden_now = lv_textarea_get_password_mode(s_field);
  lv_textarea_set_password_mode(s_field, !hidden_now);
  lv_label_set_text(s_eye_label, hidden_now ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void onRetry(lv_event_t*) {
  if (s_attempt_saved) {
    wifi::connectSaved();
    requestView(View::Status);
  } else if (s_secured || s_hidden) {
    requestView(View::Password);  // keeps what was typed
  } else {
    startAttempt();
  }
}

// --------------------------------------------------------------------------
// Views
// --------------------------------------------------------------------------

lv_obj_t* makeBody() {
  lv_obj_t* body = lv_obj_create(s_screen);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, kScreenW, kScreenH - kHeaderH);
  lv_obj_set_pos(body, 0, kHeaderH);
  return body;
}

void fillListScanning() {
  lv_obj_clean(s_list);
  lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_t* spinner = lv_spinner_create(s_list);
  lv_obj_set_size(spinner, 48, 48);
  lv_obj_t* label = lv_label_create(s_list);
  lv_label_set_text(label, "Scanning...");
  s_list_shows_results = false;
}

void addNetworkRow(size_t i) {
  const wifi::Network& net = s_networks[i];
  lv_obj_t* row = lv_button_create(s_list);
  lv_obj_set_size(row, lv_pct(100), kRowH);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x1C2630), 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_set_style_pad_hor(row, 8, 0);
  lv_obj_add_event_cb(row, onNetworkRow, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

  lv_obj_t* icon = lv_label_create(row);
  lv_label_set_text(icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(icon, signalColor(net.rssi), 0);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* name = lv_label_create(row);
  lv_label_set_text(name, net.ssid);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, kScreenW - 80);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 28, 2);

  char detail[40];
  if (isConnectedTo(net.ssid)) {
    snprintf(detail, sizeof(detail), "Connected");
  } else {
    snprintf(detail, sizeof(detail), "%s - %s signal",
             net.secured ? "Secured" : "Open", signalWord(net.rssi));
  }
  lv_obj_t* sub = lv_label_create(row);
  lv_label_set_text(sub, detail);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sub, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
  lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 28, -2);
}

void fillListResults(bool scan_failed) {
  lv_obj_clean(s_list);
  lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  s_network_count = scan_failed ? 0 : wifi::scanResults(s_networks, config::kWifiMaxListedNetworks);

  if (s_network_count == 0) {
    lv_obj_t* label = lv_label_create(s_list);
    lv_label_set_text(label, scan_failed ? "Scan failed - tap " LV_SYMBOL_REFRESH
                                         : "No networks found");
    lv_obj_set_style_pad_ver(label, 12, 0);
  }
  for (size_t i = 0; i < s_network_count; ++i) {
    addNetworkRow(i);
  }

  lv_obj_t* other = lv_button_create(s_list);
  lv_obj_set_size(other, lv_pct(100), kRowH);
  lv_obj_set_style_bg_color(other, lv_color_hex(0x1C2630), 0);
  lv_obj_set_style_radius(other, 6, 0);
  lv_obj_add_event_cb(other, onOtherRow, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* label = lv_label_create(other);
  lv_label_set_text(label, LV_SYMBOL_PLUS "  Other network...");
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  s_list_shows_results = true;
}

void buildScan() {
  makeHeader("Wi-Fi", s_cancelable ? LV_SYMBOL_LEFT : nullptr, onBack,
             LV_SYMBOL_REFRESH, onRescan);
  s_list = makeBody();
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_list, kPad, 0);
  lv_obj_set_style_pad_row(s_list, kPad, 0);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);

  wifi::startScan();
  if (wifi::scanState() == wifi::ScanState::Failed) {
    fillListResults(/*scan_failed=*/true);
  } else {
    fillListScanning();
  }
}

/** Name (hidden network) or password entry: header, one field, keyboard. */
void buildEntry(bool is_name) {
  const char* title = is_name ? "Network name" : s_ssid;
  makeHeader(title, LV_SYMBOL_LEFT, is_name ? onToNetworks : onPasswordBack,
             is_name ? "Next" : "Join", onJoin, &s_join_btn);

  const int32_t field_y = kHeaderH + kPad;
  const int32_t field_w = is_name ? kScreenW - 2 * kPad
                                  : kScreenW - 3 * kPad - kEyeBtnW;
  s_field = lv_textarea_create(s_screen);
  lv_textarea_set_one_line(s_field, true);
  lv_obj_set_size(s_field, field_w, kFieldH);
  lv_obj_set_pos(s_field, kPad, field_y);
  lv_obj_add_event_cb(s_field, onFieldChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_state(s_field, LV_STATE_FOCUSED);  // show the cursor

  if (is_name) {
    lv_textarea_set_max_length(s_field, wifi::kSsidMaxLen);
    lv_textarea_set_placeholder_text(s_field, "Hidden network name");
    lv_textarea_set_text(s_field, s_ssid);
    s_eye_label = nullptr;
  } else {
    lv_textarea_set_max_length(s_field, wifi::kPassMaxLen);
    lv_textarea_set_placeholder_text(
        s_field, s_secured ? "Password" : "Password (empty if open)");
    lv_textarea_set_password_mode(s_field, true);
    lv_textarea_set_text(s_field, s_pass);

    lv_obj_t* eye = makeButton(s_screen, LV_SYMBOL_EYE_OPEN, onToggleEye);
    lv_obj_set_size(eye, kEyeBtnW, kFieldH);
    lv_obj_set_pos(eye, kScreenW - kPad - kEyeBtnW, field_y);
    s_eye_label = lv_obj_get_child(eye, 0);
  }

  const int32_t kb_y = field_y + kFieldH + kPad;
  s_keyboard = lv_keyboard_create(s_screen);
  lv_obj_set_size(s_keyboard, kScreenW, kScreenH - kb_y);
  lv_obj_set_pos(s_keyboard, 0, kb_y);
  lv_keyboard_set_textarea(s_keyboard, s_field);
  lv_keyboard_set_popovers(s_keyboard, true);  // enlarged key while pressed
  lv_obj_add_event_cb(s_keyboard, onJoin, LV_EVENT_READY, nullptr);  // check key

  updateJoinEnabled();
}

void buildStatus() {
  const wifi::State st = wifi::state();
  s_shown_state = st;

  makeHeader("Wi-Fi", nullptr, nullptr, nullptr, nullptr);
  lv_obj_t* body = makeBody();
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(body, 8, 0);
  lv_obj_set_style_pad_bottom(body, kBottomBtnH + 2 * kPad, 0);

  const char* ssid = s_attempt_saved ? wifi::savedSsid() : s_ssid;
  char title[64];
  char detail[64] = "";

  if (st == wifi::State::Connecting) {
    lv_obj_t* spinner = lv_spinner_create(body);
    lv_obj_set_size(spinner, 56, 56);
    snprintf(title, sizeof(title), "Connecting to %s...", ssid);
  } else {
    const bool ok = st == wifi::State::Connected;
    lv_obj_t* icon = lv_label_create(body);
    lv_label_set_text(icon, ok ? LV_SYMBOL_OK : LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(
        icon, lv_palette_main(ok ? LV_PALETTE_GREEN : LV_PALETTE_AMBER), 0);
    if (ok) {
      snprintf(title, sizeof(title), "Connected to %s", ssid);
      snprintf(detail, sizeof(detail), "IP %s", WiFi.localIP().toString().c_str());
      s_connected_at_ms = millis();
    } else {
      snprintf(title, sizeof(title), "Couldn't connect to %s", ssid);
      switch (wifi::failReason()) {
        case wifi::FailReason::WrongPassword:
          snprintf(detail, sizeof(detail), "Wrong password");
          break;
        case wifi::FailReason::NotFound:
          snprintf(detail, sizeof(detail), "Network not found");
          break;
        default:
          snprintf(detail, sizeof(detail), "No answer - out of range?");
          break;
      }
    }
  }

  lv_obj_t* title_label = lv_label_create(body);
  lv_label_set_text(title_label, title);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(title_label, kScreenW - 4 * kPad);
  lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);

  if (detail[0] != '\0') {
    lv_obj_t* detail_label = lv_label_create(body);
    lv_label_set_text(detail_label, detail);
    lv_obj_set_style_text_color(detail_label, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
  }

  // Bottom buttons
  const int32_t btn_y = kScreenH - kBottomBtnH - kPad;
  if (st == wifi::State::Connecting || st == wifi::State::Failed ||
      st == wifi::State::Idle) {
    const bool show_retry = st != wifi::State::Connecting;
    const int32_t btn_w = show_retry ? (kScreenW - 3 * kPad) / 2 : kScreenW - 2 * kPad;
    lv_obj_t* networks = makeButton(s_screen, LV_SYMBOL_LIST "  Networks", onToNetworks);
    lv_obj_set_size(networks, btn_w, kBottomBtnH);
    lv_obj_set_pos(networks, kPad, btn_y);
    if (show_retry) {
      lv_obj_t* retry = makeButton(s_screen, LV_SYMBOL_REFRESH "  Try again", onRetry);
      lv_obj_set_size(retry, btn_w, kBottomBtnH);
      lv_obj_set_pos(retry, 2 * kPad + btn_w, btn_y);
    }
  }
}

void buildView(View v) {
  // Drop pointers into the old view before its objects are deleted.
  s_list = nullptr;
  s_field = nullptr;
  s_join_btn = nullptr;
  s_eye_label = nullptr;
  s_keyboard = nullptr;
  lv_obj_clean(s_screen);

  s_view = v;
  switch (v) {
    case View::Scan:
      buildScan();
      break;
    case View::Name:
      buildEntry(/*is_name=*/true);
      break;
    case View::Password:
      buildEntry(/*is_name=*/false);
      break;
    case View::Status:
      buildStatus();
      break;
    case View::None:
      break;
  }
}

void open(View first, bool cancelable) {
  if (!s_open) {
    s_screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* old = lv_screen_active();
    lv_screen_load(s_screen);
    if (old != nullptr && old != s_screen) {
      lv_obj_delete(old);
    }
    s_open = true;
  }
  s_cancelable = cancelable;
  s_pending_close = false;
  s_pending_view = View::None;
  buildView(first);
}

void close() {
  // Leaving without a working connection: go back to the saved network.
  if (!wifi::connected() && wifi::state() != wifi::State::Connecting) {
    wifi::connectSaved();
  }
  lv_obj_t* blank = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(blank, lv_color_black(), 0);
  lv_screen_load(blank);
  lv_obj_delete(s_screen);
  s_screen = nullptr;
  s_list = nullptr;
  s_field = nullptr;
  s_join_btn = nullptr;
  s_eye_label = nullptr;
  s_keyboard = nullptr;
  s_view = View::None;
  s_open = false;
}

}  // namespace

void openScan(bool cancelable) { open(View::Scan, cancelable); }

void openConnecting(bool cancelable) {
  s_attempt_saved = true;
  copyStr(s_ssid, sizeof(s_ssid), wifi::savedSsid());
  wifi::connectSaved();
  open(View::Status, cancelable);
}

bool isOpen() { return s_open; }

void update() {
  if (!s_open) {
    return;
  }
  if (s_pending_close) {
    close();
    return;
  }
  if (s_pending_view != View::None) {
    const View v = s_pending_view;
    s_pending_view = View::None;
    buildView(v);
    return;
  }

  switch (s_view) {
    case View::Scan: {
      const wifi::ScanState ss = wifi::scanState();
      if (!s_list_shows_results &&
          (ss == wifi::ScanState::Done || ss == wifi::ScanState::Failed)) {
        fillListResults(ss == wifi::ScanState::Failed);
      }
      break;
    }
    case View::Status: {
      const wifi::State st = wifi::state();
      if (st != s_shown_state) {
        buildView(View::Status);
      } else if (st == wifi::State::Connected &&
                 millis() - s_connected_at_ms >= kConnectedCloseMs) {
        close();
      }
      break;
    }
    case View::Name:
    case View::Password:
      // The keyboard's own "hide" key detaches it from the field; there is
      // nowhere to hide it to on this screen, so re-attach.
      if (s_keyboard != nullptr && lv_keyboard_get_textarea(s_keyboard) == nullptr) {
        lv_keyboard_set_textarea(s_keyboard, s_field);
      }
      break;
    default:
      break;
  }
}

}  // namespace ui::wifi_screen
