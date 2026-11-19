#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace hardware {

/**
 * XPT2046 resistive touch over bit-banged (software) SPI, as a LovyanGFX touch
 * driver. Same sampling and filtering as lgfx::Touch_XPT2046, but it needs no
 * SPI peripheral: on the CYD the VSPI controller is left to the microSD slot
 * (its pins differ from the touch pins, and a controller can only drive one
 * pin set). Touch is slow (one 57-byte read per poll) so software SPI costs
 * about 1 ms per read.
 *
 * Uses config_t pin_sclk / pin_mosi / pin_miso / pin_cs / pin_int; spi_host
 * and freq are ignored.
 */
class Touch_XPT2046_Soft : public lgfx::ITouch {
 public:
  Touch_XPT2046_Soft();

  bool init() override;
  void wakeup() override {}
  void sleep() override {}
  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override;

 private:
  /** Full-duplex SPI mode 0 transfer, in place. */
  void transfer(uint8_t* data, size_t len);
};

}  // namespace hardware
