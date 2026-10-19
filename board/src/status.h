// The onboard RGB LED as a status indicator.
//
// On a boat this is worth more than the dashboard: you walk past, look, and
// know - without a phone, without Wi-Fi, without the server being reachable.
// Which is exactly the situation where you most want to know.
//
// Everything blinks rather than sitting still, and not to save current: a
// steady LED only proves the supply is on. A moving pattern proves loop() is
// still running, and freezes the moment the firmware hangs.
//
// The one exception is a setting: the green breathing can be switched off for
// a cabin where it is too bright at night. The healthy state is then dark, and
// a short BOOT press brings the breathing back for two breaths - so dark still
// tells healthy from hung, if only when asked.

#pragma once

#include <Arduino.h>

namespace status {

void begin();
void loop();

// Alarm overrides every other state. Set by alarms.cpp while a raised alarm
// waits to be acknowledged - see A-012.
void setAlarm(bool on);
bool alarmActive();

// Feedback while the BOOT button is held, so that a long press is visibly
// building rather than a guess about how long to keep pressing.
void setButtonHeld(bool held);
void setButtonFired();

// A short BOOT press. With the breathing switched off, it shows two breaths
// of green when everything works; otherwise it changes nothing.
void showBreathing();

}  // namespace status
