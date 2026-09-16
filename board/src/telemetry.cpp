#include "telemetry.h"

#include "config.h"
#include "sht31.h"

namespace {

telemetry::Aggregate filling;  // the window being filled
telemetry::Aggregate ready;  // a closed window waiting to be collected

uint32_t windowStart = 0;
uint32_t nextSample = 0;
uint32_t droppedWindows = 0;
char state[40] = "starting";

// Read every sensor once and add what it gave to the window.
//
// A sensor with nothing to offer simply adds nothing: its channel keeps a
// count of zero and is left out of the message entirely. "No sensor fitted"
// and "measured zero" must not look the same anywhere in this system.
void sampleInto(telemetry::Aggregate &agg) {
  const sht31::Reading climate = sht31::latest();
  if (climate.valid) {
    agg.cabinTemp.add(climate.tempC);
    agg.cabinRh.add(climate.rh);
  }
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

  const telemetry::Channel *channels[] = {&agg.cabinTemp, &agg.cabinRh};
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
  nextSample = now;
  filling = Aggregate{};
  snprintf(state, sizeof(state), "window filling");
}

void loop() {
  const Config &c = config::get();
  const uint32_t now = millis();

  const uint32_t sampleMs = (uint32_t)c.sampleSecs * 1000UL;
  const uint32_t windowMs = (uint32_t)c.pubSecs * 1000UL;

  if ((int32_t)(now - nextSample) >= 0) {
    nextSample = now + sampleMs;
    sampleInto(filling);
  }

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
  sampleInto(one);
  one.n = countFor(one);
  one.windowS = 0;
  one.valid = true;
  return one;
}

uint32_t dropped() { return droppedWindows; }

const char *statusText() { return state; }

}  // namespace telemetry
