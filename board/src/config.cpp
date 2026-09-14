#include "config.h"

#include <Preferences.h>
#include <esp_mac.h>

namespace {

Preferences prefs;
Config cfg;
String suffix;

const char *NS = "boathub";

// Preferences logs an error for every key it does not find, which on a fresh
// board means a wall of red for the entirely normal "nothing stored yet" case.
// Ask first.
String str(const char *key, const String &fallback) {
  return prefs.isKey(key) ? prefs.getString(key) : fallback;
}

uint16_t u16(const char *key, uint16_t fallback) {
  return prefs.isKey(key) ? prefs.getUShort(key) : fallback;
}

void readMacSuffix() {
  uint8_t mac[6] = {0};
  // Works before Wi-Fi is started, unlike WiFi.macAddress().
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[7];
  snprintf(buf, sizeof(buf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  suffix = buf;
}

}  // namespace

namespace config {

void begin() {
  readMacSuffix();
  // Read-write even though this only reads: opening read-only on a namespace
  // that does not exist yet logs an error on every first boot, which looks
  // like a fault and is not one.
  prefs.begin(NS, /*readOnly=*/false);

  cfg.wifiSsid = str("wifi_ssid", "");
  cfg.wifiPass = str("wifi_pass", "");
  cfg.apPass = str("ap_pass", "boathub-" + suffix);
  cfg.boatId = str("boat_id", "boat-" + suffix);
  cfg.mqttHost = str("mqtt_host", "");
  cfg.mqttUser = str("mqtt_user", "");
  cfg.mqttPass = str("mqtt_pass", "");
  cfg.mqttPort = u16("mqtt_port", 1883);
  cfg.pubSecs = u16("pub_secs", 10);

  prefs.end();
}

Config &get() { return cfg; }

const String &macSuffix() { return suffix; }

void save(const Config &incoming) {
  cfg.wifiSsid = incoming.wifiSsid;
  cfg.boatId = incoming.boatId;
  cfg.mqttHost = incoming.mqttHost;
  cfg.mqttUser = incoming.mqttUser;
  cfg.mqttPort = incoming.mqttPort;
  cfg.pubSecs = incoming.pubSecs;

  // An empty password field means "leave it alone". Without this the form
  // would have to send the stored passwords to the browser just to save an
  // unrelated change.
  if (incoming.wifiPass.length() > 0) cfg.wifiPass = incoming.wifiPass;
  if (incoming.mqttPass.length() > 0) cfg.mqttPass = incoming.mqttPass;
  if (incoming.apPass.length() > 0) cfg.apPass = incoming.apPass;

  prefs.begin(NS, /*readOnly=*/false);
  prefs.putString("wifi_ssid", cfg.wifiSsid);
  prefs.putString("wifi_pass", cfg.wifiPass);
  prefs.putString("ap_pass", cfg.apPass);
  prefs.putString("boat_id", cfg.boatId);
  prefs.putString("mqtt_host", cfg.mqttHost);
  prefs.putString("mqtt_user", cfg.mqttUser);
  prefs.putString("mqtt_pass", cfg.mqttPass);
  prefs.putUShort("mqtt_port", cfg.mqttPort);
  prefs.putUShort("pub_secs", cfg.pubSecs);
  prefs.end();
}

}  // namespace config
