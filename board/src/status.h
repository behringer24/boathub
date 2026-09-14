// The onboard RGB LED as a status indicator.
//
// On a boat this is worth more than the dashboard: you walk past, look, and
// know - without a phone, without Wi-Fi, without the server being reachable.
// Which is exactly the situation where you most want to know.
//
// Everything blinks rather than sitting still, and not to save current: a
// steady LED only proves the supply is on. A moving pattern proves loop() is
// still running, and freezes the moment the firmware hangs.

#pragma once

#include <Arduino.h>

namespace status {

void begin();
void loop();

// Alarm overrides every other state. Nothing raises one yet - the threshold
// logic is a later package - but the indicator and the acknowledgement are
// ready for it.
void setAlarm(bool on);
bool alarmActive();

// Feedback while the BOOT button is held, so that a long press is visibly
// building rather than a guess about how long to keep pressing.
void setButtonHeld(bool held);
void setButtonFired();

}  // namespace status
