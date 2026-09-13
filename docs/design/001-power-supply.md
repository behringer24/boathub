# 001 - Power supply and battery measurement

| | |
|---|---|
| **Status** | Draft |
| **Stage** | 1 |
| **Roadmap package** | 1.5, 1.6 |
| **Created** | 2026-09-13 |
| **Last changed** | 2026-09-13 |
| **Touches hardware** | yes |

## 1. Goal

Turn the 12 V house supply into a protected, stable 5 V feed for the ESP32-S3, and measure the
house battery voltage accurately enough to detect a shore power failure.

Incorporates findings B2 (TVS ahead of the Schottky), B3 (battery tap ahead of the Schottky) and
B1a (what the voltage means under a charger) from [000-design-review.md](000-design-review.md).

**Out of scope:** the 4-20 mA bilge loop supply (its own document), the 3.3 V sensor rail (taken
from the DevKit's onboard regulator, which has ample headroom for the <20 mA the sensors draw).

## 2. Starting point

**The boat is permanently on shore power while unattended** (answer to open question 1). That
settles the power concept:

- continuous operation, no duty cycling
- a standard DC/DC module is fine; no low-quiescent-current part needed
- `BOOT-NETZ` and the immediate bilge alarm stay permanently available
- **the low-voltage cutoff is still required** - as a backstop for the case shore power fails and
  stays failed, not as a normal operating mode

## 3. Hardware

### Parts

| Part | Qty | Purpose | Note |
|------|-----|---------|------|
| Blade fuse holder + 2 A fuse | 1 | cable protection, close to the source | protects the **cable**, not the load |
| TVS 1.5KE20A | 1 | transient clamp | unidirectional, 17.1 V standoff, clamps 27.7 V at 54 A |
| 1N5822 | 1 | reverse polarity | 3 A / 40 V Schottky |
| DC/DC 9-36 V to 5 V, min. 3 A | 1 | ESP supply | wide input covers 11-15 V comfortably |
| 100 nF / 50 V | 2 | HF bypass, input and output | |
| 100 µF / 35 V, **105 °C** | 1 | bulk, DC/DC input | 105 °C per finding I7 |
| 470 µF / 16 V, **105 °C** | 1 | bulk, 5 V output | |
| 82 kΩ 0.1 % | 1 | divider, top leg | |
| 10 kΩ 0.1 % | 1 | divider, bottom leg | |
| 1 kΩ | 1 | series into ADS1115 A0 | **this is what makes reverse polarity safe - do not omit** |
| 100 nF / 50 V | 1 | at A0 to GND | also feeds the switched-capacitor input |

### Circuit

```
12 V house supply +
      |
    2 A fuse
      |
      +---- 1.5KE20A --------- GND      <- TVS first (B2)
      +---- 100 nF ----------- GND
      |
      +---- 82 kOhm ---+                <- battery tap, ahead of the diode (B3)
      |                |
      |                +--- 1 kOhm ---- ADS1115 A0
      |                |
      |             10 kOhm         +-- 100 nF --- GND
      |                |            |
      |               GND          A0
      |
    1N5822                              <- then reverse polarity
      |
      +---- 100 uF ----------- GND
      |
    DC/DC 12 V -> 5 V
      |
      +---- 470 uF ----------- GND
      +---- 100 nF ----------- GND
      +---- ESP32 5V/VIN
                               GND common, star point in the box
```

### Why this order

| Element | Reason |
|---------|--------|
| Fuse first | Everything downstream, the TVS included, is protected by it. A sustained overvoltage blows the fuse rather than cooking the TVS. |
| TVS second | A surge is clamped **before** it reaches the 3 A Schottky. In the guide's original order the diode sat in the surge path and was the weakest link (B2). |
| Battery tap third | Measuring ahead of the diode removes the ±80 mV of load- and temperature-dependent error that no calibration can take out (B3). |
| Schottky fourth | Reverse polarity protection for everything that follows. |

### Consequence worth knowing: reverse polarity now blows the fuse

With the TVS upstream of the diode, connecting the supply backwards makes the **unidirectional TVS
conduct in the forward direction** - it is a Zener, and backwards it is just a diode. It shorts the
input and **the 2 A fuse blows immediately.**

This is intended behaviour, not a defect:

- the 1.5KE20A withstands ~200 A for 8.3 ms, far longer than a 2 A fuse needs to clear
- a blown fuse tells you at once that the polarity is wrong

In the guide's original layout a reversed supply did nothing visible at all, which is quieter but
leaves you guessing. Worth writing on the inside of the enclosure lid.

### Divider calculation

Ratio (82 + 10) / 10 = **9.2**.

| Condition | Battery | at ADS1115 A0 | Inside ±2.048 V FSR |
|-----------|---------|---------------|---------------------|
| Battery low cutoff | 11.8 V | 1.283 V | yes |
| Resting, full | 12.7 V | 1.380 V | yes |
| Float charging | 13.6 V | 1.478 V | yes |
| Absorption | 14.4 V | 1.565 V | yes |
| Equalisation | 15.5 V | 1.685 V | yes |
| TVS clamping | 27.7 V | 3.011 V | clips, but below VDD 3.3 V - no damage |

Quiescent draw of the divider: 148 µA at 13.6 V, about 2 mW. Irrelevant next to the ESP.

**Check your charger's maximum output voltage.** The TVS starts conducting at 17.1 V standoff. Any
normal 12 V charger stays at or below 15.5 V, but a misconfigured or lithium-profile charger could
climb higher and would make the TVS heat up.

### Expected accuracy

| Contribution | Magnitude | Removed by calibration |
|--------------|-----------|------------------------|
| Resistor tolerance, 0.1 % each | up to ~27 mV at 13.6 V | yes |
| ADS1115 gain error | up to ~0.15 % | yes |
| Source impedance 8.9 kΩ against 6 MΩ input | ~0.15 % gain error | yes, **as long as the PGA is never changed afterwards** (M2) |
| Resistor tempco, 25-50 ppm/°C over 30 °C | ~20 mV | no |
| ADC resolution, ±2.048 V FSR | 0.6 mV at the battery | not needed |

**Realistic after calibration: ±20-30 mV.** Against the ±80 mV that measuring behind the diode
would have left, this is what makes the 12.9 / 13.2 V decision threshold in section 4 usable at
all.

## 4. Software

### What the voltage means under a charger

With shore power connected the charger, not the battery, sets the voltage. State of charge is
**not** readable while charging. What is readable - and far more valuable - is whether the charger
is still running.

### State machine

| State | Enter when | Meaning | Action |
|-------|------------|---------|--------|
| `CHARGING` | V ≥ 13.2 V sustained 5 min | shore power and charger healthy | normal telemetry |
| `ON_BATTERY` | V ≤ 12.9 V sustained 5 min | **shore power or charger lost** | immediate event, not at the next interval |
| `BATTERY_LOW` | V ≤ 12.0 V sustained 15 min | outage has been running a while | urgent alarm, repeat daily |
| `BATTERY_CRITICAL` | V ≤ 11.8 V sustained 15 min | protect what is left | back off, see below |

The 12.9-13.2 V gap is deliberate hysteresis so the state cannot flap.

### Debouncing is not optional

The fridge compressor and the bilge pump both cause real voltage dips. A two-second sag to 11.5 V
when the pump starts is **normal**, not a critical battery. Without debouncing this system will cry
wolf within its first week.

- decide state transitions on a **median over several minutes**, never on a single sample
- keep logging instantaneous values to telemetry - the dips themselves are useful data, for
  instance to spot the fridge short-cycling
- the sustained-time column above is a requirement, not a suggestion

### Behaviour in `BATTERY_CRITICAL`

Deep sleep with a long wake interval (30-60 min), waking only to measure and report. This drops
`BOOT-NETZ` and the immediate bilge alarm.

**That trade is deliberate.** If shore power has been gone long enough to pull the bank to 11.8 V,
the remaining capacity belongs to the bilge pump, not to the device that is watching it. A monitor
that flattens the battery keeping itself online has defeated its own purpose.

### Configurable values

All thresholds belong in NVS/Preferences, not in constants: `v_charging_on`, `v_on_battery`,
`v_low`, `v_critical`, the debounce windows, and `v_calibration_factor` (nominal 9.2).

## 5. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| ADS1115 does not answer | I2C NACK | telemetry field absent, event once; sensors and network stay up (do not reset the ESP) |
| Reading implausible (<5 V or >18 V) | range check | discard sample, do not feed the state machine |
| Charger fails, shore power fine | `ON_BATTERY` while ambient conditions are normal | same alarm - from the boat's point of view they are the same event |
| Fuse blown | total loss of power, server sees the last will | `boathub/<boat-id>/status = offline` after the keepalive expires |
| DC/DC fails | same as above | last will |

## 6. Safety

- **12 V is not harmless.** Fuse at the source, supply disconnected while wiring.
- **External 5 V off while flashing over USB.** USB, the 5 V pin and 3V3 are alternative supply
  paths, never parallel ones.
- Reverse polarity blows the fuse by design - see above. Label the enclosure.
- The 1 kΩ series resistor into A0 is a safety part: on reversed polarity it limits current into
  the ADS1115's ESD clamp to ~0.1 mA against a 10 mA limit. **Do not omit it.**
- No path exists from the server to anything in this document. Battery data is telemetry only.
- Star ground in the box (M8).

## 7. Test

### On the bench

- [ ] DC/DC delivers 5.0 V with no ESP connected
- [ ] Deliberately reverse the supply on a bench PSU with current limit: **fuse blows, nothing
      downstream is damaged**
- [ ] ESP 3V3 pin reads ~3.3 V after power-up
- [ ] Sweep the bench PSU 11 V to 15.5 V and confirm the ADC tracks linearly
- [ ] Confirm the state machine transitions at the configured thresholds, with a simulated
      two-second dip to 11.5 V producing **no** state change
- [ ] Measure total current draw at 12 V and compare against the 55-80 mA estimate in B1

### In the boat

- [ ] Polarity of the 12 V feed verified with a multimeter before connecting
- [ ] Box connected, ESP not yet fitted, 5 V output checked
- [ ] Reading agrees with a multimeter at the battery terminals
- [ ] Pull the shore power cable: `ON_BATTERY` is raised within the debounce window
- [ ] Restore shore power: state returns to `CHARGING`
- [ ] Box and DC/DC temperature checked after 30-60 minutes

### Calibration

1. Set the PGA to ±2.048 V. **Fix it before calibrating and never change it** (M2).
2. Measure the battery at the terminals with the multimeter.
3. Read the raw ADC value.
4. `v_calibration_factor = V_multimeter / V_adc_raw`, nominally 9.2.
5. Store in NVS. Re-check after the first week and after any wiring change.

Calibrate with shore power **off**, so the reading is not sitting on a charger's ripple.

## 8. Open points

| Point | Decide by | Who |
|-------|-----------|-----|
| Maximum output voltage of the installed charger | before first connection | Andreas |
| Exact threshold values - the table above is a starting point, not measured | after a week of data | both |
| Whether `BATTERY_CRITICAL` should also cut the 4-20 mA loop (20 mA is a third of the ESP's own draw) | when the bilge sensor is fitted | both |

## 9. References

- [000-design-review.md](000-design-review.md) - findings B1, B1a, B2, B3, I7, M2, M8
- [ESP32-S3-DevKitC-1 hardware reference](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [TI ADS1115 datasheet](https://www.ti.com/product/ADS1115)
- [Littelfuse 1.5KE series datasheet](https://www.mouser.com/datasheet/2/395/1_5KE_2520SERIES_O2104-3402913.pdf)
