// Sampling and aggregation.
//
// Measurements are taken every sample_secs and summarised over a window of
// pub_secs. What leaves the board is the summary, not the readings: the mean
// with the extremes beside it. A mean alone hides the fridge compressor
// cycling and the battery sagging under it, which is the part worth seeing.
//
// This sits between the sensors and the uplink on purpose. The buffer in
// A-007 will sit in the same place, and an aggregate is what it stores.
// See docs/design/A-005-server-uplink.md.

#pragma once

#include <Arduino.h>

#include "buffer.h"
#include "channel.h"

namespace telemetry {

struct Aggregate {
  bool valid = false;
  uint16_t n = 0;        // the fewest measurements behind any reported channel
  uint32_t windowS = 0;  // 0 for a spot reading

  // What identifies this record for as long as it exists.
  //
  // Publishing is at-least-once: a message whose acknowledgement is lost is
  // sent again, and the server has to recognise the second copy rather than
  // count it twice. The identity is stamped when the record is CREATED, not
  // when it is sent - a retry has to carry the same pair, or it is not a
  // retry.
  //
  // bootId increments in NVS on every boot; seq counts records within one
  // boot. Keeping seq out of NVS is deliberate: persisting it per message
  // would be hundreds of flash writes a day, and bootId already separates one
  // run from the next.
  uint16_t bootId = 0;
  uint32_t seq = 0;

  Channel cabinTemp;
  Channel cabinRh;
  Channel engineTemp;
  Channel bilgeTemp;
  Channel fridgeTemp;

  // The IMU. Heel and pitch also report their spread; see A-009 section 6.
  Channel heel;
  Channel pitch;
  Channel motion;

  // Knocks the chip detected in the window, hold-off or not. A count rather
  // than a channel: there is no mean of a knock. `knocksSeen` is false when no
  // IMU reported at all, which is not the same thing as a quiet window.
  uint16_t knocks = 0;
  bool knocksSeen = false;

  // The BME680 in the battery bay: station pressure, and the bay's own air.
  // Gas resistance only with its heater switched on - see A-010 section 4.
  Channel pressure;
  Channel bayTemp;
  Channel bayRh;
  Channel bayGas;

  // Further channels land here as the sensors arrive.
};

void begin();
void loop();

// The current state as a one-sample aggregate: n = 1, no extremes. This is
// what the BOOT button sends and what goes out on connecting, so the chain
// can be proven without waiting out a whole window.
Aggregate spot();

// The stored form. Floats become scaled integers and an empty channel becomes
// the absent sentinel, so what goes into the buffer and what goes out over the
// wire cannot drift apart - there is one conversion, used by both.
void toRecord(const Aggregate &agg, buffer::Record &out);

// This boot's number. Records from an earlier boot cannot be dated from this
// boot's clock offset, so the encoder has to be able to tell them apart.
uint16_t bootId();

// The clock as a record written now carries it: unix seconds, or 0 when the
// clock has never been set, and which clock that is. Events are stamped with
// the same pair, so the two can be laid side by side.
uint32_t wallNow();
buffer::TimeSource timeSourceNow();

const char *statusText();

}  // namespace telemetry
