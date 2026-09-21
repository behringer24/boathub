#include "uplink.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <espMqttClient.h>
#include <esp_system.h>
#include <time.h>

#include "buffer.h"
#include "ca_isrg.h"
#include "config.h"
#include "net.h"
#include "telemetry.h"

namespace {

// One client, of whichever kind the configuration asks for.
//
// Plain and TLS are different types in this library, and each carries a
// kilobyte and a half of receive buffer as a member - so the one that is not
// in use is not built at all rather than sitting there costing RAM. Everything
// after the connection is set up goes through the base class, which both
// share.
//
// UseInternalTask::NO on purpose. The library can run its own task and call
// back from it, which would mean every callback here lands in a different
// thread from the rest of the firmware - and the store-and-forward buffer in
// A-007 mutates a filesystem from exactly these callbacks. Driven from loop()
// instead, an acknowledgement arrives in the same task that publishes, and no
// part of this needs a mutex.
espMqttClient *plain = nullptr;
espMqttClientSecure *secure = nullptr;
MqttClient *mqtt = nullptr;

const uint32_t RETRY_MIN_MS = 2000;
const uint32_t RETRY_MAX_MS = 60000;

// Anything later than 2023 means NTP has answered; the RTC starts at 1970.
const time_t TIME_SANE_AFTER = 1700000000;

// QoS 1 for telemetry: the broker answers every message with a PUBACK, which
// is what A-007's buffer will delete a stored record on. At QoS 0 there is no
// answer at all, so a buffer would have to delete on a guess.
const uint8_t QOS_TELEMETRY = 1;

// Records per message while draining a backlog.
//
// Sent one at a time, a day of backlog is 288 round trips, each waiting out a
// PUBACK over whatever link the boat has. Batching is what decides whether a
// short window of connectivity is enough to catch up at all. Fifty records is
// about 9 kB of JSON, comfortably inside what the client will send in one
// message.
const size_t BATCH_RECORDS = 50;

// One batch in flight at a time. The cursor is a single position in a single
// stream, so a second batch could only be the same records again.
uint16_t batchPacket = 0;
size_t batchCount = 0;

uint32_t retryDelay = RETRY_MIN_MS;
uint32_t nextAttempt = 0;
bool publishRequested = false;
const char *status = "not connected";

String topicTelemetry, topicStatus;

// The client stores the pointers it is given rather than copying the strings,
// so these have to outlive every connection attempt. config::get() returns a
// reference that the configuration portal may rewrite underneath us.
String cfgHost, cfgClientId, cfgUser, cfgPass;

// Publishes waiting for their PUBACK.
//
// Nothing here acts on the acknowledgement yet - it is logged, and that is
// enough to prove the round trip works end to end. A-007 replaces the logging
// with advancing the buffer's read cursor, which is the whole reason this
// table exists at this stage rather than later.
struct Pending {
  uint16_t id = 0;
  uint32_t sentMs = 0;
};
const size_t PENDING_MAX = 8;
Pending pending[PENDING_MAX];

void pendingAdd(uint16_t id) {
  for (Pending &p : pending) {
    if (p.id == 0) {
      p.id = id;
      p.sentMs = millis();
      return;
    }
  }
  // Full means the broker has stopped acknowledging while the link still looks
  // up. Worth saying, because the symptom is otherwise just silence.
  Serial.printf("[mqtt] %u publishes outstanding, none acknowledged\n",
                (unsigned)PENDING_MAX);
}

void pendingAck(uint16_t id) {
  for (Pending &p : pending) {
    if (p.id != id) continue;
    Serial.printf("[mqtt] packet %u acknowledged after %lu ms\n", id,
                  (unsigned long)(millis() - p.sentMs));
    p = Pending{};
    return;
  }
  Serial.printf("[mqtt] packet %u acknowledged, but not outstanding here\n", id);
}

void pendingClear() {
  for (Pending &p : pending) p = Pending{};
}

const char *resetReason() {
  switch (esp_reset_reason()) {
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

bool timeValid() { return time(nullptr) > TIME_SANE_AFTER; }

void buildTopics() {
  const String base = "boathub/" + config::get().boatId + "/";
  topicTelemetry = base + "telemetry";
  topicStatus = base + "status";
}

// One channel becomes one, or three, fields.
//
// The extremes are written only when there is more than one sample behind
// them. A spot reading therefore carries the bare value alone - the same
// message shape as a window, without pretending to a range it never measured.
//
// A channel with no samples writes nothing at all. On the server "no sensor"
// and "measured zero" must not look the same.
void putChannel(JsonObject &o, const char *name, const buffer::StoredChannel &c, float scale) {
  if (!c.has()) return;

  o[name] = c.mean / scale;
  if (c.hasRange()) {
    o[String(name) + "_min"] = c.lo / scale;
    o[String(name) + "_max"] = c.hi / scale;
  }
}

const char *sourceName(buffer::TimeSource s) {
  switch (s) {
    case buffer::TimeSource::Ntp: return "ntp";
    case buffer::TimeSource::Gps: return "gps";
    case buffer::TimeSource::Restored: return "restored";
    default: return "none";
  }
}

String isoOf(uint32_t unixSeconds) {
  const time_t when = (time_t)unixSeconds;
  struct tm tm;
  gmtime_r(&when, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(buf);
}

// One stored record as one JSON object.
//
// This is the only encoder. A live reading goes through it as a batch of one,
// so a measurement sent immediately and the same measurement drained from
// flash a week later are byte for byte the same shape on the wire.
void putRecord(JsonArray &arr, const buffer::Record &r, uint32_t offsetIfUndated) {
  JsonObject o = arr.add<JsonObject>();

  uint32_t wall = r.tWall;
  buffer::TimeSource src = r.timeSource;

  // A record written before the clock was known carries no wall time. If the
  // clock has since been set, the offset recovered for this boot dates it -
  // as a lower bound, and labelled as one. Records on flash are never
  // rewritten; this happens as they are encoded.
  if (wall == 0 && offsetIfUndated > 0 && r.bootId == telemetry::bootId()) {
    wall = offsetIfUndated + r.tMonoS;
    src = buffer::TimeSource::Restored;
  }

  if (wall > 0) o["ts"] = isoOf(wall);
  o["time_valid"] = (src == buffer::TimeSource::Ntp || src == buffer::TimeSource::Gps);
  o["time_source"] = sourceName(src);

  o["n"] = r.n;
  if (r.windowS > 0) o["window_s"] = r.windowS;
  o["boot_id"] = r.bootId;
  o["seq"] = r.seq;

  o["uptime_s"] = r.tMonoS;
  o["heap_free"] = (uint32_t)r.heapKb * 1024;
  o["rssi_dbm"] = r.rssi;
  o["reset_reason"] = resetReason();
  if (buffer::dropped() > 0) o["dropped"] = buffer::dropped();

  putChannel(o, "battery_v", r.ch[buffer::BatteryV], 1000.0f);
  putChannel(o, "cabin_temp_c", r.ch[buffer::CabinTempC], 100.0f);
  putChannel(o, "cabin_rh", r.ch[buffer::CabinRh], 10.0f);
  putChannel(o, "engine_temp_c", r.ch[buffer::EngineTempC], 100.0f);
  putChannel(o, "bilge_temp_c", r.ch[buffer::BilgeTempC], 100.0f);
  putChannel(o, "fridge_temp_c", r.ch[buffer::FridgeTempC], 100.0f);
  putChannel(o, "bilge_level_cm", r.ch[buffer::BilgeLevelCm], 10.0f);
}

// The offset that dates records written before the clock was known, or 0.
uint32_t undatedOffset() {
  if (!timeValid()) return 0;
  const uint32_t nowMono = millis() / 1000;
  const uint32_t nowWall = (uint32_t)time(nullptr);
  return nowWall > nowMono ? nowWall - nowMono : 0;
}

// Publishes a batch of records as one array. Returns the packet id, or 0.
uint16_t publishRecords(const buffer::Record *recs, size_t count) {
  if (count == 0) return 0;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  const uint32_t offset = undatedOffset();
  for (size_t i = 0; i < count; i++) putRecord(arr, recs[i], offset);

  const size_t need = measureJson(doc) + 1;
  // measureJson is the length the document *wants*. Serialising into something
  // shorter truncates silently and publishes invalid JSON, which looks like a
  // healthy system until somebody checks the server.
  char *payload = (char *)malloc(need);
  if (!payload) {
    Serial.printf("[mqtt] no room for a %u byte payload\n", (unsigned)need);
    return 0;
  }
  const size_t n = serializeJson(doc, payload, need);

  const uint16_t id = mqtt->publish(topicTelemetry.c_str(), QOS_TELEMETRY, /*retain=*/false,
                                   reinterpret_cast<const uint8_t *>(payload), n);
  if (id == 0) {
    Serial.printf("[mqtt] publish rejected, %u records, %u bytes\n", (unsigned)count, (unsigned)n);
  } else {
    pendingAdd(id);
    if (count == 1) {
      Serial.printf("[mqtt] %s %s\n", topicTelemetry.c_str(), payload);
    } else {
      Serial.printf("[mqtt] %s %u records, %u bytes\n", topicTelemetry.c_str(), (unsigned)count,
                    (unsigned)n);
    }
  }
  free(payload);
  return id;
}

// The live reading: what the boat is doing now, as a batch of one.
void publishSpot() {
  buffer::Record r;
  telemetry::toRecord(telemetry::spot(), r);
  publishRecords(&r, 1);
}

// One batch of the backlog, if there is any and none is already in flight.
//
// The cursor does not move here. It moves in onPublished, when the broker has
// said it has the records - which is the whole reason the buffer can delete
// anything at all.
void drainStep() {
  if (batchPacket != 0) return;
  if (buffer::pending() == 0) return;

  static buffer::Record recs[BATCH_RECORDS];
  const size_t got = buffer::peek(recs, BATCH_RECORDS);
  if (got == 0) return;

  const uint16_t id = publishRecords(recs, got);
  if (id == 0) return;

  batchPacket = id;
  batchCount = got;
}

void onConnected(bool sessionPresent) {
  (void)sessionPresent;
  status = "connected";
  retryDelay = RETRY_MIN_MS;
  Serial.println("[mqtt] connected");

  // Retained, so a client arriving later sees the state straight away instead
  // of waiting for a heartbeat to lapse. Tracked like any other publish: at
  // QoS 1 it is acknowledged too, and an acknowledgement for something the
  // table has never heard of is a symptom worth keeping loud.
  const uint16_t id = mqtt->publish(topicStatus.c_str(), QOS_TELEMETRY, /*retain=*/true, "online");
  if (id != 0) pendingAdd(id);

  // A spot reading straight away, before any backlog.
  //
  // This looks like a detail and is not. A window of connectivity may be
  // minutes; spent oldest-first it goes entirely on three-day-old cabin
  // temperatures, and the one question worth answering - what is the boat
  // doing right now - is still unanswered when the window closes.
  publishSpot();
}

void onDisconnected(espMqttClientTypes::DisconnectReason reason) {
  using Reason = espMqttClientTypes::DisconnectReason;
  switch (reason) {
    case Reason::MQTT_MALFORMED_CREDENTIALS: status = "bad credentials"; break;
    case Reason::MQTT_NOT_AUTHORIZED: status = "not authorised"; break;
    case Reason::MQTT_IDENTIFIER_REJECTED: status = "client id rejected"; break;
    case Reason::MQTT_SERVER_UNAVAILABLE: status = "broker unavailable"; break;
    case Reason::TCP_DISCONNECTED: status = "unreachable"; break;
    default: status = "not connected"; break;
  }

  // Whatever was in flight died with the session. Without a clean session the
  // broker would hold it for us; the board deliberately does not ask for that
  // - see begin(). The batch is simply unsent: the cursor never moved, so the
  // next connection reads the same records again.
  pendingClear();
  batchPacket = 0;
  batchCount = 0;

  Serial.printf("[mqtt] disconnected: %s (%s)\n", status,
                espMqttClientTypes::disconnectReasonToString(reason));
}

void onPublished(uint16_t packetId) {
  pendingAck(packetId);

  if (packetId != batchPacket) return;

  // The broker has it. Only now do the records stop being the board's problem,
  // and the wall time goes to NVS with the cursor so the next boot starts from
  // a floor rather than from 1970.
  const uint32_t wallNow = timeValid() ? (uint32_t)time(nullptr) : 0;
  buffer::commit(batchCount, wallNow);
  Serial.printf("[mqtt] %u records released, %lu still waiting\n", (unsigned)batchCount,
                (unsigned long)buffer::pending());

  batchPacket = 0;
  batchCount = 0;
}

// The settings that have to be applied to the concrete client. Both kinds
// carry them, but on the derived class rather than the base, so this is a
// template instead of two copies that could drift apart.
template <typename T>
void applySettings(T *client, const Config &c) {
  client->setServer(cfgHost.c_str(), c.mqttPort);
  client->setClientId(cfgClientId.c_str());
  if (cfgUser.length()) client->setCredentials(cfgUser.c_str(), cfgPass.c_str());

  // The last will is retained for the same reason the "online" message is.
  client->setWill(topicStatus.c_str(), QOS_TELEMETRY, /*retain=*/true, "offline");
}

void beginConnect() {
  const Config &c = config::get();

  cfgHost = c.mqttHost;
  cfgClientId = c.boatId;
  cfgUser = c.mqttUser;
  cfgPass = c.mqttPass;

  if (plain) applySettings(plain, c);
  if (secure) applySettings(secure, c);

  Serial.printf("[mqtt] connecting to %s:%u%s\n", cfgHost.c_str(), c.mqttPort,
                secure ? " over TLS" : "");
  status = "connecting";
  if (!mqtt->connect()) {
    status = "connect refused";
    Serial.println("[mqtt] connect could not be started");
  }
}

}  // namespace

namespace uplink {

// Everything that does not depend on which broker is configured.
template <typename T>
void applyCommon(T *client) {
  client->setKeepAlive(30);
  // A clean session on purpose. The board's durability is its own filesystem,
  // not state the broker holds for a client that may be away for weeks.
  client->setCleanSession(true);

  client->onConnect(onConnected);
  client->onDisconnect(onDisconnected);
  client->onPublish(onPublished);
}

void begin() {
  buildTopics();
  // UTC only. No local time anywhere in the payload.
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");

  // Which kind of client, decided once. Changing it needs a restart, which a
  // board whose broker has just been reconfigured is going to get anyway.
  if (config::get().mqttTls) {
    secure = new espMqttClientSecure(espMqttClientTypes::UseInternalTask::NO);
    // The root, not the server's own certificate. That one is reissued every
    // ninety days and a board pinned to it would fall silent in three months.
    secure->setCACert(CA_ISRG_ROOT_X1);
    applyCommon(secure);
    mqtt = secure;
    Serial.println("[mqtt] TLS enabled, trusting ISRG Root X1");
  } else {
    plain = new espMqttClient(espMqttClientTypes::UseInternalTask::NO);
    applyCommon(plain);
    mqtt = plain;
  }
}

void loop() {
  const Config &c = config::get();
  const uint32_t now = millis();

  if (!c.hasBroker()) {
    status = "not configured";
    return;
  }
  if (net::state() != net::State::Online) {
    status = "waiting for network";
    return;
  }

  // A certificate is only valid between two dates, so validating one against a
  // clock that reads 1970 - or against the floor a long lay-up restored -
  // fails for a reason that has nothing to do with the certificate. NTP runs
  // over UDP and needs no TLS itself, so this is an ordering problem rather
  // than a circular one: wait for it.
  if (secure && !timeValid()) {
    status = "waiting for the clock";
    return;
  }

  // Unconditionally: this is what drives the client's state machine, including
  // the handshake that has not finished yet and the acknowledgements coming
  // back for messages already sent.
  mqtt->loop();

  if (!mqtt->connected()) {
    // Not connected is not the same as ready to connect. Between a dropped
    // socket and the disconnect callback the client is still tearing the old
    // connection down, and a connect() attempted in that window is refused.
    if (!mqtt->disconnected()) return;
    if ((int32_t)(now - nextAttempt) < 0) return;
    beginConnect();
    nextAttempt = now + retryDelay;
    retryDelay = min(retryDelay * 2, RETRY_MAX_MS);
    return;
  }

  // The BOOT button: the current state, now. It deliberately does not close
  // the running window - you press it to prove the chain works, not to cut a
  // measurement short.
  if (publishRequested) {
    publishRequested = false;
    publishSpot();
  }

  drainStep();
}

bool connected() { return mqtt->connected(); }

void publishNow() {
  if (!mqtt->connected()) {
    Serial.println("[mqtt] publish requested, but not connected");
    return;
  }
  publishRequested = true;
}

const char *statusText() { return status; }

}  // namespace uplink
