#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

/** Start the panel in the orientation from services::settings::flipScreen(). */
void displayInit();

/** The LovyanGFX rotation set by displayInit(). */
int displayRotation();

/** True when the panel was started turned 180 degrees. */
bool displayFlipped();
