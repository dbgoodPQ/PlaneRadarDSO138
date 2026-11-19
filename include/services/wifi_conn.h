#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Wi-Fi station control for the on-device settings screen. Non-blocking: start
 * an operation, then poll state()/scanState() from loop(). Call loop() every
 * iteration. Loop-task only (except connected(), which is safe anywhere).
 */
namespace services::wifi {

constexpr size_t kSsidMaxLen = 32;
constexpr size_t kPassMaxLen = 64;

enum class State : uint8_t {
  Idle,        // not connected, not trying
  Connecting,  // attempt in progress (up to kWifiConnectTimeoutMs)
  Connected,
  Failed,      // last attempt failed; see failReason()
};

enum class FailReason : uint8_t {
  None,
  WrongPassword,
  NotFound,
  Timeout,
};

struct Network {
  char ssid[kSsidMaxLen + 1];
  int8_t rssi;   // dBm
  bool secured;  // needs a password
};

enum class ScanState : uint8_t { Idle, Running, Done, Failed };

/** Set up STA mode and load the saved network from flash. */
void init();
void loop();

bool hasSaved();
const char* savedSsid();

/** Connect to the saved network (no-op without one). */
void connectSaved();
/** Try a network; it is saved only once the connection succeeds. */
void connect(const char* ssid, const char* pass);
/** Abort an attempt / disconnect and stop background reconnects. */
void stop();

State state();
FailReason failReason();
/** SSID of the current/last attempt (or connected network). */
const char* currentSsid();
bool connected();

/** Start an asynchronous scan. */
void startScan();
ScanState scanState();
/** Results of the last scan: strongest first, duplicates merged. */
size_t scanResults(Network* out, size_t max_out);

}  // namespace services::wifi
