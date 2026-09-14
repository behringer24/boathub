# 000 - Design review and spec validation

| | |
|---|---|
| **Status** | Accepted |
| **Stage** | all |
| **Roadmap package** | cross-cutting |
| **Created** | 2026-09-13 |
| **Last changed** | 2026-09-13 |
| **Touches hardware** | yes |

Review of the complete design in the project guide v0.1 against the component datasheets. Covers
what works as specified, what has to change before building, and which parts are still missing per
stage.

## Verdict

**Stage 1 is sound and will work** once three corrections are applied (B1-B3 below). The pin plan,
the divider ratio, the TVS selection and the decision to give every DS18B20 its own GPIO all hold
up against the datasheets. Two of the three corrections are wiring-order changes on the protection
board and cost nothing.

**Stage 2, 2B and 3 are not electrically complete yet.** That is intentional in the guide, but the
missing pieces are concrete and listed in section 4 - including one safety detail on the SeaTalk TX
side that is missing from the guide entirely (I5).

**One open question blocks the power design:** is there shore power at the berth? See B1.

---

## 1. Blockers - fix before building

### B1 - The monitor can flatten the battery it monitors

**System level, needs a decision.**

Estimated continuous draw of stage 1 as designed:

| Item | at 12 V |
|------|---------|
| ESP32-S3, Wi-Fi associated, no light sleep | ~40-50 mA |
| DevKit overhead (power LED, USB-UART bridge) | ~5-8 mA |
| DC/DC quiescent current (typical cheap 3 A module) | ~5-20 mA |
| Divider, sensors | <1 mA |
| **Total** | **~55-80 mA** |

That is **1.3-1.9 Ah per day**, roughly **40-60 Ah per month**. On a 100 Ah battery with 50 Ah
usable, the system flattens its own battery in about **four to six weeks** - and it does so while
reporting that it is doing it.

Note that the **DC/DC quiescent current sets the floor**, not the ESP. Deep sleep on the ESP alone
cannot get below the converter's own idle draw, so the converter choice matters more than the sleep
strategy.

**Decision needed:**

- **With shore power / charger at the berth:** continuous operation is fine, build as designed. Add
  a low-voltage warning anyway.
- **Without shore power:** this needs (a) a DC/DC with a specified low quiescent current, (b) duty
  cycling - wake every 5-10 min, measure, publish, deep sleep, and (c) a **low-voltage cutoff in
  firmware** that stops reporting below roughly 11.8 V and only wakes rarely to re-check.

Duty cycling conflicts with two stated goals: `BOOT-NETZ` being permanently available, and the
immediate bilge alarm. Both become "available while awake" unless a separate wake source is added.
**This needs answering before the enclosure is wired.**

### B2 - TVS diode sits behind the reverse-polarity diode

The guide's input chain is:

```
fuse -> 1N5822 -> TVS 1.5KE20A -> caps -> DC/DC
```

A surge arriving on the 12 V line has to pass **through the 1N5822** before the TVS can clamp it.
The 1.5KE20A clamps at 27.7 V while conducting up to 54 A; the 1N5822 is a 3 A part. The diode is
in the surge path and is the weakest link. The TVS also cannot protect the diode against a negative
transient beyond its 40 V reverse rating.

**Fix - reorder, no extra parts:**

```
12 V in
  |
2 A fuse
  |
  +---- 1.5KE20A ---- GND      <- TVS first, straight across the input
  +---- 100 nF ------ GND
  |
1N5822                          <- then reverse-polarity protection
  |
  +---- 100 uF ------ GND
  |
DC/DC 12 V -> 5 V
  |
  +---- 470 uF ------ GND
  +---- ESP32 5V/VIN
```

The fuse must stay upstream of the TVS so a sustained overvoltage blows the fuse rather than
cooking the TVS. That part of the original design is already correct.

### B3 - Battery voltage measured behind the Schottky diode

The guide acknowledges the offset and proposes to calibrate it out. **Calibration cannot remove it,
because the drop is not constant.**

1N5822 forward drop across the real operating envelope:

- load current 50-250 mA: Vf ≈ 0.20-0.29 V → **~90 mV of variation**
- temperature 0-50 °C at roughly -1.5 mV/°C: **~75 mV of drift**

Combined, roughly **±80 mV of residual error after calibration**. On a lead-acid curve, 12.60 V is
100 % SoC and 12.45 V is about 75 % - so the remaining error is **the width of a full 25 % state of
charge step**. That defeats the purpose of trend monitoring.

**Fix - move the divider tap upstream of the diode, to the fuse side.** The divider draws ~160 µA,
so it loads the feed by nothing measurable.

**Is the tap safe upstream?** Yes, checked both fault cases:

| Case | Voltage at ADS1115 A0 | Verdict |
|------|----------------------|---------|
| Normal, 14.4 V charging | 1.57 V | inside ±2.048 V FSR |
| TVS clamping at 27.7 V | 3.01 V | below VDD 3.3 V, below abs max 3.6 V |
| Reverse polarity, -12 V | clamped to -0.3 V by the internal ESD diode, ~0.1 mA through the 1 k series resistor | far below the 10 mA limit |

The existing 1 kΩ series resistor is what makes the reverse case safe - keep it.

---

## 2. Important - design decisions to settle

### I1 - SHT31 placement conflicts with itself

The SHT31 is specified to measure cabin climate, but:

- inside the sealed IP65 box it measures **box temperature**, not cabin air. The box runs roughly
  10-12 °C above ambient (see I7), and since relative humidity is temperature-dependent, a few °C
  of error becomes several %RH of error. The reading would be meaningless.
- outside the box it needs an I2C cable, and I2C is not a cable bus.

**Resolution:** mount the SHT31 outside the enclosure, within **1 m maximum**, on twisted pair with
GND, running the bus at **100 kHz**. That is comfortably within spec. If the cabin measuring point
turns out to be several metres from the box, I2C is the wrong transport and the guide's own
fallback applies - but note that a DS18B20 gives you temperature only, and you lose the humidity
reading that justified the SHT31 in the first place. Decide the mounting point before ordering.

### I2 - 4-20 mA loop may not have enough compliance voltage

Two-wire 4-20 mA transmitters typically need 12 V minimum at the transmitter. The budget:

```
12.0 V battery (discharged)  -  0.3 V Schottky  -  2.0 V burden across 100 Ω  =  9.7 V
```

Many 0-1 m probes will drop out at that point - and the failure appears exactly when the battery is
low, which is when you care most.

**Fix, in order of preference:**

1. Feed the sensor's + from the **12 V rail upstream of the Schottky** (recovers 0.3 V), and
2. use a **50 Ω shunt instead of 100 Ω** with the PGA at ±1.024 V. This halves the burden voltage
   to 1.0 V and gives **identical current resolution** (0.625 µA/LSB either way, because the FSR
   halves along with the shunt). Two 100 Ω 0.1 % resistors in parallel make the 50 Ω.

That recovers 1.3 V of headroom for free. Check the probe's minimum supply voltage spec when
ordering.

### I3 - 1-Wire pull-up is marginal on 5 m probes

4.7 kΩ is the standard value for short buses. With ~100 pF/m of cable, a 5 m probe gives ~500 pF,
so RC ≈ 2.35 µs against a 15 µs sampling window. It will probably work, but there is little margin
for a longer run or a noisy environment.

**Fix:** buy **2.2 kΩ and 3.3 kΩ** alongside the 4.7 kΩ and pick per probe on the bench. At 2.2 kΩ
the sink current is 1.5 mA, well inside the DS18B20's 4 mA rating. Also add **100 Ω in series** in
each DATA line at the board end as cheap surge and ringing protection.

Since each probe has its own GPIO rather than sharing a bus, this is far more forgiving than a
multi-drop layout - the guide's decision to split them was the right call.

### I4 - Wi-Fi: SoftAP is dragged onto the marina's channel

The ESP32 has one radio, so in AP+STA mode **the SoftAP is forced onto whatever channel the station
connects to**. Consequence: when the marina AP changes channel - many do so automatically - the
SoftAP follows, and **every device connected to `BOOT-NETZ` is disconnected.**

Not a fault, but it has to be designed for: clients must tolerate reconnects, and any local
autopilot UI in stage 2B must not treat a dropped socket as anything other than normal. If it
becomes intolerable in practice, this is the concrete argument for the second ESP the guide already
holds in reserve.

### I5 - SeaTalk TX has no fail-safe in the design (safety)

**The guide requires that TX stays high-impedance during boot, reset and firmware faults, but
specifies no circuit that achieves it.** An ESP32 GPIO is high-impedance from power-on until the
firmware configures it - roughly 300 ms - and floating again after a crash. A bare transistor base
or a 74LS07 input floating in that window can turn the driver on and **hold the SeaTalk bus low,
which takes down the instruments and the autopilot.**

**Required:** a **10 kΩ pull-down from the driver's gate/base to GND**, so the driver is guaranteed
off whenever the ESP is not actively driving it. This is one resistor and it is the single most
important part in the stage 2B circuit.

Also: prefer a **2N7002 or BSS138 N-channel MOSFET** over the 74LS07. It is open-drain by nature,
works directly from 3.3 V logic, needs no 5 V rail, and is not an obsolete TTL part. The 74LS07 in
the referenced projects is a legacy choice, not a requirement.

### I6 - NMEA2000 transceiver must be a 3.3 V part

For stage 3: **use SN65HVD230 / SN65HVD232, not MCP2551.** The MCP2551 is a 5 V part and its RX pin
outputs 5 V logic, which would sit directly on an ESP32 GPIO rated 3.6 V absolute maximum. This is
a common and expensive mistake.

Also: the BoatHub will be a **drop off the backbone, so it must not carry a 120 Ω terminator.**
Terminators belong at the two ends of the backbone only. Isolated transceivers (ISO1050, TJA1052i)
are worth the premium here because the N2K backbone carries its own power and ground reference.

### I7 - Condensation inside a sealed enclosure

A sealed IP65/IP67 box on a boat goes through daily temperature cycles, and the air inside carries
moisture. It will condense on the coldest surface, which is usually the board. IP65 keeps spray
out; it also keeps moisture in.

**Fix:** fit a **pressure-equalisation vent membrane** (Gore-type) in the enclosure, mounted
downward, and/or conformal-coat the board. Also specify **105 °C electrolytic capacitors** rather
than 85 °C parts - inside the box at summer ambient the internal temperature will reach 55-60 °C.

### I8 - Submerged bilge probe and galvanic corrosion

Many DS18B20 probes have the stainless sheath electrically bonded to the cable screen or GND
internally. A grounded stainless probe permanently submerged in bilge water, tied to the boat's
negative, becomes part of the galvanic circuit with every other underwater metal.

**Before fitting:** measure continuity between the probe sheath and each wire. If the sheath is
connected, either accept the probe as a consumable and mount it to be swapped easily - which the
guide already recommends - or sleeve it in heatshrink. Check it at every haul-out in salt or
brackish water.

---

## 3. Minor points and firmware gotchas

| # | Point |
|---|-------|
| M1 | Four modules each carrying 10 kΩ I2C pull-ups give **2.5 kΩ effective**. That works, but **do not add external pull-ups**, and run the bus at 100 kHz rather than 400 kHz. |
| M2 | The ADS1115's input impedance **changes with the PGA setting** (6 MΩ at ±2.048 V, 3 MΩ at ±1.024 V). With a ~9 kΩ source impedance that is a ~0.15 % gain error. Harmless, but **fix the PGA setting before calibrating and never change it afterwards**, or the calibration silently becomes wrong. |
| M3 | In single-ended mode the ADS1115 only uses the positive half of the FSR - that is **15 usable bits, not 16**. Still ~62.5 µV resolution at ±2.048 V; just set expectations correctly. |
| M4 | Put a **100 nF to GND at A1 too**, not only at A0. It also acts as the charge reservoir the switched-capacitor input wants. |
| M5 | A buzzer on GPIO21 draws 20-30 mA against a 40 mA absolute / 20 mA recommended GPIO limit. **Drive it through a small transistor**, not directly. |
| M6 | PubSubClient's **default buffer is 256 bytes** and the example telemetry JSON is ~250. Call `setBufferSize()` or messages will be silently dropped as they grow. |
| M7 | These are DS18B20 **clones**. Check the ROM family code is 0x28 and **verify the CRC on every read**, rejecting bad frames rather than trusting them. Also meter out the wire colours before connecting - red/black/yellow is common but not universal. |
| M8 | Use a **single-point (star) ground** in the box. In particular the 100 Ω shunt's ground must return **directly** to the ADS1115 GND - any shared return current shows up as measurement error. |
| M9 | ~~Confirm the DevKit is the WROOM-1U variant.~~ **Superseded by M9a** - the board is neither variant. The rule that survives: **never transmit without an antenna fitted**, which becomes live only once the jumper is moved to the external position. |
| M10 | For a permanent install, the DevKit's power LED and USB-UART bridge are wasted current (see B1) and can be removed. |
| M11 | TLS certificate validation fails if the clock is wrong, so **NTP must sync before the first MQTT connect**. NTP over UDP needs no TLS, so there is no chicken-and-egg problem - just ordering. |

---

## 4. Missing parts by stage

### Stage 1 - additions

| Part | Qty | Why | Finding |
|------|-----|-----|---------|
| 2.2 kΩ and 3.3 kΩ resistors | 3-4 each | alternative 1-Wire pull-ups for the 5 m probes | I3 |
| 100 Ω resistors | 3-4 | series protection in the DS18B20 DATA lines | I3 |
| 100 Ω 0.1 % (second piece) | 1 | paralleled to 50 Ω for the 4-20 mA shunt | I2 |
| Twisted-pair or shielded cable, ≤1 m | 1 | I2C run to the SHT31 outside the box | I1 |
| Pressure-equalisation vent membrane | 1 | condensation | I7 |
| Small-signal transistor + 1 kΩ + 10 kΩ | 1 set | buzzer driver, only if the buzzer is fitted | M5 |
| Electrolytics specified at 105 °C | - | specification, not an extra part | I7 |
| DC/DC with a **specified** low quiescent current | 1 | only if there is no shore power | B1 |

### Stage 2 / 2B - SeaTalk, concrete list

The guide leaves this as "resistors / Zener / protection parts". Concretely, the RX and TX stages
need:

| Part | Qty | Purpose |
|------|-----|---------|
| PC817 optocoupler (or 6N137 if faster edges are wanted) | 1-2 | galvanic isolation of SeaTalk RX; PC817's ~4 µs edges are fine against a 208 µs bit time at 4800 baud |
| 1-2 kΩ resistor | 1 | LED series resistor on the SeaTalk side of the opto |
| 10 kΩ resistor | 1 | pull-up on the ESP side of the opto output |
| 2N7002 or BSS138 N-MOSFET | 1 | SeaTalk TX open-drain driver, replaces the 74LS07 |
| **10 kΩ resistor** | 1 | **gate pull-down - the fail-safe from I5, not optional** |
| 100-470 Ω resistor | 1 | series resistor in the TX drain line |
| SMBJ15A or similar TVS | 1 | protection on the SeaTalk DATA line |
| 3-pole screw terminal 5.08 mm | 1 | already in the parts list |

**Firmware, plan for this now:** SeaTalk1 is 4800 baud with a **9th command bit**, and the
**ESP32 UART has no 9-bit mode** - it supports 5-8 data bits plus even/odd parity. The established
workaround is to configure 8 data bits with parity enabled and recover the 9th bit from the parity
result: a parity error means the 9th bit is the opposite of what the configured parity implies. The
alternative used by several SeaTalk projects is bit-banging via `ESPSoftwareSerial`. Either way
this needs proving on the bench with a logic analyser **before** the interface hardware is
finalised - it may influence whether GPIO15 needs to be an RMT-capable pin. It is the largest
unknown in stage 2.

### Stage 3 - NMEA2000

| Part | Qty | Purpose |
|------|-----|---------|
| SN65HVD230 breakout (3.3 V) | 1 | TWAI transceiver - **not** MCP2551 (I6) |
| or ISO1050 / TJA1052i | 1 | isolated alternative, preferred for N2K |
| N2K drop cable + T-piece | 1 | backbone connection |
| **No** 120 Ω terminator | - | the BoatHub is a drop, not a bus end (I6) |

---

## 5. Validated as correct

Worth recording, so these do not get re-litigated later:

| Item | Check |
|------|-------|
| Pin plan | Correctly avoids strapping pins GPIO0/3/45/46, USB GPIO19/20, and GPIO33-37 for octal PSRAM on the N16R8. All of GPIO4-18 and GPIO21 are broken out on the DevKitC-1. |
| 82 kΩ / 10 kΩ divider | Factor 9.2 confirmed. Even at the TVS clamping voltage of 27.7 V the tap reaches only 3.01 V - below VDD and below the 3.6 V absolute maximum. **The extra headroom the guide chose is what makes the upstream tap in B3 safe.** Good call. |
| 1.5KE20A selection | 17.1 V standoff, 19.0-21.0 V breakdown, 27.7 V clamp at 54 A, unidirectional. Correct for a 12 V system charging at 14.4 V - high enough not to leak while charging, low enough to protect. |
| SHT31 powered from 3.3 V | Confirmed against the Adafruit breakout: 10 kΩ pull-ups go to Vin and there is no regulator or level shifter, so Vin **must** be 3.3 V or the ESP pins would see 5 V. The guide's reasoning is exactly right. |
| SHT31 address 0x44 with ADDR open | Correct on the Adafruit board, which has a 10 kΩ pull-down on ADDR. **Verify on a clone** - a bare SHT3x must not have ADDR floating. |
| ADS1115 addresses 0x48/0x49/0x4A | Achievable: ADDR to GND / VDD / SDA respectively. |
| 100 Ω shunt maths | 4 mA → 0.4 V, 20 mA → 2.0 V correct; fits ±2.048 V FSR, though at 98 % of range (see I2). |
| One GPIO per DS18B20 | Right decision for three 5 m probes - easier to fault-find, and one dead sensor cannot take the others with it. |
| 2 A fuse | Correct marine practice: the fuse protects the **cable**, not the load. 0.5-0.75 mm² is good for far more, so 2 A is conservative and fine. |
| LittleFS with RAM buffering | Correct choice over SPIFFS - power-fail safe. At ~50 bytes per point every 6 s, a 4 MB partition holds ~130 hours of track and the write volume is nowhere near the flash endurance limit. |
| TWAI for NMEA2000 | Correct - the ESP32-S3 TWAI controller is CAN 2.0B / ISO 11898-1 compatible, and N2K is CAN at 250 kbit/s. |
| Outbound-only server connection | Correct, and the right call. No inbound ports, no autopilot path from the internet. |
| USB and external 5 V not simultaneously | Correct - Espressif treats USB, the 5 V pin and 3V3 as alternative supply paths. |

---

## 6. Open questions

Answered 2026-09-13.

| # | Question | Answer | Follow-up |
|---|----------|--------|-----------|
| 1 | Is there shore power / a charger at the berth? | **Yes - the boat is permanently on shore power while unattended.** | B1 resolved: continuous operation, no duty cycling. But see B1a below - this changes what the battery voltage *means*. |
| 2 | How far is the SHT31 mounting point from the enclosure? | Not fixed yet; it will sit **outside the box and outside the cabinet** that holds the S1. | Keep the I2C run to **2 m maximum** and stiffen the pull-up - see I1a below. Fix the exact point before the cable gland is drilled. |
| 3 | Minimum supply voltage of the 4-20 mA probe? | Unknown - the probe is not bought yet. | Becomes a **purchase criterion**: pick a probe specified from 9-10 V up. Default to the **50 Ω shunt** regardless, which makes the question far less critical. |
| 4 | Is the DevKit a WROOM-1**U** with a U.FL connector? | **Neither.** It is a third-party module (sparkleIoT XH-S3E) carrying **both** a PCB antenna and a U.FL socket, selected by a solder jumper - and it ships set to the PCB antenna. | Resolved in [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md). The supplied external antenna **does nothing until the jumper is moved** - see M9a. |
| 5 | Is the bilge probe's stainless sheath bonded to GND internally? | Unknown. | **Bench check with a multimeter** - see below. Now more urgent, see I8a. |

### M9a - the external antenna is not connected as delivered (new, supersedes M9)

The original M9 assumed the board is either a WROOM-1 (PCB antenna only) or a WROOM-1U (U.FL only),
and that a bundled external antenna implied a -1U. **Both assumptions were wrong.**

The delivered module has a PCB antenna *and* a U.FL socket, with a solder jumper choosing between
them, and the seller states it ships set to the PCB antenna: an external antenna at position ②
*"must be soldered by yourself"*.

So the bundled SMA antenna is **inert out of the box** - plugging the pigtail in changes nothing.
Using it means moving a 0402/0603 solder blob in the RF path, and afterwards the antenna must
always be fitted before transmitting.

The guide's assumption that an external antenna would be mounted high in the compartment is
therefore **not free**. Decision and measurement procedure: see
[A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) section 4 - assemble with the onboard
antenna, measure RSSI at the real mounting point, and only rework if it falls short.

### How to answer 5 on the bench

**Q5 - probe sheath.** Multimeter on continuity. One lead on the stainless sheath (scratch through
the oxide layer to get a real contact), the other on each of the three wires in turn, GND included.
Continuity to any wire means the sheath is bonded.

### B1a - what shore power changes (new)

Resolving Q1 does not just remove the blocker, it **inverts what the battery measurement is for**.

With a charger running, the battery sits at float or absorption - roughly 13.2-14.4 V. **State of
charge cannot be read from that**, because the charger, not the battery, is setting the voltage.
What the measurement now delivers instead is the single most valuable alarm in the whole system:

> Voltage falling from ~13.5 V to ~12.7 V means **the charger stopped** - shore power lost, RCD
> tripped, or someone pulled the cable.

That is worth more than the original trend monitoring, and it only works because of fix B3: a
drifting ±80 mV error would sit right on top of the charging/on-battery decision threshold, which
is only about 0.3 V wide. The threshold values themselves live in
[B-001-power-supply.md](B-001-power-supply.md) and are tuned to the AGM bank - they are deliberately
not repeated here, so they cannot drift apart.

The runtime estimate from B1 also flips from a warning into a specification: after shore power
fails, the BoatHub keeps monitoring **for roughly four to six weeks** before it becomes a burden on
the battery. The low-voltage cutoff is still needed, but as a backstop rather than a normal
operating mode - see [B-001-power-supply.md](B-001-power-supply.md).

### I1a - how long the I2C run to the SHT31 may actually be (new)

The original I1 said "1 m maximum" as a conservative guess. With the mounting point now known to be
outside the cabinet, here is the real number.

Four modules each carrying 10 kΩ pull-ups give **2.5 kΩ effective**, which is a strong pull-up.
Against the 100 kHz limit of 1000 ns rise time, with roughly 50 pF of module and trace capacitance
plus ~100 pF per metre of cable:

| Cable length | Bus capacitance | Rise time | Verdict |
|--------------|-----------------|-----------|---------|
| 2 m | ~250 pF | ~530 ns | comfortable |
| 3 m | ~350 pF | ~740 ns | fine |
| 5 m | ~550 pF | ~1165 ns | **over the limit** |

So **up to about 3 m needs nothing but care**. To reach 5-6 m, add one **3.3 kΩ pull-up pair** on
SDA and SCL in the box: that brings the bus to ~1.4 kΩ, cuts the rise time at 5 m to ~660 ns, and
still only asks 2.3 mA of the drivers - inside the 3 mA every device on this bus is specified for.
Beyond that a P82B715 bus extender pair is the correct answer, not a stiffer resistor.

Wiring rules for the run:

- **4 conductors:** 3.3 V, GND, SDA, SCL - the SHT31 is powered over the same cable, and its 1.5 mA
  makes voltage drop irrelevant.
- Twist **SDA with a ground wire and SCL with a ground wire**, not SDA against SCL - twisting the
  two signals together couples them into each other.
- Shield, if used, to GND **at the box end only**, so it does not become a ground loop.
- **100 nF between 3.3 V and GND at the sensor end**, for local decoupling at the far end of a cable.
- Route clear of the tiller pilot's motor cables, as the guide already requires.

Run the bus at **100 kHz**. There is nothing on it that benefits from 400 kHz, and every number
above doubles in difficulty if you do.

### I8a - shore power raises the stakes on the bilge probe (new)

With the boat permanently connected to shore power, the boat's negative is tied to shore earth. A
grounded stainless probe permanently submerged in the bilge is then part of a galvanic circuit that
now includes the shore connection, which **accelerates corrosion** compared with a boat lying
unconnected. Q5 moves from "worth checking" to "check before fitting".

---

## 7. References

- [ESP32-S3 datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
- [ESP32-S3-DevKitC-1 hardware reference](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
- [ESP-IDF Wi-Fi API - AP+STA channel behaviour](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html)
- [TI ADS1115 datasheet](https://www.ti.com/product/ADS1115)
- [Analog Devices DS18B20](https://www.analog.com/en/products/ds18b20.html)
- [Adafruit SHT31-D breakout pinout](https://learn.adafruit.com/adafruit-sht31-d-temperature-and-humidity-sensor-breakout/pinouts)
- [Littelfuse 1.5KE series datasheet](https://www.mouser.com/datasheet/2/395/1_5KE_2520SERIES_O2104-3402913.pdf)
- [ESP-IDF issue 9569 - reading the received parity bit](https://github.com/espressif/esp-idf/issues/9569)
- [Arduino forum - Raymarine SeaTalk 9-bit send and receive](https://forum.arduino.cc/t/raymarine-seatalk-9-bit-sending-AND-receiving/1162764)
- [APRemote - ESP32 + SeaTalk1 autopilot](https://github.com/richardJG/APRemote)
- [Seatalk Autopilot Remote Control - open-collector reference](https://github.com/AK-Homberger/Seatalk-Autopilot-Remote-Control)
