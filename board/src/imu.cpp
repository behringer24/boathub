#include "imu.h"

#include <Preferences.h>

#include "attitude.h"
#include "config.h"
#include "i2cbus.h"
#include "lsm6dsox.h"

namespace {

using attitude::Vec3;

const int PIN_INT1 = 2;  // from the pin plan, see README.md

const uint32_t READ_MS = 40;  // 25 Hz

// Once the filter has settled, the gyroscope's offset is learned over a
// minute - slow enough that a seaway averages out of it.
const float BIAS_TAU_S = 60.0f;

// A pass through loop() can take far longer than 40 ms: a failed MQTT connect
// holds it for three seconds (A-005). Integrating the last rate of turn over
// such a gap would swing the estimate by that rate times three seconds, so a
// long gap counts as one ordinary step and the accelerometer closes the rest.
const float MAX_STEP_S = 0.1f;

// Failed reads in a row before the sensor counts as gone: a second's worth.
const uint8_t MAX_FAILS = 25;

// A latched line that is high is read at most this often. A stuck line would
// otherwise have loop() reading the source register on every pass.
const uint32_t KNOCK_CHECK_MS = 10;

Preferences store;
const char *NS = "imu";

bool found = false;
bool wired = false;

attitude::Filter filter;
attitude::Level level;

// The boat's axes, expressed in the module's own: forward, to port, up.
Vec3 fwd = Vec3{};
Vec3 port = Vec3{};
Vec3 up = Vec3{};

uint32_t nextRead = 0;
uint32_t lastReadUs = 0;
uint8_t fails = 0;

uint32_t lastKnockCheck = 0;
uint32_t lastKnockMs = 0;
bool knocked = false;
uint32_t knocksHeld = 0;  // detected during a hold-off, reported with the next one
uint8_t emptyHighs = 0;   // line high with nothing latched behind it

Vec3 axisVector(uint16_t code) {
  const float sign = (code & 1) ? -1.0f : 1.0f;
  switch (code >> 1) {
    case 0: return Vec3{sign, 0.0f, 0.0f};
    case 1: return Vec3{0.0f, sign, 0.0f};
    default: return Vec3{0.0f, 0.0f, sign};
  }
}

const char *axisName(uint16_t code) {
  static const char *NAMES[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
  return code < 6 ? NAMES[code] : "?";
}

// Forward and up have to be two different axes; port follows from them, so
// the boat's axes stay right-handed like the chip's.
void loadMapping() {
  const Config &c = config::get();
  uint16_t f = c.imuForward;
  uint16_t u = c.imuUp;
  if (f > 5 || u > 5 || (f >> 1) == (u >> 1)) {
    Serial.printf("[imu] axis setting %u/%u is not two different axes - using +X forward, +Z up\n",
                  f, u);
    f = 0;
    u = 4;
  }
  fwd = axisVector(f);
  up = axisVector(u);
  port = attitude::cross(up, fwd);
  Serial.printf("[imu] %s forward, %s up\n", axisName(f), axisName(u));
}

Vec3 toBoat(const float v[3]) {
  const Vec3 m{v[0], v[1], v[2]};
  return Vec3{attitude::dot(m, fwd), attitude::dot(m, port), attitude::dot(m, up)};
}

Vec3 toModule(const Vec3 &b) {
  return Vec3{fwd.x * b.x + port.x * b.y + up.x * b.z, fwd.y * b.x + port.y * b.y + up.y * b.z,
              fwd.z * b.x + port.z * b.y + up.z * b.z};
}

float component(const Vec3 &v, uint8_t i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

// Which of the boat's axes a module axis lies along, for the log.
const char *boatAxisOf(uint8_t moduleAxis) {
  if (component(fwd, moduleAxis) != 0.0f) return "fore-and-aft";
  if (component(port, moduleAxis) != 0.0f) return "athwartships";
  return "vertical";
}

// The zero reference is kept in the module's own axes, so that correcting the
// axis setting later does not throw it away: it is a fact about how the
// module sits, not about which way it was said to face.
void loadLevel() {
  if (!store.isKey("lvl_x")) return;
  const float ref[3] = {store.getFloat("lvl_x"), store.getFloat("lvl_y"), store.getFloat("lvl_z")};
  level.set(toBoat(ref));
}

// With nothing pending the chip holds INT1 at its inactive level. Invert that
// level and the line should follow; GPIO2 is pulled down, so a missing wire
// reads low both times. Only once the knock detector is armed - until then the
// chip does not drive the pin at all.
bool checkWire() {
  lsm6dsox::setInterruptActiveLow(false);
  delay(2);
  const int idle = digitalRead(PIN_INT1);
  lsm6dsox::setInterruptActiveLow(true);
  delay(2);
  const int inverted = digitalRead(PIN_INT1);
  lsm6dsox::setInterruptActiveLow(false);
  delay(2);
  lsm6dsox::takeKnock();  // anything that latched meanwhile
  return idle == LOW && inverted == HIGH;
}

bool gone() { return fails >= MAX_FAILS; }

void sample() {
  const uint32_t nowUs = micros();
  lsm6dsox::Sample s;
  if (!lsm6dsox::read(s)) {
    if (fails < MAX_FAILS && ++fails == MAX_FAILS) {
      filter.reset();
      lastReadUs = 0;
      Serial.println("[imu] no answer - attitude dropped until it returns");
    }
    return;
  }
  if (gone()) Serial.println("[imu] answering again - attitude settles from gravity");
  fails = 0;

  float dt = lastReadUs ? (float)(nowUs - lastReadUs) / 1e6f : 0.0f;
  if (dt > MAX_STEP_S) dt = (float)READ_MS / 1000.0f;
  lastReadUs = nowUs;

  filter.update(toBoat(s.accelG), toBoat(s.gyroDps), dt);
}

void onKnock(uint8_t axes, uint32_t now) {
  sample();  // the nearest sample to the knock, for its size
  const float size = filter.motionG();

  const uint32_t holdMs = (uint32_t)config::get().imuHoldoffSecs * 1000UL;
  if (knocked && (now - lastKnockMs) < holdMs) {
    knocksHeld++;
    return;
  }
  knocked = true;
  lastKnockMs = now;

  // Bits from the chip: x = 4, y = 2, z = 1.
  Serial.print("[imu] knock,");
  bool first = true;
  for (uint8_t i = 0; i < 3; i++) {
    if (!(axes & (4 >> i))) continue;
    Serial.printf("%s %s", first ? "" : " and", boatAxisOf(i));
    first = false;
  }
  Serial.printf(", %.2f g", size);
  if (knocksHeld) Serial.printf(" - %lu more during the hold-off before it", (unsigned long)knocksHeld);
  Serial.println();
  knocksHeld = 0;
}

void checkKnock(uint32_t now) {
  if (digitalRead(PIN_INT1) != HIGH) return;
  if ((now - lastKnockCheck) < KNOCK_CHECK_MS) return;
  lastKnockCheck = now;

  const uint8_t axes = lsm6dsox::takeKnock();
  if (axes) {
    emptyHighs = 0;
    onKnock(axes, now);
    return;
  }

  // High, but the chip has nothing latched: the line is held up from outside.
  // Said once, not a hundred times a second.
  if (emptyHighs < 100 && ++emptyHighs == 100) {
    Serial.println("[imu] INT1 is high with no knock behind it - is the line stuck?");
  }
}

}  // namespace

namespace imu {

void begin() {
  i2cbus::begin();
  pinMode(PIN_INT1, INPUT_PULLDOWN);
  store.begin(NS, /*readOnly=*/false);

  if (!lsm6dsox::begin()) {
    Serial.println("[imu] nothing at 0x6A or 0x6B");
    return;
  }
  found = true;

  const Config &c = config::get();
  loadMapping();
  filter.configure((float)c.imuTauDs / 10.0f, BIAS_TAU_S);
  loadLevel();

  const float armed = lsm6dsox::armKnock((float)c.imuKnockMg / 1000.0f);
  wired = checkWire();

  Serial.printf("[imu] LSM6DSOX at 0x%02X, tau %.1f s, knock at a jump of %.3f g\n",
                lsm6dsox::address(), (float)c.imuTauDs / 10.0f, armed);
  if (!wired) Serial.println("[imu] INT1 does not follow the chip - knocks will go undetected");
  if (!level.isSet()) Serial.println("[imu] not levelled - heel and pitch stay out until it is");
}

void loop() {
  if (!found) return;
  const uint32_t now = millis();

  if ((int32_t)(now - nextRead) >= 0) {
    nextRead = now + READ_MS;
    sample();
  }
  checkKnock(now);
}

Reading latest() {
  Reading r;
  if (!found || gone() || !filter.started()) return r;

  r.valid = true;
  r.motionG = filter.motionG();
  r.level = level.isSet();
  if (r.level) {
    const Vec3 u = level.apply(filter.up());
    r.heelDeg = attitude::heelDeg(u);
    r.pitchDeg = attitude::pitchDeg(u);
  }
  return r;
}

bool present() { return found; }

const char *statusText() {
  if (!found) return "not found";
  if (gone()) return "no answer";
  if (filter.settling()) return wired ? "settling" : "settling, no INT1";
  if (!level.isSet()) return wired ? "not level" : "not level, no INT1";
  return wired ? "ok" : "ok, no INT1";
}

bool levelNow() {
  if (!found || gone() || filter.settling()) return false;

  const Vec3 ref = toModule(filter.up());
  store.putFloat("lvl_x", ref.x);
  store.putFloat("lvl_y", ref.y);
  store.putFloat("lvl_z", ref.z);
  level.set(filter.up());

  Serial.println("[imu] levelled - heel and pitch are measured from here");
  return true;
}

}  // namespace imu
