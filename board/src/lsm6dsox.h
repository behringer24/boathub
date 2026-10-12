// LSM6DSOX accelerometer and gyroscope, register level.
//
// Only what A-009 uses: acceleration and rate of turn on demand, and the
// chip's own knock detector raising INT1. No FIFO, no embedded functions.
//
// Axes are the chip's own, as printed on the Modulino. Turning them into the
// boat's fore-and-aft, athwartships and vertical is the caller's job, because
// it depends on how the module was mounted.
// See docs/design/A-009-imu-heel-and-motion.md.

#pragma once

#include <Arduino.h>

namespace lsm6dsox {

// Accelerometer at 104 Hz, +-4 g: fast enough for the knock detector to see a
// jolt that lasts tens of milliseconds, and wide enough not to clip one.
// Gyroscope at 26 Hz, +-250 dps: a boat rolls at tens of degrees per second at
// most, and attitude is read at 25 Hz anyway.
const float ACCEL_HZ = 104.0f;
const float GYRO_HZ = 26.0f;

struct Sample {
  float accelG[3] = {0, 0, 0};    // x, y, z in g; at rest the axis pointing up reads +1
  float gyroDps[3] = {0, 0, 0};   // x, y, z in degrees per second, right-handed
};

// Looks for the chip at 0x6A, then 0x6B, resets it and configures it. Returns
// false if neither address answers with the LSM6DSOX's identity. The I2C bus
// has to be started first - see i2cbus.h.
bool begin();

// The address it answered at, 0 before a successful begin().
uint8_t address();

// The latest output registers: twelve bytes in one transaction. The chip keeps
// the high and low byte of each value together (block data update), so a read
// that lands mid-update never mixes two samples.
bool read(Sample &out);

// Arms the knock detector on INT1. The chip compares the change between two
// consecutive samples - 9.6 ms apart at 104 Hz - against the threshold on
// every axis, which tells a knock from the large but slow accelerations of a
// seaway. `jumpG` is the size of that change; the chip resolves it in steps of
// 0.125 g, up to 7.9 g.
//
// The line is latched: it stays high until takeKnock() reads the source, so
// loop() only has to look at the pin when it comes round. Returns the
// threshold actually set.
float armKnock(float jumpG);

// Reads and clears the knock source. Returns the axes that crossed the
// threshold as bits (x = 4, y = 2, z = 1), 0 if none did.
uint8_t takeKnock();

// Inverts the interrupt pins' idle level. Used once at start to prove the INT1
// wire: with nothing pending the line is low, inverted it is high, and a
// GPIO that sees both is connected to it.
bool setInterruptActiveLow(bool activeLow);

}  // namespace lsm6dsox
