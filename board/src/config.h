// Configuration held in NVS.
//
// Nothing in here has a compiled-in default that is a secret. Where a default
// is needed it is derived from the MAC address, so two boards never share one.
// See docs/design/A-004-wifi-and-configuration-portal.md.

#pragma once

#include <Arduino.h>

struct Config {
  String wifiSsid;
  String wifiPass;
  String apPass;
  String boatId;
  String mqttHost;
  String mqttUser;
  String mqttPass;
  uint16_t mqttPort = 1883;
  uint16_t pubSecs = 10;

  bool hasStation() const { return wifiSsid.length() > 0; }
  bool hasBroker() const { return mqttHost.length() > 0; }
};

namespace config {

// Loads from NVS, filling in the MAC-derived defaults where nothing is stored.
void begin();

Config &get();

// Writes to NVS. Empty password fields mean "keep what is stored", so the page
// can change an SSID without the password being sent to the browser first.
void save(const Config &incoming);

// Last three bytes of the MAC as lower-case hex, e.g. "a4f2c1".
const String &macSuffix();

// Puts the access point password back to the MAC-derived default. The only
// real lockout this design allows is forgetting it - everything else stays
// reachable, because the access point is never switched off.
void resetApPassword();

}  // namespace config
