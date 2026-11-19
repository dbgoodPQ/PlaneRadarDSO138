#pragma once

#include <cstdint>

namespace hardware::touch {

/**
 * Apply the stored touch calibration, or run the calibration screen when none
 * is stored (first boot) or a finger is held on the screen during power-up.
 * Call once after displayInit().
 */
void init();

/** Show the 4-corner calibration screen and save the result to flash. */
void calibrate();

/** Erase the stored calibration (next boot will ask again). */
void clearCalibration();

/** Current touch point in screen coordinates; false when not touched. */
bool read(int16_t* x, int16_t* y);

}  // namespace hardware::touch
