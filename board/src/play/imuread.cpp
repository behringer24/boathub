// ESP32 BoatHub - IMU reader
//
// A bench tool, not firmware. Build and flash it with
//
//   pio run -d board -e imuread -t upload -t monitor
//
// Three questions, answered on the desk before the module goes anywhere near
// the boat:
//
//   1. Is the INT1 wire connected? Checked once at start, by making the chip
//      drive the line low and then high and watching GPIO2 follow.
//   2. Do the axes match the print on the module? The "up" column names the
//      axis gravity pulls along. Stand the module on each edge in turn and it
//      should name the arrow pointing at the ceiling.
//   3. Does the filter behave? Heel and pitch are printed beside what gravity
//      alone says. Shake the module: the bracketed figures jump about, the
//      filtered ones barely move. That is the difference between a working
//      fusion and a bare accelerometer.
//
// Heel and pitch assume the provisional mounting: X toward the bow, Y to port,
// the component side up. The firmware will take that from a setting; here it
// is fixed.
//
// Keys: z = level now, c = clear the level, r = restart the filter.

#include <Arduino.h>

#include "attitude.h"
#include "i2cbus.h"
#include "lsm6dsox.h"

static const int PIN_INT1 = 2;  // from the pin plan, see README.md

static const uint32_t READ_MS = 40;    // 25 Hz, as the firmware will poll
static const uint32_t PRINT_MS = 500;
static const float TAU_S = 3.0f;       // the starting point A-009 gives
static const float BIAS_TAU_S = 60.0f;
static const float KNOCK_JUMP_G = 0.5f;

static bool found = false;
static attitude::Filter filter;
static attitude::Level level;
static lsm6dsox::Sample last;
static uint32_t lastReadUs = 0;

static attitude::Vec3 vec(const float v[3]) { return {v[0], v[1], v[2]}; }

// The axis gravity pulls along, with its sign: "+Z" lying flat, "+X" standing
// on the edge with the X arrow pointing up.
static void upAxis(const float a[3], char out[3]) {
  uint8_t best = 0;
  for (uint8_t i = 1; i < 3; i++) {
    if (fabsf(a[i]) > fabsf(a[best])) best = i;
  }
  out[0] = a[best] >= 0.0f ? '+' : '-';
  out[1] = (char)('X' + best);
  out[2] = '\0';
}

// With nothing pending the chip holds INT1 at its inactive level. Invert that
// level and the line should follow. GPIO2 is pulled down, so a missing wire
// reads low both times - and a stuck line reads the same both times either way.
//
// Only once the knock detector is armed: until something is routed to INT1 the
// chip leaves the pin undriven, and it reads low whatever the polarity.
static void checkInterruptWire() {
  lsm6dsox::setInterruptActiveLow(false);
  delay(2);
  const int idle = digitalRead(PIN_INT1);
  lsm6dsox::setInterruptActiveLow(true);
  delay(2);
  const int inverted = digitalRead(PIN_INT1);
  lsm6dsox::setInterruptActiveLow(false);
  delay(2);
  lsm6dsox::takeKnock();  // anything that latched meanwhile

  Serial.printf("INT1 -> GPIO%d: %s idle, %s inverted - ", PIN_INT1, idle ? "high" : "low",
                inverted ? "high" : "low");
  if (idle == LOW && inverted == HIGH) {
    Serial.println("connected");
  } else if (idle == LOW && inverted == LOW) {
    Serial.println("NOT CONNECTED: no wire, or it is on the wrong pad");
  } else if (idle == HIGH && inverted == HIGH) {
    Serial.println("STUCK HIGH: check the wire is not on 3.3 V");
  } else {
    Serial.println("INVERTED: unexpected - check the wiring");
  }
}

static void takeSample() {
  const uint32_t now = micros();
  lsm6dsox::Sample s;
  if (!lsm6dsox::read(s)) return;

  const float dt = lastReadUs ? (float)(now - lastReadUs) / 1e6f : 0.0f;
  lastReadUs = now;
  last = s;
  filter.update(vec(s.accelG), vec(s.gyroDps), dt);
}

static void printLine() {
  char up[3];
  upAxis(last.accelG, up);

  // What gravity alone says, through the same level, for comparison.
  const attitude::Vec3 bare = level.apply(attitude::normalised(vec(last.accelG)));
  const attitude::Vec3 fused = level.apply(filter.up());
  const attitude::Vec3 bias = filter.biasDps();

  Serial.printf("up %s | a %+6.3f %+6.3f %+6.3f g | w %+6.2f %+6.2f %+6.2f dps | "
                "heel %+5.1f (%+5.1f) | pitch %+5.1f (%+5.1f) | motion %.3f g | "
                "bias %+5.2f %+5.2f %+5.2f\n",
                up, last.accelG[0], last.accelG[1], last.accelG[2], last.gyroDps[0],
                last.gyroDps[1], last.gyroDps[2], attitude::heelDeg(fused), attitude::heelDeg(bare),
                attitude::pitchDeg(fused), attitude::pitchDeg(bare), filter.motionG(), bias.x,
                bias.y, bias.z);
}

static void handleKey(int key) {
  switch (key) {
    case 'z':
      level.set(filter.up());
      Serial.println("--- level set: heel and pitch are measured from here");
      break;
    case 'c':
      level.clear();
      Serial.println("--- level cleared: heel and pitch are the module's own");
      break;
    case 'r':
      filter.reset();
      Serial.println("--- filter restarted: the gyro bias is learned again");
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== IMU reader ===");
  i2cbus::begin();

  if (!lsm6dsox::begin()) {
    Serial.println("no LSM6DSOX at 0x6A or 0x6B - run the i2cscan environment first");
    return;
  }
  found = true;
  Serial.printf("LSM6DSOX at 0x%02X\n", lsm6dsox::address());
  Serial.printf("accel +-%.0f g at %.0f Hz, gyro +-%.0f dps at %.0f Hz, read at %lu Hz\n",
                lsm6dsox::ACCEL_RANGE_G, lsm6dsox::ACCEL_HZ, lsm6dsox::GYRO_RANGE_DPS,
                lsm6dsox::GYRO_HZ, (unsigned long)(1000 / READ_MS));

  pinMode(PIN_INT1, INPUT_PULLDOWN);
  const float armed = lsm6dsox::armKnock(KNOCK_JUMP_G);
  Serial.printf("knock detector: a jump of %.3f g between two samples, on INT1\n", armed);
  checkInterruptWire();

  filter.configure(TAU_S, BIAS_TAU_S);

  Serial.println();
  Serial.println("Axes as printed on the module. Heel and pitch assume X toward the bow");
  Serial.println("and the component side up; gravity alone in brackets.");
  Serial.println("The gyro bias settles within the first half minute - keep it still.");
  Serial.println("Keys: z = level now, c = clear level, r = restart filter.");
  Serial.println();
}

void loop() {
  if (!found) {
    delay(1);
    return;
  }

  const uint32_t now = millis();
  static uint32_t nextRead = 0;
  static uint32_t nextPrint = 0;

  if ((int32_t)(now - nextRead) >= 0) {
    nextRead = now + READ_MS;
    takeSample();
  }

  // The chip holds the line high until the source is read, so looking at the
  // pin on every pass is enough - no interrupt routine is needed to catch it.
  if (digitalRead(PIN_INT1) == HIGH) {
    const uint8_t axes = lsm6dsox::takeKnock();
    if (axes) {
      takeSample();  // the nearest sample to the knock, for its size
      Serial.printf("*** knock on%s%s%s, motion now %.2f g\n", (axes & 4) ? " X" : "",
                    (axes & 2) ? " Y" : "", (axes & 1) ? " Z" : "", filter.motionG());
    }
  }

  if ((int32_t)(now - nextPrint) >= 0) {
    nextPrint = now + PRINT_MS;
    printLine();
  }

  if (Serial.available()) handleKey(Serial.read());

  delay(1);  // hand the core back to FreeRTOS
}
