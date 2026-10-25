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

int16_t i16(const char *key, int16_t fallback) {
  return prefs.isKey(key) ? prefs.getShort(key) : fallback;
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

  cfg.imuForward = u16("imu_fwd", 0);
  cfg.imuUp = u16("imu_up", 4);
  cfg.imuTauDs = u16("imu_tau", 30);
  cfg.imuKnockMg = u16("imu_knock", 500);
  cfg.imuHoldoffSecs = u16("imu_hold", 60);

  cfg.bmeGas = flag("bme_gas", false);
  cfg.bmePressureOffsetPa = i16("bme_p_off", 0);

  cfg.batCalMilli = u16("bat_cal", 11000);
  cfg.bilgeFullCm = u16("bilge_fs", 100);
  cfg.bilgeOffsetMm = i16("bilge_off", 0);

  cfg.alarmFrost = flag("al_frost", true);
  cfg.frostC10 = i16("frost_c10", 30);
  cfg.frostMins = u16("frost_min", 10);
  cfg.alarmFridge = flag("al_fridge", true);
  cfg.fridgeArmC10 = i16("fr_arm_c10", 60);
  cfg.fridgeArmMins = u16("fr_arm_min", 60);
  cfg.fridgeWarmC10 = i16("fr_warm_c10", 100);
  cfg.fridgeWarmMins = u16("fr_warm_min", 30);
  cfg.alarmHumidity = flag("al_rh", true);
  cfg.rhWarn = u16("rh_warn", 85);
  cfg.rhHours = u16("rh_hours", 6);
  cfg.rhClear = u16("rh_clear", 80);

  cfg.ledBreathe = flag("led_breathe", true);

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
  cfg.imuForward = incoming.imuForward;
  cfg.imuUp = incoming.imuUp;
  cfg.imuTauDs = incoming.imuTauDs;
  cfg.imuKnockMg = incoming.imuKnockMg;
  cfg.imuHoldoffSecs = incoming.imuHoldoffSecs;
  cfg.bmeGas = incoming.bmeGas;
  cfg.bmePressureOffsetPa = incoming.bmePressureOffsetPa;
  cfg.batCalMilli = incoming.batCalMilli;
  cfg.bilgeFullCm = incoming.bilgeFullCm;
  cfg.bilgeOffsetMm = incoming.bilgeOffsetMm;
  cfg.alarmFrost = incoming.alarmFrost;
  cfg.frostC10 = incoming.frostC10;
  cfg.frostMins = incoming.frostMins;
  cfg.alarmFridge = incoming.alarmFridge;
  cfg.fridgeArmC10 = incoming.fridgeArmC10;
  cfg.fridgeArmMins = incoming.fridgeArmMins;
  cfg.fridgeWarmC10 = incoming.fridgeWarmC10;
  cfg.fridgeWarmMins = incoming.fridgeWarmMins;
  cfg.alarmHumidity = incoming.alarmHumidity;
  cfg.rhWarn = incoming.rhWarn;
  cfg.rhHours = incoming.rhHours;
  cfg.rhClear = incoming.rhClear;
  cfg.ledBreathe = incoming.ledBreathe;

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
  prefs.putUShort("imu_fwd", cfg.imuForward);
  prefs.putUShort("imu_up", cfg.imuUp);
  prefs.putUShort("imu_tau", cfg.imuTauDs);
  prefs.putUShort("imu_knock", cfg.imuKnockMg);
  prefs.putUShort("imu_hold", cfg.imuHoldoffSecs);
  prefs.putBool("bme_gas", cfg.bmeGas);
  prefs.putShort("bme_p_off", cfg.bmePressureOffsetPa);
  prefs.putUShort("bat_cal", cfg.batCalMilli);
  prefs.putUShort("bilge_fs", cfg.bilgeFullCm);
  prefs.putShort("bilge_off", cfg.bilgeOffsetMm);
  prefs.putBool("al_frost", cfg.alarmFrost);
  prefs.putShort("frost_c10", cfg.frostC10);
  prefs.putUShort("frost_min", cfg.frostMins);
  prefs.putBool("al_fridge", cfg.alarmFridge);
  prefs.putShort("fr_arm_c10", cfg.fridgeArmC10);
  prefs.putUShort("fr_arm_min", cfg.fridgeArmMins);
  prefs.putShort("fr_warm_c10", cfg.fridgeWarmC10);
  prefs.putUShort("fr_warm_min", cfg.fridgeWarmMins);
  prefs.putBool("al_rh", cfg.alarmHumidity);
  prefs.putUShort("rh_warn", cfg.rhWarn);
  prefs.putUShort("rh_hours", cfg.rhHours);
  prefs.putUShort("rh_clear", cfg.rhClear);
  prefs.putBool("led_breathe", cfg.ledBreathe);
  prefs.end();
}

}  // namespace config
