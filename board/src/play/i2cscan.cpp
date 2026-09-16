// ESP32 BoatHub - I2C bus scanner
//
// A bench tool, not firmware. Build and flash it with
//
//   pio run -d board -e i2cscan -t upload -t monitor
//
// It answers three questions in this order, and the order matters:
//
//   1. is anything connected to the bus at all?
//   2. is the bus electrically healthy?
//   3. which devices answer, and at which addresses?
//
// Most "the sensor does not work" sessions are really question 1 or 2, and a
// plain scanner that only answers 3 reports "nothing found" for all three -
// which sends you looking at the sensor when the fault is a wire.
//
// The permanent version of this lives in the configuration portal, not here.
// A scan blocks the bus for some 15 ms, so it is an action you trigger, never
// something the main loop does.

#include <Arduino.h>
#include <Wire.h>

// From the pin plan. Any GPIO can carry I2C on an ESP32 - the peripheral is
// routed through the GPIO matrix rather than being wired to fixed pins as on
// an AVR - which is why the pins have to be named explicitly at Wire.begin().
static const int PIN_SDA = 8;
static const int PIN_SCL = 9;

// 100 kHz, not 400. Four modules on this bus each carry a 10 k pull-up, which
// in parallel is 2.5 k; at 400 kHz the rise time over that becomes marginal.
// Nothing here is fast enough to care.
static const uint32_t I2C_HZ = 100000;

static const uint32_t RESCAN_MS = 2000;

// Addresses this project expects. Turning a hex number into a name is the
// difference between "0x49 answered" and "the second ADS1115 is alive".
struct Known {
  uint8_t addr;
  const char *what;
};

static const Known KNOWN[] = {
  {0x44, "SHT3x (ADDR low / open)"},
  {0x45, "SHT3x (ADDR high)"},
  {0x48, "ADS1115 (ADDR to GND)"},
  {0x49, "ADS1115 (ADDR to VDD)"},
  {0x4A, "ADS1115 (ADDR to SDA)"},
  {0x4B, "ADS1115 (ADDR to SCL)"},
  {0x68, "DS3231 real-time clock"},
  {0x76, "BME280 / BMP280 - not part of this project"},
  {0x77, "BME280 / BMP280 - not part of this project"},
};

static const char *describe(uint8_t addr) {
  for (const Known &k : KNOWN) {
    if (k.addr == addr) return k.what;
  }
  return "unknown device";
}

// --- question 1 and 2: is there a bus before we talk to it? -----------------

// Both lines idle high, held there by the pull-ups on the sensor breakouts.
// Reading them as plain inputs before Wire.begin() distinguishes three states
// that a scan alone cannot tell apart.
//
// The trick is the internal pull-down: at ~45 k it loses against an external
// 10 k pull-up, so a line that still reads high is genuinely being pulled up
// by something out there. A floating, unconnected pin reads low instead.
static bool checkLine(const char *name, int pin) {
  pinMode(pin, INPUT_PULLDOWN);
  delayMicroseconds(50);
  const bool pulledUp = digitalRead(pin) == HIGH;

  pinMode(pin, INPUT);
  delayMicroseconds(50);
  const bool idleHigh = digitalRead(pin) == HIGH;

  if (pulledUp && idleHigh) {
    Serial.printf("  %s  ok - pulled up by the breakout\n", name);
    return true;
  }
  if (!pulledUp && !idleHigh) {
    Serial.printf("  %s  NO PULL-UP - nothing connected, or VIN has no power\n", name);
    return false;
  }
  // Pull-up present but the line will not come up: something is holding it.
  Serial.printf("  %s  STUCK LOW - shorted to GND, or a device is hanging on the bus\n", name);
  return false;
}

static bool checkBus() {
  Serial.println("bus check:");
  const bool sda = checkLine("SDA", PIN_SDA);
  const bool scl = checkLine("SCL", PIN_SCL);

  if (!sda || !scl) {
    Serial.println("  -> scanning anyway, but expect nothing to answer");
    return false;
  }
  return true;
}

// --- question 3: who answers? -----------------------------------------------

// There is no "list yourself" command in I2C. A scan is 112 separate
// transactions: address the device, send no data, and watch the ninth clock
// pulse. A device that recognises its own address pulls SDA low there - that
// acknowledgement is the entire answer.
//
// 0x00-0x07 and 0x78-0x7F are reserved by the I2C specification (general call,
// 10-bit addressing and so on) and are deliberately not probed: some devices
// react badly to being addressed there.
static uint8_t scan(uint8_t *found, uint8_t maxFound) {
  uint8_t count = 0;
  uint8_t busErrors = 0;

  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    const uint8_t err = Wire.endTransmission();

    switch (err) {
      case 0:  // acknowledged
        if (count < maxFound) found[count] = addr;
        count++;
        break;
      case 2:  // no acknowledgement - the normal answer for an empty address
        break;
      default:
        // 4 = other error, 5 = timeout. These say the *bus* is in trouble,
        // not that this address is empty, so they are worth counting: 112 of
        // them means the wiring, not the devices.
        busErrors++;
        break;
    }
  }

  if (busErrors > 0) {
    Serial.printf("  %u addresses returned a bus error, not a clean NACK\n", busErrors);
  }
  return count;
}

// --- Arduino entry points ---------------------------------------------------

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== I2C scanner ===");
  Serial.printf("SDA GPIO%d, SCL GPIO%d, %lu kHz\n", PIN_SDA, PIN_SCL,
                (unsigned long)(I2C_HZ / 1000));
  Serial.println();

  checkBus();

  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  Serial.println();
  Serial.println("scanning every 2 s - plug a module in and watch it appear");
}

void loop() {
  static uint32_t nextScan = 0;
  static uint8_t lastFound[16];
  static uint8_t lastCount = 0xFF;  // impossible, so the first scan always prints

  const uint32_t now = millis();
  if ((int32_t)(now - nextScan) < 0) {
    delay(1);
    return;
  }
  nextScan = now + RESCAN_MS;

  uint8_t found[16];
  const uint32_t started = millis();
  const uint8_t count = scan(found, sizeof(found));
  const uint32_t took = millis() - started;

  // Print only when the picture changes. A scanner that repeats itself every
  // two seconds is unreadable, and the thing you actually want to see is the
  // moment a device appears or drops off.
  bool changed = (count != lastCount);
  if (!changed) {
    for (uint8_t i = 0; i < count && i < sizeof(found); i++) {
      if (found[i] != lastFound[i]) {
        changed = true;
        break;
      }
    }
  }
  if (!changed) {
    delay(1);
    return;
  }

  Serial.println();
  Serial.printf("[%lus] %u device%s, scan took %lu ms\n", (unsigned long)(now / 1000), count,
                count == 1 ? "" : "s", (unsigned long)took);

  if (count == 0) {
    Serial.println("  nothing answered - re-run the bus check above before suspecting a sensor");
  }
  for (uint8_t i = 0; i < count && i < sizeof(found); i++) {
    Serial.printf("  0x%02X  %s\n", found[i], describe(found[i]));
  }

  lastCount = count;
  memcpy(lastFound, found, sizeof(found));

  delay(1);  // hand the core back to FreeRTOS
}
