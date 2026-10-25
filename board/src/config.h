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

  // Three rates that have nothing to do with each other: measuring has to be
  // fast enough for alarms to react, publishing only fast enough to follow a
  // cabin, and an alarm waits for neither. See A-005.
  // TLS to the broker. Off for a broker on the bench, on for one reached
  // across the internet - where the credentials would otherwise cross marina
  // Wi-Fi in the clear.
  bool mqttTls = false;

  uint16_t sampleSecs = 10;
  uint16_t pubSecs = 300;

  // SHT31 heater. Condensation on the sensor leaves it stuck at 100 %RH long
  // after the air has dried; a short pulse drives the water off. The numbers
  // are here rather than compiled in because a sensor mounted somewhere that
  // never condenses does not need any of it - see A-008.
  bool sht31Heater = true;
  uint16_t sht31HeatAboveRh = 95;  // %RH that counts as saturated
  uint16_t sht31SoakMins = 30;     // how long it must stay there first
  uint16_t sht31HeatSecs = 10;     // how long to heat
  uint16_t sht31CoolSecs = 120;    // how long to wait before trusting it again

  // IMU. Which of the module's printed axes points forward and which points
  // up, as an axis code: 0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z. A
  // setting rather than a soldering decision, because nothing downstream can
  // tell a module turned a quarter in the horizontal from a boat that pitches
  // instead of heeling - see A-009 section 4.
  uint16_t imuForward = 0;     // +X
  uint16_t imuUp = 4;          // +Z, component side up
  uint16_t imuTauDs = 30;      // filter time constant in tenths of a second
  uint16_t imuKnockMg = 500;   // knock threshold: a jump of this many mg between samples
  uint16_t imuHoldoffSecs = 60;  // one knock reported, not forty

  // BME680. The gas heater is off unless switched on here: a hotplate at
  // 320 C serves no purpose in a battery bay, where a failing charger can vent
  // hydrogen - see A-010 section 4. The offset corrects the sensor against a
  // nearby station, in Pa, station minus board; the height above sea level is
  // the server's business, not this one's.
  bool bmeGas = false;
  int16_t bmePressureOffsetPa = 0;

  // The battery divider's factor in thousandths: battery voltage over the
  // voltage at A0. Nominally 11.000; the calibration against a multimeter
  // replaces it - see B-001 section 7.
  uint16_t batCalMilli = 11000;

  // The bilge sender: its range, the level at 20 mA, and an offset added to
  // every reading - the sensing face's height above the bilge floor, or a
  // correction of its zero. See B-003.
  uint16_t bilgeFullCm = 100;
  int16_t bilgeOffsetMm = 0;

  // Alarms. Temperatures in tenths of a degree, so they keep their decimal
  // in an integer key. Each alarm can be switched off: a boat with no fridge
  // probe, or one laid up dry, should not live with an alarm it cannot use -
  // see A-012.
  bool alarmFrost = true;
  int16_t frostC10 = 30;     // below 3.0 C at the coldest sensor
  uint16_t frostMins = 10;   // for this long, and as long again to clear

  bool alarmFridge = true;
  int16_t fridgeArmC10 = 60;    // armed once below 6.0 C ...
  uint16_t fridgeArmMins = 60;  // ... for an hour; cleared below it again
  int16_t fridgeWarmC10 = 100;  // raised above 10.0 C ...
  uint16_t fridgeWarmMins = 30; // ... for half an hour

  bool alarmHumidity = true;
  uint16_t rhWarn = 85;    // a warning at or above this %RH ...
  uint16_t rhHours = 6;    // ... for this long
  uint16_t rhClear = 80;   // cleared below this

  // Status LED. Off, the healthy state is dark instead of breathing green - a
  // cabin at night is darker than any brightness setting. Every other state
  // still shows, and a short BOOT press brings the breathing back for a few
  // seconds - see A-004 section 5.
  bool ledBreathe = true;

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
