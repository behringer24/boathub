// ESP32 BoatHub - firmware
//
// At this stage: bring the board onto the network, let it be configured over
// its own access point, and publish a heartbeat to the MQTT broker every few
// seconds. Sensors are added to the same payload as they come up.
//
// docs/design/A-004-wifi-and-configuration-portal.md
// docs/design/A-005-server-uplink.md

#include <Arduino.h>

#include "button.h"
#include "config.h"
#include "ds18b20.h"
#include "net.h"
#include "portal.h"
#include "sht31.h"
#include "telemetry.h"
#include "status.h"
#include "uplink.h"

namespace {

const uint32_t EXPECTED_FLASH = 16u * 1024u * 1024u;  // N16
// ESP.getPsramSize() reports the SPIRAM heap total, which sits a few KB below
// the raw chip size because of allocator overhead. Check a floor.
const uint32_t MIN_PSRAM = 8u * 1024u * 1024u - 64u * 1024u;  // R8

// Checked on every boot rather than only when somebody remembers to flash the
// bringup firmware. A check you have to invoke is a check that does not run -
// and a board whose PSRAM was lost to a configuration change starts perfectly
// happily, then fails years later in something that needed it.
void reportHardware() {
  const uint32_t flash = ESP.getFlashChipSize();
  const uint32_t psram = ESP.getPsramSize();

  // A reported size is not proof. Write a megabyte and read it back.
  bool psramWorks = false;
  if (psram >= MIN_PSRAM) {
    const size_t block = 1024 * 1024;
    void *buf = ps_malloc(block);
    if (buf != NULL) {
      memset(buf, 0xA5, block);
      psramWorks = ((uint8_t *)buf)[block - 1] == 0xA5;
      free(buf);
    }
  }

  Serial.printf("chip:  %s rev %d, %d cores, %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), (unsigned long)getCpuFrequencyMhz());
  // Rounded, not truncated. The usable PSRAM is a few KB short of 8 MB because
  // of allocator overhead, and truncating would print "7 MB" on a healthy
  // board - which sends somebody hunting for a fault that is not there.
  const uint32_t half = 1u << 19;
  Serial.printf("flash: %lu MB%s\n", (unsigned long)((flash + half) >> 20),
                flash == EXPECTED_FLASH ? "" : "   *** expected 16 MB ***");
  Serial.printf("psram: %lu MB, %s\n", (unsigned long)((psram + half) >> 20),
                psramWorks ? "write/read ok" : "*** NOT WORKING ***");

  if (flash != EXPECTED_FLASH || !psramWorks) {
    Serial.println("*** this is not the expected N16R8 configuration ***");
    Serial.println("*** flash the bringup environment for the full check ***");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== ESP32 BoatHub ===");
  reportHardware();

  config::begin();
  Serial.printf("boat id: %s\n", config::get().boatId.c_str());

  net::begin();
  sht31::begin();
  ds18b20::begin();
  telemetry::begin();
  portal::begin();
  uplink::begin();
  status::begin();
  button::begin();
}

void loop() {
  // Independent state machines, none of which blocks. A broker that is down
  // must not stall the portal, and a marina outage must not stall either of
  // them - the same rule the sensors will follow.
  net::loop();
  sht31::loop();
  ds18b20::loop();
  telemetry::loop();
  portal::loop();
  uplink::loop();
  status::loop();
  button::loop();

  static uint32_t lastReport = 0;
  const uint32_t now = millis();
  if (now - lastReport >= 30000) {
    lastReport = now;
    const sht31::Reading climate = sht31::latest();
    char cabin[32] = "cabin --";
    if (climate.valid) {
      snprintf(cabin, sizeof(cabin), "cabin %.1fC %.0f%%", climate.tempC, climate.rh);
    }

    // Which probes are alive, at a glance. A probe that was never found and a
    // probe that enumerated and has since gone quiet are different faults, so
    // they read differently: "-" was never there, "?" is there and silent.
    char probes[48];
    int at = snprintf(probes, sizeof(probes), "probes");
    for (uint8_t i = 0; i < ds18b20::ProbeCount && at > 0 && (size_t)at < sizeof(probes); i++) {
      const ds18b20::Probe p = (ds18b20::Probe)i;
      const char initial = ds18b20::name(p)[0] - ('a' - 'A');
      const ds18b20::Reading probe = ds18b20::latest(p);
      const int room = sizeof(probes) - at;
      at += probe.valid ? snprintf(probes + at, room, " %c%.1f", initial, probe.tempC)
                        : snprintf(probes + at, room, " %c%s", initial,
                                   ds18b20::present(p) ? "?" : "-");
    }

    Serial.printf("[status] wifi %s %s | broker %s | %s (%s) | %s | heap %lu\n", net::stateName(),
                  net::stationIp().c_str(), uplink::statusText(), cabin, sht31::statusText(),
                  probes, (unsigned long)ESP.getFreeHeap());
  }

  delay(1);  // hand the core back to FreeRTOS
}
