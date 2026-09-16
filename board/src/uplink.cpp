#include "uplink.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "net.h"
#include "sht31.h"

namespace {

WiFiClient tcp;
PubSubClient mqtt(tcp);

const uint32_t RETRY_MIN_MS = 2000;
const uint32_t RETRY_MAX_MS = 60000;

// Anything later than 2023 means NTP has answered; the RTC starts at 1970.
const time_t TIME_SANE_AFTER = 1700000000;

uint32_t retryDelay = RETRY_MIN_MS;
uint32_t nextAttempt = 0;
uint32_t nextPublish = 0;
bool publishRequested = false;
const char *status = "not connected";

String topicTelemetry, topicStatus;

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

bool tryConnect() {
  const Config &c = config::get();
  mqtt.setServer(c.mqttHost.c_str(), c.mqttPort);

  const char *user = c.mqttUser.length() ? c.mqttUser.c_str() : nullptr;
  const char *pass = c.mqttPass.length() ? c.mqttPass.c_str() : nullptr;

  Serial.printf("[mqtt] connecting to %s:%u\n", c.mqttHost.c_str(), c.mqttPort);

  // The last will is retained, so a client connecting after the board has gone
  // sees "offline" straight away instead of waiting for a heartbeat to lapse.
  const bool ok = mqtt.connect(c.boatId.c_str(), user, pass, topicStatus.c_str(), 0,
                               /*retain=*/true, "offline");
  if (ok) {
    mqtt.publish(topicStatus.c_str(), "online", /*retain=*/true);
    status = "connected";
    Serial.println("[mqtt] connected");
    return true;
  }

  switch (mqtt.state()) {
    case 4: status = "bad credentials"; break;
    case 5: status = "not authorised"; break;
    case -2: status = "unreachable"; break;
    default: status = "connect failed"; break;
  }
  Serial.printf("[mqtt] %s (state %d)\n", status, mqtt.state());
  return false;
}

void publish() {
  JsonDocument doc;

  if (timeValid()) {
    doc["ts"] = isoTime();
    doc["time_valid"] = true;
  } else {
    doc["time_valid"] = false;
  }
  doc["uptime_s"] = millis() / 1000;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["rssi_dbm"] = net::rssi();
  doc["reset_reason"] = resetReason();

  // A field with no reading is left out entirely, never sent as zero: on the
  // server "no sensor" and "measured zero" must not look the same. latest()
  // already returns invalid for a stale reading, which covers both a sensor
  // that has stopped answering and the window in which the heater runs.
  const sht31::Reading climate = sht31::latest();
  if (climate.valid) {
    // Rounded to the sensor's own accuracy. Seven digits of float noise in
    // every message would cost bytes and imply a precision that is not there.
    doc["cabin_temp_c"] = roundf(climate.tempC * 100.0f) / 100.0f;
    doc["cabin_rh"] = roundf(climate.rh * 100.0f) / 100.0f;
  }

  char payload[512];
  // measureJson is the length the document *wants*. serializeJson would
  // silently truncate into a shorter buffer and publish invalid JSON, which
  // looks like a healthy system until somebody checks the server.
  if (measureJson(doc) >= sizeof(payload)) {
    Serial.printf("[mqtt] payload too large: %u bytes\n", (unsigned)measureJson(doc));
    return;
  }
  const size_t n = serializeJson(doc, payload, sizeof(payload));

  if (!mqtt.publish(topicTelemetry.c_str(), payload, n)) {
    // Never silent: a payload outgrowing the buffer looks exactly like a
    // healthy system until somebody checks the server.
    Serial.printf("[mqtt] publish failed, %u bytes\n", (unsigned)n);
    return;
  }
  Serial.printf("[mqtt] %s %s\n", topicTelemetry.c_str(), payload);
}

}  // namespace

namespace uplink {

void begin() {
  buildTopics();
  // UTC only. No local time anywhere in the payload.
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  // The default is 256 bytes and the payload grows with every sensor.
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
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

  if (!mqtt.connected()) {
    status = "not connected";
    if ((int32_t)(now - nextAttempt) < 0) return;
    if (tryConnect()) {
      retryDelay = RETRY_MIN_MS;
      nextPublish = now;  // first message immediately, so the link is visible at once
    } else {
      nextAttempt = now + retryDelay;
      retryDelay = min(retryDelay * 2, RETRY_MAX_MS);
    }
    return;
  }

  mqtt.loop();

  if (publishRequested || (int32_t)(now - nextPublish) >= 0) {
    publishRequested = false;
    nextPublish = now + (uint32_t)c.pubSecs * 1000;
    publish();
  }
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
