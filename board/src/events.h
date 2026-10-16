// Events: what happens, the moment it happens.
//
// Every event is written to a small queue on flash first, and the uplink sends
// whatever is waiting. An event raised with no connection is therefore sent
// late rather than lost - the telemetry buffer's discipline, at the size of a
// few events a day. The queue holds 32-byte records and nothing that needs
// the network, so emitting an event never waits for anything.
// See docs/design/A-011-events.md.

#pragma once

#include <Arduino.h>

#include "buffer.h"

namespace events {

enum class Kind : uint8_t {
  Boot = 1,
  SensorLost,
  SensorBack,
  Knock,
  Alarm,
  Ack,
};

// Named by where they measure, the same prefixes the telemetry fields use.
enum class Sensor : uint8_t { Cabin = 0, Bay, Imu, Engine, Bilge, Fridge, Count, None = 0xFF };

enum class Alarm : uint8_t { Frost = 0, FridgeWarm, Humidity, Count };

// An alarm lights the LED and wants acknowledging; a warning does neither.
enum class Severity : uint8_t { Alarm = 0, Warning = 1 };

// The bits of a knock's `axes`: the boat's axes, not the module's.
const uint8_t AXIS_FORE_AND_AFT = 1;
const uint8_t AXIS_ATHWARTSHIPS = 2;
const uint8_t AXIS_VERTICAL = 4;

struct Event {
  uint32_t seq = 0;  // within this boot, separate from telemetry's
  uint16_t bootId = 0;
  uint32_t tMonoS = 0;  // seconds since boot - always known
  uint32_t tWall = 0;   // unix seconds, 0 when the clock was never set
  buffer::TimeSource timeSource = buffer::TimeSource::None;

  Kind kind = Kind::Boot;
  uint8_t subject = 0;  // a Sensor, an Alarm or the reset reason, by kind
  uint8_t detail = 0;   // a knock's AXIS_ bits; an alarm's Sensor
  bool raised = false;  // an alarm: raised, or cleared
  Severity severity = Severity::Alarm;
  uint16_t count = 0;   // a knock: those held back; a boot: one bit per Sensor that answered
  float value = 0.0f;   // a knock's g, an alarm's reading
  float limit = 0.0f;   // the threshold an alarm was judged against
};

// Opens the queue. Called after buffer::begin(), which mounts the filesystem,
// and after telemetry::begin(), which numbers the boot. Without a filesystem
// events are logged and not sent; nothing else is affected.
bool begin();

// The ways to raise one. Each is stamped with its identity and the time as it
// is made, written to the queue and logged.
void boot(uint8_t resetReason, uint16_t sensors);
void sensorLost(Sensor s);
void sensorBack(Sensor s);
void knock(uint8_t axes, float g, uint16_t held);
void alarm(Alarm a, bool raised, Severity severity, Sensor s, float value, float limit);
void ack();

// For the uplink: what is waiting, a look at it, and releasing it.
//
// peek() copies up to `max` slots from the front of the queue without
// consuming them and returns how many slots it read; `got` says how many of
// those decoded into `out`. commit() takes the slot count, so a record that
// failed to decode is released with its batch rather than read forever.
uint32_t pending();
size_t peek(Event *out, size_t max, size_t &got);
void commit(size_t slots);

// Events lost to the queue's limit since boot. Never silent: it travels in
// every message that follows.
uint32_t dropped();

const char *sensorName(Sensor s);
const char *alarmName(Alarm a);
const char *resetReasonName(uint8_t reason);

const char *statusText();

}  // namespace events
