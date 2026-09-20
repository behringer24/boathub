# A-008 - SHT31 cabin climate

| | |
|---|---|
| **Phase** | A |
| **Software version** | v2 |
| **Touches hardware** | no |

## 1. Goal

Read cabin temperature and relative humidity from the SHT31 at 0x44 and feed them into the 5 min
aggregate as `cabin_temp_c` and `cabin_rh`.

**Out of scope:** wiring, addressing and cable length, which are in
[A-002](A-002-bench-setup-usb.md); the alarm thresholds, which belong to the alarm logic.

## 2. Starting point

The sensor shares the I2C bus on GPIO8/9 with the ADS1115 converters and the IMU - **that sharing drives most of the
decisions below.** Anything this sensor does to the bus, it does to the analog side too.

Humidity is the reason this sensor exists. Temperature it measures as well, and the cabin figure is
worth having, but the condensation and mould warning is what the SHT31 is for and what the DS18B20
cannot give.

## 3. Reading it

### Single shot, not periodic

The SHT3x can free-run at up to 10 Hz. It should not: at a 10 s sample interval a free-running
sensor spends its time measuring nothing anyone asked for, and **continuous high-repeatability
measurement warms the sensor by a measurable fraction of a degree.** A humidity sensor that heats
itself reports humidity that is too low, because relative humidity falls as temperature rises.

Single shot at high repeatability - 12.5 ms per measurement, once every 10 s - is a duty cycle of
roughly one part in a thousand. Self-heating at that rate is not measurable.

### Without clock stretching

The command has two variants. Clock stretching lets the sensor **hold SCL low** until the
measurement is finished, so the read call simply blocks and returns data.

That is the wrong one here. Holding SCL low holds it low for *everybody*: the converters sit on
the same two wires and cannot be talked to for those 12.5 ms. Use the non-stretching command
(`0x2400`), return to the loop, and collect the result on a later pass 15 ms later.

The same discipline as the DS18B20 conversion in [A-003](A-003-ds18b20-temperature-sensors.md), for
the same reason: nothing in `loop()` waits.

### CRC on every word

The SHT3x appends a CRC-8 to each 16-bit value - two values, six bytes. **Check both.** The sensor
sits on up to 3 m of cable outside the enclosure, and a corrupted humidity reading is perfectly
plausible-looking. A failed CRC discards the sample; it is not an error worth reporting unless it
persists.

### The status register catches a silent reset

`0xF32D` returns a status word whose bit 4 means *system reset detected*. A sensor that browned out
and restarted answers normally afterwards, with its configuration back at defaults - the same class
of fault as the DS18B20's 85 °C reading, and just as invisible unless something looks.

Read it occasionally, log a reset, and clear it. If it keeps setting itself, the supply to the
sensor is the suspect, not the sensor.

## 4. The heater

The SHT3x has an on-chip heater, and on a boat it earns its place.

**The failure it prevents:** in a cold cabin the air reaches saturation and condensation forms on
the sensor itself. A wet sensor reads 100 %RH and keeps reading 100 %RH after the air has dried,
until the water evaporates on its own. That can take days. The reading is not merely uninformative,
it is **stuck** - which is worse, because it looks like a measurement.

Pulsing the heater drives the condensation off and the sensor starts tracking again.

### It cannot be used casually

While the heater runs, the sensor is several degrees above cabin temperature, so both temperature
and humidity readings are meaningless - and stay meaningless for a minute or two afterwards while
it cools back down.

So the cycle is:

| Step | |
|------|--|
| Trigger | `cabin_rh` above 95 % continuously for 30 min |
| Heat | 10 s |
| Cool | 120 s |
| During heat and cool | **readings discarded**, not fed into the aggregate |

At 10 s every half hour the energy cost is nothing, and the duty cycle is low enough not to bias
the long-term temperature record.

**A heater cycle makes `n` dip in that window** - roughly 13 of 30 samples are discarded. That is
expected and not a fault. It is worth knowing before the *samples per window* panel in
[A-006](A-006-telemetry-storage.md) sends somebody hunting.

All four numbers belong in NVS, and the heater needs an off switch: a sensor mounted somewhere that
never condenses does not need it, and being able to rule it out is worth more than the feature.

## 5. Validating a reading

| Check | Reject if |
|-------|-----------|
| CRC | either word fails |
| Range, temperature | outside -20 to +60 °C |
| Range, humidity | outside 0 to 100 %RH |
| Heater state | the sample falls inside a heat or cool window |
| Rate of change | more than 10 °C or 30 %RH between consecutive 10 s samples |

A rejected sample is simply not counted into the window: `n` drops, and the aggregate is still
correct for the samples that remain. Rejecting is always better than averaging in a value that is
known to be wrong.

## 6. Feeding the aggregate

Temperature and humidity each contribute mean, min and max over the 5 min window, exactly like
every other channel ([A-005](A-005-server-uplink.md)).

Humidity is the channel where min and max earn their keep most clearly: a cabin that sat at 70 %
and touched 98 % once overnight is a condensation risk, and a mean of 74 % says nothing about it.

If every sample in a window was rejected, the fields are **absent** from the message rather than
zero - the rule that runs through the whole system.

## 7. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Sensor does not acknowledge | I2C NACK | fields absent, event once; the ADS1115s on the same bus keep working |
| CRC fails occasionally | CRC check | discard the sample, count it; a rising rate points at the cable |
| CRC fails constantly | as above | event: the sensor or its cable needs attention |
| Sensor reset itself | status bit 4 | log it, re-apply configuration, keep reading |
| Reads a hard 100 %RH for hours | plausibility over time | the heater cycle exists for exactly this |
| Whole bus dead | every device NACKs | not this sensor's problem to solve - the bus scan in the portal answers it |

## 8. Verification

- [ ] Sensor answers at 0x44 and returns plausible room values
- [ ] Both CRCs check out over a few hundred consecutive reads
- [ ] Breathing on the sensor moves humidity within a second or two, and it recovers afterwards
- [ ] A second sensor at 0x45 agrees with the first within ±0.2 °C and ±2 %RH - the cheapest way to
      find a bad one
- [ ] The measurement does not block: the ADS1115s remain readable during an SHT31 measurement
- [ ] A heater cycle raises the temperature reading and the samples in it are discarded, not averaged
- [ ] `n` dips as expected during a heater cycle and recovers afterwards
- [ ] Unplugging the sensor removes its fields from the message and leaves the rest intact
- [ ] Pulling the sensor's supply and restoring it sets the reset bit, which is logged

## 9. References

- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - wiring, addressing, cable length, bus rules
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the same
  non-blocking discipline on the 1-Wire side
- [A-005-server-uplink.md](A-005-server-uplink.md) - the aggregate this feeds
- [Sensirion SHT3x datasheet](https://sensirion.com/products/catalog/SHT31-DIS-B)
