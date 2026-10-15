// Station pressure and the battery bay's air, from the BME680.
//
// One forced measurement per sample interval, collected on a later pass, so
// nothing here waits. There is no CRC on this chip: range and rate of change
// are what keep a bad reading out of the window.
//
// A sensor that stops answering is looked for again every interval, and on its
// return it is reset and its calibration read afresh - it may be the spare.
// See docs/design/A-010-bme680-barometer-and-battery-bay.md.

#pragma once

#include <Arduino.h>

namespace barometer {

struct Reading {
  bool valid = false;
  float pressureHpa = 0.0f;  // station pressure, offset applied - not sea level
  float tempC = 0.0f;        // the bay's air, not the batteries
  float rh = 0.0f;

  // Only with the gas heater switched on, and only once the plate has reached
  // its set-point.
  bool gasValid = false;
  float gasKohm = 0.0f;
};

void begin();
void loop();

// The most recent accepted sample, or an invalid Reading if there is none
// fresh enough. For the status line and for a spot message; does not consume.
Reading latest();

// Consumes the reading: true once per accepted measurement, never twice - the
// same contract as every other sensor's takeFresh().
bool takeFresh(Reading &out);

// Has a BME680 ever answered? "Not fitted" against "fitted and unhappy".
bool present();

// Short human-readable state for the configuration page and the serial log.
const char *statusText();

}  // namespace barometer
