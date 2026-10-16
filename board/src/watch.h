// Which sensors are answering: the boot event, and an event when a sensor that
// was answering stops, and when it comes back.
//
// Every driver already knows when its reading has gone stale; this only
// watches the drivers' latest() from outside, so not one of them has to know
// that events exist.
// See docs/design/A-011-events.md section 3.

#pragma once

#include <Arduino.h>

namespace watch {

// The boot event, with the sensors that answered at start. Called once every
// driver's begin() has run.
void begin();

void loop();

}  // namespace watch
