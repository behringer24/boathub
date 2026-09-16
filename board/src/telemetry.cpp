#include "telemetry.h"

#include "config.h"
#include "ds18b20.h"
#include "sht31.h"

namespace {

telemetry::Aggregate filling;  // the window being filled
telemetry::Aggregate ready;  // a closed window waiting to be collected

uint32_t windowStart = 0;
uint32_t droppedWindows = 0;
char state[40] = "starting";

// Collect whatever the sensors have produced since the last pass.
//
// There is deliberately no clock here. Each sensor measures on its own
// schedule and hands each result over exactly once, so the window counts
// measurements rather than polls. A timer of its own would drift against the
// sensors' timers, and a reading would sometimes be counted twice and
// sometimes skipped - which would quietly make the sample count in every
// message a different thing from what it claims to be.
//
// A sensor with nothing new adds nothing: its channel keeps a count of zero
// and is left out of the message entirely. "No sensor fitted" and "measured
// zero" must not look the same anywhere in this system.
void collectInto(telemetry::Aggregate &agg) {
  sht31::Reading climate;
  if (sht31::takeFresh(climate)) {
    agg.cabinTemp.add(climate.tempC);
    agg.cabinRh.add(climate.rh);
  }

  ds18b20::Reading probe;
  if (ds18b20::takeFresh(ds18b20::Engine, probe)) agg.engineTemp.add(probe.tempC);
  if (ds18b20::takeFresh(ds18b20::Bilge, probe)) agg.bilgeTemp.add(probe.tempC);
  if (ds18b20::takeFresh(ds18b20::Fridge, probe)) agg.fridgeTemp.add(probe.tempC);
}

// The current state rather than a new measurement: a spot message must not
// wait for the next one to come round.
void snapshotInto(telemetry::Aggregate &agg) {
  const sht31::Reading climate = sht31::latest();
  if (climate.valid) {
    agg.cabinTemp.add(climate.tempC);
    agg.cabinRh.add(climate.rh);
  }

  const ds18b20::Reading engine = ds18b20::latest(ds18b20::Engine);
  if (engine.valid) agg.engineTemp.add(engine.tempC);
  const ds18b20::Reading bilge = ds18b20::latest(ds18b20::Bilge);
  if (bilge.valid) agg.bilgeTemp.add(bilge.tempC);
  const ds18b20::Reading fridge = ds18b20::latest(ds18b20::Fridge);
  if (fridge.valid) agg.fridgeTemp.add(fridge.tempC);
}

// How many samples to claim for the window as a whole.
//
// Each channel has its own count, and they can differ - the SHT31 takes no
// samples while its heater runs, while a 1-Wire probe alongside it carries on.
// The message carries one number, so it carries the **smallest**: it answers
// "is any average in this row built on fewer samples than it should be?",
// which is the question worth asking. Overstating it would hide exactly the
// fault the number exists to expose.
uint16_t countFor(const telemetry::Aggregate &agg) {
  uint16_t lowest = 0;
  bool any = false;

  const telemetry::Channel *channels[] = {&agg.cabinTemp,  &agg.cabinRh,   &agg.engineTemp,
                                         &agg.bilgeTemp, &agg.fridgeTemp};
  for (const telemetry::Channel *c : channels) {
    if (!c->has()) continue;
    if (!any || c->n < lowest) lowest = c->n;
    any = true;
  }
  return any ? lowest : 0;
}

}  // namespace

namespace telemetry {

void begin() {
  const uint32_t now = millis();
  windowStart = now;
  filling = Aggregate{};
  snprintf(state, sizeof(state), "window filling");
}

void loop() {
  const Config &c = config::get();
  const uint32_t now = millis();

  const uint32_t windowMs = (uint32_t)c.pubSecs * 1000UL;

  collectInto(filling);

  if ((now - windowStart) < windowMs) return;

  // Close the window.
  filling.windowS = (now - windowStart) / 1000;
  filling.n = countFor(filling);
  filling.valid = true;

  if (ready.valid) {
    // Nothing collected the previous one. Keep the newer - without a buffer
    // the fresher state is the more useful of the two - and say so, because a
    // window quietly disappearing is the failure this whole design is against.
    droppedWindows++;
    Serial.printf("[telemetry] window dropped, %lu total - uplink was not ready\n",
                  (unsigned long)droppedWindows);
  }
  ready = filling;

  snprintf(state, sizeof(state), "%u samples/window, %lu dropped", ready.n,
           (unsigned long)droppedWindows);

  filling = Aggregate{};
  windowStart = now;
}

bool take(Aggregate &out) {
  if (!ready.valid) return false;
  out = ready;
  ready = Aggregate{};
  return true;
}

Aggregate spot() {
  Aggregate one;
  snapshotInto(one);
  one.n = countFor(one);
  one.windowS = 0;
  one.valid = true;
  return one;
}

uint32_t dropped() { return droppedWindows; }

const char *statusText() { return state; }

}  // namespace telemetry
