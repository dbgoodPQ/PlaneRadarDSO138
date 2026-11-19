#include "hardware/xpt2046_soft.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cstring>

namespace hardware {

namespace {

/** Half clock period. The XPT2046 allows up to 2.5 MHz; ~250 kHz is plenty. */
constexpr uint32_t kHalfPeriodUs = 2;

}  // namespace

Touch_XPT2046_Soft::Touch_XPT2046_Soft() {
  _cfg.x_min = 300;
  _cfg.x_max = 3900;
  _cfg.y_min = 400;
  _cfg.y_max = 3900;
}

bool Touch_XPT2046_Soft::init() {
  _inited = false;
  if (_cfg.pin_cs < 0 || _cfg.pin_sclk < 0 || _cfg.pin_mosi < 0 ||
      _cfg.pin_miso < 0) {
    return false;
  }
  pinMode(_cfg.pin_cs, OUTPUT);
  digitalWrite(_cfg.pin_cs, HIGH);
  pinMode(_cfg.pin_sclk, OUTPUT);
  digitalWrite(_cfg.pin_sclk, LOW);
  pinMode(_cfg.pin_mosi, OUTPUT);
  digitalWrite(_cfg.pin_mosi, LOW);
  pinMode(_cfg.pin_miso, INPUT);
  if (_cfg.pin_int >= 0) {
    pinMode(_cfg.pin_int, INPUT);
  }
  _inited = true;
  return true;
}

void Touch_XPT2046_Soft::transfer(uint8_t* data, size_t len) {
  const int sclk = _cfg.pin_sclk;
  const int mosi = _cfg.pin_mosi;
  const int miso = _cfg.pin_miso;
  for (size_t i = 0; i < len; ++i) {
    uint8_t out = data[i];
    uint8_t in = 0;
    for (int bit = 0; bit < 8; ++bit) {
      // Mode 0: data set up on the falling edge, sampled on the rising edge.
      if (out & 0x80) {
        lgfx::gpio_hi(mosi);
      } else {
        lgfx::gpio_lo(mosi);
      }
      out <<= 1;
      delayMicroseconds(kHalfPeriodUs);
      lgfx::gpio_hi(sclk);
      in = static_cast<uint8_t>((in << 1) | (lgfx::gpio_in(miso) ? 1 : 0));
      delayMicroseconds(kHalfPeriodUs);
      lgfx::gpio_lo(sclk);
    }
    data[i] = in;
  }
}

uint_fast8_t Touch_XPT2046_Soft::getTouchRaw(lgfx::touch_point_t* tp,
                                             uint_fast8_t count) {
  if (!_inited || count == 0) {
    return 0;
  }
  tp->size = 0;

  // PENIRQ is low while the panel is pressed: skip the read when it is not.
  if (_cfg.pin_int >= 0 && lgfx::gpio_in(_cfg.pin_int)) {
    return 0;
  }

  // Same command sequence as lgfx::Touch_XPT2046: 7 rounds of
  // Y (0x91), Z1 (0xB1), X (0xD1), Z2 (0xC1), then power down (0x80).
  uint8_t data[57];
  memset(data, 0, 8);
  data[0] = 0x91;
  data[2] = 0xB1;
  data[4] = 0xD1;
  data[6] = 0xC1;
  data[56] = 0x80;
  memcpy(&data[8], data, 8);
  memcpy(&data[16], data, 16);
  memcpy(&data[32], data, 24);

  lgfx::gpio_lo(_cfg.pin_cs);
  transfer(data, sizeof(data));
  lgfx::gpio_hi(_cfg.pin_cs);

  size_t ix = 0;
  size_t iy = 0;
  size_t iz = 0;
  uint_fast16_t xt[7];
  uint_fast16_t yt[7];
  uint_fast16_t zt[7];
  for (size_t j = 0; j < 7; ++j) {
    const uint8_t* d = &data[j * 8];
    const int x = (d[5] << 8 | d[6]) >> 3;
    const int y = (d[1] << 8 | d[2]) >> 3;
    const int z = 0x3200 + y - x + (((d[3] << 8 | d[4]) - (d[7] << 8 | d[8])) >> 1);
    if (x > 128 && x <= 3968) {
      xt[ix++] = x;
    }
    if (y > 128 && y <= 3968) {
      yt[iy++] = y;
    }
    if (z > 0) {
      zt[iz++] = z;
    }
  }
  if (ix < 3 || iy < 3 || iz < 3) {
    return 0;
  }

  // Median of the samples rejects resistive-panel spikes.
  std::sort(xt, xt + ix);
  std::sort(yt, yt + iy);
  std::sort(zt, zt + iz);
  tp->x = xt[ix >> 1];
  tp->y = yt[iy >> 1];
  tp->size = zt[iz >> 1] >> 8;
  return tp->size ? 1 : 0;
}

}  // namespace hardware
