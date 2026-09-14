// NTP and the MQTT uplink.
//
// Publishes; never subscribes to anything that can make the board act. There
// is no control path from the server and none is planned - autopilot control
// stays on the local on-board Wi-Fi.
// See docs/design/A-005-server-uplink.md.

#pragma once

#include <Arduino.h>

namespace uplink {

void begin();
void loop();

bool connected();

// Publish at the next opportunity instead of waiting for the interval. Used by
// the BOOT button: stand at the box, press, and watch the server.
void publishNow();

// Short human-readable state for the configuration page and the serial log.
const char *statusText();

}  // namespace uplink
