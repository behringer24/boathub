#include "barometer.h"

#include "bme680.h"
#include "config.h"
#include "i2cbus.h"

namespace {

// A measurement that has not reported new data this long after it was due is
// given up: the chip was reset in the middle of it, and the next interval
// writes the configuration again.
const uint32_t LATE_MS = 200;

// How often to look again while a measurement is late. Every pass through
// loop() would be a ten-byte read a millisecond, on a bus the converters and
// the IMU share.
const uint32_t POLL_MS = 10;

uint32_t sampleMs() {
  const uint16_t secs = config::get().sampleSecs;
  return (secs == 0 ? 1u : secs) * 1000UL;
}

// As the SHT31: three and a half intervals, floored at 30 s.
uint32_t staleMs() {
  const uint32_t scaled = (uint32_t)config::get().sampleSecs * 3500UL;
  return scaled < 30000UL ? 30000UL : scaled;
}

enum class Phase { Absent, Resetting, Idle, Measuring };

Phase phase = Phase::Absent;
uint32_t phaseSince = 0;
uint32_t nextSample = 0;
uint32_t nextPoll = 0;
bool gasThisTime = false;

barometer::Reading last;
uint32_t lastAt = 0;
bool unread = false;

// The previous reading that passed the range checks, accepted or not.
//
// The rate check compares against this, not against the last accepted value.
// Against the last accepted value, one real step larger than the limit - a
// sensor unplugged for an hour while a front came through - would be rejected,
// and so would every reading after it, forever. Against the previous reading a
// spike costs two samples, a real step one, and nothing can lock the sensor out.
bool havePrev = false;
uint32_t prevAt = 0;
float prevHpa = 0.0f;
float prevTempC = 0.0f;
float prevRh = 0.0f;

bool seen = false;
bool lossSaid = false;
bool wrongChipSaid = false;
bool bme688Said = false;
const char *state = "not started";

// A rejection is logged when it starts, not every ten seconds while it lasts.
void setState(const char *s) {
  if (strcmp(s, state) != 0 && strcmp(s, "ok") != 0) Serial.printf("[bme680] %s\n", s);
  state = s;
}

void setPhase(Phase p, uint32_t now) {
  phase = p;
  phaseSince = now;
}

// The sensor has stopped answering. Its fields drop out once the last reading
// goes stale; the other devices on the bus carry on regardless.
void lose(uint32_t now) {
  if (!lossSaid) {
    Serial.printf("[bme680] no answer at 0x%02X - its fields stay out until it returns\n",
                  bme680::ADDRESS);
    lossSaid = true;
  }
  state = "no answer";
  havePrev = false;
  setPhase(Phase::Absent, now);
}

// Is there a BME680 at the address? If so, reset it and come back for the
// calibration once the reset is done.
void look(uint32_t now) {
  const uint8_t id = bme680::identify();
  if (id == 0) {
    state = seen ? "no answer" : "not found";
    return;
  }
  if (id != bme680::CHIP_ID) {
    if (!wrongChipSaid) {
      Serial.printf("[bme680] 0x%02X answers with chip ID 0x%02X - not a BME680, not read. "
                    "i2cscan names it\n",
                    bme680::ADDRESS, id);
      wrongChipSaid = true;
    }
    state = "not a BME680";
    return;
  }
  if (!bme680::reset()) return;
  setPhase(Phase::Resetting, now);
}

void ready(uint32_t now) {
  Serial.printf("[bme680] %s at 0x%02X%s\n", bme680::isBme688() ? "BME688" : "BME680",
                bme680::ADDRESS, seen ? ", answering again" : "");
  if (bme680::isBme688() && config::get().bmeGas && !bme688Said) {
    Serial.println("[bme680] gas is read on a BME680 only - the heater stays off on this BME688");
    bme688Said = true;
  }
  seen = true;
  lossSaid = false;
  state = "ok";
  setPhase(Phase::Idle, now);
  nextSample = now;
}

void check(const bme680::Sample &s, uint32_t now) {
  const Config &c = config::get();
  const float hpa = (s.pressurePa + (float)c.bmePressureOffsetPa) / 100.0f;

  // The sensor's own pressure range, and what the battery bay of a boat in
  // use can plausibly reach. Outside these it is a fault, not a reading.
  if (hpa < 300.0f || hpa > 1100.0f || s.tempC < -20.0f || s.tempC > 60.0f || s.rh < 0.0f ||
      s.rh > 100.0f) {
    setState("implausible");
    return;
  }

  // A jump this large in one interval is not weather and not air moving.
  // Only between consecutive readings: after a gap there is nothing to
  // compare with.
  const bool consecutive = havePrev && (now - prevAt) <= 2 * sampleMs();
  const bool jumped = consecutive && (fabsf(hpa - prevHpa) > 2.0f ||
                                      fabsf(s.tempC - prevTempC) > 10.0f ||
                                      fabsf(s.rh - prevRh) > 30.0f);
  havePrev = true;
  prevAt = now;
  prevHpa = hpa;
  prevTempC = s.tempC;
  prevRh = s.rh;
  if (jumped) {
    setState("jumped");
    return;
  }

  last.valid = true;
  last.pressureHpa = hpa;
  last.tempC = s.tempC;
  last.rh = s.rh;
  last.gasValid = s.gasValid;
  last.gasKohm = s.gasKohm;
  lastAt = now;
  unread = true;
  setState("ok");
}

}  // namespace

namespace barometer {

void begin() {
  i2cbus::begin();

  const uint32_t now = millis();
  if (bme680::begin()) {
    ready(now);
    return;
  }
  // Not there, not a BME680, or it did not finish starting. loop() looks
  // again every interval, so a module plugged in later is found then.
  look(now);
  if (phase == Phase::Absent && strcmp(state, "not found") == 0) {
    Serial.printf("[bme680] nothing at 0x%02X\n", bme680::ADDRESS);
  }
  nextSample = now + sampleMs();
}

void loop() {
  const uint32_t now = millis();

  switch (phase) {
    case Phase::Resetting:
      if ((now - phaseSince) < bme680::RESET_MS) return;
      if (bme680::readCalibration()) {
        ready(now);
      } else {
        lose(now);
      }
      return;

    case Phase::Measuring: {
      if ((int32_t)(now - nextPoll) < 0) return;
      bme680::Sample s;
      const bme680::Result r = bme680::collect(s, gasThisTime);
      if (r == bme680::Result::NotReady &&
          (now - phaseSince) < bme680::durationMs(gasThisTime) + LATE_MS) {
        nextPoll = now + POLL_MS;
        return;
      }
      setPhase(Phase::Idle, now);
      switch (r) {
        case bme680::Result::Ok: check(s, now); break;
        case bme680::Result::NoAnswer: lose(now); break;
        case bme680::Result::NotReady: setState("no new data"); break;
        case bme680::Result::Skipped: setState("reset itself, sample skipped"); break;
      }
      return;
    }

    case Phase::Absent:
    case Phase::Idle:
      break;
  }

  if ((int32_t)(now - nextSample) < 0) return;
  nextSample = now + sampleMs();

  if (phase == Phase::Absent) {
    look(now);
    return;
  }

  // The heater, if it is switched on at all, is set for the air the last
  // reading found.
  gasThisTime = config::get().bmeGas;
  if (!bme680::start(gasThisTime, last.valid ? last.tempC : 25.0f)) {
    lose(now);
    return;
  }
  setPhase(Phase::Measuring, now);
  nextPoll = now + bme680::durationMs(gasThisTime);
}

Reading latest() {
  if (!last.valid) return Reading{};
  if ((millis() - lastAt) > staleMs()) return Reading{};
  return last;
}

bool takeFresh(Reading &out) {
  if (!unread) return false;
  unread = false;
  out = last;
  return true;
}

bool present() { return seen; }

const char *statusText() { return state; }

}  // namespace barometer
