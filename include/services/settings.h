#pragma once

#include <cstdint>

/**
 * User settings, persisted in NVS and mirrored to the SD card config file
 * (services::config_store). Read and written on the loop task.
 */
namespace services::settings {

constexpr uint8_t kBrightnessMinPct = 10;
constexpr uint8_t kBrightnessMaxPct = 100;

/**
 * Load from NVS. Call once at boot, before displayInit() (the screen
 * orientation comes from here) and before config_store::init().
 */
void init();

bool useMiles();
bool showRunways();
bool showGroundAircraft();
uint8_t brightnessPct();
/** Index into ui::radar::kRangePresets. */
uint8_t rangeIndex();
/**
 * Screen turned 180 degrees (USB connector on the left). Applied at power-up
 * only: a change takes effect after a restart (and a touch recalibration).
 */
bool flipScreen();

/**
 * Setters persist to NVS and, unless `from_sd` is set (values being loaded
 * from the card), write the SD config file too.
 */
void setUseMiles(bool v, bool from_sd = false);
void setShowRunways(bool v, bool from_sd = false);
void setShowGroundAircraft(bool v, bool from_sd = false);
void setBrightnessPct(uint8_t pct, bool from_sd = false);
void setRangeIndex(uint8_t index, bool from_sd = false);
void setFlipScreen(bool v, bool from_sd = false);

}  // namespace services::settings
