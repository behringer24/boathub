#include "watch.h"

#include <esp_system.h>

#include "analog.h"
#include "barometer.h"
#include "ds18b20.h"
#include "events.h"
#include "imu.h"
#include "sht31.h"

namespace {

using events::Sensor;

// How long a sensor that was answering may go without a valid reading before
// it counts as lost. On top of each driver's own staleness, so a single
// failed read never comes close.
const uint32_t LOST_AFTER_MS = 60000;

const uint32_t CHECK_MS = 1000;

const uint8_t COUNT = (uint8_t)Sensor::Count;

struct Track {
  bool answered = false;  // gave a valid reading this boot
  bool lost = false;      // reported lost, not yet back
  uint32_t lastValid = 0;
};

Track track[COUNT];
uint32_t nextCheck = 0;

bool fitted(Sensor s) {
  switch (s) {
    case Sensor::Cabin: return sht31::present();
    case Sensor::Bay: return barometer::present();
    case Sensor::Imu: return imu::present();
    case Sensor::Engine: return ds18b20::present(ds18b20::Engine);
    case Sensor::Bilge: return ds18b20::present(ds18b20::Bilge);
    case Sensor::Fridge: return ds18b20::present(ds18b20::Fridge);
    case Sensor::Battery: return analog::present();
    case Sensor::BilgeLevel: return analog::bilgeFitted();
    default: return false;
  }
}

bool valid(Sensor s) {
  switch (s) {
    case Sensor::Cabin: return sht31::latest().valid;
    case Sensor::Bay: return barometer::latest().valid;
    case Sensor::Imu: return imu::latest().valid;
    case Sensor::Engine: return ds18b20::latest(ds18b20::Engine).valid;
    case Sensor::Bilge: return ds18b20::latest(ds18b20::Bilge).valid;
    case Sensor::Fridge: return ds18b20::latest(ds18b20::Fridge).valid;
    case Sensor::Battery: return analog::latestBattery().valid;
    case Sensor::BilgeLevel: return analog::latestBilge().valid;
    default: return false;
  }
}

// No readings by design. The SHT31's heater cycle takes over two minutes, and
// reporting it as a loss would put a false event in the record every time the
// cabin is damp.
bool excused(Sensor s) { return s == Sensor::Cabin && sht31::heating(); }

}  // namespace

namespace watch {

void begin() {
  uint16_t sensors = 0;
  for (uint8_t i = 0; i < COUNT; i++) {
    if (fitted((Sensor)i)) sensors |= (uint16_t)(1u << i);
  }
  events::boot((uint8_t)esp_reset_reason(), sensors);
}

void loop() {
  const uint32_t now = millis();
  if ((int32_t)(now - nextCheck) < 0) return;
  nextCheck = now + CHECK_MS;

  for (uint8_t i = 0; i < COUNT; i++) {
    const Sensor s = (Sensor)i;
    Track &t = track[i];

    if (valid(s) || excused(s)) {
      if (t.lost) events::sensorBack(s);
      t.lost = false;
      t.answered = true;
      t.lastValid = now;
      continue;
    }

    // A sensor that never answered this boot is not lost - it is not fitted,
    // and the boot event already says so.
    if (!t.answered || t.lost) continue;
    if ((now - t.lastValid) < LOST_AFTER_MS) continue;

    t.lost = true;
    events::sensorLost(s);
  }
}

}  // namespace watch
