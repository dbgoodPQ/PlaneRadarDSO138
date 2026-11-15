#pragma once

namespace hardware::boot_button {

enum class Event { None, Tap, Hold };

void init();

/**
 * Poll from loop() (every ~10 ms). Tap = press shorter than kBootHoldMs;
 * Hold = fired once when the press reaches kBootHoldMs.
 */
Event poll();

}  // namespace hardware::boot_button
