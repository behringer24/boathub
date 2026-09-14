// The onboard BOOT button.
//
// GPIO0 also selects the boot mode while the chip is resetting, which is why
// the carrier's IO0 terminal is off limits and why "hold it during power-up"
// is a gesture that already means something else. Reading it after boot is
// exactly what it is there for.
//
// See docs/design/A-004-wifi-and-configuration-portal.md.

#pragma once

namespace button {

void begin();
void loop();

}  // namespace button
