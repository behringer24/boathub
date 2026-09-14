// ESP32 BoatHub - firmware
//
// At this stage: bring the board onto the network, let it be configured over
// its own access point, and publish a heartbeat to the MQTT broker every few
// seconds. Sensors are added to the same payload as they come up.
//
// docs/design/A-004-wifi-and-configuration-portal.md
// docs/design/A-005-server-uplink.md

#include <Arduino.h>

#include "config.h"
#include "net.h"
#include "portal.h"
#include "uplink.h"

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== ESP32 BoatHub ===");

  config::begin();
  Serial.printf("boat id: %s\n", config::get().boatId.c_str());

  net::begin();
  portal::begin();
  uplink::begin();
}

void loop() {
  // Three independent state machines, none of which blocks. A broker that is
  // down must not stall the portal, and a marina outage must not stall either
  // of them - the same rule the sensors will follow.
  net::loop();
  portal::loop();
  uplink::loop();

  static uint32_t lastReport = 0;
  const uint32_t now = millis();
  if (now - lastReport >= 30000) {
    lastReport = now;
    Serial.printf("[status] wifi %s %s | broker %s | heap %lu\n", net::stateName(),
                  net::stationIp().c_str(), uplink::statusText(),
                  (unsigned long)ESP.getFreeHeap());
  }

  delay(1);  // hand the core back to FreeRTOS
}
