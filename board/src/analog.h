// The battery voltage and the bilge level, from the ADS1115.
//
// Every sample interval converts A0, the battery divider, and then A1, the
// bilge loop - one conversion at a time, each collected on a later pass, so
// nothing here waits. The two are different readings and are handed over
// separately.
//
// The battery is reported whenever the converter answers, 0 V included: a
// board on USB with the battery switched off at the isolator reads 0 V, and
// that is worth sending. The bilge is reported only while a sender drives at
// least 3.6 mA through the loop. Without one there is nothing to report, and
// the field stays out rather than reading as a dry bilge.
// See docs/design/B-001-power-supply.md and B-003-bilge-level.md.

#pragma once

#include <Arduino.h>

namespace analog {

struct Battery {
  bool valid = false;
  float volts = 0.0f;   // at the battery, calibration factor applied
  float inputV = 0.0f;  // at A0, before it - what a calibration divides by
};

struct Bilge {
  bool valid = false;
  float levelCm = 0.0f;  // offset applied, never below zero
  float loopMa = 0.0f;
};

void begin();
void loop();

// The most recent reading, or an invalid one if there is none fresh enough.
// For the status line and for a spot message; does not consume.
Battery latestBattery();
Bilge latestBilge();

// Consume the reading: true once per conversion, never twice - the same
// contract as every other sensor's takeFresh(). The bilge only with a sender.
bool takeBattery(Battery &out);
bool takeBilge(Bilge &out);

// The loop current at the last conversion of A1, sender or not. False if
// there is none fresh. For the configuration page, which should show the
// current that says "no sender" as well as the one that gives a level.
bool loopCurrent(float &mA);

// Has the converter answered this boot? Has a bilge sender delivered a
// reading this boot? "Not fitted" against "fitted and unhappy", for each.
bool present();
bool bilgeFitted();

// Short human-readable state for the configuration page and the serial log.
const char *statusText();

}  // namespace analog
