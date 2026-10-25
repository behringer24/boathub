#include "events.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>

#include "telemetry.h"

namespace {

const char *DIR = "/evt";
const char *PATH = "/evt/queue.bin";
const char *TMP = "/evt/queue.tmp";
const char *NVS_NS = "boathub-evt";

const size_t BYTES = 32;
const uint8_t VERSION = 1;

// Waiting events, at most: 16 kB of flash. Of a week of events nobody could
// receive, the newest are the ones still worth acting on, so the oldest go.
const uint32_t LIMIT = 512;

// The read position, in slots from the start of the file, and the number of
// slots in it. The file is only ever appended to; released slots stay in it
// until everything is released and the file is removed, or until it has grown
// to twice the limit and the waiting tail is copied into a fresh one.
uint32_t readIdx = 0;
uint32_t slots = 0;

uint32_t droppedEvents = 0;
uint32_t nextSeq = 0;
bool mounted = false;
char state[40] = "not started";

const char *SENSOR_NAMES[] = {"cabin",  "bay",    "imu",     "engine",
                              "bilge",  "fridge", "battery", "bilge_level"};
const char *ALARM_NAMES[] = {"frost", "fridge_warm", "humidity"};

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

void putFloat(uint8_t *p, float v) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  put32(p, bits);
}

float getFloat(const uint8_t *p) {
  const uint32_t bits = get32(p);
  float v;
  memcpy(&v, &bits, sizeof(v));
  return v;
}

// Layout, little-endian, written out for the same reason as the telemetry
// record's: where a field sits must never drift.
//
//    0  seq        4     14  flags     1     18  count     2
//    4  boot_id    2     15  kind      1     20  value     4  float
//    6  t_mono_s   4     16  subject   1     24  limit     4  float
//   10  t_wall     4     17  detail    1     28  reserved  3,  31  version
//
// flags: bits 0-1 the time source, bit 2 an alarm raised, bit 3 a warning.
// detail: a knock's axes, an alarm's sensor.
void encode(const events::Event &e, uint8_t *b) {
  memset(b, 0, BYTES);
  put32(b + 0, e.seq);
  put16(b + 4, e.bootId);
  put32(b + 6, e.tMonoS);
  put32(b + 10, e.tWall);
  b[14] = (uint8_t)((uint8_t)e.timeSource & 0x03) | (e.raised ? 0x04 : 0x00) |
          (e.severity == events::Severity::Warning ? 0x08 : 0x00);
  b[15] = (uint8_t)e.kind;
  b[16] = e.subject;
  b[17] = e.detail;
  put16(b + 18, e.count);
  putFloat(b + 20, e.value);
  putFloat(b + 24, e.limit);
  b[BYTES - 1] = VERSION;
}

bool decode(const uint8_t *b, events::Event &e) {
  if (b[BYTES - 1] != VERSION) return false;
  const uint8_t kind = b[15];
  if (kind < (uint8_t)events::Kind::Boot || kind > (uint8_t)events::Kind::Ack) return false;

  e = events::Event{};
  e.seq = get32(b + 0);
  e.bootId = get16(b + 4);
  e.tMonoS = get32(b + 6);
  e.tWall = get32(b + 10);
  e.timeSource = (buffer::TimeSource)(b[14] & 0x03);
  e.raised = (b[14] & 0x04) != 0;
  e.severity = (b[14] & 0x08) ? events::Severity::Warning : events::Severity::Alarm;
  e.kind = (events::Kind)kind;
  e.subject = b[16];
  e.detail = b[17];
  e.count = get16(b + 18);
  e.value = getFloat(b + 20);
  e.limit = getFloat(b + 24);
  return true;
}

void saveCursor() {
  Preferences store;
  if (!store.begin(NVS_NS, /*readOnly=*/false)) return;
  store.putULong("read", readIdx);
  store.end();
}

void describe() {
  snprintf(state, sizeof(state), "%lu waiting, %lu dropped", (unsigned long)(slots - readIdx),
           (unsigned long)droppedEvents);
}

// The waiting tail into a fresh file, so released slots stop taking space.
//
// The order is what makes it safe to lose power at any point. The copy is
// written under another name first, and the read position goes to zero before
// the old file is removed: a crash before the removal leaves the old file and
// a position of zero, which sends events again that the broker already has -
// the server turns those away by their identity - and never skips one.
bool compact() {
  const uint32_t waiting = slots - readIdx;
  if (waiting == 0) {
    LittleFS.remove(PATH);
    slots = readIdx = 0;
    saveCursor();
    return true;
  }

  uint8_t *tail = (uint8_t *)malloc(waiting * BYTES);
  if (!tail) return false;
  File in = LittleFS.open(PATH, "r");
  if (!in) {
    free(tail);
    return false;
  }
  in.seek(readIdx * BYTES);
  const uint32_t kept = in.read(tail, waiting * BYTES) / BYTES;
  in.close();

  File out = LittleFS.open(TMP, "w");
  if (!out) {
    free(tail);
    return false;
  }
  const size_t wrote = out.write(tail, kept * BYTES);
  out.close();
  free(tail);
  if (wrote != kept * BYTES) {
    LittleFS.remove(TMP);
    return false;
  }

  readIdx = 0;
  saveCursor();
  LittleFS.remove(PATH);
  if (!LittleFS.rename(TMP, PATH)) {
    Serial.println("[events] could not replace the queue file");
    slots = 0;
    mounted = false;
    return false;
  }
  slots = kept;
  return true;
}

bool append(const events::Event &e) {
  if (!mounted) return false;

  if (slots - readIdx >= LIMIT) {
    readIdx++;
    droppedEvents++;
    saveCursor();
    Serial.printf("[events] %lu waiting - the oldest is dropped\n", (unsigned long)LIMIT);
  }
  if (slots >= 2 * LIMIT) compact();

  uint8_t b[BYTES];
  encode(e, b);

  const char *mode = slots == 0 ? "w" : "a";
  File f = LittleFS.open(PATH, mode);
  if (!f) {
    // The directory is made at start; this means it went missing since.
    LittleFS.mkdir(DIR);
    f = LittleFS.open(PATH, mode);
  }
  if (!f) return false;
  const size_t written = f.write(b, BYTES);
  f.close();
  if (written != BYTES) {
    // Whatever part of it landed would put every later record out of step.
    compact();
    return false;
  }

  slots++;
  describe();
  return true;
}

events::Event make(events::Kind kind) {
  events::Event e;
  e.seq = nextSeq++;
  e.bootId = telemetry::bootId();
  e.tMonoS = millis() / 1000;
  e.tWall = telemetry::wallNow();
  e.timeSource = telemetry::timeSourceNow();
  e.kind = kind;
  return e;
}

void store(const events::Event &e) {
  if (!append(e)) Serial.println("[events] could not be stored - it will not be sent");
}

}  // namespace

namespace events {

bool begin() {
  if (!buffer::ready()) {
    snprintf(state, sizeof(state), "no filesystem");
    Serial.println("[events] no filesystem - events are logged, not sent");
    return false;
  }

  {
    Preferences prefs;
    if (prefs.begin(NVS_NS, /*readOnly=*/false)) {
      if (prefs.isKey("read")) readIdx = prefs.getULong("read", 0);
      prefs.end();
    }
  }

  // The directory listing rather than opening the files by name: an open of a
  // file that is not there logs an error, and on a board that has never raised
  // an event that is the normal case.
  size_t size = 0;
  bool havePath = false;
  bool haveTmp = false;
  File dir = LittleFS.open(DIR);
  if (!dir || !dir.isDirectory()) {
    LittleFS.mkdir(DIR);
  } else {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      const String name = String(f.name());
      if (name.endsWith("queue.bin")) {
        havePath = true;
        size = f.size();
      } else if (name.endsWith("queue.tmp")) {
        haveTmp = true;
      }
    }
    dir.close();
  }

  // A compaction cut short. With the old file still there, the copy is the
  // half-finished one; without it, the copy is the queue.
  if (haveTmp && havePath) LittleFS.remove(TMP);
  if (haveTmp && !havePath && LittleFS.rename(TMP, PATH)) {
    File f = LittleFS.open(PATH, "r");
    size = f ? f.size() : 0;
    if (f) f.close();
    readIdx = 0;
  }

  slots = size / BYTES;
  if (readIdx > slots) readIdx = slots;
  mounted = true;

  // A torn tail, or a file whose every event has been released.
  if (size % BYTES != 0 || (slots > 0 && readIdx == slots)) compact();

  describe();
  Serial.printf("[events] %s\n", state);
  return true;
}

void boot(uint8_t resetReason, uint16_t sensors) {
  Event e = make(Kind::Boot);
  e.subject = resetReason;
  e.count = sensors;
  Serial.printf("[event] boot, %s\n", resetReasonName(resetReason));
  store(e);
}

void sensorLost(Sensor s) {
  Event e = make(Kind::SensorLost);
  e.subject = (uint8_t)s;
  Serial.printf("[event] %s lost\n", sensorName(s));
  store(e);
}

void sensorBack(Sensor s) {
  Event e = make(Kind::SensorBack);
  e.subject = (uint8_t)s;
  Serial.printf("[event] %s back\n", sensorName(s));
  store(e);
}

void knock(uint8_t axes, float g, uint16_t held) {
  Event e = make(Kind::Knock);
  e.detail = axes;
  e.value = g;
  e.count = held;
  store(e);
}

void alarm(Alarm a, bool raised, Severity severity, Sensor s, float value, float limit) {
  Event e = make(Kind::Alarm);
  e.subject = (uint8_t)a;
  e.raised = raised;
  e.severity = severity;
  e.detail = (uint8_t)s;
  e.value = value;
  e.limit = limit;
  Serial.printf("[event] %s %s - %s %.1f against %.1f\n", alarmName(a),
                raised ? "raised" : "cleared", sensorName(s), value, limit);
  store(e);
}

void ack() {
  Serial.println("[event] alarms acknowledged at the box");
  store(make(Kind::Ack));
}

uint32_t pending() { return mounted ? slots - readIdx : 0; }

size_t peek(Event *out, size_t max, size_t &got) {
  got = 0;
  if (!mounted || max == 0 || readIdx >= slots) return 0;

  File f = LittleFS.open(PATH, "r");
  if (!f) return 0;
  f.seek(readIdx * BYTES);

  size_t read = 0;
  while (read < max && readIdx + read < slots) {
    uint8_t b[BYTES];
    if (f.read(b, BYTES) != BYTES) break;
    read++;
    if (decode(b, out[got])) got++;
  }
  f.close();
  return read;
}

void commit(size_t count) {
  if (!mounted || count == 0) return;

  readIdx += count;
  if (readIdx > slots) readIdx = slots;
  if (readIdx == slots) {
    // Everything is out. Removed before the position is saved: lose power in
    // between and the position is clamped to an empty file at the next start.
    LittleFS.remove(PATH);
    slots = readIdx = 0;
  }
  saveCursor();
  describe();
}

uint32_t dropped() { return droppedEvents; }

const char *sensorName(Sensor s) {
  const uint8_t i = (uint8_t)s;
  return i < (uint8_t)Sensor::Count ? SENSOR_NAMES[i] : "none";
}

const char *alarmName(Alarm a) {
  const uint8_t i = (uint8_t)a;
  return i < (uint8_t)Alarm::Count ? ALARM_NAMES[i] : "unknown";
}

const char *resetReasonName(uint8_t reason) {
  switch ((esp_reset_reason_t)reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    default: return "UNKNOWN";
  }
}

const char *statusText() { return state; }

}  // namespace events
