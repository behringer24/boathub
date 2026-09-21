#include "telemetry.h"

#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "buffer.h"
#include "config.h"
#include "net.h"
#include "ds18b20.h"
#include "sht31.h"

namespace {

telemetry::Aggregate filling;  // the window being filled

uint32_t windowStart = 0;

// Anything later than 2023 means the clock has been set; the RTC starts at
// 1970 and there is no battery on this board.
const time_t TIME_SANE_AFTER = 1700000000;

const char *NVS_NS = "boathub-seq";
uint16_t bootNumber = 0;
uint32_t nextSeq = 0;

// Whether the clock has ever been set by NTP this boot, and whether it was
// started from the floor the last run left behind.
//
// The flag is set from a callback rather than polled: sntp_get_sync_status()
// reports COMPLETED only in the moment of the sync and then goes back, so
// anything that asks a second later gets the wrong answer.
volatile bool ntpSynced = false;
bool clockFromFloor = false;
bool clockCarried = false;

void onTimeSync(struct timeval *) { ntpSynced = true; }

bool clockSane() { return time(nullptr) > TIME_SANE_AFTER; }

// Which clock produced the timestamp on a record written now.
//
// `restored` covers two ways of having a time that was not verified this boot:
// the floor read back from NVS, and a clock that simply kept running across a
// soft reset, where the RTC domain survives. The second is the better of the
// two - it is the real time, merely not re-checked - and both are lower bounds
// rather than claims.
//
// Without the second, a restart from the configuration portal produced records
// carrying a perfectly good timestamp labelled `none`, which is a contradiction
// the server has no way to resolve.
buffer::TimeSource timeSource() {
  if (ntpSynced) return buffer::TimeSource::Ntp;
  if ((clockFromFloor || clockCarried) && clockSane()) return buffer::TimeSource::Restored;
  return buffer::TimeSource::None;
}
char state[40] = "starting";

// One channel into its stored form. An empty channel keeps the absent
// sentinel rather than becoming a zero, and a channel with a single sample
// keeps its mean and no range - the same distinction the JSON and the database
// make.
void store(buffer::StoredChannel &out, const telemetry::Channel &c, float scale) {
  if (!c.has()) return;
  out.mean = (int16_t)lroundf(c.mean() * scale);
  if (c.n > 1) {
    out.lo = (int16_t)lroundf(c.lo * scale);
    out.hi = (int16_t)lroundf(c.hi * scale);
  }
}

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
  // One NVS write per boot, not one per message. The counter wraps at 65535,
  // which is some 180 years of daily restarts.
  Preferences store;
  store.begin(NVS_NS, /*readOnly=*/false);
  bootNumber = store.getUShort("boot", 0) + 1;
  store.putUShort("boot", bootNumber);
  store.end();
  Serial.printf("[telemetry] boot %u\n", bootNumber);

  // Registered before uplink::begin() calls configTime, which is the only
  // reason the order in setup() matters here.
  sntp_set_time_sync_notification_cb(onTimeSync);

  // A floor beats 1970. The last run wrote the wall time it knew beside its
  // cursor; starting from that, a record of this boot is dated to within the
  // length of the outage - seconds after a watchdog reset, and only genuinely
  // wrong after a long lay-up. Records say `restored` so the server knows the
  // difference between a timestamp and a lower bound.
  // A clock that is already running was set before this boot and survived -
  // a watchdog or a deliberate restart keeps the RTC domain alive.
  clockCarried = clockSane();

  const uint32_t floorWall = buffer::restoredFloor();
  if (floorWall > 0 && !clockSane()) {
    struct timeval tv;
    tv.tv_sec = (time_t)floorWall;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    clockFromFloor = true;
    Serial.printf("[telemetry] clock restored to a floor of %lu\n", (unsigned long)floorWall);
  }

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

  // Close the window and hand it to the buffer. Whether the uplink happens to
  // be connected is none of this module's business: a window goes to flash,
  // and the drain sends it whenever there is somewhere to send it.
  filling.windowS = (now - windowStart) / 1000;
  filling.n = countFor(filling);
  filling.valid = true;
  filling.bootId = bootNumber;
  filling.seq = nextSeq++;

  buffer::Record rec;
  telemetry::toRecord(filling, rec);
  if (!buffer::append(rec)) {
    Serial.println("[telemetry] window could not be stored");
  }

  snprintf(state, sizeof(state), "%u samples/window, %lu waiting", filling.n,
           (unsigned long)buffer::pending());

  filling = Aggregate{};
  windowStart = now;
}

Aggregate spot() {
  Aggregate one;
  snapshotInto(one);
  one.n = countFor(one);
  one.windowS = 0;
  one.bootId = bootNumber;
  one.seq = nextSeq++;
  one.valid = true;
  return one;
}

// The stored form.
//
// One conversion for both paths, because a live reading and a record coming
// back off flash have to arrive at the server identically. Doing it twice
// would be two places for a scale factor to be wrong in.
void toRecord(const Aggregate &agg, buffer::Record &out) {
  out = buffer::Record{};

  out.seq = agg.seq;
  out.bootId = agg.bootId;
  out.tMonoS = millis() / 1000;
  out.windowS = (uint16_t)agg.windowS;
  out.n = agg.n > 255 ? 255 : (uint8_t)agg.n;
  out.spot = (agg.windowS == 0);

  out.timeSource = timeSource();
  const time_t wall = time(nullptr);
  out.tWall = clockSane() ? (uint32_t)wall : 0;

  out.rssi = (int8_t)net::rssi();
  const uint32_t heap = ESP.getFreeHeap() / 1024;
  out.heapKb = heap > 65535 ? 65535 : (uint16_t)heap;

  store(out.ch[buffer::CabinTempC], agg.cabinTemp, 100.0f);
  store(out.ch[buffer::CabinRh], agg.cabinRh, 10.0f);
  store(out.ch[buffer::EngineTempC], agg.engineTemp, 100.0f);
  store(out.ch[buffer::BilgeTempC], agg.bilgeTemp, 100.0f);
  store(out.ch[buffer::FridgeTempC], agg.fridgeTemp, 100.0f);
}

uint16_t bootId() { return bootNumber; }

const char *statusText() { return state; }

}  // namespace telemetry
