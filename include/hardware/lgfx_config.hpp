#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

#if defined(PLANE_RADAR_DSO138)

/** LovyanGFX device: ILI9341 (DSO138 LCD module) on an 8-bit parallel bus. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_Parallel8 _bus;
  lgfx::Panel_ILI9341 _panel;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.i2s_port = I2S_NUM_0;
      cfg.freq_write = config::kDisplayBusWriteHz;
      cfg.pin_wr = config::kDisplayPinWr;
      cfg.pin_rd = config::kDisplayPinRd;
      cfg.pin_rs = config::kDisplayPinDc;  // RS = D/C
      cfg.pin_d0 = config::kDisplayPinD0;
      cfg.pin_d1 = config::kDisplayPinD1;
      cfg.pin_d2 = config::kDisplayPinD2;
      cfg.pin_d3 = config::kDisplayPinD3;
      cfg.pin_d4 = config::kDisplayPinD4;
      cfg.pin_d5 = config::kDisplayPinD5;
      cfg.pin_d6 = config::kDisplayPinD6;
      cfg.pin_d7 = config::kDisplayPinD7;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = config::kDisplayPinCs;
      cfg.pin_rst = config::kDisplayPinRst;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;   // native portrait size; rotation makes it 320x240
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.readable = false;    // radar never reads pixels back
      cfg.bus_shared = false;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

#else

/** LovyanGFX device: GC9A01 on SPI. Pin values come from config.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = -1;
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

#endif
