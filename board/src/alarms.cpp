#include "alarms.h"

#include "barometer.h"
#include "config.h"
#include "ds18b20.h"
#include "events.h"
#include "sht31.h"
#include "status.h"

namespace {

using events::Alarm;
using events::Sensor;
using events::Severity;

// Constants rather than settings - A-012 section 6.
const float FROST_CLEAR_ABOVE_K = 1.0f;
const uint32_t HUMIDITY_CLEAR_MS = 30UL * 60UL * 1000UL;

const uint8_t COUNT = (uint8_t)Alarm::Count;

// A condition that has to hold, reading after reading, before it counts.
//
// A missing reading neither starts nor breaks a run: a lost sensor does not
// prove the cabin warmed up. A gap longer than the hold itself does end it,
// so a condition cannot be completed by two readings hours apart.
struct Run {
  bool active = false;
  uint32_t since = 0;
  uint32_t lastSeen = 0;

  bool held(bool hasReading, bool condition, uint32_t now, uint32_t holdMs) {
    if (!hasReading) {
      if (active && (now - lastSeen) > holdMs) active = false;
      return false;
    }
    lastSeen = now;
    if (!condition) {
      active = false;
      return false;
    }
    if (!active) {
      active = true;
      since = now;
    }
    return (now - since) >= holdMs;
  }

  void reset() { active = false; }
};

struct State {
  bool raised = false;
  bool acked = false;
};

const Severity SEVERITY[COUNT] = {Severity::Alarm, Severity::Alarm, Severity::Warning};

State state[COUNT];

Run frostRun, frostClearRun;
Run fridgeArmRun, fridgeWarmRun;
Run humidRun, humidClearRun;

enum class Fridge { Disarmed, Armed, Raised };
Fridge fridge = Fridge::Disarmed;

uint32_t nextEval = 0;

uint32_t minutes(uint16_t m) { return (uint32_t)m * 60UL * 1000UL; }

uint32_t sampleMs() {
  const uint16_t secs = config::get().sampleSecs;
  return (secs == 0 ? 1u : secs) * 1000UL;
}

// Red while any raised alarm - not a warning - waits to be acknowledged.
void showLed() {
  bool show = false;
  for (uint8_t i = 0; i < COUNT; i++) {
    if (state[i].raised && !state[i].acked && SEVERITY[i] == Severity::Alarm) show = true;
  }
  status::setAlarm(show);
}

void raise(Alarm a, Sensor s, float value, float limit) {
  State &st = state[(uint8_t)a];
  st.raised = true;
  st.acked = false;
  events::alarm(a, true, SEVERITY[(uint8_t)a], s, value, limit);
  showLed();
}

void clear(Alarm a, Sensor s, float value, float limit) {
  State &st = state[(uint8_t)a];
  st.raised = false;
  st.acked = false;
  events::alarm(a, false, SEVERITY[(uint8_t)a], s, value, limit);
  showLed();
}

// The coldest of the sensors that can see frost coming. The fridge is not one
// of them.
bool coldest(float &tempC, Sensor &which) {
  bool any = false;
  auto consider = [&](bool valid, float t, Sensor s) {
    if (!valid) return;
    if (!any || t < tempC) {
      tempC = t;
      which = s;
    }
    any = true;
  };

  const sht31::Reading cabin = sht31::latest();
  consider(cabin.valid, cabin.tempC, Sensor::Cabin);
  const ds18b20::Reading engine = ds18b20::latest(ds18b20::Engine);
  consider(engine.valid, engine.tempC, Sensor::Engine);
  const ds18b20::Reading bilge = ds18b20::latest(ds18b20::Bilge);
  consider(bilge.valid, bilge.tempC, Sensor::Bilge);
  const barometer::Reading bay = barometer::latest();
  consider(bay.valid, bay.tempC, Sensor::Bay);
  return any;
}

void judgeFrost(const Config &c, uint32_t now) {
  float t = 0.0f;
  Sensor which = Sensor::None;
  const bool has = coldest(t, which);
  const float limit = c.frostC10 / 10.0f;
  const float clearAt = limit + FROST_CLEAR_ABOVE_K;
  const uint32_t hold = minutes(c.frostMins);

  if (!state[(uint8_t)Alarm::Frost].raised) {
    if (frostRun.held(has, t < limit, now, hold)) {
      frostClearRun.reset();
      raise(Alarm::Frost, which, t, limit);
    }
  } else if (frostClearRun.held(has, t >= clearAt, now, hold)) {
    frostRun.reset();
    clear(Alarm::Frost, which, t, clearAt);
  }
}

// Armed by being cold, so that a fridge switched off for an empty boat raises
// nothing - unless it was running, and then once.
void judgeFridge(const Config &c, uint32_t now) {
  const ds18b20::Reading f = ds18b20::latest(ds18b20::Fridge);
  const float armBelow = c.fridgeArmC10 / 10.0f;
  const float warmAbove = c.fridgeWarmC10 / 10.0f;

  switch (fridge) {
    case Fridge::Disarmed:
      if (fridgeArmRun.held(f.valid, f.tempC < armBelow, now, minutes(c.fridgeArmMins))) {
        fridgeArmRun.reset();
        fridge = Fridge::Armed;
        Serial.printf("[alarms] fridge armed - below %.1f C for %u min\n", armBelow,
                      c.fridgeArmMins);
      }
      return;

    case Fridge::Armed:
      if (fridgeWarmRun.held(f.valid, f.tempC > warmAbove, now, minutes(c.fridgeWarmMins))) {
        fridgeWarmRun.reset();
        fridge = Fridge::Raised;
        raise(Alarm::FridgeWarm, Sensor::Fridge, f.tempC, warmAbove);
      }
      return;

    case Fridge::Raised:
      // Raising it disarmed it: a fridge switched off on purpose alarms once,
      // and not again until it has been cold and armed itself afresh.
      if (f.valid && f.tempC < armBelow) {
        fridge = Fridge::Disarmed;
        clear(Alarm::FridgeWarm, Sensor::Fridge, f.tempC, armBelow);
      }
      return;
  }
}

void judgeHumidity(const Config &c, uint32_t now) {
  const sht31::Reading r = sht31::latest();
  const float warn = (float)c.rhWarn;
  const float clearBelow = (float)c.rhClear;

  if (!state[(uint8_t)Alarm::Humidity].raised) {
    if (humidRun.held(r.valid, r.rh >= warn, now, (uint32_t)c.rhHours * 60UL * 60UL * 1000UL)) {
      humidClearRun.reset();
      raise(Alarm::Humidity, Sensor::Cabin, r.rh, warn);
    }
  } else if (humidClearRun.held(r.valid, r.rh < clearBelow, now, HUMIDITY_CLEAR_MS)) {
    humidRun.reset();
    clear(Alarm::Humidity, Sensor::Cabin, r.rh, clearBelow);
  }
}

}  // namespace

namespace alarms {

void begin() {
  const Config &c = config::get();
  Serial.printf("[alarms] frost %s, fridge %s, humidity %s\n", c.alarmFrost ? "on" : "off",
                c.alarmFridge ? "on" : "off", c.alarmHumidity ? "on" : "off");
  nextEval = millis() + sampleMs();
}

void loop() {
  const uint32_t now = millis();
  if ((int32_t)(now - nextEval) < 0) return;
  nextEval = now + sampleMs();

  const Config &c = config::get();
  if (c.alarmFrost) judgeFrost(c, now);
  if (c.alarmFridge) judgeFridge(c, now);
  if (c.alarmHumidity) judgeHumidity(c, now);
}

void acknowledge() {
  for (uint8_t i = 0; i < COUNT; i++) {
    if (state[i].raised) state[i].acked = true;
  }
  events::ack();
  showLed();
}

String raised() {
  String out;
  for (uint8_t i = 0; i < COUNT; i++) {
    if (!state[i].raised) continue;
    if (out.length()) out += ", ";
    out += events::alarmName((Alarm)i);
  }
  return out.length() ? out : String("none");
}

const char *fridgeState() {
  if (!config::get().alarmFridge) return "off";
  switch (fridge) {
    case Fridge::Armed: return "armed";
    case Fridge::Raised: return "raised";
    default: return "disarmed";
  }
}

}  // namespace alarms
