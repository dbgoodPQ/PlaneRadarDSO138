#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "hardware/xpt2046_soft.hpp"

/**
 * LovyanGFX device for the ESP32-2432S028R (CYD, original 1-USB version):
 *  - ILI9341 320x240 on HSPI (SPI2_HOST) with DMA
 *  - XPT2046 resistive touch over software SPI (VSPI is left to the microSD)
 *  - PWM backlight on GPIO 21
 * All pins come from config.h.
 */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  hardware::Touch_XPT2046_Soft _touch;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;  // HSPI
      cfg.spi_mode = 0;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.freq_read = config::kDisplaySpiReadHz;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = config::kDisplayPinSclk;
      cfg.pin_mosi = config::kDisplayPinMosi;
      cfg.pin_miso = config::kDisplayPinMiso;
      cfg.pin_dc = config::kDisplayPinDc;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = config::kDisplayPinCs;
      cfg.pin_rst = config::kDisplayPinRst;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;  // native portrait; rotation gives 320x240
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;  // touch has its own bus
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = config::kDisplayPinBacklight;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      // Raw ADC window before calibration; replaced by the stored calibration.
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 200;
      cfg.y_max = 3700;
      cfg.pin_int = config::kTouchPinIrq;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.pin_sclk = config::kTouchPinSclk;
      cfg.pin_mosi = config::kTouchPinMosi;
      cfg.pin_miso = config::kTouchPinMiso;
      cfg.pin_cs = config::kTouchPinCs;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
