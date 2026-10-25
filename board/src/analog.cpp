#include "analog.h"

#include "ads1115.h"
#include "config.h"
#include "i2cbus.h"

namespace {

const uint8_t INPUT_BATTERY = 0;
const uint8_t INPUT_BILGE = 1;

// R22, which the loop current flows through to ground.
const float BURDEN_OHMS = 100.0f;

// The lower fault limit of NAMUR NE 43. Below it nothing is driving the loop:
// no sender, a cut cable, or no 12 V. A property of the standard rather than
// of the boat, so it is not a setting.
const float SENDER_MIN_MA = 3.6f;
const float LOOP_ZERO_MA = 4.0f;
const float LOOP_SPAN_MA = 16.0f;

// A conversion still running this long after it should have finished is given
// up for this interval, and the next one starts afresh.
const uint32_t LATE_MS = 50;
const uint32_t POLL_MS = 2;

uint32_t sampleMs() {
  const uint16_t secs = config::get().sampleSecs;
  return (secs == 0 ? 1u : secs) * 1000UL;
}

// As the SHT31: three and a half intervals, floored at 30 s.
uint32_t staleMs() {
  const uint32_t scaled = (uint32_t)config::get().sampleSecs * 3500UL;
  return scaled < 30000UL ? 30000UL : scaled;
}

enum class Phase { Absent, Idle, Converting };

Phase phase = Phase::Absent;
uint8_t converting = INPUT_BATTERY;
uint32_t startedAt = 0;
uint32_t nextPoll = 0;
uint32_t nextSample = 0;

analog::Battery battery;
uint32_t batteryAt = 0;
bool batteryUnread = false;

analog::Bilge bilge;  // valid only while a sender drives the loop
uint32_t bilgeAt = 0;
bool bilgeUnread = false;

float loopMa = 0.0f;
uint32_t loopAt = 0;
bool loopConverted = false;
bool senderOn = false;

bool seen = false;
bool senderSeen = false;
bool lossSaid = false;
const char *state = "not started";

// A problem is logged when it starts, not every ten seconds while it lasts.
void setState(const char *s) {
  if (strcmp(s, state) != 0 && strcmp(s, "ok") != 0) Serial.printf("[ads1115] %s\n", s);
  state = s;
}

// The converter has stopped answering. Both readings drop out once they go
// stale; the other devices on the bus carry on regardless.
void lose() {
  if (!lossSaid) {
    Serial.printf("[ads1115] no answer at 0x%02X - battery and bilge stay out until it returns\n",
                  ads1115::ADDRESS);
    lossSaid = true;
  }
  state = "no answer";
  phase = Phase::Absent;
}

void found() {
  Serial.printf("[ads1115] converter at 0x%02X%s\n", ads1115::ADDRESS,
                seen ? ", answering again" : "");
  seen = true;
  lossSaid = false;
  state = "ok";
  phase = Phase::Idle;
}

// The loop current as a level. Clamped to the loop's range first: just under
// 4 mA is a sender reading a little low in air, and anything over 20 mA is a
// bilge at or beyond the sender's range - or a sender signalling a fault high,
// which reads as full, the safe direction to be wrong in.
float levelFor(float mA) {
  const Config &c = config::get();
  const float above = constrain(mA, LOOP_ZERO_MA, LOOP_ZERO_MA + LOOP_SPAN_MA) - LOOP_ZERO_MA;
  const float cm = above / LOOP_SPAN_MA * c.bilgeFullCm + c.bilgeOffsetMm / 10.0f;
  return cm > 0.0f ? cm : 0.0f;
}

void acceptBattery(float volts, uint32_t now) {
  battery.valid = true;
  battery.inputV = volts;
  battery.volts = volts * config::get().batCalMilli / 1000.0f;
  batteryAt = now;
  batteryUnread = true;
}

void acceptBilge(float volts, uint32_t now) {
  loopMa = volts / BURDEN_OHMS * 1000.0f;
  loopAt = now;
  loopConverted = true;

  const bool driven = loopMa >= SENDER_MIN_MA;
  if (driven != senderOn) {
    Serial.printf(driven ? "[ads1115] bilge sender on the loop, %.1f mA\n"
                         : "[ads1115] no bilge sender on the loop, %.1f mA\n",
                  loopMa);
    senderOn = driven;
  }

  // Nothing driving the loop is not a fault: the field simply stays out.
  if (!driven) {
    bilge = analog::Bilge{};
    bilgeUnread = false;
    return;
  }

  senderSeen = true;
  bilge.valid = true;
  bilge.loopMa = loopMa;
  bilge.levelCm = levelFor(loopMa);
  bilgeAt = now;
  bilgeUnread = true;
}

// Starts the conversion of one input, or counts the converter as gone.
void convert(uint8_t input, uint32_t now) {
  if (!ads1115::start(input)) {
    lose();
    return;
  }
  converting = input;
  startedAt = now;
  nextPoll = now + ads1115::CONVERSION_MS;
  phase = Phase::Converting;
}

// Collects the conversion under way, and starts the bilge once the battery is
// done - both within one interval.
void collect(uint32_t now) {
  if ((int32_t)(now - nextPoll) < 0) return;

  float volts = 0.0f;
  const ads1115::Result r = ads1115::collect(volts);
  if (r == ads1115::Result::NotReady && (now - startedAt) < ads1115::CONVERSION_MS + LATE_MS) {
    nextPoll = now + POLL_MS;
    return;
  }

  phase = Phase::Idle;
  switch (r) {
    case ads1115::Result::Ok:
      if (converting == INPUT_BATTERY) {
        acceptBattery(volts, now);
      } else {
        acceptBilge(volts, now);
      }
      setState("ok");
      break;
    case ads1115::Result::NoAnswer: lose(); return;
    case ads1115::Result::NotReady: setState("no conversion"); break;
    case ads1115::Result::Skipped: setState("reset itself, sample skipped"); break;
  }

  if (converting == INPUT_BATTERY) convert(INPUT_BILGE, now);
}

}  // namespace

namespace analog {

void begin() {
  i2cbus::begin();

  const uint32_t now = millis();
  nextSample = now + sampleMs();
  if (!ads1115::present()) {
    state = "not found";
    Serial.printf("[ads1115] nothing at 0x%02X\n", ads1115::ADDRESS);
    return;
  }
  found();

  // The first pair now, waited for. Whether a bilge sender is on the loop is
  // only known from a conversion, and the boot event lists what answered at
  // start. Twenty milliseconds, once, in setup().
  convert(INPUT_BATTERY, now);
  const uint32_t deadline = millis() + 2 * (ads1115::CONVERSION_MS + LATE_MS);
  while (phase == Phase::Converting && (int32_t)(millis() - deadline) < 0) {
    delay(1);
    collect(millis());
  }
}

void loop() {
  const uint32_t now = millis();

  if (phase == Phase::Converting) {
    collect(now);
    return;
  }

  if ((int32_t)(now - nextSample) < 0) return;
  nextSample = now + sampleMs();

  // A converter that stopped answering, or was never there, is looked for
  // again every interval, so a module plugged in later is found then.
  if (phase == Phase::Absent) {
    if (!ads1115::present()) {
      state = seen ? "no answer" : "not found";
      return;
    }
    found();
  }
  convert(INPUT_BATTERY, now);
}

Battery latestBattery() {
  if (!battery.valid) return Battery{};
  if ((millis() - batteryAt) > staleMs()) return Battery{};
  return battery;
}

Bilge latestBilge() {
  if (!bilge.valid) return Bilge{};
  if ((millis() - bilgeAt) > staleMs()) return Bilge{};
  return bilge;
}

bool takeBattery(Battery &out) {
  if (!batteryUnread) return false;
  batteryUnread = false;
  out = battery;
  return true;
}

bool takeBilge(Bilge &out) {
  if (!bilgeUnread) return false;
  bilgeUnread = false;
  out = bilge;
  return true;
}

bool loopCurrent(float &mA) {
  if (!loopConverted || (millis() - loopAt) > staleMs()) return false;
  mA = loopMa;
  return true;
}

bool present() { return seen; }

bool bilgeFitted() { return senderSeen; }

const char *statusText() { return state; }

}  // namespace analog
