#include "net.h"

#include <WiFi.h>

#include "config.h"

namespace {

const char *AP_SSID = "BOOT-NETZ";

const uint32_t RETRY_MIN_MS = 1000;
const uint32_t RETRY_MAX_MS = 60000;

net::State current = net::State::Portal;
uint32_t retryDelay = RETRY_MIN_MS;
uint32_t nextAttempt = 0;
bool attempting = false;

void startAttempt(uint32_t now) {
  const Config &c = config::get();
  Serial.printf("[wifi] connecting to \"%s\"\n", c.wifiSsid.c_str());
  WiFi.begin(c.wifiSsid.c_str(), c.wifiPass.c_str());
  attempting = true;
  current = net::State::Connecting;
  // Give the association a window of its own before counting it as failed.
  nextAttempt = now + 15000;
}

}  // namespace

namespace net {

void begin() {
  const Config &c = config::get();

  WiFi.persistent(false);  // credentials live in our NVS namespace, not the SDK's
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);  // the backoff below is ours

  const bool ap = WiFi.softAP(AP_SSID, c.apPass.c_str());
  Serial.printf("[wifi] access point \"%s\" %s, password \"%s\", address %s\n", AP_SSID,
                ap ? "up" : "FAILED", c.apPass.c_str(), WiFi.softAPIP().toString().c_str());

  if (c.hasStation()) {
    startAttempt(millis());
  } else {
    Serial.println("[wifi] no station configured - open http://192.168.4.1 on BOOT-NETZ");
    current = State::Portal;
  }
}

void loop() {
  const uint32_t now = millis();
  const Config &c = config::get();

  if (!c.hasStation()) {
    current = State::Portal;
    return;
  }

  const bool up = WiFi.status() == WL_CONNECTED;

  if (up) {
    if (current != State::Online) {
      Serial.printf("[wifi] online, address %s, rssi %d dBm\n", WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
      current = State::Online;
      retryDelay = RETRY_MIN_MS;  // a success resets the backoff
      attempting = false;
    }
    return;
  }

  if (current == State::Online) {
    Serial.println("[wifi] connection lost");
    current = State::Connecting;
    attempting = false;
    nextAttempt = now;  // one immediate retry, then back off
  }

  if ((int32_t)(now - nextAttempt) < 0) {
    return;
  }

  if (attempting) {
    // The window expired without an association.
    Serial.printf("[wifi] no association, retrying in %lu s\n", (unsigned long)(retryDelay / 1000));
    WiFi.disconnect();
    attempting = false;
    nextAttempt = now + retryDelay;
    retryDelay = min(retryDelay * 2, RETRY_MAX_MS);
    return;
  }

  startAttempt(now);
}

State state() { return current; }

const char *stateName() {
  switch (current) {
    case State::Online: return "online";
    case State::Connecting: return "connecting";
    default: return "portal";
  }
}

String apIp() { return WiFi.softAPIP().toString(); }

String stationIp() {
  return current == State::Online ? WiFi.localIP().toString() : String("-");
}

int rssi() { return current == State::Online ? WiFi.RSSI() : 0; }

}  // namespace net
