// ESP32 BoatHub - BME680 reader
//
// A bench tool, not firmware. Build and flash it with
//
//   pio run -d board -e bmeread -t upload -t monitor
//
// Built from the firmware's own driver, so what is tried on the desk is what
// will run in the battery bay. It answers the bench checks in A-010 section 11:
//
//   - Is it a BME680 at 0x76, and does its calibration read?
//   - Are the values plausible? Pressure within a few hPa of a nearby
//     station's once its height is allowed for - this is station pressure,
//     and a weather report gives sea level - and temperature and humidity
//     near the SHT31's.
//   - Is the gas heater off? Every line shows ctrl_gas_0 and ctrl_gas_1 as
//     read back from the chip: heat_off set, run_gas clear.
//   - Does it resolve height? Press z with the module on the desk, lift it by
//     a metre: "ref" falls by about 0.12 hPa, and comes back when it is put
//     down again. The mean of the last eight readings is what is compared, so
//     the noise of a single one does not hide it.
//
// One measurement a second, ten times the firmware's rate. The temperature
// may read a few hundredths of a degree warmer for it.
//
// Keys: z = pressure reference here, g = gas heater on/off (off at start; on
// is for trying the gas readout on the desk, never in the battery bay).

#include <Arduino.h>

#include "bme680.h"
#include "i2cbus.h"

static const uint32_t PERIOD_MS = 1000;
static const uint8_t AVERAGE_OF = 8;

static bool found = false;
static bool gas = false;
static float lastTempC = 25.0f;

static float history[AVERAGE_OF];
static uint8_t filled = 0;
static uint8_t head = 0;
static bool haveRef = false;
static float refHpa = 0.0f;

static float average() {
  float sum = 0.0f;
  for (uint8_t i = 0; i < filled; i++) sum += history[i];
  return filled ? sum / filled : 0.0f;
}

static void remember(float hpa) {
  history[head] = hpa;
  head = (head + 1) % AVERAGE_OF;
  if (filled < AVERAGE_OF) filled++;
}

static void printGasControl() {
  uint8_t g0 = 0, g1 = 0;
  if (!bme680::readGasControl(g0, g1)) {
    Serial.print(" | gas ctrl unreadable");
    return;
  }
  // heat_off is bit 3 of ctrl_gas_0; run_gas is bit 4 of ctrl_gas_1 on the
  // BME680 and bit 5 on the BME688.
  Serial.printf(" | ctrl_gas_0 0x%02X heat_off %s | ctrl_gas_1 0x%02X run_gas %s", g0,
                (g0 & 0x08) ? "set" : "CLEAR", g1, (g1 & 0x30) ? "SET" : "clear");
}

static void measure() {
  if (!bme680::start(gas, lastTempC)) {
    Serial.println("no answer to start() - is the module still connected?");
    return;
  }

  // The firmware returns to its loop here; a bench tool can simply wait.
  const uint32_t started = millis();
  bme680::Sample s;
  bme680::Result r = bme680::Result::NotReady;
  delay(bme680::durationMs(gas));
  while (true) {
    r = bme680::collect(s, gas);
    if (r != bme680::Result::NotReady || millis() - started > 1000) break;
    delay(10);
  }
  const uint32_t took = millis() - started;

  switch (r) {
    case bme680::Result::NoAnswer:
      Serial.println("no answer to the read");
      return;
    case bme680::Result::NotReady:
      Serial.printf("status 0x%02X: no new data after %lu ms\n", s.status, (unsigned long)took);
      return;
    case bme680::Result::Skipped:
      Serial.printf("status 0x%02X: skipped values (raw P 0x%05lX T 0x%05lX H 0x%04X) - the chip "
                    "was reset\n",
                    s.status, (unsigned long)s.rawPressure, (unsigned long)s.rawTemp,
                    s.rawHumidity);
      return;
    case bme680::Result::Ok:
      break;
  }

  lastTempC = s.tempC;
  const float hpa = s.pressurePa / 100.0f;
  remember(hpa);

  Serial.printf("P %8.2f hPa (mean %8.3f", hpa, average());
  if (haveRef) Serial.printf(", ref %+6.3f", average() - refHpa);
  Serial.printf(") | T %6.2f C | RH %5.1f %% | status 0x%02X in %lu ms", s.tempC, s.rh, s.status,
                (unsigned long)took);
  if (gas) {
    if (s.gasValid) {
      Serial.printf(" | gas %.1f kOhm", s.gasKohm);
    } else {
      Serial.printf(" | gas not valid (flags 0x%02X)", s.gasStatus);
    }
  }
  printGasControl();
  Serial.println();
}

static void handleKey(int key) {
  switch (key) {
    case 'z':
      if (filled == 0) break;
      refHpa = average();
      haveRef = true;
      Serial.printf("--- reference %.3f hPa: lift the module a metre and watch ref fall by ~0.12\n",
                    refHpa);
      break;
    case 'g':
      if (bme680::isBme688()) {
        Serial.println("--- this is a BME688: the driver reads gas on a BME680 only");
        break;
      }
      gas = !gas;
      Serial.println(gas ? "--- gas heater ON, 320 C for 150 ms each reading - bench only. "
                           "Temperature reads warm and humidity dry while it runs"
                         : "--- gas heater off");
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== BME680 reader ===");
  i2cbus::begin();

  const uint8_t id = bme680::identify();
  if (id == 0) {
    Serial.printf("nothing at 0x%02X - is SDO tied to GND? Run the i2cscan environment first\n",
                  bme680::ADDRESS);
    return;
  }
  if (id != bme680::CHIP_ID) {
    Serial.printf("0x%02X answers with chip ID 0x%02X, not a BME680 (0x61) - i2cscan names it\n",
                  bme680::ADDRESS, id);
    return;
  }
  if (!bme680::begin()) {
    Serial.println("chip ID is right, but the reset or the calibration read failed");
    return;
  }
  found = true;

  Serial.printf("%s at 0x%02X, chip ID 0x%02X, calibration read\n",
                bme680::isBme688() ? "BME688" : "BME680", bme680::ADDRESS, id);
  Serial.println("forced mode: humidity x1, temperature x2, pressure x16, IIR off");
  Serial.println("Pressure is station pressure with no offset. A weather report gives sea level,");
  Serial.println("which is higher by about 0.12 hPa for every metre the sensor sits above it.");
  Serial.println("Keys: z = pressure reference here, g = gas heater on/off.");
  Serial.println();
}

void loop() {
  if (!found) {
    delay(1);
    return;
  }

  static uint32_t next = 0;
  const uint32_t now = millis();
  if ((int32_t)(now - next) >= 0) {
    next = now + PERIOD_MS;
    measure();
  }

  if (Serial.available()) handleKey(Serial.read());

  delay(1);  // hand the core back to FreeRTOS
}
