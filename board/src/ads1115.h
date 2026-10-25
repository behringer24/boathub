// ADS1115 analogue-to-digital converter, register level.
//
// Single shot only: start() writes the whole configuration and starts one
// conversion, and collect() picks it up on a later pass, so nothing here waits
// for it. Between conversions the converter powers down. Because every start
// writes the channel and the gain again, a converter that reset in between is
// configured by the next conversion rather than read at its power-on defaults
// - and collect() notices the reset rather than returning a stale result.
//
// Converter no. 1 only, at 0x48: A0 is the battery divider, A1 the bilge loop.
// See docs/design/B-001-power-supply.md and B-003-bilge-level.md.

#pragma once

#include <Arduino.h>

namespace ads1115 {

// ADDR to GND, the converter in J6.
const uint8_t ADDRESS = 0x48;

// The full-scale range of every conversion, +-2.048 V. Fixed here and nowhere
// else: the battery's calibration factor includes the converter's gain error
// at this setting, and a different one would silently make the factor wrong.
const float FULL_SCALE_V = 2.048f;

// 128 samples per second is 7.8 ms a conversion. The internal oscillator is
// within 10 %, and the converter has to wake up first.
const uint32_t CONVERSION_MS = 10;

enum class Result : uint8_t {
  Ok,
  NoAnswer,  // NACK, or a short read
  NotReady,  // still converting - look again on the next pass
  Skipped,   // the configuration read back is not the one written: reset since start()
};

// Does anything acknowledge at the address? The converter has no ID register,
// so this is as far as presence can be established.
bool present();

// Starts a single-shot conversion of one input, 0-3, against ground. False if
// the converter did not acknowledge.
bool start(uint8_t input);

// The conversion started last, in volts at the input pin.
Result collect(float &volts);

}  // namespace ads1115
