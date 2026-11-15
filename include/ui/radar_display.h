#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Free the frame buffer (~57 KB) while another screen (LVGL) is shown. */
void radarDisplayRelease();

}  // namespace ui
