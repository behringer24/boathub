// ESP32 BoatHub - LED playground
//
// A sandbox for learning the Arduino/ESP32 model. Build and flash it with
//
//   pio run -d board -e play -t upload
//
// It needs no parts beyond the DevKit itself: the WS2812 on GPIO48 and the
// onboard BOOT button on GPIO0. Press BOOT to step through the patterns.
//
// The one habit worth carrying into the real firmware: nothing in loop() may
// block. Every pattern here is a pure function of millis() - the code never
// waits for a pattern to finish. Once sensors, Wi-Fi and the watchdog share
// this loop, a single delay() in the wrong place stalls all of them.

#include <Arduino.h>

// GPIO0 also selects the boot mode while the chip is resetting, which is why
// A-001 forbids the IO0 screw terminal. Reading the onboard button *after*
// boot is exactly what it is there for, and is safe.
static const uint8_t PIN_BUTTON = 0;  // active low

// RGB_BUILTIN is not a GPIO number. The core defines it as
// SOC_GPIO_PIN_COUNT + PIN_NEOPIXEL so it can never collide with a real pin;
// neopixelWrite() subtracts the offset again. The physical pin is GPIO48.

// Keep this low. The WS2812 at full scale is painful to look at on a bench.
static const uint8_t LEVEL = 24;

enum Mode : uint8_t {
  MODE_OFF, MODE_BLINK, MODE_BREATHE, MODE_RAINBOW, MODE_HEARTBEAT, MODE_COUNT
};

static const char *MODE_NAMES[MODE_COUNT] = {
  "off", "blink", "breathe", "rainbow", "heartbeat"
};

static Mode mode = MODE_BLINK;

// --- patterns: each one derives its colour from "now", nothing is stateful --

static void renderOff(uint32_t) {
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
}

static void renderBlink(uint32_t now) {
  // On for the first 100 ms of every second.
  const bool on = (now % 1000) < 100;
  neopixelWrite(RGB_BUILTIN, 0, on ? LEVEL : 0, 0);
}

static void renderBreathe(uint32_t now) {
  // Triangle wave over 2 s, squared so the ramp looks linear to the eye -
  // perceived brightness is roughly the square root of emitted light.
  const uint32_t phase = now % 2000;
  const uint32_t up = (phase < 1000) ? phase : (2000 - phase);  // 0..1000
  const uint32_t v = (up * up * LEVEL) / (1000UL * 1000UL);
  neopixelWrite(RGB_BUILTIN, 0, (uint8_t)v, 0);
}

static void renderRainbow(uint32_t now) {
  const uint16_t hue = (now / 8) % 360;  // a full turn every ~2.9 s
  const uint8_t f = (uint8_t)(((hue % 60) * LEVEL) / 60);
  const uint8_t q = LEVEL - f;
  uint8_t r = 0, g = 0, b = 0;
  switch (hue / 60) {
    case 0:  r = LEVEL; g = f;     b = 0;     break;
    case 1:  r = q;     g = LEVEL; b = 0;     break;
    case 2:  r = 0;     g = LEVEL; b = f;     break;
    case 3:  r = 0;     g = q;     b = LEVEL; break;
    case 4:  r = f;     g = 0;     b = LEVEL; break;
    default: r = LEVEL; g = 0;     b = q;     break;
  }
  neopixelWrite(RGB_BUILTIN, r, g, b);
}

static void renderHeartbeat(uint32_t now) {
  // Two short pulses, then a long pause - readable across a cabin.
  const uint32_t phase = now % 1500;
  const bool on = (phase < 90) || (phase >= 220 && phase < 310);
  neopixelWrite(RGB_BUILTIN, on ? LEVEL : 0, 0, 0);
}

// --- input ------------------------------------------------------------------

static void pollButton(uint32_t now) {
  static bool stable = HIGH;      // released; the pull-up holds the pin high
  static bool lastRead = HIGH;
  static uint32_t changedAt = 0;

  const bool raw = digitalRead(PIN_BUTTON);
  if (raw != lastRead) {
    lastRead = raw;
    changedAt = now;
  }

  // A mechanical contact bounces for a few milliseconds. Without this filter
  // one press would be counted several times. 30 ms of quiet makes it stick.
  if ((now - changedAt) >= 30 && raw != stable) {
    stable = raw;
    if (stable == LOW) {  // falling edge: just pressed
      mode = (Mode)((mode + 1) % MODE_COUNT);
      Serial.printf("mode -> %s\n", MODE_NAMES[mode]);
    }
  }
}

// --- Arduino entry points ---------------------------------------------------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.println();
  Serial.println("=== LED playground ===");
  Serial.println("Press the BOOT button to step through the patterns.");
  Serial.printf("starting in mode: %s\n", MODE_NAMES[mode]);
}

void loop() {
  const uint32_t now = millis();
  static uint32_t lastFrame = 0;
  static uint32_t lastReport = 0;
  static uint32_t iterations = 0;

  iterations++;
  pollButton(now);

  // Rate-limit the output. The WS2812 is a timed serial protocol, not a pin
  // you set: every update blocks for about 30 us. 50 frames per second is
  // already more than an eye resolves.
  if (now - lastFrame >= 20) {
    lastFrame = now;
    switch (mode) {
      case MODE_BLINK:     renderBlink(now);     break;
      case MODE_BREATHE:   renderBreathe(now);   break;
      case MODE_RAINBOW:   renderRainbow(now);   break;
      case MODE_HEARTBEAT: renderHeartbeat(now); break;
      default:             renderOff(now);       break;
    }
  }

  if (now - lastReport >= 5000) {
    Serial.printf("%-9s | %lu loops/s | uptime %lus | heap %lu\n",
                  MODE_NAMES[mode], (unsigned long)(iterations / 5),
                  (unsigned long)(now / 1000), (unsigned long)ESP.getFreeHeap());
    iterations = 0;
    lastReport = now;
  }

  //Serial.printf("heartbeat %lu  uptime %lus  heap %lu\n", (unsigned long)iterations,
  //              (unsigned long)(millis() / 1000), (unsigned long)ESP.getFreeHeap());

  // Hands the CPU back to FreeRTOS for a millisecond. loop() is an ordinary
  // FreeRTOS task at priority 1, and the idle task below it only runs when
  // nothing else wants the core. On this build only CPU0's idle task is
  // watched (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0) and loop() lives on
  // CPU1, so a tight loop would survive here - but it would starve anything
  // that later shares the core, so yield anyway.
  delay(1);
}
