#include "button.h"

#include <Arduino.h>

#include "config.h"
#include "status.h"
#include "uplink.h"

namespace {

const uint8_t PIN = 0;  // active low

const uint32_t DEBOUNCE_MS = 30;
// Below a second is a short press. Anything between is neither, so that a
// hesitant press does nothing rather than something surprising.
const uint32_t SHORT_MAX_MS = 1000;
// Deliberately uncomfortable. Anything under a few seconds eventually happens
// by accident while feeling for the box in the dark.
const uint32_t LONG_MS = 8000;
// Show that a long press is building, or nobody knows whether to keep holding.
const uint32_t HINT_AFTER_MS = 1000;

bool stable = HIGH;
bool lastRead = HIGH;
uint32_t changedAt = 0;
uint32_t pressedAt = 0;
bool longFired = false;

void shortPress() {
  // Context decides. If it is flashing red the press means "seen"; otherwise
  // it means "send now". One idea, not two: I am here and I am responding to
  // what you are showing me.
  if (status::alarmActive()) {
    status::setAlarm(false);
    Serial.println("[button] alarm acknowledged");
    return;
  }
  Serial.println("[button] publishing now");
  uplink::publishNow();
}

void longPress() {
  Serial.println("[button] resetting the access point password to its default");
  status::setButtonFired();
  config::resetApPassword();
  delay(600);  // let the confirmation be seen and the log drain
  ESP.restart();
}

}  // namespace

namespace button {

void begin() { pinMode(PIN, INPUT_PULLUP); }

void loop() {
  const uint32_t now = millis();
  const bool raw = digitalRead(PIN);

  if (raw != lastRead) {
    lastRead = raw;
    changedAt = now;
  }
  if ((now - changedAt) < DEBOUNCE_MS || raw == stable) {
    // Still bouncing, or nothing new - but a press already counted may be
    // growing into a long one.
    if (stable == LOW && !longFired) {
      const uint32_t held = now - pressedAt;
      if (held >= LONG_MS) {
        longFired = true;
        longPress();
      } else if (held >= HINT_AFTER_MS) {
        status::setButtonHeld(true);
      }
    }
    return;
  }

  stable = raw;

  if (stable == LOW) {  // pressed
    pressedAt = now;
    longFired = false;
    return;
  }

  // Released.
  status::setButtonHeld(false);
  const uint32_t held = now - pressedAt;
  if (!longFired && held < SHORT_MAX_MS) {
    shortPress();
  }
}

}  // namespace button
