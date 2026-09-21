// Bench check for the store-and-forward buffer.
//
// The buffer is the one module whose failures are invisible from outside: a
// record encoded wrongly still looks like a record, and a cursor that does not
// advance looks like a quiet link. So it gets exercised on the real filesystem,
// on the real board, before anything depends on it.
//
//   pio run -d board -e bufcheck -t upload -t monitor
//
// It refuses to run while records are waiting, because committing them here
// would throw away measurements that were never sent.
//
// See docs/design/A-007-store-and-forward.md.

#include <Arduino.h>

#include "../buffer.h"

namespace {

int checks = 0;
int failures = 0;

void ok(const char *what, bool pass) {
  checks++;
  if (!pass) failures++;
  Serial.printf("  %s %s\n", pass ? "ok  " : "FAIL", what);
}

void set(buffer::StoredChannel &c, int16_t mean, int16_t lo, int16_t hi) {
  c.mean = mean;
  c.lo = lo;
  c.hi = hi;
}

buffer::Record make(uint32_t seq) {
  buffer::Record r;
  r.seq = seq;
  r.bootId = 4242;
  r.tMonoS = seq * 240;
  r.tWall = 1790000000UL + seq * 240;
  r.windowS = 240;
  r.n = 23;
  r.timeSource = buffer::TimeSource::Ntp;
  r.spot = (seq % 7 == 0);
  r.rssi = -61;
  r.heapKb = 272;

  // A channel with values, one with a mean but no range - the shape a spot
  // reading has - and one absent throughout.
  set(r.ch[buffer::CabinTempC], (int16_t)(2750 + seq), (int16_t)(2700 + seq),
      (int16_t)(2800 + seq));
  r.ch[buffer::EngineTempC].mean = (int16_t)(2600 + seq);
  set(r.ch[buffer::BatteryV], 12800, 12700, 12900);
  return r;
}

bool same(const buffer::Record &a, const buffer::Record &b) {
  if (a.seq != b.seq || a.bootId != b.bootId) return false;
  if (a.tMonoS != b.tMonoS || a.tWall != b.tWall) return false;
  if (a.windowS != b.windowS || a.n != b.n) return false;
  if (a.timeSource != b.timeSource || a.spot != b.spot) return false;
  if (a.rssi != b.rssi || a.heapKb != b.heapKb) return false;
  for (uint8_t i = 0; i < buffer::ChannelCount; i++) {
    if (a.ch[i].mean != b.ch[i].mean) return false;
    if (a.ch[i].lo != b.ch[i].lo) return false;
    if (a.ch[i].hi != b.ch[i].hi) return false;
  }
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== buffer bench check ===");

  if (!buffer::begin()) {
    Serial.println("buffer::begin() failed - nothing further can be checked");
    return;
  }
  Serial.printf("found: %s, restored floor %lu\n", buffer::statusText(),
                (unsigned long)buffer::restoredFloor());

  if (buffer::pending() > 0) {
    Serial.printf("\n%lu records are waiting to be sent.\n", (unsigned long)buffer::pending());
    Serial.println("Refusing to run: committing here would throw away measurements that");
    Serial.println("never reached the server. Drain them with the normal firmware first.");
    return;
  }

  const size_t COUNT = 40;
  Serial.printf("\nappending %u records\n", (unsigned)COUNT);
  bool appended = true;
  for (uint32_t i = 0; i < COUNT; i++) appended &= buffer::append(make(i));
  ok("every append reported success", appended);
  ok("pending() counts them", buffer::pending() == COUNT);

  Serial.println("\nreading them back");
  buffer::Record back[COUNT];
  const size_t got = buffer::peek(back, COUNT);
  ok("peek() returns the whole batch", got == COUNT);

  bool identical = (got == COUNT);
  for (size_t i = 0; i < got && identical; i++) identical = same(make(i), back[i]);
  ok("every field survives the round trip", identical);

  ok("an absent channel stays absent", !back[0].ch[buffer::BilgeLevelCm].has());
  ok("a mean without a range keeps its mean", back[0].ch[buffer::EngineTempC].has());
  ok("and reports no range", !back[0].ch[buffer::EngineTempC].hasRange());
  ok("the spot flag survives", back[0].spot && !back[1].spot);

  Serial.println("\npeek() does not consume");
  buffer::Record again[4];
  const size_t got2 = buffer::peek(again, 4);
  ok("the same records come back", got2 == 4 && same(again[0], back[0]));

  Serial.println("\ncommitting half");
  buffer::commit(COUNT / 2, 1790000000UL);
  ok("pending() drops by half", buffer::pending() == COUNT / 2);

  buffer::Record after[4];
  const size_t got3 = buffer::peek(after, 4);
  ok("reading resumes after the committed records", got3 == 4 && same(after[0], make(COUNT / 2)));

  Serial.println("\ncommitting the rest");
  buffer::commit(COUNT / 2, 1790000000UL);
  ok("the buffer is empty", buffer::pending() == 0);
  ok("nothing was dropped", buffer::dropped() == 0);

  Serial.println();
  Serial.printf("=== %d checks, %d failed ===\n", checks, failures);
  if (failures == 0) {
    Serial.println("The cursor survives a restart: run this again and the first line");
    Serial.println("should report 0 pending, not 40.");
  }
}

void loop() { delay(1000); }
