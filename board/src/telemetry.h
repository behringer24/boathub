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

namespace telemetry {

// One measured quantity over one window.
struct Channel {
  uint16_t n = 0;
  float sum = 0.0f;
  float lo = 0.0f;
  float hi = 0.0f;

  void add(float v) {
    if (n == 0) {
      lo = hi = v;
    } else {
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    sum += v;
    n++;
  }

  bool has() const { return n > 0; }
  float mean() const { return n ? sum / (float)n : 0.0f; }
};

struct Aggregate {
  bool valid = false;
  uint16_t n = 0;        // see below - the fewest samples behind any reported channel
  uint32_t windowS = 0;  // 0 for a spot reading

  Channel cabinTemp;
  Channel cabinRh;
  // Further channels land here as the sensors arrive.
};

void begin();
void loop();

// A closed window, if one is waiting. Taking it clears the slot.
bool take(Aggregate &out);

// The current state as a one-sample aggregate: n = 1, no extremes. This is
// what the BOOT button sends and what goes out on connecting, so the chain
// can be proven without waiting out a whole window.
Aggregate spot();

// Windows that closed while nothing collected them. Until the buffer exists
// this is the honest measure of what the uplink lost.
uint32_t dropped();

const char *statusText();

}  // namespace telemetry
