#pragma once

#include <lvgl.h>

namespace ui::runway {

/**
 * Recompute the large-airport runway segments and labels for the current
 * location and range. Call before invalidating the radar (not per draw).
 */
void update();

/** Draw the last computed runways; (ox, oy) is the radar area's origin. */
void draw(lv_layer_t* layer, int ox, int oy);

}  // namespace ui::runway
