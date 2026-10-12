#include "lsm6dsox.h"

#include <Wire.h>

#include "i2cbus.h"

namespace {

// Register addresses and bits, from ST's datasheet and its reference driver.
const uint8_t REG_WHO_AM_I = 0x0F;
const uint8_t REG_CTRL1_XL = 0x10;
const uint8_t REG_CTRL2_G = 0x11;
const uint8_t REG_CTRL3_C = 0x12;
const uint8_t REG_CTRL9_XL = 0x18;
const uint8_t REG_WAKE_UP_SRC = 0x1B;
const uint8_t REG_OUTX_L_G = 0x22;  // gyro x, y, z, then accel x, y, z
const uint8_t REG_TAP_CFG0 = 0x56;
const uint8_t REG_TAP_CFG2 = 0x58;
const uint8_t REG_WAKE_UP_THS = 0x5B;
const uint8_t REG_WAKE_UP_DUR = 0x5C;
const uint8_t REG_MD1_CFG = 0x5E;

const uint8_t CHIP_ID = 0x6C;

const uint8_t CTRL3_SW_RESET = 0x01;
const uint8_t CTRL3_IF_INC = 0x04;     // register address advances on a multi-byte read
const uint8_t CTRL3_H_LACTIVE = 0x20;  // interrupt pins active low
const uint8_t CTRL3_BDU = 0x40;        // block data update

const uint8_t CTRL9_I3C_DISABLE = 0x02;

// Output data rate and full scale, as register fields.
const uint8_t XL_ODR_104HZ = 0x4 << 4;
const uint8_t XL_FS_4G = 0x2 << 2;
const uint8_t G_ODR_26HZ = 0x2 << 4;
const uint8_t G_FS_250DPS = 0x0 << 1;

// Sensitivity at those full scales.
const float G_PER_LSB = 0.000122f;     // 0.122 mg at +-4 g
const float DPS_PER_LSB = 0.00875f;    // 8.75 mdps at +-250 dps

// One step of the wake-up threshold is a sixty-fourth of full scale, and the
// chip compares it against the slope filter, which is half the change between
// consecutive samples. At +-4 g that makes a step 0.0625 g of slope - a jump
// of 0.125 g.
const float JUMP_G_PER_STEP = 2.0f * 4.0f / 64.0f;

const uint8_t TAP_CFG0_LIR = 0x01;              // latch the interrupt
const uint8_t TAP_CFG0_INT_CLR_ON_READ = 0x40;  // reading the source clears it
const uint8_t TAP_CFG2_INTERRUPTS_ENABLE = 0x80;
const uint8_t MD1_INT1_WU = 0x20;

uint8_t addr = 0;

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Register address first, then a repeated start and the read - one
// transaction, so nothing else on the bus can slip in between.
bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool readReg(uint8_t reg, uint8_t &value) { return readRegs(reg, &value, 1); }

int16_t le16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

bool identify(uint8_t candidate) {
  addr = candidate;
  uint8_t id = 0;
  if (readReg(REG_WHO_AM_I, id) && id == CHIP_ID) return true;
  addr = 0;
  return false;
}

}  // namespace

namespace lsm6dsox {

bool begin() {
  if (!i2cbus::ready()) return false;
  if (!identify(0x6A) && !identify(0x6B)) return false;

  // Back to defaults first. Its registers survive a reset of the ESP, so the
  // configuration a previous run left behind is never assumed - the same rule
  // as the SHT31's heater.
  if (!writeReg(REG_CTRL3_C, CTRL3_SW_RESET)) return false;
  // The bit clears itself when the reset is done. A read that fails while it
  // runs is retried rather than taken as an answer.
  const uint32_t start = millis();
  uint8_t ctrl3 = CTRL3_SW_RESET;
  while (ctrl3 & CTRL3_SW_RESET) {
    if (millis() - start > 50) return false;
    delay(1);
    if (!readReg(REG_CTRL3_C, ctrl3)) ctrl3 = CTRL3_SW_RESET;
  }

  // I3C off. The chip speaks both, and ST's own examples switch I3C off on a
  // plain I2C bus, so that ordinary traffic can never be mistaken for it.
  uint8_t ctrl9 = 0;
  if (!readReg(REG_CTRL9_XL, ctrl9)) return false;
  if (!writeReg(REG_CTRL9_XL, ctrl9 | CTRL9_I3C_DISABLE)) return false;

  return writeReg(REG_CTRL3_C, CTRL3_BDU | CTRL3_IF_INC) &&
         writeReg(REG_CTRL1_XL, XL_ODR_104HZ | XL_FS_4G) &&
         writeReg(REG_CTRL2_G, G_ODR_26HZ | G_FS_250DPS);
}

uint8_t address() { return addr; }

bool read(Sample &out) {
  if (addr == 0) return false;
  uint8_t buf[12];
  if (!readRegs(REG_OUTX_L_G, buf, sizeof(buf))) return false;
  for (uint8_t i = 0; i < 3; i++) {
    out.gyroDps[i] = (float)le16(buf + 2 * i) * DPS_PER_LSB;
    out.accelG[i] = (float)le16(buf + 6 + 2 * i) * G_PER_LSB;
  }
  return true;
}

float armKnock(float jumpG) {
  if (addr == 0) return 0.0f;

  long steps = lroundf(jumpG / JUMP_G_PER_STEP);
  if (steps < 1) steps = 1;
  if (steps > 63) steps = 63;

  // Slope filter rather than the high-pass (SLOPE_FDS = 0), one sample above
  // the threshold is enough (WAKE_DUR = 0), and steps of a sixty-fourth of
  // full scale (WAKE_THS_W = 0).
  const bool ok = writeReg(REG_TAP_CFG0, TAP_CFG0_LIR | TAP_CFG0_INT_CLR_ON_READ) &&
                  writeReg(REG_WAKE_UP_DUR, 0x00) &&
                  writeReg(REG_WAKE_UP_THS, (uint8_t)steps) &&
                  writeReg(REG_MD1_CFG, MD1_INT1_WU) &&
                  writeReg(REG_TAP_CFG2, TAP_CFG2_INTERRUPTS_ENABLE);
  if (!ok) return 0.0f;

  takeKnock();  // whatever latched while it was being set up
  return (float)steps * JUMP_G_PER_STEP;
}

uint8_t takeKnock() {
  uint8_t src = 0;
  if (addr == 0 || !readReg(REG_WAKE_UP_SRC, src)) return 0;
  const uint8_t WU_IA = 0x08;
  return (src & WU_IA) ? (src & 0x07) : 0;
}

bool setInterruptActiveLow(bool activeLow) {
  if (addr == 0) return false;
  uint8_t ctrl3 = 0;
  if (!readReg(REG_CTRL3_C, ctrl3)) return false;
  ctrl3 = activeLow ? (ctrl3 | CTRL3_H_LACTIVE) : (ctrl3 & ~CTRL3_H_LACTIVE);
  return writeReg(REG_CTRL3_C, ctrl3);
}

}  // namespace lsm6dsox
