// SHT31 cabin temperature and humidity.
//
// Nothing here blocks. A measurement takes about 13 ms and is collected on a
// later pass through loop(), because the three ADS1115 share these two wires
// and must not be held up by it.
// See docs/design/A-008-sht31-cabin-climate.md.

#pragma once

#include <Arduino.h>

namespace sht31 {

struct Reading {
  bool valid = false;  // false also while the heater runs and while it cools
  float tempC = 0.0f;
  float rh = 0.0f;
};

void begin();
void loop();

// The most recent accepted sample, or an invalid Reading if there is none
// fresh enough. Staleness covers two cases with one mechanism: a sensor that
// has stopped answering, and the heater cycle during which samples are
// deliberately not taken.
Reading latest();

// Has the sensor ever acknowledged? Distinguishes "not fitted" from "fitted
// and currently unhappy" in the status line.
bool present();

// Short human-readable state for the configuration page and the serial log.
const char *statusText();

}  // namespace sht31
