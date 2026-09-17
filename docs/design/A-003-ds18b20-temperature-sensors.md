# A-003 - DS18B20 temperature sensors

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | yes |

## 1. Goal

Three potted 5 m DS18B20 probes measuring engine bay, bilge water and fridge temperature, each on
its own 1-Wire bus, reporting `engine_temp_c`, `bilge_temp_c` and `fridge_temp_c` to telemetry.

**Out of scope:** the optional fourth probe for tank or battery temperature (section 8), bilge
water *level* - that is the 4-20 mA probe in phase B - and the alarm thresholds themselves, which
belong to the alarm logic document.

## 2. Starting point

Three potted 5 m probes, one per measuring point. GPIO4/5/6 are reserved for them in the pin
plan and are all on the **top row** of the carrier's screw terminals, next to each other - see
[A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md).

This is built on USB power, on a breadboard, with no 12 V anywhere - see
[A-002-bench-setup-usb.md](A-002-bench-setup-usb.md).

### Why one GPIO per probe

All three could share a single 1-Wire bus. Three separate buses cost three GPIOs and buy:

- **fault isolation** - a shorted or broken cable takes out one reading, not all three. That is the
  guardrail from CLAUDE.md made physical.
- **no addressing ambiguity** - one device per bus means a Skip-ROM read always talks to the
  intended sensor, and you cannot confuse two probes that were enumerated in a different order
  after a reboot.
- **independent pull-up tuning** - the bilge cable may end up a different length from the engine
  bay one.

The cost is three pins, and the pin plan has them spare. Keep it.

## 3. Hardware

### Parts

| Part | Qty | Purpose | Note |
|------|-----|---------|------|
| DS18B20-compatible potted probe, 5 m | 3 | engine bay, bilge, fridge | sold in 3-packs |
| 2.0-2.2 kΩ resistor | 3 | 1-Wire pull-up, one per bus | **not** 4.7 kΩ - see below |
| 100 Ω resistor | 3 | series protection in each DATA line | |
| 3-pole screw terminal | 3 | detachable probe connection, on the perfboard | cables must come off for service |
| Clamp-on ferrite | 3 | optional, conducted noise at the box entry | |

### Pin assignment

| GPIO | Carrier terminal | Signal | Direction | Level | Cable label |
|------|------------------|--------|-----------|-------|-------------|
| 4 | `IO4`, top row | 1-Wire DATA, engine bay | bidirectional, open-drain | 3.3 V | `MOTOR-T` |
| 5 | `IO5`, top row | 1-Wire DATA, bilge water | bidirectional, open-drain | 3.3 V | `BILGE-T` |
| 6 | `IO6`, top row | 1-Wire DATA, fridge | bidirectional, open-drain | 3.3 V | `FRIDGE-T` |
| 7 | `IO7`, top row | spare, fourth bus | - | - | see section 8 |

### Circuit

One channel, repeated identically three times:

```
  3.3 V ----+---- 2.2 kOhm ----+--------------------------- yellow   DATA
            |                  |
            |             (bus node)                        [ 5 m potted probe ]
            |                  |
            |               100 Ohm
            |                  |
            |                GPIO4
            |
            +------------------------------------------------ red     VDD
  GND -------------------------------------------------------- black   GND
```

The pull-up sits at the **bus node**, so it drives the cable directly. The 100 Ω sits between the
bus node and the GPIO, where it protects the pin without slowing the edge.

**Three wires, not two - parasite power is not used.** Some clone probes report parasite capability
incorrectly, so pin it off explicitly in firmware rather than letting the library auto-detect.

### Why 2.2 kΩ and not the textbook 4.7 kΩ

4.7 kΩ is the standard value for a bus a few centimetres long. At 5 m it is thin:

| Pull-up | Cable ~500 pF | τ = RC | Rise to threshold | Sink current | Margin in the 15 µs read slot |
|---------|---------------|--------|-------------------|--------------|-------------------------------|
| 4.7 kΩ | 5 m | 2.35 µs | ~2.8 µs | 0.70 mA | works, little headroom |
| 3.3 kΩ | 5 m | 1.65 µs | ~2.0 µs | 1.00 mA | comfortable |
| **2.2 kΩ** | 5 m | 1.10 µs | ~1.3 µs | 1.50 mA | generous |
| **2.0 kΩ** | 5 m | 1.00 µs | ~1.2 µs | 1.65 mA | generous |

**Anything from about 1.5 kΩ to 3.3 kΩ is right**, so fit whatever the assortment holds - the value
is not critical, only the order of magnitude is. Every one of them is well inside the DS18B20's
4 mA sink rating. Do not go below ~1.5 kΩ: at 1 kΩ the sink current reaches 3.3 mA and the low
level starts to lift.

**Verify on the bench with the real 5 m cables** (section 7). Testing with short jumpers proves
nothing - the cable capacitance that makes 4.7 kΩ marginal simply is not there.

### Electrical constraints

- The 100 Ω series resistor costs low-level margin in only one of the two directions, and the two
  never coincide - whichever end is pulling, the other is not.

  | Who pulls low | Current through the 100 Ω | Result |
  |---------------|---------------------------|--------|
  | The ESP | the full pull-up current, 1.65 mA at 2.0 kΩ | 0.17 V across it, so the sensor sees ~0.27 V against its V<sub>IL</sub> of 0.8 V |
  | The sensor | none - the GPIO is a high-impedance input | the pin sees the sensor's V<sub>OL</sub> of ~0.4 V directly, against the ESP32's V<sub>IL</sub> limit of 0.825 V |

  Valid across the whole pull-up range above; at 1.5 kΩ the drop is 0.22 V and the conclusion is
  unchanged.
- **Do not add clamping diodes** on the data lines. A 3.3 V zener or TVS adds tens of pF, which
  costs more in edge quality than it buys in protection at these voltages.
- The 100 Ω will not save a GPIO from a cable shorted to 12 V. **Route the probe cables away from
  power wiring**, particularly the tiller pilot's motor leads, as the project guide already
  requires.
- Total sensor draw is ~1.5 mA per probe during conversion, ~1 µA idle. Irrelevant against the
  AMS1117's headroom.

### Mounting

| Probe | Where | Why |
|-------|-------|-----|
| `MOTOR-T` | free in the air in the engine bay, **not** on the block or exhaust | measures bay ambient; strain-relieve the cable |
| `BILGE-T` | low, fully submersible, **easily replaceable** | see the corrosion note below |
| `FRIDGE-T` | in the air inside the box, away from the evaporator | measures usable interior temperature, not the cold plate |

**The bilge probe is a consumable.** On cheap probes the stainless alloy is unspecified, and many
have the sheath bonded internally to GND. A grounded stainless probe permanently submerged, tied to
the boat's negative - which permanent shore power ties to shore earth - sits in a galvanic circuit
with every other underwater metal. Check continuity before fitting (section 7),
mount it so it can be swapped without dismantling anything, and inspect it at every haul-out.

## 4. Software

### Libraries

`OneWire` plus `DallasTemperature`, three independent bus instances.

### Conversion strategy

Resolution is configurable at 9-12 bits. **Use 11 bits**: 0.125 °C resolution at a 375 ms
conversion, against 750 ms for the 12-bit default. Nothing here needs 0.0625 °C.

**Do not block on the conversion.** The obvious `requestTemperatures` call waits for the full
conversion time and would stall the loop - and with it Wi-Fi and MQTT - every cycle. Instead:

1. `setWaitForConversion(false)`
2. issue Convert T on all three buses back to back
3. note the time, return to the main loop
4. read all three once the conversion time has elapsed

The three buses convert in parallel, so the whole set costs one conversion time, not three.

### Identity check

With one device per bus a Skip-ROM read always works, but it cannot tell you **which** probe is
attached. The realistic failure is not electrical: it is unplugging three identical cables during
service and reconnecting two of them the wrong way round, after which engine bay temperature
silently reports bilge water.

So: **read each probe's 64-bit ROM at commissioning and store it in NVS.** On every read, compare.
A mismatch is logged and surfaced in telemetry as a warning - it should not stop the reading, since
a legitimately replaced sensor must not take the channel down. The local web UI gets a "re-learn
probe IDs" action for exactly that case.

### Validating a reading

In order, before a value is allowed into telemetry:

| Check | Reject if |
|-------|-----------|
| CRC | scratchpad CRC fails |
| Disconnected | exactly `-127.0` - the library's sentinel for no response |
| **Power-on default** | exactly `85.0` - see below |
| Plausible range | outside the per-location range below |
| Rate of change | more than 10 °C between consecutive 60 s samples |

**The 85 °C trap.** 85.0 °C is the power-on reset value of the DS18B20 scratchpad. It is also a
perfectly legal temperature, which is what makes it nasty: an exact 85.0 almost always means the
conversion never completed or the sensor browned out, not that something is hot. Treat an exact
85.0 as suspect and require a corroborating second reading before believing it.

| Probe | Plausible range |
|-------|-----------------|
| `MOTOR-T` | -20 to +80 °C |
| `BILGE-T` | -5 to +40 °C |
| `FRIDGE-T` | -20 to +30 °C |

### Configurable values

In NVS, not compiled in: the three ROM IDs, per-probe plausibility ranges, resolution, sample
interval, and any per-probe offset from the ice-bath check.

## 5. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Cable unplugged or broken | `-127.0`, no ROM response | that field absent from telemetry, event once; the other two buses unaffected |
| DATA shorted to GND | no response on that bus only | as above - this is what the separate buses buy |
| Intermittent contact | CRC failures | discard the sample, count the rate; sustained failures raise a warning |
| Sensor browns out mid-conversion | exactly `85.0` | discard, retry next cycle |
| Probe swapped during service | stored ROM ID mismatch | log and flag in telemetry, keep reading |
| Bilge probe corroded through | drifting or implausible values, then `-127.0` | treat as a consumable, replace |

None of these may reset the ESP or affect the network path.

## 6. Safety

- Nothing here runs above 3.3 V, so this is the low-risk part of the build.
- The real hazard is **a probe cable chafing onto 12 V wiring** in the engine bay. That kills the
  GPIO and possibly the module. Separate routing and proper strain relief are the mitigation, not
  the 100 Ω resistor.
- The bilge probe is submerged and grounded - a galvanic concern for the *boat*, not an electrical
  one for the ESP. See the mounting note above.
- No reading from this document feeds anything that can act on the boat. Temperature is telemetry
  only.

## 7. Test

### On the bench

- [ ] **Wire colours metered out before connecting.** Red/black/yellow is common but not universal
      on clones, and swapping VDD and GND destroys the sensor. In diode-test mode the meter's
      **positive lead on GND** shows a forward drop to both other wires, because the ESD diodes sit
      with their anode there. Only a conducting reading proves anything: the blocking direction is
      not a clean open circuit, since the chip part-powers itself through that path.
- [ ] The same test will **not** separate VDD from DATA. The parasite-power diode between them sits
      deeper in the die than the ESD structures, and a meter on a low resistance range often cannot
      forward-bias it. With GND established, settle the other two by trying them: wired the wrong
      way round the sensor takes its supply through the pull-up, browns out the moment it converts,
      and reports nothing on that GPIO. That costs a restart, not a sensor. A probe that gets warm
      is a different matter - pull it, GND is wrong.
- [ ] Each probe enumerates, **ROM family code is 0x28**
- [ ] ROM IDs recorded and written to NVS
- [ ] CRC passes over a few hundred consecutive reads
- [ ] **Pull-up comparison with the full 5 m cables**: count CRC errors over ~500 reads at 2.2 kΩ
      and at 4.7 kΩ. Expect zero at 2.2 kΩ; if 4.7 kΩ also gives zero, the margin still favours
      2.2 kΩ
- [ ] Ice bath check: all three in a stirred ice-water slurry read 0.0 °C ±0.5 °C
- [ ] Unplug one probe mid-run: only that field drops out, no reset, others keep reporting
- [ ] Short one DATA line to GND: only that bus fails
- [ ] Swap two probes: the ROM mismatch warning fires
- [ ] Loop timing unaffected - confirm the conversion is non-blocking by watching MQTT keepalive

### In the boat

- [ ] Cables labelled `MOTOR-T`, `BILGE-T`, `FRIDGE-T` at both ends
- [ ] **Bilge probe sheath continuity checked** against all three wires before fitting
- [ ] Bilge probe mounted so it can be replaced without dismantling anything
- [ ] Engine bay probe in free air, clear of the block and exhaust, cable strain-relieved
- [ ] Fridge probe in air, not touching the evaporator
- [ ] Probe cables routed clear of power and tiller-pilot motor wiring
- [ ] All three plausible after an hour in place

### Calibration

DS18B20 is factory calibrated to ±0.5 °C, which is good enough for every use here. The ice bath is
a **sanity check to catch a bad sensor**, not a calibration step. Only if one probe is clearly off
does it get an offset stored in NVS - and then note which probe, because the offset follows the
sensor, not the channel.

## 8. Open points

| Point | Decide by | Who |
|-------|-----------|-----|
| What GPIO7 gets: the optional tank probe, or a battery probe for temperature-compensated thresholds | when either becomes necessary | both |
| Whether the fridge duty cycle is worth deriving and reporting | after a week of data | both |
| Whether engine bay temperature should feed the charging discriminator in [B-001](B-001-power-supply.md) | after the first motoring trip | both |

### GPIO7 is contested, but it does not have to be

The project guide reserves GPIO7 for an optional water tank probe;
[B-001](B-001-power-supply.md) raised a battery probe for temperature-compensating the AGM
thresholds. Both want a fourth 1-Wire bus.

This is not a real conflict: the carrier has `IO1`, `IO2`, `IO10`-`IO14`, `IO38`-`IO42`, `IO47` and
`IO48` genuinely free (see [A-001](A-001-devkit-and-carrier.md)). A fifth bus costs a pin nobody
else wants. Decide by need, not by scarcity.

### Two readings that are worth more than their face value

Both are observations to confirm with real data before anything is built on them:

- **The fridge probe reveals the compressor duty cycle.** Its sawtooth gives run time versus rest
  time, and with the compressor's current draw that yields a daily Ah estimate - for the one load
  that dominates consumption away from the dock. That is the number voltage monitoring cannot
  give (see the accuracy limits in [B-001](B-001-power-supply.md)).
- **Engine bay temperature indicates the engine running**, which could sharpen the shore-power-loss
  alarm. B-001 gates that alarm on 6 hours of prior charging, whose known weak spot is a long
  motoring passage looking like shore power. A hot engine bay coinciding with the charging period
  distinguishes alternator from charger far more directly than elapsed time does. Summer sun also
  warms the bay, so this is a corroborating signal, not a sole criterion.

## 9. References

- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - carrier terminals, free spare pins
- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the bench environment this is built in
- [B-001-power-supply.md](B-001-power-supply.md) - charging discriminator, SoC accuracy limits
- [Analog Devices DS18B20 datasheet](https://www.analog.com/en/products/ds18b20.html)
