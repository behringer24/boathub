// BME680 pressure, temperature and humidity, register level.
//
// Forced mode only: start() writes the whole configuration and starts one
// measurement, and collect() picks it up on a later pass, so nothing here
// waits for the 43 ms a measurement takes. The chip does not stretch the
// clock, which leaves the bus free for everything else in between.
//
// The compensation is Bosch's own integer arithmetic on the calibration words
// stored in the chip - no BSEC, which is only needed to turn gas resistance
// into an air-quality index.
// See docs/design/A-010-bme680-barometer-and-battery-bay.md.

#pragma once

#include <Arduino.h>

namespace bme680 {

// SDO tied to GND at the module. A floating SDO leaves the address undefined,
// which in practice is a sensor that answers sometimes - see A-010 section 5.
const uint8_t ADDRESS = 0x76;
const uint8_t CHIP_ID = 0x61;

// Humidity x1, temperature x2, pressure x16, IIR filter off: the datasheet's
// own weather configuration. A measurement then takes about 43 ms.
const uint32_t MEASURE_MS = 50;

// The heater set-point when gas is switched on. Bosch's starting values.
const uint16_t HEATER_C = 320;
const uint16_t HEATER_MS = 150;

enum class Result : uint8_t {
  Ok,
  NoAnswer,  // NACK, or a short read
  NotReady,  // new_data_0 still clear - look again on the next pass
  Skipped,   // the chip reported a value as skipped: it has been reset since start()
};

struct Sample {
  uint8_t status = 0;  // meas_status_0 as read: bit 7 new_data_0, bit 5 measuring

  // Raw readings, as the chip delivered them. 0x80000 and 0x8000 mean skipped.
  uint32_t rawPressure = 0;
  uint32_t rawTemp = 0;
  uint16_t rawHumidity = 0;

  float tempC = 0.0f;
  float pressurePa = 0.0f;
  float rh = 0.0f;

  // Only with gas switched on. gasStatus is gas_r_lsb's flags: bit 5
  // gas_valid_r, bit 4 heat_stab_r - the heater reached its set-point.
  bool gasValid = false;
  uint8_t gasStatus = 0;
  float gasKohm = 0.0f;
};

// The chip ID at 0x76, 0 if nothing answered. Reads the variant too, which
// tells a BME680 from a BME688: same ID, different gas readout.
uint8_t identify();

// Soft reset, then the calibration words. Separate from identify() so that a
// caller in loop() can wait out the reset on a later pass rather than here.
bool reset();
const uint32_t RESET_MS = 10;
bool readCalibration();

// identify(), reset(), wait, readCalibration() - for setup() and bench tools,
// where a 10 ms wait costs nothing.
bool begin();

// A BME688 answers with the same chip ID. Pressure, temperature and humidity
// are the same; the gas readout is not, and this driver only reads the
// BME680's.
bool isBme688();

// Writes the full configuration and starts one measurement. Written every
// time, because the chip has no status bit that reports its own reset: after
// a brown-out it comes back asleep with every measurement set to skipped.
//
// `gas` switches the heater on for this measurement - see A-010 section 4.
// Ignored on a BME688; the heater stays off there. `ambientC` sets the heater
// resistance for the set-point and only matters with gas on.
bool start(bool gas, float ambientC = 25.0f);

// How long after start() a measurement is finished.
uint32_t durationMs(bool gas);

// Reads the result: ten bytes from 0x1D, fifteen with gas. Compensates
// temperature first, because its intermediate value feeds the other two.
Result collect(Sample &out, bool gas);

// The two gas control registers as the chip holds them now. With the heater
// off, ctrl_gas_0 has heat_off (0x08) set and ctrl_gas_1 has run_gas clear.
bool readGasControl(uint8_t &ctrlGas0, uint8_t &ctrlGas1);

}  // namespace bme680
