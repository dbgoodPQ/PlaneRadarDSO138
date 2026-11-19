#pragma once

#include <cstdint>

/**
 * User settings, persisted in NVS and mirrored to the SD card config file
 * (services::config_store). Read and written on the loop task.
 */
namespace services::settings {

constexpr uint8_t kBrightnessMinPct = 10;
constexpr uint8_t kBrightnessMaxPct = 100;

/** Load from NVS. Call once at boot, before config_store::init(). */
void init();

bool useMiles();
bool showRunways();
bool showGroundAircraft();
uint8_t brightnessPct();
/** Index into ui::radar::kRangePresets. */
uint8_t rangeIndex();

/**
 * Setters persist to NVS and, unless `from_sd` is set (values being loaded
 * from the card), write the SD config file too.
 */
void setUseMiles(bool v, bool from_sd = false);
void setShowRunways(bool v, bool from_sd = false);
void setShowGroundAircraft(bool v, bool from_sd = false);
void setBrightnessPct(uint8_t pct, bool from_sd = false);
void setRangeIndex(uint8_t index, bool from_sd = false);

}  // namespace services::settings
