// One measured quantity over one window.
//
// A plain sensor adds one reading per measurement. The IMU produces 250
// readings in the same time, summarises them into one of these and hands that
// over instead, and the window merges it. So the window's mean, spread and
// extremes are exact over every reading, while `n` still counts measurements -
// 30 for a full window, whichever sensor it is.
// See docs/design/A-009-imu-heel-and-motion.md section 6.

#pragma once

#include <Arduino.h>
#include <math.h>

namespace telemetry {

struct Channel {
  uint16_t n = 0;        // measurements: one add() or one merge() each
  uint32_t samples = 0;  // readings behind them

  // Double precision. A spread of a tenth of a degree beside a heel of twenty
  // is a difference in the fifth digit of the sum of squares, and thousands of
  // additions in a float's seven digits lose it.
  double sum = 0.0;
  double sumSq = 0.0;
  float lo = 0.0f;
  float hi = 0.0f;

  void add(float v) {
    extend(v, v);
    sum += v;
    sumSq += (double)v * v;
    samples++;
    n++;
  }

  // A summary of many readings, counted as one measurement.
  void merge(const Channel &c) {
    if (!c.has()) return;
    extend(c.lo, c.hi);
    sum += c.sum;
    sumSq += c.sumSq;
    samples += c.samples;
    n++;
  }

  bool has() const { return samples > 0; }

  // A range, or a spread, needs more than one reading behind it. A spot
  // reading has neither and must not pretend to.
  bool hasRange() const { return samples > 1; }

  float mean() const { return samples ? (float)(sum / samples) : 0.0f; }

  // Standard deviation over every reading.
  float sd() const {
    if (samples < 2) return 0.0f;
    const double m = sum / samples;
    const double var = sumSq / samples - m * m;
    return var > 0.0 ? (float)sqrt(var) : 0.0f;
  }

 private:
  void extend(float low, float high) {
    if (samples == 0) {
      lo = low;
      hi = high;
      return;
    }
    if (low < lo) lo = low;
    if (high > hi) hi = high;
  }
};

}  // namespace telemetry
