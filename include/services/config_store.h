#pragma once

/**
 * Settings + location mirrored to /planeradar.json on the microSD card.
 *
 * At boot the card file wins (edits made on a PC apply) and its values are
 * copied into NVS; without a card everything runs from NVS. Every change is
 * written to NVS by its owner (services::settings / services::location), which
 * then calls save() to rewrite the card file. Wi-Fi credentials and touch
 * calibration never go to the card.
 */
namespace services::config_store {

enum class SdState { NoCard, Ok, ReadError, WriteError };

/**
 * Mount the card and load the config file, applying its values. Creates the
 * file from the current settings when the card has none. Call at boot after
 * settings::init() and location::init().
 */
void init();

/** Rewrite the card file from the current settings (any task). */
void save();

/** Last card result, for the Settings screen. */
SdState state();

}  // namespace services::config_store
