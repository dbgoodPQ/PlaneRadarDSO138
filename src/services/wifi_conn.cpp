#include "services/wifi_conn.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>

#include "config.h"

namespace services::wifi {

namespace {

constexpr char kNvsNamespace[] = "wifi";
constexpr char kNvsKeySsid[] = "ssid";
constexpr char kNvsKeyPass[] = "pass";
constexpr char kHostname[] = "plane-radar";

/** Disconnect events from the previous attempt can still arrive just after a
 *  new one starts; reasons are only trusted after this delay. */
constexpr unsigned long kReasonSettleMs = 500;

// ESP-IDF disconnect reasons (wifi_err_reason_t) we act on.
constexpr uint8_t kReasonAssocLeave = 8;  // our own disconnect()
constexpr uint8_t kReason4WayTimeout = 15;
constexpr uint8_t kReasonNoApFound = 201;
constexpr uint8_t kReasonAuthFail = 202;
constexpr uint8_t kReasonHandshakeTimeout = 204;

char s_saved_ssid[kSsidMaxLen + 1] = "";
char s_saved_pass[kPassMaxLen + 1] = "";

char s_try_ssid[kSsidMaxLen + 1] = "";
char s_try_pass[kPassMaxLen + 1] = "";
bool s_try_is_new = false;  // save on success

State s_state = State::Idle;
FailReason s_fail = FailReason::None;
unsigned long s_attempt_start_ms = 0;

/** Keep retrying the saved network after it drops (off after stop()). */
bool s_background_reconnect = false;
unsigned long s_last_retry_ms = 0;

/** Written by the Wi-Fi event task, read by the loop task. */
volatile uint8_t s_last_disconnect_reason = 0;

ScanState s_scan = ScanState::Idle;
Network s_results[config::kWifiMaxListedNetworks];
size_t s_result_count = 0;

void copyStr(char* dst, size_t dst_size, const char* src) {
  strncpy(dst, src != nullptr ? src : "", dst_size - 1);
  dst[dst_size - 1] = '\0';
}

bool linkUp() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void onDisconnected(arduino_event_id_t, arduino_event_info_t info) {
  const uint8_t reason = info.wifi_sta_disconnected.reason;
  if (reason != kReasonAssocLeave) {
    s_last_disconnect_reason = reason;
  }
}

void saveCredentials() {
  Preferences prefs;
  prefs.begin(kNvsNamespace, /*readOnly=*/false);
  prefs.putString(kNvsKeySsid, s_try_ssid);
  prefs.putString(kNvsKeyPass, s_try_pass);
  prefs.end();
  copyStr(s_saved_ssid, sizeof(s_saved_ssid), s_try_ssid);
  copyStr(s_saved_pass, sizeof(s_saved_pass), s_try_pass);
  Serial.printf("Wi-Fi saved: %s\n", s_saved_ssid);
}

void beginAttempt(const char* ssid, const char* pass, bool is_new) {
  copyStr(s_try_ssid, sizeof(s_try_ssid), ssid);
  copyStr(s_try_pass, sizeof(s_try_pass), pass);
  s_try_is_new = is_new;

  WiFi.disconnect(/*wifioff=*/false);
  s_last_disconnect_reason = 0;
  WiFi.setAutoReconnect(true);
  WiFi.begin(s_try_ssid, s_try_pass[0] != '\0' ? s_try_pass : nullptr);

  s_state = State::Connecting;
  s_fail = FailReason::None;
  s_attempt_start_ms = millis();
  Serial.printf("Wi-Fi connecting: %s\n", s_try_ssid);
}

void fail(FailReason reason) {
  // Stop the driver from retrying the same (bad) credentials.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(/*wifioff=*/false);
  s_state = State::Failed;
  s_fail = reason;
  s_last_retry_ms = millis();
  Serial.printf("Wi-Fi failed: %s (reason %u)\n", s_try_ssid,
                s_last_disconnect_reason);
}

void updateConnecting() {
  if (linkUp()) {
    s_state = State::Connected;
    s_background_reconnect = true;
    if (s_try_is_new) {
      saveCredentials();
    }
    Serial.printf("Wi-Fi connected: %s  IP %s\n", s_try_ssid,
                  WiFi.localIP().toString().c_str());
    return;
  }
  const unsigned long elapsed = millis() - s_attempt_start_ms;
  const uint8_t reason = elapsed >= kReasonSettleMs ? s_last_disconnect_reason : 0;
  if (reason == kReasonAuthFail || reason == kReason4WayTimeout ||
      reason == kReasonHandshakeTimeout) {
    fail(FailReason::WrongPassword);
    return;
  }
  if (elapsed >= config::kWifiConnectTimeoutMs) {
    fail(reason == kReasonNoApFound ? FailReason::NotFound : FailReason::Timeout);
  }
}

void updateScan() {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  if (n < 0) {
    s_scan = ScanState::Failed;
    return;
  }

  s_result_count = 0;
  for (int16_t i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty() || ssid.length() > kSsidMaxLen) {
      continue;  // hidden networks are added by name via "Other..."
    }
    const int8_t rssi = static_cast<int8_t>(WiFi.RSSI(i));

    // Same SSID from several access points: keep the strongest.
    size_t existing = s_result_count;
    for (size_t j = 0; j < s_result_count; ++j) {
      if (strcmp(s_results[j].ssid, ssid.c_str()) == 0) {
        existing = j;
        break;
      }
    }
    if (existing < s_result_count) {
      if (rssi <= s_results[existing].rssi) {
        continue;
      }
      // Remove it; it is re-inserted at its new position below.
      memmove(&s_results[existing], &s_results[existing + 1],
              (s_result_count - existing - 1) * sizeof(Network));
      --s_result_count;
    }

    // Insertion sort, strongest first; drop the weakest when full.
    size_t pos = 0;
    while (pos < s_result_count && s_results[pos].rssi >= rssi) {
      ++pos;
    }
    if (pos >= static_cast<size_t>(config::kWifiMaxListedNetworks)) {
      continue;
    }
    const size_t to_move =
        std::min(s_result_count, static_cast<size_t>(config::kWifiMaxListedNetworks) - 1) - pos;
    memmove(&s_results[pos + 1], &s_results[pos], to_move * sizeof(Network));
    copyStr(s_results[pos].ssid, sizeof(s_results[pos].ssid), ssid.c_str());
    s_results[pos].rssi = rssi;
    s_results[pos].secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    if (s_result_count < static_cast<size_t>(config::kWifiMaxListedNetworks)) {
      ++s_result_count;
    }
  }
  WiFi.scanDelete();
  s_scan = ScanState::Done;
}

}  // namespace

void init() {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    prefs.getString(kNvsKeySsid, s_saved_ssid, sizeof(s_saved_ssid));
    prefs.getString(kNvsKeyPass, s_saved_pass, sizeof(s_saved_pass));
    prefs.end();
  }

  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname(kHostname);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // lower latency for the ADS-B polling
  WiFi.setAutoReconnect(false);
  WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void loop() {
  if (s_scan == ScanState::Running) {
    updateScan();
  }

  switch (s_state) {
    case State::Connecting:
      updateConnecting();
      break;
    case State::Connected:
      if (!linkUp()) {
        // Dropped: the driver's auto-reconnect gets the first go.
        s_state = State::Idle;
        s_last_retry_ms = millis();
        Serial.println("Wi-Fi lost");
      }
      break;
    case State::Idle:
    case State::Failed:
      if (linkUp()) {
        s_state = State::Connected;  // auto-reconnect succeeded
        Serial.println("Wi-Fi back");
      } else if (s_background_reconnect && hasSaved() &&
                 s_scan != ScanState::Running &&
                 millis() - s_last_retry_ms >= config::kWifiReconnectIntervalMs) {
        s_last_retry_ms = millis();
        beginAttempt(s_saved_ssid, s_saved_pass, /*is_new=*/false);
      }
      break;
  }
}

bool hasSaved() { return s_saved_ssid[0] != '\0'; }

const char* savedSsid() { return s_saved_ssid; }

void connectSaved() {
  if (!hasSaved()) {
    return;
  }
  s_background_reconnect = true;
  beginAttempt(s_saved_ssid, s_saved_pass, /*is_new=*/false);
}

void connect(const char* ssid, const char* pass) {
  beginAttempt(ssid, pass, /*is_new=*/true);
}

void stop() {
  s_background_reconnect = false;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(/*wifioff=*/false);
  s_state = State::Idle;
  s_fail = FailReason::None;
}

State state() { return s_state; }

FailReason failReason() { return s_fail; }

const char* currentSsid() { return s_try_ssid; }

bool connected() { return WiFi.status() == WL_CONNECTED; }

void startScan() {
  if (s_state == State::Connecting) {
    stop();  // the radio cannot scan while it is joining a network
  }
  s_result_count = 0;
  const int16_t r = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false);
  s_scan = (r == WIFI_SCAN_FAILED) ? ScanState::Failed : ScanState::Running;
}

ScanState scanState() { return s_scan; }

size_t scanResults(Network* out, size_t max_out) {
  const size_t n = std::min(s_result_count, max_out);
  memcpy(out, s_results, n * sizeof(Network));
  return n;
}

}  // namespace services::wifi
