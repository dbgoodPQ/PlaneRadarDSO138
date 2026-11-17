#pragma once

/**
 * The radar as an LVGL screen: the radar fills the left 2/3 of the panel, the
 * selected plane's details (ui::detail_panel) the right 1/3. LVGL draws it
 * through its partial buffer, so no full-frame buffer is needed.
 *
 * Tap a plane = select it (tap empty space to deselect). Tap the range
 * indicator (top right of the radar) = next range. Hold the radar for
 * config::kSettingsLongPressMs = settings request (see takeSettingsRequest()).
 */
namespace ui::radar_screen {

/** Create the radar screen, load it, and delete the previous screen. */
void open();

/** False once another screen replaced (deleted) it. */
bool isOpen();

/** Cycle to the next range preset and redraw. */
void nextRange();

/** True once after the user held a finger on the radar long enough. */
bool takeSettingsRequest();

}  // namespace ui::radar_screen
