// ESP32 BoatHub - ADS1115 reader
//
// A bench meter, not firmware. Build and flash it with
//
//   pio run -d board -e adsread -t upload -t monitor
//
// It finds every ADS1115 on the bus and prints all four channels in volts,
// twice a second, with a bar so a turning potentiometer is obvious at a
// glance.
//
// Two jobs, months apart. Now: prove the converter actually converts, which a
// bus scan cannot tell you - an address that answers is not a working ADC.
// Later, in phase B: watch the raw voltage at the battery divider while
// comparing it against a multimeter, before trusting the firmware's
// calibration factor.

#include <Adafruit_ADS1X15.h>
#include <Arduino.h>
#include <Wire.h>

static const int PIN_SDA = 8;
static const int PIN_SCL = 9;
static const uint32_t I2C_HZ = 100000;

// The four addresses the ADDR pin can select.
static const uint8_t ADDRESSES[] = {0x48, 0x49, 0x4A, 0x4B};
static const char *ADDR_TIED_TO[] = {"GND", "VDD", "SDA", "SCL"};

// +-4.096 V, so a potentiometer across the 3.3 V rail stays on scale. The
// battery divider in phase B will use +-2.048 V instead, because 14.4 V / 9.2
// is 1.57 V and the smaller range gives twice the resolution.
//
// The range does *not* raise what the input may see: VDD + 0.3 V is the limit
// whatever the gain says. A potentiometer between 3.3 V and GND cannot exceed
// it, which is what makes this a safe way to sweep the whole scale.
static const adsGain_t GAIN = GAIN_ONE;
static const float FULL_SCALE_V = 4.096f;

static Adafruit_ADS1115 ads[4];
static bool found[4] = {false, false, false, false};
static uint8_t foundCount = 0;

// A bar is worth more than the number here: turning the potentiometer should
// be visibly continuous, and a jump or a dead patch shows up immediately.
static void bar(float volts, char *out, size_t len) {
  const size_t width = len - 1;
  float frac = volts / 3.3f;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;

  const size_t filled = (size_t)(frac * width);
  for (size_t i = 0; i < width; i++) out[i] = i < filled ? '#' : '.';
  out[width] = '\0';
}

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== ADS1115 reader ===");
  Serial.printf("range +-%.3f V, so 0 to 3.3 V fits on scale\n", FULL_SCALE_V);
  Serial.println();

  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);

  for (uint8_t i = 0; i < 4; i++) {
    if (!ads[i].begin(ADDRESSES[i], &Wire)) continue;
    ads[i].setGain(GAIN);
    found[i] = true;
    foundCount++;
    Serial.printf("0x%02X  found   (ADDR tied to %s)\n", ADDRESSES[i], ADDR_TIED_TO[i]);
  }

  if (foundCount == 0) {
    Serial.println("no ADS1115 answered - run the i2cscan environment first");
    return;
  }

  Serial.println();
  Serial.println("A potentiometer: outer pins to 3.3 V and GND, wiper to A0.");
  Serial.println("A floating input reads noise, not zero - that is not a fault.");
  Serial.println();
}

void loop() {
  static uint32_t next = 0;
  const uint32_t now = millis();

  if ((int32_t)(now - next) < 0) {
    delay(1);
    return;
  }
  next = now + 500;

  if (foundCount == 0) {
    delay(1);
    return;
  }

  for (uint8_t i = 0; i < 4; i++) {
    if (!found[i]) continue;

    for (uint8_t ch = 0; ch < 4; ch++) {
      // This blocks for about 8 ms at the default 128 SPS: the library starts
      // a conversion and waits for it. Acceptable for a bench tool with
      // nothing else to do. The firmware cannot work this way - four channels
      // would stall the loop for 32 ms every round - so phase B starts the
      // conversion and collects it on a later pass, as the SHT31 already does.
      const int16_t raw = ads[i].readADC_SingleEnded(ch);
      const float volts = ads[i].computeVolts(raw);

      char b[33];
      bar(volts, b, sizeof(b));
      Serial.printf("0x%02X A%u  %7.4f V  %6d  [%s]\n", ADDRESSES[i], ch, volts, raw, b);
    }
    if (foundCount > 1) Serial.println();
  }
  Serial.println("---");

  delay(1);  // hand the core back to FreeRTOS
}
