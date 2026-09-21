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

bool flag(const char *key, bool fallback) {
  return prefs.isKey(key) ? prefs.getBool(key) : fallback;
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
  cfg.mqttTls = flag("mqtt_tls", false);
  cfg.sampleSecs = u16("smpl_secs", 10);
  cfg.pubSecs = u16("pub_secs", 300);

  cfg.sht31Heater = flag("sht_heat", true);
  cfg.sht31HeatAboveRh = u16("sht_rh", 95);
  cfg.sht31SoakMins = u16("sht_soak", 30);
  cfg.sht31HeatSecs = u16("sht_on", 10);
  cfg.sht31CoolSecs = u16("sht_cool", 120);

  prefs.end();
}

Config &get() { return cfg; }

void resetApPassword() {
  cfg.apPass = "boathub-" + suffix;
  prefs.begin(NS, /*readOnly=*/false);
  prefs.putString("ap_pass", cfg.apPass);
  prefs.end();
  Serial.printf("[config] access point password reset to \"%s\"\n", cfg.apPass.c_str());
}

const String &macSuffix() { return suffix; }

void save(const Config &incoming) {
  cfg.wifiSsid = incoming.wifiSsid;
  cfg.boatId = incoming.boatId;
  cfg.mqttHost = incoming.mqttHost;
  cfg.mqttUser = incoming.mqttUser;
  cfg.mqttPort = incoming.mqttPort;
  cfg.mqttTls = incoming.mqttTls;
  cfg.sampleSecs = incoming.sampleSecs;
  cfg.pubSecs = incoming.pubSecs;
  cfg.sht31Heater = incoming.sht31Heater;
  cfg.sht31HeatAboveRh = incoming.sht31HeatAboveRh;
  cfg.sht31SoakMins = incoming.sht31SoakMins;
  cfg.sht31HeatSecs = incoming.sht31HeatSecs;
  cfg.sht31CoolSecs = incoming.sht31CoolSecs;

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
  prefs.putBool("mqtt_tls", cfg.mqttTls);
  prefs.putUShort("smpl_secs", cfg.sampleSecs);
  prefs.putUShort("pub_secs", cfg.pubSecs);
  prefs.putBool("sht_heat", cfg.sht31Heater);
  prefs.putUShort("sht_rh", cfg.sht31HeatAboveRh);
  prefs.putUShort("sht_soak", cfg.sht31SoakMins);
  prefs.putUShort("sht_on", cfg.sht31HeatSecs);
  prefs.putUShort("sht_cool", cfg.sht31CoolSecs);
  prefs.end();
}

}  // namespace config
