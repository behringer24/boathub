#include "config.h"

#include <Preferences.h>
#include <esp_mac.h>

namespace {

Preferences prefs;
Config cfg;
String suffix;

const char *NS = "boathub";

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
  prefs.begin(NS, /*readOnly=*/true);

  cfg.wifiSsid = prefs.getString("wifi_ssid", "");
  cfg.wifiPass = prefs.getString("wifi_pass", "");
  cfg.apPass = prefs.getString("ap_pass", "boathub-" + suffix);
  cfg.boatId = prefs.getString("boat_id", "boat-" + suffix);
  cfg.mqttHost = prefs.getString("mqtt_host", "");
  cfg.mqttUser = prefs.getString("mqtt_user", "");
  cfg.mqttPass = prefs.getString("mqtt_pass", "");
  cfg.mqttPort = prefs.getUShort("mqtt_port", 1883);
  cfg.pubSecs = prefs.getUShort("pub_secs", 10);

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
