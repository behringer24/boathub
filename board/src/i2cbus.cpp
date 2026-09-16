#include "i2cbus.h"

#include <Wire.h>

namespace {

bool started = false;

}  // namespace

namespace i2cbus {

void begin() {
  if (started) return;

  // Any GPIO can carry I2C on an ESP32 - the peripheral is routed through the
  // GPIO matrix rather than being wired to fixed pins as on an AVR - so the
  // pins have to be named explicitly.
  const int PIN_SDA = 8;
  const int PIN_SCL = 9;

  // 100 kHz, not 400. Four modules each carry a 10 k pull-up, which in
  // parallel is about 2.5 k, and the SHT31 sits on up to 3 m of cable. At
  // 400 kHz the rise time over that becomes marginal. Nothing on this bus is
  // fast enough to care.
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  started = true;
  Serial.printf("[i2c] SDA GPIO%d, SCL GPIO%d, 100 kHz\n", PIN_SDA, PIN_SCL);
}

bool ready() { return started; }

bool probe(uint8_t addr) {
  if (!started) return false;
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

}  // namespace i2cbus
