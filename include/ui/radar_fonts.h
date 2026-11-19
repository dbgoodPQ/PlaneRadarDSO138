#pragma once

#include <lvgl.h>

/**
 * Noto Sans Bold, ASCII + Latin-1 (accents, degree sign), 4 bpp, generated with lv_font_conv 1.5.3
 * (see the header of each src/ui/fonts/noto_sans_bold_*.c for the options).
 */
LV_FONT_DECLARE(noto_sans_bold_11)
LV_FONT_DECLARE(noto_sans_bold_13)
LV_FONT_DECLARE(noto_sans_bold_16)

namespace ui::radar {

/** N/S/E/W. */
inline const lv_font_t* fontCardinal() { return &noto_sans_bold_13; }
/** Ring-3 scale label, aircraft tags, airport idents. */
inline const lv_font_t* fontSmall() { return &noto_sans_bold_11; }
/** Range indicator, top right. */
inline const lv_font_t* fontLarge() { return &noto_sans_bold_16; }

}  // namespace ui::radar
