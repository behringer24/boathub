// Heel, pitch, motion and knocks from the LSM6DSOX.
//
// Attitude is polled at 25 Hz and run through the filter in attitude.h; knocks
// are watched for by the chip itself, which latches INT1 until it is read. So
// nothing here waits, and the module takes about 3 % of the I2C bus rather
// than the 14 % that sampling fast enough for knocks would cost.
// See docs/design/A-009-imu-heel-and-motion.md.

#pragma once

#include <Arduino.h>

namespace imu {

struct Reading {
  bool valid = false;  // the sensor is answering and the filter has started
  bool level = false;  // a zero reference is set, so heel and pitch mean something
  float heelDeg = 0.0f;   // positive to starboard
  float pitchDeg = 0.0f;  // positive bow-up
  float motionG = 0.0f;   // acceleration less gravity, last sample
};

void begin();
void loop();

// The present state. Heel and pitch are only meaningful when `level` is set:
// measured against an unknown mounting angle they are worse than nothing.
Reading latest();

bool present();

// Short human-readable state for the configuration page and the serial log.
const char *statusText();

// Records the boat's present attitude as zero, in NVS. Refused while there is
// no sensor, and for the first half minute after a start while the gyroscope's
// offset is still being learned - a zero taken then would be off by it.
bool levelNow();

}  // namespace imu
