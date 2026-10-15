#include "bme680.h"

#include <Wire.h>

#include "i2cbus.h"

namespace {

using bme680::ADDRESS;

// Register addresses and fields, from the datasheet and Bosch's BME68x API.
const uint8_t REG_FIELD0 = 0x1D;  // meas_status_0, then pressure, temperature, humidity, gas
const uint8_t REG_RES_HEAT0 = 0x5A;
const uint8_t REG_GAS_WAIT0 = 0x64;
const uint8_t REG_CTRL_GAS_0 = 0x70;
const uint8_t REG_CTRL_GAS_1 = 0x71;
const uint8_t REG_CTRL_HUM = 0x72;
const uint8_t REG_CTRL_MEAS = 0x74;
const uint8_t REG_CONFIG = 0x75;
const uint8_t REG_CHIP_ID = 0xD0;
const uint8_t REG_SOFT_RESET = 0xE0;
const uint8_t REG_VARIANT_ID = 0xF0;

// The calibration words sit in three blocks: 23 bytes at 0x8A, 14 at 0xE1 and
// 5 at 0x00, read into one array in that order.
const uint8_t REG_COEFF1 = 0x8A;
const uint8_t REG_COEFF2 = 0xE1;
const uint8_t REG_COEFF3 = 0x00;
const size_t LEN_COEFF1 = 23;
const size_t LEN_COEFF2 = 14;
const size_t LEN_COEFF3 = 5;

const uint8_t SOFT_RESET_CMD = 0xB6;

const uint8_t OSRS_H_X1 = 0x01;       // ctrl_hum, bits 2:0
const uint8_t OSRS_T_X2 = 0x02 << 5;  // ctrl_meas, bits 7:5
const uint8_t OSRS_P_X16 = 0x05 << 2; // ctrl_meas, bits 4:2
const uint8_t MODE_FORCED = 0x01;     // ctrl_meas, bits 1:0
const uint8_t FILTER_OFF = 0x00;      // config, bits 4:2

const uint8_t HEAT_OFF = 0x08;  // ctrl_gas_0, bit 3
// ctrl_gas_1: run_gas is bit 4 on the BME680 and bit 5 on the BME688.
// Switching gas off clears the whole register, so both are clear whichever
// chip it is; nb_conv in bits 3:0 selects heater set-point 0.
const uint8_t RUN_GAS = 0x10;

const uint8_t NEW_DATA = 0x80;   // meas_status_0, bit 7
const uint8_t GAS_VALID = 0x20;  // gas_r_lsb, bit 5
const uint8_t HEAT_STAB = 0x10;  // gas_r_lsb, bit 4

// What the data registers hold for a measurement that was not made - after a
// reset every oversampling setting is "skipped".
const uint32_t SKIPPED_20 = 0x80000;
const uint16_t SKIPPED_16 = 0x8000;

struct Calibration {
  uint16_t par_t1;
  int16_t par_t2;
  int8_t par_t3;

  uint16_t par_p1;
  int16_t par_p2;
  int8_t par_p3;
  int16_t par_p4;
  int16_t par_p5;
  int8_t par_p6;
  int8_t par_p7;
  int16_t par_p8;
  int16_t par_p9;
  uint8_t par_p10;

  uint16_t par_h1;
  uint16_t par_h2;
  int8_t par_h3;
  int8_t par_h4;
  int8_t par_h5;
  uint8_t par_h6;
  int8_t par_h7;

  int8_t par_gh1;
  int16_t par_gh2;
  int8_t par_gh3;
  uint8_t res_heat_range;
  int8_t res_heat_val;
  int8_t range_sw_err;
};

Calibration calib;
bool calibrated = false;
bool variantHigh = false;  // a BME688

// The temperature compensation's intermediate value. Pressure and humidity
// are compensated from it, which is why temperature always goes first.
int32_t t_fine = 0;

// A write is register and value pairs, so one transaction carries the whole
// configuration, in the order given, with nothing else on the bus in between.
bool writePairs(const uint8_t *pairs, size_t len) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(pairs, len);
  return Wire.endTransmission() == 0;
}

// Register address first, then a repeated start and the read. The chip
// advances the address on a read, so a block comes back in one transaction.
bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)ADDRESS, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

uint16_t concat(uint8_t msb, uint8_t lsb) { return (uint16_t)(((uint16_t)msb << 8) | lsb); }

// ---------------------------------------------------------------------------
// The compensation and heater formulas below are Bosch's, from the BME68x
// Sensor API v4.4.8 (bme68x.c), integer variant, adapted to this file's
// calibration struct. Two guards against a division by zero are added: an
// unreadable calibration must not be able to stop the ESP32.
//
// Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.
//
// BSD-3-Clause
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// ---------------------------------------------------------------------------

// In 0.01 degree C. Sets t_fine.
int16_t calc_temperature(uint32_t temp_adc) {
  int64_t var1;
  int64_t var2;
  int64_t var3;

  var1 = ((int32_t)temp_adc >> 3) - ((int32_t)calib.par_t1 << 1);
  var2 = (var1 * (int32_t)calib.par_t2) >> 11;
  var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
  var3 = ((var3) * ((int32_t)calib.par_t3 << 4)) >> 14;
  t_fine = (int32_t)(var2 + var3);
  return (int16_t)(((t_fine * 5) + 128) >> 8);
}

// In Pa.
uint32_t calc_pressure(uint32_t pres_adc) {
  int32_t var1;
  int32_t var2;
  int32_t var3;
  int32_t pressure_comp;

  // Decides whether to multiply or divide first, for the least loss of
  // precision without overflowing: (1 << 31) >> 1.
  const int32_t pres_ovf_check = INT32_C(0x40000000);

  var1 = (((int32_t)t_fine) >> 1) - 64000;
  var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (int32_t)calib.par_p6) >> 2;
  var2 = var2 + ((var1 * (int32_t)calib.par_p5) << 1);
  var2 = (var2 >> 2) + ((int32_t)calib.par_p4 << 16);
  var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((int32_t)calib.par_p3 << 5)) >> 3) +
         (((int32_t)calib.par_p2 * var1) >> 1);
  var1 = var1 >> 18;
  var1 = ((32768 + var1) * (int32_t)calib.par_p1) >> 15;
  if (var1 == 0) return 0;  // added: outside any range check, so rejected there
  pressure_comp = 1048576 - pres_adc;
  pressure_comp = (int32_t)((pressure_comp - (var2 >> 12)) * ((uint32_t)3125));
  if (pressure_comp >= pres_ovf_check) {
    pressure_comp = ((pressure_comp / var1) << 1);
  } else {
    pressure_comp = ((pressure_comp << 1) / var1);
  }

  var1 = ((int32_t)calib.par_p9 * (int32_t)(((pressure_comp >> 3) * (pressure_comp >> 3)) >> 13)) >> 12;
  var2 = ((int32_t)(pressure_comp >> 2) * (int32_t)calib.par_p8) >> 13;
  var3 = ((int32_t)(pressure_comp >> 8) * (int32_t)(pressure_comp >> 8) *
          (int32_t)(pressure_comp >> 8) * (int32_t)calib.par_p10) >> 17;
  pressure_comp = (int32_t)(pressure_comp) + ((var1 + var2 + var3 + ((int32_t)calib.par_p7 << 7)) >> 4);

  return (uint32_t)pressure_comp;
}

// In 0.001 %RH, capped to 0-100 %.
uint32_t calc_humidity(uint16_t hum_adc) {
  int32_t var1;
  int32_t var2;
  int32_t var3;
  int32_t var4;
  int32_t var5;
  int32_t var6;
  int32_t temp_scaled;
  int32_t calc_hum;

  temp_scaled = (((int32_t)t_fine * 5) + 128) >> 8;
  var1 = (int32_t)(hum_adc - ((int32_t)((int32_t)calib.par_h1 * 16))) -
         (((temp_scaled * (int32_t)calib.par_h3) / ((int32_t)100)) >> 1);
  var2 = ((int32_t)calib.par_h2 *
          (((temp_scaled * (int32_t)calib.par_h4) / ((int32_t)100)) +
           (((temp_scaled * ((temp_scaled * (int32_t)calib.par_h5) / ((int32_t)100))) >> 6) /
            ((int32_t)100)) +
           (int32_t)(1 << 14))) >> 10;
  var3 = var1 * var2;
  var4 = (int32_t)calib.par_h6 << 7;
  var4 = ((var4) + ((temp_scaled * (int32_t)calib.par_h7) / ((int32_t)100))) >> 4;
  var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
  var6 = (var4 * var5) >> 1;
  calc_hum = (((var3 + var6) >> 10) * ((int32_t)1000)) >> 12;
  if (calc_hum > 100000) {
    calc_hum = 100000;
  } else if (calc_hum < 0) {
    calc_hum = 0;
  }

  return (uint32_t)calc_hum;
}

// In ohms. The BME680's readout; the BME688 has a formula of its own.
uint32_t calc_gas_resistance_low(uint16_t gas_res_adc, uint8_t gas_range) {
  int64_t var1;
  uint64_t var2;
  int64_t var3;
  static const uint32_t lookup_table1[16] = {
      UINT32_C(2147483647), UINT32_C(2147483647), UINT32_C(2147483647), UINT32_C(2147483647),
      UINT32_C(2147483647), UINT32_C(2126008810), UINT32_C(2147483647), UINT32_C(2130303777),
      UINT32_C(2147483647), UINT32_C(2147483647), UINT32_C(2143188679), UINT32_C(2136746228),
      UINT32_C(2147483647), UINT32_C(2126008810), UINT32_C(2147483647), UINT32_C(2147483647)};
  static const uint32_t lookup_table2[16] = {
      UINT32_C(4096000000), UINT32_C(2048000000), UINT32_C(1024000000), UINT32_C(512000000),
      UINT32_C(255744255),  UINT32_C(127110228),  UINT32_C(64000000),   UINT32_C(32258064),
      UINT32_C(16016016),   UINT32_C(8000000),    UINT32_C(4000000),    UINT32_C(2000000),
      UINT32_C(1000000),    UINT32_C(500000),     UINT32_C(250000),     UINT32_C(125000)};

  var1 = (int64_t)((1340 + (5 * (int64_t)calib.range_sw_err)) * ((int64_t)lookup_table1[gas_range])) >> 16;
  var2 = (((int64_t)((int64_t)gas_res_adc << 15) - (int64_t)(16777216)) + var1);
  var3 = (((int64_t)lookup_table2[gas_range] * (int64_t)var1) >> 9);
  if (var2 == 0) return 0;  // added
  return (uint32_t)((var3 + ((int64_t)var2 >> 1)) / (int64_t)var2);
}

// The heater resistance register value for a target temperature in degrees
// C, given the ambient temperature.
uint8_t calc_res_heat(uint16_t temp, int8_t amb_temp) {
  int32_t var1;
  int32_t var2;
  int32_t var3;
  int32_t var4;
  int32_t var5;
  int32_t heatr_res_x100;

  if (temp > 400) temp = 400;

  var1 = (((int32_t)amb_temp * calib.par_gh3) / 1000) * 256;
  var2 = (calib.par_gh1 + 784) * (((((calib.par_gh2 + 154009) * temp * 5) / 100) + 3276800) / 10);
  var3 = var1 + (var2 / 2);
  var4 = (var3 / (calib.res_heat_range + 4));
  var5 = (131 * calib.res_heat_val) + 65536;
  heatr_res_x100 = (int32_t)(((var4 / var5) - 250) * 34);
  return (uint8_t)((heatr_res_x100 + 50) / 100);
}

// The gas_wait register value for a heating time in ms: six bits of value and
// two of a factor of four.
uint8_t calc_gas_wait(uint16_t dur) {
  uint8_t factor = 0;

  if (dur >= 0xfc0) return 0xff;
  while (dur > 0x3F) {
    dur = dur / 4;
    factor += 1;
  }
  return (uint8_t)(dur + (factor * 64));
}

// --- end of the code from Bosch's API --------------------------------------

bool heaterOn(bool gas) { return gas && !variantHigh; }

}  // namespace

namespace bme680 {

uint8_t identify() {
  uint8_t id = 0;
  if (!readRegs(REG_CHIP_ID, &id, 1)) return 0;
  uint8_t variant = 0;
  if (id == CHIP_ID && readRegs(REG_VARIANT_ID, &variant, 1)) variantHigh = (variant == 0x01);
  return id;
}

bool reset() {
  calibrated = false;
  const uint8_t cmd[] = {REG_SOFT_RESET, SOFT_RESET_CMD};
  return writePairs(cmd, sizeof(cmd));
}

bool readCalibration() {
  uint8_t c[LEN_COEFF1 + LEN_COEFF2 + LEN_COEFF3];
  if (!readRegs(REG_COEFF1, c, LEN_COEFF1)) return false;
  if (!readRegs(REG_COEFF2, c + LEN_COEFF1, LEN_COEFF2)) return false;
  if (!readRegs(REG_COEFF3, c + LEN_COEFF1 + LEN_COEFF2, LEN_COEFF3)) return false;

  // Offsets into that array, as Bosch's API names them.
  calib.par_t1 = concat(c[32], c[31]);
  calib.par_t2 = (int16_t)concat(c[1], c[0]);
  calib.par_t3 = (int8_t)c[2];

  calib.par_p1 = concat(c[5], c[4]);
  calib.par_p2 = (int16_t)concat(c[7], c[6]);
  calib.par_p3 = (int8_t)c[8];
  calib.par_p4 = (int16_t)concat(c[11], c[10]);
  calib.par_p5 = (int16_t)concat(c[13], c[12]);
  calib.par_p6 = (int8_t)c[15];
  calib.par_p7 = (int8_t)c[14];
  calib.par_p8 = (int16_t)concat(c[19], c[18]);
  calib.par_p9 = (int16_t)concat(c[21], c[20]);
  calib.par_p10 = c[22];

  // The two 12-bit humidity words share a byte: h1 takes its low nibble, h2
  // its high one.
  calib.par_h1 = (uint16_t)(((uint16_t)c[25] << 4) | (c[24] & 0x0F));
  calib.par_h2 = (uint16_t)(((uint16_t)c[23] << 4) | (c[24] >> 4));
  calib.par_h3 = (int8_t)c[26];
  calib.par_h4 = (int8_t)c[27];
  calib.par_h5 = (int8_t)c[28];
  calib.par_h6 = c[29];
  calib.par_h7 = (int8_t)c[30];

  calib.par_gh1 = (int8_t)c[35];
  calib.par_gh2 = (int16_t)concat(c[34], c[33]);
  calib.par_gh3 = (int8_t)c[36];
  calib.res_heat_val = (int8_t)c[37];
  calib.res_heat_range = (c[39] & 0x30) / 16;
  calib.range_sw_err = ((int8_t)(c[41] & 0xF0)) / 16;

  // A chip that answered with nothing but zeros or ones has not given us its
  // calibration, whatever the bus said.
  if (calib.par_t1 == 0 || calib.par_t1 == 0xFFFF || calib.par_p1 == 0 || calib.par_p1 == 0xFFFF) {
    return false;
  }
  calibrated = true;
  return true;
}

bool begin() {
  if (!i2cbus::ready()) return false;
  if (identify() != CHIP_ID) return false;
  if (!reset()) return false;
  delay(RESET_MS);
  return readCalibration();
}

bool isBme688() { return variantHigh; }

bool start(bool gas, float ambientC) {
  if (!calibrated) return false;
  const bool heat = heaterOn(gas);

  uint8_t pairs[14];
  size_t n = 0;
  if (heat) {
    const float amb = ambientC < -40.0f ? -40.0f : (ambientC > 85.0f ? 85.0f : ambientC);
    pairs[n++] = REG_RES_HEAT0;
    pairs[n++] = calc_res_heat(HEATER_C, (int8_t)lroundf(amb));
    pairs[n++] = REG_GAS_WAIT0;
    pairs[n++] = calc_gas_wait(HEATER_MS);
  }
  pairs[n++] = REG_CONFIG;
  pairs[n++] = FILTER_OFF;
  pairs[n++] = REG_CTRL_GAS_0;
  pairs[n++] = heat ? 0x00 : HEAT_OFF;
  pairs[n++] = REG_CTRL_GAS_1;
  pairs[n++] = heat ? RUN_GAS : 0x00;
  // ctrl_hum before ctrl_meas: the datasheet says a humidity setting only
  // takes effect with the next write to ctrl_meas. And ctrl_meas last of all,
  // because writing forced mode is what starts the measurement.
  pairs[n++] = REG_CTRL_HUM;
  pairs[n++] = OSRS_H_X1;
  pairs[n++] = REG_CTRL_MEAS;
  pairs[n++] = OSRS_T_X2 | OSRS_P_X16 | MODE_FORCED;
  return writePairs(pairs, n);
}

uint32_t durationMs(bool gas) { return MEASURE_MS + (heaterOn(gas) ? HEATER_MS : 0); }

Result collect(Sample &out, bool gas) {
  out = Sample{};
  if (!calibrated) return Result::NoAnswer;
  const bool heat = heaterOn(gas);

  // meas_status_0, a byte Bosch's API calls the measurement index, three of
  // pressure, three of temperature, two of humidity - and with gas, three
  // unused and the two of gas_r.
  uint8_t b[15];
  if (!readRegs(REG_FIELD0, b, heat ? 15 : 10)) return Result::NoAnswer;

  out.status = b[0];
  out.rawPressure = ((uint32_t)b[2] << 12) | ((uint32_t)b[3] << 4) | (b[4] >> 4);
  out.rawTemp = ((uint32_t)b[5] << 12) | ((uint32_t)b[6] << 4) | (b[7] >> 4);
  out.rawHumidity = (uint16_t)(((uint16_t)b[8] << 8) | b[9]);

  if (!(b[0] & NEW_DATA)) return Result::NotReady;
  if (out.rawPressure == SKIPPED_20 || out.rawTemp == SKIPPED_20 || out.rawHumidity == SKIPPED_16) {
    return Result::Skipped;
  }

  out.tempC = (float)calc_temperature(out.rawTemp) / 100.0f;
  out.pressurePa = (float)calc_pressure(out.rawPressure);
  out.rh = (float)calc_humidity(out.rawHumidity) / 1000.0f;

  if (heat) {
    out.gasStatus = b[14] & (GAS_VALID | HEAT_STAB);
    // A reading taken before the plate reached its set-point is not a
    // reading of the air.
    out.gasValid = out.gasStatus == (GAS_VALID | HEAT_STAB);
    if (out.gasValid) {
      const uint16_t adc = (uint16_t)(((uint16_t)b[13] << 2) | (b[14] >> 6));
      out.gasKohm = (float)calc_gas_resistance_low(adc, b[14] & 0x0F) / 1000.0f;
    }
  }
  return Result::Ok;
}

bool readGasControl(uint8_t &ctrlGas0, uint8_t &ctrlGas1) {
  uint8_t b[2];
  if (!readRegs(REG_CTRL_GAS_0, b, 2)) return false;
  ctrlGas0 = b[0];
  ctrlGas1 = b[1];
  return true;
}

}  // namespace bme680
