#include "status.h"

#include "net.h"
#include "uplink.h"

namespace {

// Low on purpose. The WS2812 at full scale is uncomfortable to look at, and
// nothing here needs to be seen from across the cabin.
const uint8_t LEVEL = 20;

// 50 frames a second is more than an eye resolves, and the WS2812 is a timed
// protocol that blocks for about 30 us per update - no reason to run it faster.
const uint32_t FRAME_MS = 20;

bool alarmActive = false;  // not "alarm": POSIX declares alarm() in unistd.h
uint32_t lastFrame = 0;

// Only transmit when something actually changed.
//
// The WS2812 is pure timing: neopixelWrite() bit-bangs through the RMT, and
// under Wi-Fi load a transmission can be stretched enough to corrupt a bit,
// which shows as a brief wrong value. Repainting an unchanged colour 50 times
// a second is 50 chances a second to glitch, for no benefit - and during the
// dark phase of a fade a stray flash is exactly what you notice.
void write(uint8_t r, uint8_t g, uint8_t b) {
  static uint8_t lastR = 255, lastG = 255, lastB = 255;
  if (r == lastR && g == lastG && b == lastB) return;
  lastR = r;
  lastG = g;
  lastB = b;
#ifdef RGB_BUILTIN
  neopixelWrite(RGB_BUILTIN, r, g, b);
#endif
}

// A single short pulse per period.
bool blink(uint32_t now, uint32_t period, uint32_t on) {
  return (now % period) < on;
}

// One second dark, one second fading up, one second fading down.
//
// Squared on purpose: perceived brightness is roughly the square root of
// emitted light, so a linear ramp appears to rush the bright end and crawl at
// the dark one. Squaring the ramp makes it look even.
uint8_t breathe(uint32_t now) {
  const uint32_t phase = now % 3000;
  if (phase < 1000) return 0;  // the pause
  const uint32_t t = (phase < 2000) ? (phase - 1000)   // 0..999, rising
                                    : (2999 - phase);  // 999..0, falling
  return (uint8_t)((t * t * LEVEL) / (1000UL * 1000UL));
}

// Two short pulses, then a pause - readable at a glance and clearly different
// from a single blink.
bool doubleBlink(uint32_t now, uint32_t period) {
  const uint32_t phase = now % period;
  return phase < 90 || (phase >= 220 && phase < 310);
}

}  // namespace

namespace status {

void begin() {
  write(0, 0, 0);
#ifndef RGB_BUILTIN
  Serial.println("[status] no RGB LED for this board definition");
#endif
}

void setAlarm(bool on) { alarmActive = on; }

void loop() {
  const uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  // Alarm first: it has to be visible regardless of what the network is doing.
  // Twice a second, which reads as urgent next to everything else here.
  if (alarmActive) {
    write(blink(now, 500, 250) ? LEVEL : 0, 0, 0);
    return;
  }

  switch (net::state()) {
    case net::State::Portal:
      // Nothing configured. Slow blue: waiting for somebody, not broken.
      write(0, 0, blink(now, 2000, 400) ? LEVEL : 0);
      return;

    case net::State::Connecting:
      // Credentials known, no association yet. Yellow, once a second.
      write(blink(now, 1000, 150) ? LEVEL : 0, blink(now, 1000, 150) ? LEVEL / 2 : 0, 0);
      return;

    case net::State::Online:
      if (!uplink::connected()) {
        // Wi-Fi is up, the broker is not answering. Same yellow, doubled, so
        // the two "not finished yet" states are distinguishable in the dark.
        const bool on = doubleBlink(now, 1500);
        write(on ? LEVEL : 0, on ? LEVEL / 2 : 0, 0);
        return;
      }
      // Everything works. Green breathing - calm rather than urgent, and
      // still moving, which is what proves the loop is alive rather than
      // merely powered.
      write(0, breathe(now), 0);
      return;
  }
}

}  // namespace status
