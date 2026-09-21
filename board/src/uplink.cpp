#include "uplink.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <espMqttClient.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "net.h"
#include "telemetry.h"

namespace {

// UseInternalTask::NO on purpose. The library can run its own task and call
// back from it, which would mean every callback here lands in a different
// thread from the rest of the firmware - and the store-and-forward buffer in
// A-007 will mutate a filesystem from exactly these callbacks. Driven from
// loop() instead, an acknowledgement arrives in the same task that publishes,
// and no part of this needs a mutex.
espMqttClient mqtt(espMqttClientTypes::UseInternalTask::NO);

const uint32_t RETRY_MIN_MS = 2000;
const uint32_t RETRY_MAX_MS = 60000;

// Anything later than 2023 means NTP has answered; the RTC starts at 1970.
const time_t TIME_SANE_AFTER = 1700000000;

// QoS 1 for telemetry: the broker answers every message with a PUBACK, which
// is what A-007's buffer will delete a stored record on. At QoS 0 there is no
// answer at all, so a buffer would have to delete on a guess.
const uint8_t QOS_TELEMETRY = 1;

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

String isoTime() {
  const time_t now = time(nullptr);
  struct tm tm;
  gmtime_r(&now, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(buf);
}

void buildTopics() {
  const String base = "boathub/" + config::get().boatId + "/";
  topicTelemetry = base + "telemetry";
  topicStatus = base + "status";
}

// Rounded to the sensor's own accuracy. Seven digits of float noise in every
// message would cost bytes and imply a precision that is not there.
float round2(float v) { return roundf(v * 100.0f) / 100.0f; }

// One channel becomes one, or three, fields.
//
// The extremes are written only when there is more than one sample behind
// them. A spot reading therefore carries the bare value alone - the same
// message shape as a window, without pretending to a range it never measured.
//
// A channel with no samples writes nothing at all. On the server "no sensor"
// and "measured zero" must not look the same.
void putChannel(JsonDocument &doc, const char *name, const telemetry::Channel &c) {
  if (!c.has()) return;

  doc[name] = round2(c.mean());
  if (c.n > 1) {
    doc[String(name) + "_min"] = round2(c.lo);
    doc[String(name) + "_max"] = round2(c.hi);
  }
}

void publish(const telemetry::Aggregate &agg) {
  JsonDocument doc;

  if (timeValid()) {
    doc["ts"] = isoTime();
    doc["time_valid"] = true;
  } else {
    doc["time_valid"] = false;
  }

  doc["n"] = agg.n;
  if (agg.windowS > 0) doc["window_s"] = agg.windowS;

  // The record's identity, so a message the broker acknowledged but whose
  // acknowledgement never arrived can be sent again without being counted
  // twice. Stamped when the record was made, not now.
  doc["boot_id"] = agg.bootId;
  doc["seq"] = agg.seq;

  // Diagnostics are read here, at the moment the message is built, rather than
  // averaged over the window. An averaged uptime would mean nothing.
  doc["uptime_s"] = millis() / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["rssi_dbm"] = net::rssi();
  doc["reset_reason"] = resetReason();

  putChannel(doc, "cabin_temp_c", agg.cabinTemp);
  putChannel(doc, "cabin_rh", agg.cabinRh);
  putChannel(doc, "engine_temp_c", agg.engineTemp);
  putChannel(doc, "bilge_temp_c", agg.bilgeTemp);
  putChannel(doc, "fridge_temp_c", agg.fridgeTemp);

  char payload[512];
  // measureJson is the length the document *wants*. serializeJson would
  // silently truncate into a shorter buffer and publish invalid JSON, which
  // looks like a healthy system until somebody checks the server.
  if (measureJson(doc) >= sizeof(payload)) {
    Serial.printf("[mqtt] payload too large: %u bytes\n", (unsigned)measureJson(doc));
    return;
  }
  const size_t n = serializeJson(doc, payload, sizeof(payload));

  const uint16_t id = mqtt.publish(topicTelemetry.c_str(), QOS_TELEMETRY, /*retain=*/false,
                                   reinterpret_cast<const uint8_t *>(payload), n);
  if (id == 0) {
    // Never silent: a message that never left looks exactly like a healthy
    // system until somebody checks the server.
    Serial.printf("[mqtt] publish rejected, %u bytes\n", (unsigned)n);
    return;
  }
  pendingAdd(id);
  Serial.printf("[mqtt] %s %s\n", topicTelemetry.c_str(), payload);
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
  const uint16_t id = mqtt.publish(topicStatus.c_str(), QOS_TELEMETRY, /*retain=*/true, "online");
  if (id != 0) pendingAdd(id);

  // A spot reading straight away. Waiting out a five minute window before the
  // first message would make a working link look like a broken one.
  publish(telemetry::spot());
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
  // - see begin().
  pendingClear();

  Serial.printf("[mqtt] disconnected: %s (%s)\n", status,
                espMqttClientTypes::disconnectReasonToString(reason));
}

void onPublished(uint16_t packetId) { pendingAck(packetId); }

void beginConnect() {
  const Config &c = config::get();

  cfgHost = c.mqttHost;
  cfgClientId = c.boatId;
  cfgUser = c.mqttUser;
  cfgPass = c.mqttPass;

  mqtt.setServer(cfgHost.c_str(), c.mqttPort);
  mqtt.setClientId(cfgClientId.c_str());
  if (cfgUser.length()) mqtt.setCredentials(cfgUser.c_str(), cfgPass.c_str());

  // The last will is retained for the same reason the "online" message is.
  mqtt.setWill(topicStatus.c_str(), QOS_TELEMETRY, /*retain=*/true, "offline");

  Serial.printf("[mqtt] connecting to %s:%u\n", cfgHost.c_str(), c.mqttPort);
  status = "connecting";
  if (!mqtt.connect()) {
    status = "connect refused";
    Serial.println("[mqtt] connect could not be started");
  }
}

}  // namespace

namespace uplink {

void begin() {
  buildTopics();
  // UTC only. No local time anywhere in the payload.
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");

  mqtt.setKeepAlive(30);
  // A clean session on purpose. The board's durability is its own filesystem
  // once A-007 exists, not state the broker holds for a client that may be
  // away for weeks.
  mqtt.setCleanSession(true);

  mqtt.onConnect(onConnected);
  mqtt.onDisconnect(onDisconnected);
  mqtt.onPublish(onPublished);
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

  // Unconditionally: this is what drives the client's state machine, including
  // the handshake that has not finished yet and the acknowledgements coming
  // back for messages already sent.
  mqtt.loop();

  if (!mqtt.connected()) {
    // Not connected is not the same as ready to connect. Between a dropped
    // socket and the disconnect callback the client is still tearing the old
    // connection down, and a connect() attempted in that window is refused.
    if (!mqtt.disconnected()) return;
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
    publish(telemetry::spot());
  }

  telemetry::Aggregate agg;
  if (telemetry::take(agg)) publish(agg);
}

bool connected() { return mqtt.connected(); }

void publishNow() {
  if (!mqtt.connected()) {
    Serial.println("[mqtt] publish requested, but not connected");
    return;
  }
  publishRequested = true;
}

const char *statusText() { return status; }

}  // namespace uplink
