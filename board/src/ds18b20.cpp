#include "ds18b20.h"

#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>

#include "config.h"

namespace {

const uint8_t PINS[ds18b20::ProbeCount] = {4, 5, 6};
const char *NAMES[ds18b20::ProbeCount] = {"engine", "bilge", "fridge"};

// Plausibility per location. A value outside these is not a measurement, it is
// a fault, and averaging it into a window would corrupt the window rather than
// show the problem.
const float MIN_C[ds18b20::ProbeCount] = {-20.0f, -5.0f, -20.0f};
const float MAX_C[ds18b20::ProbeCount] = {80.0f, 40.0f, 30.0f};

// 11 bits is 0.125 C and a 375 ms conversion, against 750 ms for the 12-bit
// default. Nothing on a boat needs 0.0625 C.
const uint8_t RESOLUTION = 11;
const uint32_t CONVERT_MS = 400;  // 375 ms plus margin

// A jump larger than this between consecutive readings is not a cabin heating
// up, it is a bad read.
const float MAX_JUMP_C = 10.0f;

// The scratchpad's power-on value is exactly 85.0 C. It is also a legal
// temperature, which is what makes it dangerous: an exact 85.0 almost always
// means the conversion never finished or the sensor browned out, not that
// something is hot.
const float POWER_ON_VALUE = 85.0f;

OneWire wire[ds18b20::ProbeCount] = {OneWire(PINS[0]), OneWire(PINS[1]), OneWire(PINS[2])};
DallasTemperature bus[ds18b20::ProbeCount] = {DallasTemperature(&wire[0]), DallasTemperature(&wire[1]),
                                              DallasTemperature(&wire[2])};

DeviceAddress rom[ds18b20::ProbeCount];
bool found[ds18b20::ProbeCount] = {false, false, false};
bool swapped[ds18b20::ProbeCount] = {false, false, false};

ds18b20::Reading last[ds18b20::ProbeCount];
uint32_t lastAt[ds18b20::ProbeCount] = {0, 0, 0};
bool unread[ds18b20::ProbeCount] = {false, false, false};
const char *state[ds18b20::ProbeCount] = {"not started", "not started", "not started"};

bool converting = false;
uint32_t convertStarted = 0;
uint32_t nextSample = 0;

Preferences store;
const char *NS = "ds18b20";

uint32_t sampleMs() {
  const uint16_t secs = config::get().sampleSecs;
  return (secs == 0 ? 1u : secs) * 1000UL;
}

uint32_t staleMs() {
  const uint32_t scaled = (uint32_t)config::get().sampleSecs * 3500UL;
  return scaled < 30000UL ? 30000UL : scaled;
}

String romText(const DeviceAddress a) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X%02X%02X", a[0], a[1], a[2], a[3], a[4], a[5],
           a[6], a[7]);
  return String(buf);
}

// Which probe is on which bus cannot be told apart electrically - all three
// are identical. The realistic failure is not a broken sensor but three
// identical cables unplugged for service and two of them put back the wrong
// way round, after which engine bay temperature silently reports bilge water.
//
// So the address is recorded the first time a probe is seen and compared on
// every start. A mismatch is reported but does not stop the reading: a
// legitimately replaced sensor must not take the channel down with it.
void checkIdentity(uint8_t i) {
  const String key = String("rom") + (char)('0' + i);
  const String seen = romText(rom[i]);

  if (!store.isKey(key.c_str())) {
    store.putString(key.c_str(), seen);
    Serial.printf("[ds18b20] %s: learned %s\n", NAMES[i], seen.c_str());
    return;
  }

  const String stored = store.getString(key.c_str());
  if (stored == seen) return;

  swapped[i] = true;
  Serial.printf("[ds18b20] %s: WRONG PROBE - expected %s, found %s\n", NAMES[i], stored.c_str(),
                seen.c_str());
  Serial.println("[ds18b20] cables swapped during service, or a probe was replaced");
}

void collect(uint8_t i, uint32_t now) {
  if (!found[i]) return;

  // The library validates the scratchpad CRC internally and returns this
  // sentinel when it fails, so a corrupted reading over 5 m of cable arrives
  // here as a disconnection rather than as a plausible-looking number.
  const float t = bus[i].getTempC(rom[i]);

  if (t == DEVICE_DISCONNECTED_C) {
    state[i] = "no answer";
    return;
  }
  if (t == POWER_ON_VALUE) {
    state[i] = "power-on value";
    return;
  }
  if (t < MIN_C[i] || t > MAX_C[i]) {
    state[i] = "implausible";
    return;
  }
  if (last[i].valid && fabsf(t - last[i].tempC) > MAX_JUMP_C) {
    state[i] = "jumped";
    return;
  }

  last[i].valid = true;
  last[i].tempC = t;
  lastAt[i] = now;
  unread[i] = true;
  state[i] = swapped[i] ? "ok, wrong probe" : "ok";
}

}  // namespace

namespace ds18b20 {

void begin() {
  store.begin(NS, /*readOnly=*/false);

  for (uint8_t i = 0; i < ProbeCount; i++) {
    bus[i].begin();
    // The conversion is collected on a later pass. Without this the library
    // would sit in a delay for the whole 375 ms.
    bus[i].setWaitForConversion(false);

    if (!bus[i].getAddress(rom[i], 0)) {
      state[i] = "not found";
      Serial.printf("[ds18b20] %s: nothing on GPIO%u\n", NAMES[i], PINS[i]);
      continue;
    }

    // Family code 0x28 is the DS18B20. These are clones, so it is worth
    // confirming rather than assuming.
    if (rom[i][0] != 0x28) {
      state[i] = "not a DS18B20";
      Serial.printf("[ds18b20] %s: family code 0x%02X, not 0x28\n", NAMES[i], rom[i][0]);
      continue;
    }

    found[i] = true;
    bus[i].setResolution(rom[i], RESOLUTION);

    // Three wires are used, so this should be false. If it is true the probe
    // is running off the data line, which means VDD is not actually connected.
    if (bus[i].isParasitePowerMode()) {
      Serial.printf("[ds18b20] %s: reports parasite power - is VDD connected?\n", NAMES[i]);
    }

    state[i] = "starting";
    Serial.printf("[ds18b20] %s: GPIO%u, %s\n", NAMES[i], PINS[i], romText(rom[i]).c_str());
    checkIdentity(i);
  }
}

void loop() {
  const uint32_t now = millis();

  if (converting) {
    if ((now - convertStarted) < CONVERT_MS) return;
    for (uint8_t i = 0; i < ProbeCount; i++) collect(i, now);
    converting = false;
    nextSample = now + sampleMs();
    return;
  }

  if ((int32_t)(now - nextSample) < 0) return;

  // All three buses are started back to back and then waited on once. The
  // probes convert in parallel, so the whole set costs one conversion time
  // rather than three.
  bool any = false;
  for (uint8_t i = 0; i < ProbeCount; i++) {
    if (!found[i]) continue;
    bus[i].requestTemperatures();
    any = true;
  }

  if (!any) {
    nextSample = now + sampleMs();
    return;
  }

  converting = true;
  convertStarted = now;
}

Reading latest(Probe p) {
  const uint8_t i = (uint8_t)p;
  if (!last[i].valid) return Reading{};
  if ((millis() - lastAt[i]) > staleMs()) return Reading{};
  return last[i];
}

bool takeFresh(Probe p, Reading &out) {
  const uint8_t i = (uint8_t)p;
  if (!unread[i]) return false;
  unread[i] = false;
  out = last[i];
  return true;
}

bool present(Probe p) { return found[(uint8_t)p]; }

const char *statusText(Probe p) { return state[(uint8_t)p]; }

const char *name(Probe p) { return NAMES[(uint8_t)p]; }

void relearn() {
  for (uint8_t i = 0; i < ProbeCount; i++) {
    const String key = String("rom") + (char)('0' + i);
    store.remove(key.c_str());
    swapped[i] = false;
    if (found[i]) {
      store.putString(key.c_str(), romText(rom[i]));
      state[i] = "ok";
    }
  }
  Serial.println("[ds18b20] probe identities relearned");
}

}  // namespace ds18b20
