// Wi-Fi: the access point is always up, the station comes and goes.
//
// Nothing here blocks. The station is polled and retried with backoff, so a
// marina outage slows the rest of the firmware down by nothing at all.
// See docs/design/A-004-wifi-and-configuration-portal.md.

#pragma once

#include <Arduino.h>

namespace net {

enum class State {
  Portal,      // nothing configured - the access point is all there is
  Connecting,  // credentials known, station not associated
  Online       // station associated and addressed
};

void begin();
void loop();

State state();
const char *stateName();

String apIp();
String stationIp();
int rssi();

}  // namespace net
