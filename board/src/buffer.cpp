#include "buffer.h"

#include <LittleFS.h>
#include <Preferences.h>

namespace {

const char *DIR = "/tel";
const char *NVS_NS = "boathub-buf";

// The cursor: which segment, and how many of its records have been released.
// Persisted after each acknowledged batch rather than each record - roughly a
// hundred writes a day, which NVS wear levelling does not notice.
uint32_t readSeg = 0;
uint32_t readIdx = 0;
uint32_t writeSeg = 0;
uint32_t writeIdx = 0;

uint32_t droppedRecords = 0;
uint32_t floorWall = 0;
bool mounted = false;
char state[48] = "not started";

// Several record formats can sit on flash at once: segments written before the
// record grew, and those written since. The extension says which, so a backlog
// in an old format drains at its own record size instead of being misread at
// the new one - or abandoned.
//
// Old-format segments are never written again, and every one of them is older
// than every newer one. So one number per old format, the highest segment of
// it found at start, says which format any segment is in, without asking the
// filesystem - whose open() logs an error for every file it does not find.
struct Format {
  const char *ext;
  size_t bytes;
  uint8_t version;
  uint8_t channels;  // how many of the channels it carries, from the first
};
const Format V1 = {".seg", 64, 1, 7};
const Format V2 = {".sg2", 96, 2, 10};
const Format V3 = {".sg3", buffer::RECORD_BYTES, buffer::RECORD_VERSION, buffer::ChannelCount};

const Format *OLD_FORMATS[] = {&V1, &V2};
const size_t OLD_COUNT = sizeof(OLD_FORMATS) / sizeof(OLD_FORMATS[0]);

bool haveOld[OLD_COUNT] = {false};
uint32_t lastOld[OLD_COUNT] = {0};

const Format &formatOf(uint32_t n) {
  for (size_t i = 0; i < OLD_COUNT; i++) {
    if (haveOld[i] && n <= lastOld[i]) return *OLD_FORMATS[i];
  }
  return V3;
}

String segPath(uint32_t n) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s/%06lu%s", DIR, (unsigned long)n, formatOf(n).ext);
  return String(buf);
}

void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Layout, little-endian. Offsets are written out rather than derived, because
// the one thing that must never drift is where a field sits.
//
//   0  seq          4   10  t_wall    4   18  rssi       1
//   4  boot_id      2   14  window_s  2   19  heap_kb    2
//   6  t_mono_s     4   16  n         1   21  14 channels x mean, lo, hi
//                       17  flags     1  105  heel_sd 2, 107 pitch_sd 2,
//                                        109  knocks 2, 111-126 reserved,
//                                        127  version
//
// The first 21 bytes and the first ten channels sit where the 96-byte format
// had them, and the first seven where the 64-byte format had them. What
// followed the channels in the 96-byte format - the spreads and the knock
// count at 81, 83 and 85 - moved back behind the four new ones.
const size_t TAIL_V2 = 81;
const size_t TAIL_V3 = 105;

void encode(const buffer::Record &r, uint8_t *b) {
  memset(b, 0, buffer::RECORD_BYTES);

  put32(b + 0, r.seq);
  put16(b + 4, r.bootId);
  put32(b + 6, r.tMonoS);
  put32(b + 10, r.tWall);
  put16(b + 14, r.windowS);
  b[16] = r.n;
  b[17] = (uint8_t)((uint8_t)r.timeSource & 0x03) | (r.spot ? 0x04 : 0x00);
  b[18] = (uint8_t)r.rssi;
  put16(b + 19, r.heapKb);

  uint8_t *c = b + 21;
  for (uint8_t i = 0; i < buffer::ChannelCount; i++) {
    put16(c + i * 6 + 0, (uint16_t)r.ch[i].mean);
    put16(c + i * 6 + 2, (uint16_t)r.ch[i].lo);
    put16(c + i * 6 + 4, (uint16_t)r.ch[i].hi);
  }

  put16(b + TAIL_V3 + 0, (uint16_t)r.heelSd);
  put16(b + TAIL_V3 + 2, (uint16_t)r.pitchSd);
  put16(b + TAIL_V3 + 4, r.knocks);

  b[buffer::RECORD_BYTES - 1] = buffer::RECORD_VERSION;
}

// The header and the channels every format shares. A record of any format
// comes out as a current Record, with whatever the old one did not carry left
// absent.
void decodeCommon(const uint8_t *b, buffer::Record &r, uint8_t channels) {
  r = buffer::Record{};
  r.seq = get32(b + 0);
  r.bootId = get16(b + 4);
  r.tMonoS = get32(b + 6);
  r.tWall = get32(b + 10);
  r.windowS = get16(b + 14);
  r.n = b[16];
  r.timeSource = (buffer::TimeSource)(b[17] & 0x03);
  r.spot = (b[17] & 0x04) != 0;
  r.rssi = (int8_t)b[18];
  r.heapKb = get16(b + 19);

  const uint8_t *c = b + 21;
  for (uint8_t i = 0; i < channels; i++) {
    r.ch[i].mean = (int16_t)get16(c + i * 6 + 0);
    r.ch[i].lo = (int16_t)get16(c + i * 6 + 2);
    r.ch[i].hi = (int16_t)get16(c + i * 6 + 4);
  }
}

bool decode(const uint8_t *b, const Format &fmt, buffer::Record &r) {
  if (b[fmt.bytes - 1] != fmt.version) return false;
  decodeCommon(b, r, fmt.channels);
  if (&fmt == &V1) return true;

  // The spreads and the knock count, wherever this format keeps them.
  const uint8_t *t = b + (&fmt == &V2 ? TAIL_V2 : TAIL_V3);
  r.heelSd = (int16_t)get16(t + 0);
  r.pitchSd = (int16_t)get16(t + 2);
  r.knocks = get16(t + 4);
  return true;
}

uint32_t recordsIn(uint32_t seg) {
  File f = LittleFS.open(segPath(seg), "r");
  if (!f) return 0;
  const uint32_t bytes = f.size();
  f.close();
  // A torn tail is discarded rather than read: integer division drops any
  // partial record, and the next append overwrites it.
  return bytes / formatOf(seg).bytes;
}

void saveCursor(uint32_t wallNow) {
  Preferences store;
  if (!store.begin(NVS_NS, /*readOnly=*/false)) return;
  store.putULong("rseg", readSeg);
  store.putULong("ridx", readIdx);
  if (wallNow > 0) store.putULong("wall", wallNow);
  store.end();
}

void describe() {
  snprintf(state, sizeof(state), "%lu pending, %lu dropped", (unsigned long)buffer::pending(),
           (unsigned long)droppedRecords);
}

}  // namespace

namespace buffer {

bool begin() {
  // The cursor is read first, because whether it exists is what turns a
  // failed mount from "nothing here yet" into "something was here and is
  // gone". Opened read/write so the namespace is created rather than reported
  // missing, and every key asked about before it is read - Preferences logs an
  // error for one it does not find, which on a fresh board is the normal case.
  bool hadCursor = false;
  {
    Preferences store;
    if (store.begin(NVS_NS, /*readOnly=*/false)) {
      hadCursor = store.isKey("rseg");
      if (hadCursor) {
        readSeg = store.getULong("rseg", 0);
        readIdx = store.getULong("ridx", 0);
      }
      if (store.isKey("wall")) floorWall = store.getULong("wall", 0);
      store.end();
    }
  }

  // The partition label, spelled out. In partitions.csv the entry is named
  // "littlefs" and its *subtype* is spiffs - the subtype is what the ESP-IDF
  // partition table calls this kind of storage, and it is not the name. The
  // Arduino driver looks up by name and defaults to "spiffs", so leaving this
  // out finds nothing at all:
  //
  //   E esp_littlefs: partition "spiffs" could not be found
  //
  // Mounted without formatOnFail first, so that a filesystem which had to be
  // formatted is something this module can say out loud instead of something
  // it does quietly.
  if (!LittleFS.begin(/*formatOnFail=*/false, "/littlefs", 10, "littlefs")) {
    if (hadCursor) {
      // The one case worth shouting about: records existed, and they are gone.
      Serial.println("[buffer] !!! the buffer filesystem could not be mounted and was formatted");
      Serial.println("[buffer] !!! every stored measurement that had not been sent is lost");
    } else {
      Serial.println("[buffer] formatting the buffer partition for first use");
    }
    if (!LittleFS.begin(/*formatOnFail=*/true, "/littlefs", 10, "littlefs")) {
      snprintf(state, sizeof(state), "filesystem unavailable");
      Serial.println("[buffer] LittleFS would not mount - buffering is off");
      return false;
    }
    LittleFS.mkdir(DIR);
    readSeg = readIdx = 0;
  }

  // The segments on flash decide what is real, not the cursor: a cursor
  // pointing at a segment that was evicted has to move forward, and one
  // pointing past the end of a segment has to be clamped.
  uint32_t lowest = UINT32_MAX, highest = 0;
  bool any = false;
  File dir = LittleFS.open(DIR);
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const String name = String(f.name());
    int old = -1;
    for (size_t i = 0; i < OLD_COUNT; i++) {
      if (name.endsWith(OLD_FORMATS[i]->ext)) old = (int)i;
    }
    if (old < 0 && !name.endsWith(V3.ext)) continue;
    const uint32_t n = (uint32_t)strtoul(name.c_str(), nullptr, 10);
    if (n < lowest) lowest = n;
    if (n > highest) highest = n;
    if (old >= 0) {
      if (!haveOld[old] || n > lastOld[old]) lastOld[old] = n;
      haveOld[old] = true;
    }
    any = true;
  }
  dir.close();

  // The highest segment in any old format. Every one up to it drains at its
  // own size and is never appended to.
  bool haveAnyOld = false;
  uint32_t lastAnyOld = 0;
  for (size_t i = 0; i < OLD_COUNT; i++) {
    if (!haveOld[i]) continue;
    if (!haveAnyOld || lastOld[i] > lastAnyOld) lastAnyOld = lastOld[i];
    haveAnyOld = true;
  }

  if (!any) {
    readSeg = writeSeg = (readSeg > 0 ? readSeg : 0);
    readIdx = writeIdx = 0;
  } else {
    if (readSeg < lowest) {
      readSeg = lowest;
      readIdx = 0;
    }
    writeSeg = highest;
    writeIdx = recordsIn(writeSeg);
    if (readSeg == writeSeg && readIdx > writeIdx) readIdx = writeIdx;

    // An old-format segment is drained, never appended to: new records start
    // a segment of their own after it.
    if (haveAnyOld && writeSeg <= lastAnyOld) {
      writeSeg = lastAnyOld + 1;
      writeIdx = 0;
    }
  }
  for (size_t i = 0; i < OLD_COUNT; i++) {
    if (!haveOld[i]) continue;
    Serial.printf("[buffer] segments up to %lu are in the %u-byte format and drain as they are\n",
                  (unsigned long)lastOld[i], (unsigned)OLD_FORMATS[i]->bytes);
  }

  mounted = true;
  describe();
  Serial.printf("[buffer] %s, floor %lu\n", state, (unsigned long)floorWall);
  return true;
}

bool append(const Record &r) {
  if (!mounted) return false;

  if (writeIdx >= RECORDS_PER_SEGMENT) {
    writeSeg++;
    writeIdx = 0;
  }

  // The quota. The oldest segment goes, and what it held is counted - never
  // discarded silently, which is the whole point of this module.
  while (writeSeg >= readSeg && (writeSeg - readSeg) >= MAX_SEGMENTS) {
    const uint32_t lost = recordsIn(readSeg) - readIdx;
    LittleFS.remove(segPath(readSeg));
    droppedRecords += lost;
    Serial.printf("[buffer] quota reached, segment %lu dropped with %lu records\n",
                  (unsigned long)readSeg, (unsigned long)lost);
    readSeg++;
    readIdx = 0;
    saveCursor(0);
  }

  uint8_t bytes[RECORD_BYTES];
  encode(r, bytes);

  File f = LittleFS.open(segPath(writeSeg), writeIdx == 0 ? "w" : "a");
  if (!f) {
    // The directory is created with the filesystem, so this means it went
    // missing afterwards. Repaired here rather than at every boot, where the
    // check itself would log an error for the normal case.
    LittleFS.mkdir(DIR);
    f = LittleFS.open(segPath(writeSeg), writeIdx == 0 ? "w" : "a");
  }
  if (!f) {
    Serial.printf("[buffer] cannot open segment %lu for writing\n", (unsigned long)writeSeg);
    return false;
  }
  const size_t written = f.write(bytes, RECORD_BYTES);
  f.close();
  if (written != RECORD_BYTES) {
    Serial.println("[buffer] short write - flash full?");
    return false;
  }

  writeIdx++;
  describe();
  return true;
}

size_t peek(Record *out, size_t max) {
  if (!mounted || max == 0) return 0;

  size_t got = 0;
  uint32_t seg = readSeg;
  uint32_t idx = readIdx;

  while (got < max) {
    if (seg > writeSeg) break;

    const uint32_t have = (seg == writeSeg) ? writeIdx : recordsIn(seg);
    if (idx >= have) {
      if (seg == writeSeg) break;
      seg++;
      idx = 0;
      continue;
    }

    File f = LittleFS.open(segPath(seg), "r");
    if (!f) {
      // A segment that vanished under us is skipped rather than retried
      // forever.
      if (seg == writeSeg) break;
      seg++;
      idx = 0;
      continue;
    }
    const Format &fmt = formatOf(seg);
    f.seek(idx * fmt.bytes);
    while (got < max && idx < have) {
      uint8_t bytes[RECORD_BYTES];  // the largest of the formats
      if (f.read(bytes, fmt.bytes) != fmt.bytes) break;
      if (decode(bytes, fmt, out[got])) got++;
      idx++;
    }
    f.close();
  }

  return got;
}

void commit(size_t count, uint32_t wallNow) {
  if (!mounted || count == 0) return;

  uint32_t left = count;
  while (left > 0) {
    const uint32_t have = (readSeg == writeSeg) ? writeIdx : recordsIn(readSeg);
    const uint32_t inThis = have > readIdx ? have - readIdx : 0;

    if (left < inThis) {
      readIdx += left;
      left = 0;
      break;
    }

    left -= inThis;
    if (readSeg == writeSeg) {
      // Everything written has been released. The segment stays - it is still
      // the one being appended to - and the cursor sits at its end.
      readIdx = writeIdx;
      break;
    }

    LittleFS.remove(segPath(readSeg));
    readSeg++;
    readIdx = 0;
  }

  saveCursor(wallNow);
  describe();
}

uint32_t pending() {
  if (!mounted) return 0;
  uint32_t total = 0;
  for (uint32_t s = readSeg; s <= writeSeg; s++) {
    const uint32_t have = (s == writeSeg) ? writeIdx : recordsIn(s);
    const uint32_t from = (s == readSeg) ? readIdx : 0;
    if (have > from) total += have - from;
  }
  return total;
}

bool ready() { return mounted; }

uint32_t dropped() { return droppedRecords; }

uint32_t restoredFloor() { return floorWall; }

const char *statusText() { return state; }

}  // namespace buffer
