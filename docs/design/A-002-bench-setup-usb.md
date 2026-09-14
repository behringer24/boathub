# A-002 - Bench setup on USB power

| | |
|---|---|
| **Status** | Draft |
| **Stage** | 1, phase A |
| **Roadmap package** | A.1 - A.4 |
| **Created** | 2026-09-13 |
| **Last changed** | 2026-09-13 |
| **Touches hardware** | yes (breadboard only) |

> Design documents are numbered in creation order. This one is **the first thing to build** -
> see the phase order in [../ROADMAP.md](../ROADMAP.md).

## 1. Goal

Get the complete sensor set and the whole network stack working on a breadboard, powered from USB
alone. **No 12 V exists at this point** - not on the bench, not in the enclosure, nowhere.

The 12 V supply from [B-001-power-supply.md](B-001-power-supply.md) is built at the *end* of the bench
phase, once everything else is stable.

**Out of scope:** battery voltage measurement (needs a real supply, phase B), enclosure and
installation (phase C).

## 2. Why this order

The project guide sequences it this way and it is worth spelling out why:

- **Variable isolation.** If the ESP resets mid-transmission after the DIY power supply is in the
  loop, you cannot tell whether it is the converter, the wiring, or the firmware. Proving the
  firmware on a known-good USB supply first removes one whole class of suspects.
- **The power supply is the only part that can hurt you.** Everything in this phase runs at 3.3 V
  and 5 V. The fuse, TVS and DC/DC work is the part where a mistake costs a board or a finger, and
  it deserves its own undivided session.
- **It produces something usable early.** At the end of phase A the system reports temperatures
  and humidity to the server over TLS. Plugged into any USB charger, that is already a real - if
  incomplete - boat monitor.

## 3. Hardware

### Power path in this phase

```
USB-C (CH343P port)  ->  5 V  ->  AMS1117-3.3  ->  3.3 V rail
                                                      |
                                   +------------------+------------------+
                                   |                  |                  |
                              3 x DS18B20          SHT31-D         3 x ADS1115
```

Everything hangs off the DevKit's own regulator. Nothing else is needed.

### Which USB port

Use the **CH343P port** (the USB-to-serial one), not the native ESP32-S3 port. The native port
occupies GPIO19/20, and the serial monitor is the main debugging tool in this phase. See
[A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md).

### Current budget

| Consumer | Draw |
|----------|------|
| ESP32-S3, Wi-Fi TX peaks | 350-500 mA |
| DevKit overhead (AMS1117 ground current, PWR LED, CH343P, WS2812) | 10-18 mA |
| 3 x DS18B20 during conversion | ~5 mA |
| SHT31-D | ~1.5 mA |
| 3 x ADS1115 | ~0.5 mA |
| **Total peak** | **~400-525 mA** |

USB 2.0 nominally supplies 500 mA, so the peak sits right at the limit. In practice a decent port
and a short, thick cable handle it.

**If the ESP resets exactly when Wi-Fi connects, suspect the USB supply before the code.** A thin
cable or a weak hub laptop port will brown out on the transmit burst. Try a different port, a
shorter cable, or a powered hub before debugging firmware.

### Breadboard notes

- Sensor and I2C wiring is short and uncritical at this stage - a breadboard is fine.
- **Use the real 5 m probe cables for the DS18B20 pull-up test (I3).** With short jumpers the
  cable capacitance that makes 4.7 kΩ marginal simply does not exist, so a short-cable test proves
  nothing about the installed system. Try 4.7 kΩ, 3.3 kΩ and 2.2 kΩ with the full cable length.
- The SHT31 and all three ADS1115 each carry 10 kΩ I2C pull-ups, giving **2.5 kΩ effective**.
  **Do not add external pull-ups** (M1). Run the bus at 100 kHz.
- Set the ADS1115 addresses one module at a time and confirm with an I2C scanner:
  ADDR→GND = 0x48, ADDR→VDD = 0x49, ADDR→SDA = 0x4A.

## 4. Testing the ADS1115 without 12 V

The ADC still has to be proven, just not against a battery. Two levels:

### Level 1 - always possible, no bench PSU

Build a 2:1 divider from the 3.3 V rail (two equal resistors, e.g. 10 kΩ) and measure the midpoint.

- expected ~1.65 V, verified against the multimeter
- proves I2C, addressing, PGA selection and conversion
- a 10 kΩ potentiometer across the rail is even better - sweep it and check linearity end to end

### Level 2 - if an adjustable bench supply is available

The **battery divider is a separate circuit from the power supply**, and can be tested on its own
long before the protection board exists. Build just the divider on the breadboard:

```
bench PSU +  ---- 82 kOhm ---+--- 1 kOhm --- ADS1115 A0
                             |
                          10 kOhm      +-- 100 nF -- GND
                             |
                            GND
```

Sweep the PSU from 11 V to 15.5 V and confirm the ADC tracks linearly. That gets the calibration
factor established early, and the reading can then be cross-checked once the real supply is built.

> **Safety.** This is the one step in phase A with a voltage that can destroy the ESP. Build the
> divider on a **separate part of the breadboard**, apply the PSU, and **verify with a multimeter
> that the tap really sits near 1.5 V before connecting anything to the ADS1115.** A slipped
> jumper putting 15 V on a GPIO ends the evening. Current-limit the PSU if it can.

If no bench supply is available, skip level 2 - it belongs to phase B anyway.

## 5. What the telemetry looks like in this phase

`battery_v` simply does not exist yet. Omit the field rather than sending a placeholder - a zero or
a null would read as a flat battery on the server side.

```json
{
  "ts": "2026-09-13T08:15:00Z",
  "cabin_temp_c": 8.4,
  "cabin_rh": 72.1,
  "engine_temp_c": 7.9,
  "bilge_temp_c": 6.1,
  "fridge_temp_c": 5.2,
  "seatalk_online": false
}
```

The server side must therefore tolerate absent fields from the start. That is worth getting right
now rather than retrofitting - stages 2 and 2.5 add fields the same way.

## 6. Failure modes to exercise deliberately

The watchdog and fault decoupling (A.9) are much easier to test on the bench than in the boat.
Pull each of these on purpose and confirm the rest keeps running:

| Provoke | Expected |
|---------|----------|
| Unplug one DS18B20 mid-run | that field drops out, the other two keep reporting, no reset |
| Short an I2C line briefly | bus recovers or is re-initialised; no permanent hang |
| Wrong Wi-Fi password | sensors keep reading locally, `BOOT-NETZ` stays up, reconnect retried |
| Server unreachable | telemetry queues or is dropped cleanly, no reset loop |
| Pull power mid-write | comes back up cleanly |

A broken sensor must never take the system down - that is a guardrail, not a nice-to-have.

## 7. Test

- [ ] Blink and serial output over the CH343P port
- [ ] Carrier terminal order verified against [A-001](A-001-devkit-and-carrier.md) section 3
- [ ] Each DS18B20 detected individually, family code 0x28, **CRC verified on every read** (M7)
- [ ] DS18B20 wire colours metered out before connecting - red/black/yellow is common, not universal
- [ ] Pull-up value chosen using the **full 5 m cables**
- [ ] SHT31 responds at 0x44, plausible temperature and humidity
- [ ] All three ADS1115 on 0x48 / 0x49 / 0x4A via I2C scanner
- [ ] ADS1115 reading matches the multimeter on a known divider
- [ ] PGA fixed at the value that will be used in service (M2)
- [ ] `BOOT-NETZ` appears, a phone connects and reaches the local web UI
- [ ] Station connects to home Wi-Fi, survives a router reboot
- [ ] Server shows heartbeat and telemetry
- [ ] Last will fires when the ESP is unplugged
- [ ] Every failure mode in section 6 exercised
- [ ] Runs unattended for 24 h without a reset

## 8. Exit criteria into phase B

Do not start the power supply until all of the above pass **and** the system has run 24 hours on
USB without intervention. The point of this phase is to be able to say, later, "the firmware is
not the problem".

### The changeover itself

When phase B is built, the transition needs care:

1. Flash the final firmware **over USB, with no external 5 V connected**.
2. Disconnect USB.
3. Only then connect the 12 V side.

**Never both at once.** USB, the 5 V pin and 3V3 are alternative supply paths, not parallel ones.
This is the single rule most likely to be broken in a moment of impatience, and it is the one that
costs a board.

## 9. Open points

| Point | Decide by | Who |
|-------|-----------|-----|
| Whether an adjustable bench PSU is available for the level 2 divider test | before A.4 | Andreas |
| MQTT broker and TLS certificate set up on the Docker host | before A.7 | Andreas |

**Resolved 2026-09-14 - PlatformIO, not the Arduino IDE.** The firmware project lives in `board/`
with `framework = arduino` on platform 7.1.3 (Arduino core 2.0.17). The deciding argument is that
the N16R8 needs explicit flash, PSRAM and partition overrides, and PlatformIO keeps those in a
checked-in, reviewable `platformio.ini` instead of in IDE menu settings.

## 10. References

- [000-design-review.md](000-design-review.md) - findings I3, M1, M2, M7
- [B-001-power-supply.md](B-001-power-supply.md) - what gets built after this phase
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - ports, pin mapping, terminals to avoid
