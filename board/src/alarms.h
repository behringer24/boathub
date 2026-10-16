// Frost, a warm fridge and a damp cabin: judged on the board, shown on the
// LED, sent as events.
//
// Evaluated once per measurement interval on each sensor's latest reading.
// Nothing raises on a single reading - a condition has to hold for its time,
// and the reverse has to hold before it clears.
// See docs/design/A-012-alarm-and-threshold-logic.md.

#pragma once

#include <Arduino.h>

namespace alarms {

void begin();
void loop();

// The BOOT button: everything raised now counts as seen. The LED goes back to
// its normal state and an event says so; the alarms stay raised until their
// conditions clear. Only ever from the box - nothing from the server reaches
// this.
void acknowledge();

// The alarms raised now, e.g. "frost, humidity", or "none". For the status
// line, the portal and /status.
String raised();

// The fridge alarm's state: "off", "disarmed", "armed" or "raised".
const char *fridgeState();

}  // namespace alarms
