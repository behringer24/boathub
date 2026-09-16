// The shared I2C bus.
//
// Owned here rather than by whichever sensor happens to start first: the
// SHT31 and the three ADS1115 all sit on these two wires, and the pins and
// the clock are a property of the bus, not of any one device.
// See docs/design/A-002-bench-setup-usb.md.

#pragma once

#include <Arduino.h>

namespace i2cbus {

void begin();

// True once begin() has run. Sensors call this rather than starting the bus
// themselves, so the order of begin() calls in setup() cannot matter.
bool ready();

// Does anything acknowledge this address? Used for presence reporting, not
// for reading - a device that answers is not necessarily a working one.
bool probe(uint8_t addr);

}  // namespace i2cbus
