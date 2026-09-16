// Three DS18B20 probes, each on its own 1-Wire bus.
//
// Nothing here blocks. A conversion takes 375 ms and is collected on a later
// pass through loop(); waiting for it would stall Wi-Fi, MQTT and every other
// sensor for well over a third of a second, every cycle.
//
// One GPIO per probe rather than one shared bus: a shorted or broken cable
// then takes out one reading instead of all three.
// See docs/design/A-003-ds18b20-temperature-sensors.md.

#pragma once

#include <Arduino.h>

namespace ds18b20 {

enum Probe : uint8_t { Engine = 0, Bilge = 1, Fridge = 2, ProbeCount = 3 };

struct Reading {
  bool valid = false;
  float tempC = 0.0f;
};

void begin();
void loop();

// The most recent accepted reading, or invalid if there is none fresh enough.
// Does not consume: for the status line and for spot messages.
Reading latest(Probe p);

// Consumes the reading: true once per accepted conversion, never twice. What
// an aggregator has to use, so the sample count means measurements rather
// than polls.
bool takeFresh(Probe p, Reading &out);

bool present(Probe p);

// Short human-readable state, per probe, for the configuration page and the
// serial log.
const char *statusText(Probe p);

const char *name(Probe p);

// Forget the stored ROM addresses and adopt whatever is attached now. Needed
// after a probe is legitimately replaced - otherwise the identity check would
// report a swap for the rest of the sensor's life.
void relearn();

}  // namespace ds18b20
