#include "services/config_store.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cmath>
#include <cstring>

#include "config.h"
#include "services/radar_location.h"
#include "services/settings.h"
#include "ui/radar_range.h"

namespace services::config_store {

namespace {

constexpr char kPath[] = "/planeradar.json";
constexpr char kTmpPath[] = "/planeradar.tmp";
constexpr int kVersion = 1;
/** Don't retry mounting a missing card more often than this (it blocks). */
constexpr unsigned long kMountRetryMs = 10000;

SPIClass s_spi(VSPI);
SemaphoreHandle_t s_mutex = nullptr;
bool s_mounted = false;
unsigned long s_last_mount_try_ms = 0;
SdState s_state = SdState::NoCard;

class Lock {
 public:
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

bool mount() {
  s_last_mount_try_ms = millis();
  s_spi.begin(config::kSdPinSclk, config::kSdPinMiso, config::kSdPinMosi,
              config::kSdPinCs);
  if (!SD.begin(config::kSdPinCs, s_spi, config::kSdSpiHz)) {
    SD.end();
    s_spi.end();
    s_mounted = false;
    s_state = SdState::NoCard;
    return false;
  }
  s_mounted = true;
  Serial.printf("SD card: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  return true;
}

void unmount() {
  SD.end();
  s_spi.end();
  s_mounted = false;
}

/** Index of the preset whose ring-3 distance is `km`, or -1. */
int presetForKm(float km) {
  for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
    if (fabsf(ui::radar::kRangePresets[i].ring3_km - km) < 0.5f) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void build(JsonDocument& doc) {
  namespace st = services::settings;
  doc["version"] = kVersion;
  doc["units"] = st::useMiles() ? "mi" : "km";
  doc["runways"] = st::showRunways();
  doc["ground_aircraft"] = st::showGroundAircraft();
  doc["brightness"] = st::brightnessPct();
  doc["range_km"] = static_cast<int>(
      lroundf(ui::radar::kRangePresets[st::rangeIndex()].ring3_km));
  if (services::location::isSet()) {
    char name[services::location::kNameMaxLen + 1];
    services::location::name(name, sizeof(name));
    JsonObject loc = doc["location"].to<JsonObject>();
    loc["lat"] = serialized(String(services::location::lat(), 6));
    loc["lon"] = serialized(String(services::location::lon(), 6));
    loc["name"] = name;
    loc["approximate"] = services::location::isApproximate();
  }
}

/** Apply every valid field in `doc`; fields that are missing keep NVS values. */
void apply(const JsonDocument& doc) {
  namespace st = services::settings;
  constexpr bool kFromSd = true;

  const char* units = doc["units"] | "";
  if (strcmp(units, "km") == 0 || strcmp(units, "mi") == 0) {
    st::setUseMiles(units[0] == 'm', kFromSd);
  }
  if (doc["runways"].is<bool>()) {
    st::setShowRunways(doc["runways"].as<bool>(), kFromSd);
  }
  if (doc["ground_aircraft"].is<bool>()) {
    st::setShowGroundAircraft(doc["ground_aircraft"].as<bool>(), kFromSd);
  }
  if (doc["brightness"].is<int>()) {
    st::setBrightnessPct(
        static_cast<uint8_t>(constrain(doc["brightness"].as<int>(), 0, 100)),
        kFromSd);
  }
  if (doc["range_km"].is<float>()) {
    const int idx = presetForKm(doc["range_km"].as<float>());
    if (idx >= 0) {
      st::setRangeIndex(static_cast<uint8_t>(idx), kFromSd);
    } else {
      Serial.println("SD config: range_km must be 5, 10, 15, 20 or 25");
    }
  }
  JsonObjectConst loc = doc["location"];
  if (!loc.isNull() && loc["lat"].is<double>() && loc["lon"].is<double>()) {
    if (!services::location::save(loc["lat"].as<double>(),
                                  loc["lon"].as<double>(), loc["name"] | "",
                                  loc["approximate"] | false, kFromSd)) {
      Serial.println("SD config: location out of range, ignored");
    }
  }
}

/** Write the current config: temp file, then swap it in. Lock held. */
bool writeFile() {
  JsonDocument doc;
  build(doc);

  File f = SD.open(kTmpPath, FILE_WRITE);
  if (!f) {
    return false;
  }
  const size_t written = serializeJsonPretty(doc, f);
  f.close();
  if (written == 0) {
    return false;
  }
  // FAT can't rename over an existing file. If power drops between remove and
  // rename, init() recovers from the temp file.
  if (SD.exists(kPath) && !SD.remove(kPath)) {
    return false;
  }
  return SD.rename(kTmpPath, kPath);
}

/** Read the config file (or a leftover temp file). Lock held. */
bool readFile(JsonDocument* doc, bool* found) {
  *found = false;
  const char* path = kPath;
  if (!SD.exists(kPath)) {
    if (!SD.exists(kTmpPath)) {
      return true;  // no file yet: not an error
    }
    path = kTmpPath;
    Serial.println("SD config: recovering from interrupted save");
  }
  File f = SD.open(path, FILE_READ);
  if (!f) {
    return false;
  }
  *found = true;
  const DeserializationError err = deserializeJson(*doc, f);
  f.close();
  if (err) {
    Serial.printf("SD config: %s is not valid JSON (%s); using saved settings\n",
                  path, err.c_str());
    return false;
  }
  return true;
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
  JsonDocument doc;
  bool found = false;
  bool read_ok = false;
  {
    const Lock lock;
    if (!mount()) {
      Serial.println("SD card: not found (settings from internal flash)");
      return;
    }
    read_ok = readFile(&doc, &found);
  }
  if (!read_ok) {
    // Leave a bad file alone until the user changes a setting.
    s_state = SdState::ReadError;
    return;
  }
  if (found) {
    apply(doc);  // setters run with from_sd: they don't call save()
    Serial.println("SD config: loaded /planeradar.json");
  }
  const Lock lock;
  // Write back: creates the file on a fresh card, and rewrites it in the
  // canonical form (also finishing an interrupted save).
  s_state = writeFile() ? SdState::Ok : SdState::WriteError;
  if (!found) {
    Serial.println(s_state == SdState::Ok ? "SD config: created /planeradar.json"
                                          : "SD config: could not create file");
  }
}

void save() {
  if (s_mutex == nullptr) {
    return;
  }
  const Lock lock;
  if (!s_mounted) {
    if (millis() - s_last_mount_try_ms < kMountRetryMs || !mount()) {
      return;
    }
  }
  if (writeFile()) {
    s_state = SdState::Ok;
    return;
  }
  // Most likely the card was pulled: drop the mount so a later save retries.
  Serial.println("SD config: write failed");
  s_state = SdState::WriteError;
  unmount();
}

SdState state() { return s_state; }

}  // namespace services::config_store
