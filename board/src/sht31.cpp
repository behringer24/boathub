#include "sht31.h"

#include <Wire.h>

#include "config.h"
#include "i2cbus.h"

namespace {

const uint8_t ADDR = 0x44;  // ADDR pin low or open

// Single shot, high repeatability, *without* clock stretching.
//
// The stretching variant lets the sensor hold SCL low until the measurement
// is done, so a read call simply blocks and returns data. That would hold SCL
// low for everybody - the three ADS1115 are on the same two wires and could
// not be talked to for those 13 ms. So: send the command, go away, come back.
const uint16_t CMD_MEASURE = 0x2400;
const uint16_t CMD_HEATER_ON = 0x306D;
const uint16_t CMD_HEATER_OFF = 0x3066;
const uint16_t CMD_READ_STATUS = 0xF32D;
const uint16_t CMD_CLEAR_STATUS = 0x3041;

// Datasheet maximum for high repeatability is 15 ms. A little margin costs
// nothing here because the wait happens in the background.
const uint32_t MEASURE_WAIT_MS = 20;

const uint32_t SAMPLE_MS = 10000;

// A reading older than this counts as no reading. Three and a half sample
// intervals, so one missed measurement is tolerated and a stopped sensor is
// not reported as current data.
const uint32_t STALE_MS = 35000;

const uint32_t STATUS_CHECK_MS = 300000;  // 5 min

enum class Phase { Idle, Measuring, Heating, Cooling };

Phase phase = Phase::Idle;
uint32_t phaseSince = 0;
uint32_t nextSample = 0;
uint32_t nextStatusCheck = 0;

sht31::Reading last;
uint32_t lastAt = 0;

bool seen = false;
const char *state = "not started";

// When the humidity first went above the threshold and stayed there. Zero
// means it is not currently high.
uint32_t rhHighSince = 0;

uint32_t crcErrors = 0;

// Sensirion CRC-8: polynomial 0x31, initialised to 0xFF, no final inversion.
// One byte follows each 16-bit word, and both are worth checking - the sensor
// sits on up to 3 m of cable, and a corrupted humidity value looks entirely
// plausible.
uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

bool command(uint16_t cmd) {
  Wire.beginTransmission(ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

void setPhase(Phase p, uint32_t now) {
  phase = p;
  phaseSince = now;
}

// Bit 4 of the status word means "system reset detected". A sensor that
// browned out and restarted answers normally afterwards, with its
// configuration back at defaults - invisible unless something looks.
void checkStatus() {
  if (!command(CMD_READ_STATUS)) return;
  if (Wire.requestFrom((int)ADDR, 3) != 3) return;

  uint8_t buf[3];
  for (uint8_t i = 0; i < 3; i++) buf[i] = Wire.read();
  if (crc8(buf, 2) != buf[2]) return;

  const uint16_t status = ((uint16_t)buf[0] << 8) | buf[1];
  if (status & (1 << 4)) {
    Serial.println("[sht31] sensor reported a reset - check its supply");
    command(CMD_CLEAR_STATUS);
  }
}

// Collect a finished measurement. Six bytes: temperature, its CRC, humidity,
// its CRC.
void collect(uint32_t now) {
  if (Wire.requestFrom((int)ADDR, 6) != 6) {
    state = "no answer";
    return;
  }

  uint8_t buf[6];
  for (uint8_t i = 0; i < 6; i++) buf[i] = Wire.read();

  if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) {
    crcErrors++;
    state = "crc error";
    return;
  }

  const uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
  const uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];

  const float tempC = -45.0f + 175.0f * (float)rawT / 65535.0f;
  const float rh = 100.0f * (float)rawH / 65535.0f;

  // Plausibility. A value outside these is not a cabin, it is a fault, and
  // averaging it in would corrupt the window rather than show the problem.
  if (tempC < -20.0f || tempC > 60.0f || rh < 0.0f || rh > 100.0f) {
    state = "implausible";
    return;
  }

  // A jump this large in ten seconds is not air moving, it is a bad read.
  if (last.valid && (fabsf(tempC - last.tempC) > 10.0f || fabsf(rh - last.rh) > 30.0f)) {
    state = "jumped";
    return;
  }

  last.valid = true;
  last.tempC = tempC;
  last.rh = rh;
  lastAt = now;
  seen = true;
  state = "ok";
}

// Condensation on the sensor leaves it reading 100 %RH long after the air has
// dried - stuck, not merely uninformative, which is worse because it looks
// like a measurement. The heater drives the water off.
//
// It cannot be used casually: while it runs the sensor sits several degrees
// above cabin temperature and stays there for a minute or two afterwards, so
// no samples are taken during either window. That is why n dips in a window
// containing a heater cycle - by design, not a fault.
void maybeStartHeater(uint32_t now) {
  const Config &c = config::get();
  if (!c.sht31Heater) return;

  if (last.rh < (float)c.sht31HeatAboveRh) {
    rhHighSince = 0;
    return;
  }
  if (rhHighSince == 0) {
    rhHighSince = now;
    return;
  }
  if ((now - rhHighSince) < (uint32_t)c.sht31SoakMins * 60000UL) return;

  if (command(CMD_HEATER_ON)) {
    Serial.printf("[sht31] humidity above %u %% for %u min - heating %u s\n", c.sht31HeatAboveRh,
                  c.sht31SoakMins, c.sht31HeatSecs);
    rhHighSince = 0;
    setPhase(Phase::Heating, now);
    state = "heating";
  }
}

}  // namespace

namespace sht31 {

void begin() {
  i2cbus::begin();

  if (!i2cbus::probe(ADDR)) {
    state = "not found";
    Serial.printf("[sht31] nothing at 0x%02X\n", ADDR);
    return;
  }

  seen = true;
  state = "starting";
  // The heater survives a warm reset of the ESP but not of the sensor, so its
  // state is never assumed - it is switched off explicitly on every start.
  command(CMD_HEATER_OFF);
  command(CMD_CLEAR_STATUS);
  Serial.printf("[sht31] found at 0x%02X\n", ADDR);
}

void loop() {
  const uint32_t now = millis();
  const Config &c = config::get();

  switch (phase) {
    case Phase::Heating:
      if ((now - phaseSince) >= (uint32_t)c.sht31HeatSecs * 1000UL) {
        command(CMD_HEATER_OFF);
        setPhase(Phase::Cooling, now);
        state = "cooling";
      }
      return;  // no measuring while the sensor is warm

    case Phase::Cooling:
      if ((now - phaseSince) >= (uint32_t)c.sht31CoolSecs * 1000UL) {
        setPhase(Phase::Idle, now);
        nextSample = now;
        state = "ok";
      }
      return;

    case Phase::Measuring:
      if ((now - phaseSince) < MEASURE_WAIT_MS) return;
      collect(now);
      setPhase(Phase::Idle, now);
      nextSample = now + SAMPLE_MS;
      if (last.valid && lastAt == now) maybeStartHeater(now);
      return;

    case Phase::Idle:
      break;
  }

  if ((int32_t)(now - nextStatusCheck) >= 0) {
    nextStatusCheck = now + STATUS_CHECK_MS;
    if (seen) checkStatus();
  }

  if ((int32_t)(now - nextSample) < 0) return;

  if (!command(CMD_MEASURE)) {
    state = "no answer";
    nextSample = now + SAMPLE_MS;
    return;
  }
  setPhase(Phase::Measuring, now);
}

Reading latest() {
  if (!last.valid) return Reading{};
  if ((millis() - lastAt) > STALE_MS) return Reading{};
  return last;
}

bool present() { return seen; }

const char *statusText() { return state; }

}  // namespace sht31
