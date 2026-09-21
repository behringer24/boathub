// Store and forward: every aggregate goes to flash and is released only once
// the broker has acknowledged it.
//
// A passage without marina Wi-Fi is then recorded rather than lost, and so is
// an evening when the server is switched off. Without this a window that
// closes while the uplink is down is simply overwritten by the next one.
//
// Records are fixed at 64 bytes, so record n sits at offset n * 64: no index,
// no scanning, and a torn write at the tail is detectable rather than
// corrupting what is behind it. They live in segment files of 1024 records,
// because releasing storage then costs one remove() instead of rewriting a
// file from the front.
//
// See docs/design/A-007-store-and-forward.md.

#pragma once

#include <Arduino.h>

namespace buffer {

// 64 bytes on flash, little-endian. Changing this layout is changing the file
// format: bump RECORD_VERSION and decide what happens to what is already
// stored.
const size_t RECORD_BYTES = 64;
const uint8_t RECORD_VERSION = 1;

const size_t RECORDS_PER_SEGMENT = 1024;  // 64 kB
const size_t MAX_SEGMENTS = 16;           // 1 MB, some 55 days of aggregates

// Which clock dated a record. Two bits of `flags`, and the reason the server
// can tell a synced timestamp from a lower bound.
enum class TimeSource : uint8_t {
  None = 0,      // no clock this boot; boot_id and tMonoS order these
  Restored = 1,  // a floor carried across a restart, plus elapsed time
  Ntp = 2,
  Gps = 3,  // from stage 2
};

// One channel as stored: scaled to a 16-bit integer, with a sentinel for
// "this sensor produced nothing". Never zero - on the server "no sensor" and
// "measured zero" must not look alike.
struct StoredChannel {
  int16_t mean = ABSENT;
  int16_t lo = ABSENT;
  int16_t hi = ABSENT;

  static const int16_t ABSENT = INT16_MIN;

  bool has() const { return mean != ABSENT; }
  bool hasRange() const { return lo != ABSENT && hi != ABSENT; }
};

// The channels, in the order they occupy the record. Adding one is a format
// change; the reserved byte is not room for a channel.
enum Channel : uint8_t {
  BatteryV = 0,   // mV
  CabinTempC,     // 0.01 C
  CabinRh,        // 0.1 %
  EngineTempC,    // 0.01 C
  BilgeTempC,     // 0.01 C
  FridgeTempC,    // 0.01 C
  BilgeLevelCm,   // mm
  ChannelCount,   // 7
};

struct Record {
  uint32_t seq = 0;
  uint16_t bootId = 0;
  uint32_t tMonoS = 0;  // seconds since boot - always known
  uint32_t tWall = 0;   // unix seconds, 0 when the clock was never set
  uint16_t windowS = 0;
  uint8_t n = 0;
  TimeSource timeSource = TimeSource::None;
  bool spot = false;
  int8_t rssi = 0;
  uint16_t heapKb = 0;

  StoredChannel ch[ChannelCount];
};

// Mounts the filesystem, finds the segments and restores the cursor. Safe to
// call when there is nothing there yet.
//
// Returns false if the filesystem could not be mounted, in which case every
// other call here is a no-op: a board that cannot buffer still has to measure
// and publish.
bool begin();

// Stores one record. Evicts the oldest segment when the quota is reached,
// counting what that cost - see dropped().
bool append(const Record &r);

// Copies up to `max` records from the cursor without consuming them. The
// cursor only moves on commit(), so a batch that is never acknowledged is
// simply read again.
size_t peek(Record *out, size_t max);

// Releases the first `count` records at the cursor, persists the new position
// and deletes any segment that is now fully drained.
//
// `wallNow` is stored beside the cursor as a floor for the next boot: unix
// seconds if the clock is known, 0 if it is not.
void commit(size_t count, uint32_t wallNow);

// Records waiting to be sent.
uint32_t pending();

// Records lost to the quota since boot. Not silent: this belongs in the
// payload, because a buffer that overflows quietly would reintroduce exactly
// the loss this module exists to prevent.
uint32_t dropped();

// The wall time stored with the cursor before the last restart, or 0. A lower
// bound on the real time, which is better than 1970.
uint32_t restoredFloor();

// Short human-readable state for the configuration page and the serial log.
const char *statusText();

}  // namespace buffer
