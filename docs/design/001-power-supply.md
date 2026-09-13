# 001 - Power supply and battery measurement

| | |
|---|---|
| **Status** | Draft |
| **Stage** | 1, phase 1B |
| **Roadmap package** | 1B.1 - 1B.5 |
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

> **This is not the first thing to build.** The sensor set and the whole network stack are proven
> on USB power first - see [003-bench-setup-usb.md](003-bench-setup-usb.md). Everything here comes
> at the *end* of the bench phase, so that a brownout later can be blamed on the converter rather
> than on the firmware. The changeover procedure is in 003, section 8.

## 2. Starting point

**The boat is permanently on shore power while unattended** (answer to open question 1). That
settles the power concept:

- continuous operation, no duty cycling
- a standard DC/DC module is fine; no low-quiescent-current part needed
- `BOOT-NETZ` and the immediate bilge alarm stay permanently available
- **the low-voltage cutoff is still required** - as a backstop for the case shore power fails and
  stays failed, not as a normal operating mode

### House bank

**2 x 100 Ah AGM in parallel = 200 Ah nominal, ~100 Ah usable** to the conventional 50 % limit.

There is **no solar and no wind generator**. Away from the dock the only charging source is the
alternator, so the bank is a pure reserve between engine runs.

Two facts about AGM drive the thresholds in section 4, and both differ from flooded lead-acid:

- **AGM rests higher.** 50 % state of charge is about **12.3 V** at rest, where a flooded battery
  would read roughly 12.06 V. Thresholds carried over from generic lead-acid tables sit far too
  low and would let the bank run down to 20-30 % before warning.
- **Most AGM must not be equalised.** Absorption is typically 14.4-14.7 V, float 13.2-13.8 V, and
  the 15.5 V equalisation step that a flooded battery tolerates will damage AGM. Check the charger
  is not configured for a flooded profile.

### Two operating situations, one set of rules

| | Marina, unattended | Underway or at anchor |
|---|---|---|
| Charging | shore charger, continuous float | alternator only, while the engine runs |
| Typical load | BoatHub alone, ~60 mA (fridge normally off) | fridge 25-45 Ah/day, plus autopilot and instruments |
| Reserve on 100 Ah usable | **~10 weeks** | **1.5-2.5 days** between engine runs |
| Losing charge means | something failed - **alarm** | entirely normal - **no alarm** |

The right-hand column is why the alarm cannot be a plain voltage threshold: underway the system is
below every charging threshold all day long. The discriminator is in section 4.

**If the fridge is left running in the marina**, the reserve collapses from ~10 weeks to **under 3
days**. That is the case where the shore-power-loss alarm earns its keep.

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
| Critical, AGM ~30 % | 12.0 V | 1.304 V | yes |
| Warning, AGM ~50 % | 12.3 V | 1.337 V | yes |
| Resting, full AGM | 12.85 V | 1.397 V | yes |
| Float charging | 13.2-13.8 V | 1.435-1.500 V | yes |
| Absorption, AGM | 14.4-14.7 V | 1.565-1.598 V | yes |
| Equalisation (flooded profile - **wrong for AGM**) | 15.5 V | 1.685 V | yes |
| TVS clamping | 27.7 V | 3.011 V | clips, but below VDD 3.3 V - no damage |

Quiescent draw of the divider: 148 µA at 13.6 V, about 2 mW. Irrelevant next to the ESP - it is
roughly 0.25 % of total system draw, and less than a quarter of what the DevKit's RGB LED consumes
while dark.

**Check your charger's profile.** Two separate concerns:

- The TVS starts conducting at 17.1 V standoff. A misconfigured or lithium-profile charger could
  climb there and make it heat up.
- More likely and more damaging: a charger set to a **flooded** profile will try to equalise at
  15.5 V+, which most AGM must never see. If the reading ever sits above ~14.8 V for an extended
  period, the charger is set wrong for this bank.

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

Voltage thresholds for **AGM**, which rests roughly 0.2 V higher than flooded lead-acid at the same
state of charge.

| State | Enter when | ~SoC | Meaning |
|-------|------------|------|---------|
| `CHARGING` | V ≥ 13.1 V sustained 5 min | - | a charging source is active |
| `ON_BATTERY` | V ≤ 12.8 V sustained 5 min | - | nothing is charging |
| `BATTERY_LOW` | V ≤ 12.3 V sustained 15 min | ~50 % | the conventional AGM limit - act now |
| `BATTERY_CRITICAL` | V ≤ 12.0 V sustained 15 min | ~30 % | protect the bank |

The 12.8-13.1 V gap is deliberate hysteresis so the state cannot flap. Note that a fully charged
AGM rests at about 12.85 V, so after a charger stops the voltage settles across the `ON_BATTERY`
threshold over an hour or two rather than instantly - that delay is expected.

### The alarm is not the state

`ON_BATTERY` on its own is **not** an alarm condition. Underway the system sits there all day, and
an alarm that fires every time you leave the dock is an alarm that gets muted - and is then missing
in the marina, which is the one place it matters.

The discriminator needs no mode switch, no user action and no SeaTalk:

> **Raise "shore power lost" only if the system was in `CHARGING` continuously for at least 6 hours
> immediately before dropping to `ON_BATTERY`.**

Only a shore charger holds a float voltage for that long. An alternator run is measured in
hours at most, and usually far less.

| Situation | Preceded by ≥6 h charging? | Alarm |
|-----------|---------------------------|-------|
| Marina, shore power fails | yes - float for days | **yes** |
| Engine run, then sailing on | no - too short | no |
| A day under sail | no - never reached `CHARGING` | no |
| Long motoring passage, then engine off | possibly yes | false positive, see below |

The remaining false positive is a motoring passage of over six hours. It is largely
self-suppressing, because the alarm has to leave the boat over marina Wi-Fi that is not reachable
at sea; the event queues and is either dropped or acknowledged on arrival. **From stage 2 onwards**
it can be suppressed properly by also requiring `seatalk_online == false` - if the instruments are
powered, somebody is aboard.

`BATTERY_LOW` and `BATTERY_CRITICAL` are raised in **both** situations. A bank at 50 % is worth
knowing about whether or not anybody is aboard.

### Debouncing is not optional

The fridge compressor and the bilge pump both cause real voltage dips. A two-second sag to 11.5 V
when the pump starts is **normal**, not a critical battery. Without debouncing this system will cry
wolf within its first week.

- decide state transitions on a **median over several minutes**, never on a single sample
- keep logging instantaneous values to telemetry - the dips themselves are useful data, for
  instance to spot the fridge short-cycling
- the sustained-time column above is a requirement, not a suggestion

### How accurate is the state of charge?

Not very, and the document should be honest about it. The thresholds above are **resting**
voltages, but the battery is rarely at rest - the fridge cycles, so most readings are taken under
some load. AGM sags roughly 0.1-0.15 V under a compressor load on a 200 Ah bank, so a measured
12.2 V may well be 12.35 V rested, or about 55 % rather than 45 %.

Realistic accuracy is **±10-15 % state of charge**. That is fine for "run the engine today" and for
"something has gone wrong at the dock". It is not good enough to plan a passage around. Proper Ah
counting would need a shunt, which was considered and rejected - see section 8.

### Behaviour in `BATTERY_CRITICAL`

This is the one place the two situations need different behaviour, and it uses the same 6-hour
history as the alarm:

| Preceded by sustained shore charging? | Interpretation | Behaviour |
|---------------------------------------|----------------|-----------|
| yes | nobody aboard | **deep sleep**, 30-60 min wake interval, measure and report only |
| no | somebody aboard | **stay awake**, keep `BOOT-NETZ` and the local UI live, warn locally |

Unattended, the trade is deliberate: if an outage has pulled the bank to 12.0 V, the remaining
capacity belongs to the bilge pump, not to the device watching it. A monitor that flattens the
battery keeping itself online has defeated its own purpose.

Aboard, the opposite holds. Going to sleep at 12.0 V would remove the display exactly when the
reading becomes actionable - the moment you need to decide whether to start the engine.

### Configurable values

All thresholds belong in NVS/Preferences, not in constants: `v_charging_on` (13.1), `v_on_battery`
(12.8), `v_low` (12.3), `v_critical` (12.0), the debounce windows, the **charging-history window**
(6 h), and `v_calibration_factor` (nominal 9.2).

Battery chemistry is a configuration item too. Swapping the AGM bank for flooded or lithium moves
every threshold in the table, so they must not be compiled in.

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
- [ ] **Alarm precondition:** hold 13.5 V for less than the history window, drop to 12.5 V →
      `ON_BATTERY` is entered but **no alarm** is raised
- [ ] **Alarm precondition, inverse:** hold 13.5 V beyond the history window, then drop →
      alarm **is** raised. Shorten the window to minutes for the bench run
- [ ] Measure total current draw at 12 V and compare against the 55-80 mA estimate in B1

### In the boat

- [ ] Polarity of the 12 V feed verified with a multimeter before connecting
- [ ] Box connected, ESP not yet fitted, 5 V output checked
- [ ] Reading agrees with a multimeter at the battery terminals
- [ ] Pull the shore power cable after a long float period: `ON_BATTERY` **and** the shore-power-loss
      alarm are raised
- [ ] Restore shore power: state returns to `CHARGING`, alarm clears
- [ ] Start the engine, run it, shut down: `ON_BATTERY` is entered but **no alarm** is raised
- [ ] Confirm the charger holds AGM levels - absorption at or below ~14.7 V, no 15.5 V
      equalisation step
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
| Charger profile - confirm it is set for AGM, not flooded | before first connection | Andreas |
| Exact threshold values - the table is a starting point, not measured on this bank | after a week of data | both |
| Charging-history window - 6 h is a reasoned guess; tune it once real float and engine-run patterns are logged | after the first trip | both |
| Whether `BATTERY_CRITICAL` should also cut the 4-20 mA loop (20 mA is a third of the ESP's own draw) | when the bilge sensor is fitted | both |
| Battery temperature sensor on the spare GPIO7, for temperature-compensated thresholds | only if the readings prove too seasonal | both |

### Considered and rejected: a current shunt for Ah counting

Proper state-of-charge measurement needs Ah counting through a shunt, not voltage. Two routes were
looked at and both were dropped for now:

- **A bus-based battery monitor** (Yacht Devices YDBM-02, Victron SmartShunt with a gateway). These
  are **NMEA2000 devices** - "SeaTalkNG" is N2K with a Raymarine connector, not the SeaTalk1 bus on
  the S1 - so they would land in stage 3, not stage 2. Decisive objection: the boat's instruments
  are switched off when nobody is aboard, so a bus device is dead exactly when the marina alarm
  matters. Keeping an N2K backbone powered for it would cost more current than the entire BoatHub.
- **A DIY shunt on a spare ADS1115 channel** (differential, ±0.256 V, suits a 75 mV shunt). Works
  in both situations and stays independent of any bus, but needs a heavy shunt in the main battery
  negative and a state-of-charge algorithm - Peukert compensation, charge efficiency,
  synchronisation on full charge - that is easy to get subtly wrong.

Voltage-only monitoring is accepted instead, with the accuracy limits stated in section 4. If the
cruising case later becomes more important, the DIY route stays open - the spare ADS1115 channels
are already there, and in stage 3 the BoatHub could then publish its own battery data as
**PGN 127508** (roadmap item 3.4) rather than consuming someone else's.

## 9. References

- [000-design-review.md](000-design-review.md) - findings B1, B1a, B2, B3, I7, M2, M8
- [ESP32-S3-DevKitC-1 hardware reference](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [TI ADS1115 datasheet](https://www.ti.com/product/ADS1115)
- [Littelfuse 1.5KE series datasheet](https://www.mouser.com/datasheet/2/395/1_5KE_2520SERIES_O2104-3402913.pdf)
