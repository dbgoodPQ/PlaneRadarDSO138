#pragma once

/**
 * Settings (LVGL): location (address search), Wi-Fi, units, runways, ground
 * aircraft, brightness, SD card status. Drive it from loop(): open(), then
 * update() while isOpen(), and route takeExit().
 */
namespace ui::settings_screen {

enum class Exit {
  None,
  Radar,  // Back, or a new location was picked
  Wifi,   // open the Wi-Fi screen (come back here when it closes)
};

/** Create the screen, load it and delete the previous one. */
void open();

bool isOpen();

/** Poll searches and status; apply deferred view changes. Call every loop. */
void update();

/** Where to go next (once); the screen is closed when this is not None. */
Exit takeExit();

}  // namespace ui::settings_screen
