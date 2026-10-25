#include "ads1115.h"

#include <Wire.h>

#include "i2cbus.h"

namespace {

const uint8_t REG_CONVERSION = 0x00;
const uint8_t REG_CONFIG = 0x01;

// The configuration register, field by field (datasheet table 8).
const uint16_t OS = 0x8000;             // write: start a conversion; read: 1 = not converting
const uint16_t MUX_SINGLE_A0 = 0x4000;  // input A0 against ground; each further input adds 0x1000
const uint16_t PGA_2V048 = 0x0400;      // +-2.048 V
const uint16_t MODE_SINGLE = 0x0100;    // power down after one conversion
const uint16_t DR_128SPS = 0x0080;
const uint16_t COMP_OFF = 0x0003;       // comparator disabled, ALERT/RDY left high impedance

// What the last start() wrote, without the start bit. Read back in collect():
// after a reset the register holds its defaults, which differ in the input
// selection, and the conversion register holds nothing worth reading.
uint16_t written = 0;

bool writeRegister(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(ads1115::ADDRESS);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(ads1115::ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)ads1115::ADDRESS, 2) != 2) return false;
  const uint8_t hi = Wire.read();
  const uint8_t lo = Wire.read();
  value = ((uint16_t)hi << 8) | lo;
  return true;
}

}  // namespace

namespace ads1115 {

bool present() { return i2cbus::probe(ADDRESS); }

bool start(uint8_t input) {
  written = (uint16_t)(MUX_SINGLE_A0 + ((uint16_t)(input & 0x03) << 12)) | PGA_2V048 |
            MODE_SINGLE | DR_128SPS | COMP_OFF;
  return writeRegister(REG_CONFIG, OS | written);
}

Result collect(float &volts) {
  uint16_t config = 0;
  if (!readRegister(REG_CONFIG, config)) return Result::NoAnswer;
  if ((config & OS) == 0) return Result::NotReady;
  if ((config & ~OS) != written) return Result::Skipped;

  uint16_t raw = 0;
  if (!readRegister(REG_CONVERSION, raw)) return Result::NoAnswer;
  volts = (float)(int16_t)raw * (FULL_SCALE_V / 32768.0f);
  return Result::Ok;
}

}  // namespace ads1115
