# A-002 - Bench setup on USB power

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | yes (breadboard only) |

> Design documents are numbered in creation order. This one is **the first thing to build** -
> see the phase order in [../ROADMAP.md](../ROADMAP.md).

## 1. Goal

Get the complete sensor set and the whole network stack working on a breadboard, powered from USB
alone. **No 12 V exists at this point** - not on the bench, not in the enclosure, nowhere.

The 12 V supply from [B-001-power-supply.md](B-001-power-supply.md) is built at the *end* of the bench
phase, once everything else is stable.

**You need one thing that is not on the bench:** an MQTT broker reachable over TLS, with its
certificate in place, before the server uplink step. Everything before that runs without it.

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
- **Use the real 5 m probe cables for the DS18B20 pull-up test.** With short jumpers the
  cable capacitance that makes 4.7 kΩ marginal simply does not exist, so a short-cable test proves
  nothing about the installed system. Try 4.7 kΩ, 3.3 kΩ and 2.2 kΩ with the full cable length.

### The I2C bus

All four modules - the SHT31 and the three ADS1115 - carry 10 kΩ pull-ups to their own supply pin,
which puts roughly **2.5 kΩ on the bus**. That is already a strong pull-up, so:

- **Do not add external pull-ups.**
- The BME680 breakout usually brings a pair of its own, the IMU board none. Leave the BME680's
  fitted: with them the bus still stays above the 1.1 kΩ at which the 3 mA every device here can
  sink runs out.
- Run the bus at **100 kHz**. Nothing on it benefits from 400 kHz, and every figure below doubles
  in difficulty if you do.
- Set the ADS1115 addresses one module at a time and confirm with an I2C scanner:
  ADDR→GND = 0x48, ADDR→VDD = 0x49, ADDR→SDA = 0x4A.
- The SHT31 answers at **0x44** with its address pin low and **0x45** with it high, so two of them
  fit on this bus. Breakouts often abbreviate the pin to `AD` and put it on the back of the board
  next to `AL`, the alert output; `AL` can be left unconnected.
- Most breakouts carry a 10 kΩ pull-down on the address pin, so leaving `AD` open gives **0x44**.
  Check yours: the resistors sit on the back, and a `103` next to the `AD` hole is that pull-down.
  For 0x45, tie `AD` to 3.3 V - a hard tie wins against the pull-down.
- The datasheet wants the address pin at a defined level, so on a board **without** that pull-down
  it must be tied by hand. A floating address pin gives a sensor that answers sometimes.
- The BME680 answers at **0x76** with `SDO` tied to ground. Its `CS` is tied hard to 3.3 V as well:
  one low edge on it switches the chip to SPI until its power is removed. Both links are made on
  the module - [A-010](A-010-bme680-barometer-and-battery-bay.md).

**Power the SHT31 from 3.3 V, never 5 V.** Its pull-ups go to the supply pin, and typical breakouts
carry neither a regulator nor a level shifter, so a 5 V feed puts 5 V onto ESP32 pins rated 3.6 V
absolute maximum.

#### How long the cable runs may be

The SHT31 measures cabin climate, so it cannot live inside the sealed enclosure: the box runs
10-12 °C above ambient, and because relative humidity depends on temperature, a few degrees of
error become several %RH of error. It has to sit outside, which means I2C over a cable. The BME680
in the battery bay sits outside for the same reason, on a run of about 15 cm
([A-010](A-010-bme680-barometer-and-battery-bay.md)).

**What counts is the total of every cable on the bus** - the SHT31's run, the BME680's and the IMU
cable inside the box. Capacitance adds up whichever terminal a cable hangs off.

Against the 100 kHz limit of 1000 ns rise time, with ~50 pF of module and trace capacitance plus
~100 pF per metre of cable:

| Total cable length | Bus capacitance | Rise time | Verdict |
|--------------------|-----------------|-----------|---------|
| 2 m | ~250 pF | ~530 ns | comfortable |
| 3 m | ~350 pF | ~740 ns | fine |
| 5 m | ~550 pF | ~1165 ns | over the limit |

**Up to about 3 m in total needs nothing but care.** To reach 5-6 m, add one **3.3 kΩ pull-up
pair** on SDA and SCL inside the box: that takes the bus to ~1.4 kΩ, cuts the rise time at 5 m to
~660 ns, and still asks only 2.3 mA of the drivers - inside the 3 mA every device here is specified
for. Beyond that the correct answer is a P82B715 bus extender pair, not a stiffer resistor.

Wiring rules for the runs:

- **One cable per sensor**, each into J2 on the main board: 3.3 V, GND, SDA, SCL. The sensor is
  powered over its own cable; at a milliamp or two the voltage drop is irrelevant.
- Twist **SDA with a ground wire and SCL with a ground wire** - not SDA against SCL, which couples
  the two signals into each other.
- A shield, if the cable has one, goes to GND **at the box end only**, so it cannot become a ground
  loop.
- **100 nF between 3.3 V and GND at the far end of the SHT31's run**, for local decoupling at the
  end of a long cable. The BME680's 15 cm need nothing beyond the module's own.
- Route clear of the tiller pilot's motor cables, and the BME680's clear of the 230 V shore-power
  terminals in the battery bay.

#### The cable

Both runs are **Cat 5e or Cat 6 patch cable**: four twisted pairs of stranded copper, round, and
in most drawers already. Each run is wired the same way:

| Pair | Core | Signal | J2 pole |
|------|------|--------|---------|
| brown | brown | 3.3 V | 1 |
| | white | GND | 2 |
| orange | orange | SCL | 3 |
| | white | GND | 2 |
| blue | blue | SDA | 4 |
| | white | GND | 2 |
| green | both | **cut off at both ends** | |

In these cables the white partner of each pair is plain white, so the three cannot be told apart -
and they need not be: **all three are ground.** That gives the supply and each signal a ground of
its own in its twisted pair, which is exactly the rule above. The green pair is cut off flush with
the jacket at both ends.

- **One ferrule per core per cable at J2:** 0.25 mm² on brown, orange and blue, and one 0.5 mm²
  over the three whites twisted together. With both runs fitted each pole of J2 holds two
  ferrules, one from each cable. **Pull-test every ferrule** after tightening - a shared pole can
  hold one firmly and the other barely.
- **Stranded, copper and round.** Solid installation cable breaks at a joint under vibration, `CCA`
  takes solder badly, and a flat patch cable does not seal in a cable gland. The outer diameter has
  to sit within the gland's 3.5-7 mm.
- Each run leaves the box through **its own gland**. A gland seals around one cable, not two.

The IMU's cable inside the box is the same patch cable, wired the same way, with its interrupt on
the green core - [A-009](A-009-imu-heel-and-motion.md). On every cable in this system **brown is
3.3 V and white is only ever ground.**

Write the scheme on the inside of the enclosure lid. Somebody will be looking at these wires years
from now, in a dark locker, with a torch.

#### Wire colours on the bench

On the breadboard the sensors hang on jumpers or ready-made I2C cables. There is no standard, but
Qwiic and STEMMA QT agree, and ready-made I2C cables are wired that way:

| Conductor | Colour |
|-----------|--------|
| `GND` | black |
| 3.3 V | red |
| `SDA` | blue |
| `SCL` | yellow |

**Grove uses a different scheme** - yellow is `SDA` there and white is `SCL`. Yellow therefore means
two different things depending on where the cable came from. Meter any ready-made cable before
trusting it, the same discipline the probe wiring needs in
[A-003](A-003-ds18b20-temperature-sensors.md).

## 4. Testing the ADS1115 without 12 V

The ADC still has to be proven, just not against a battery.

Three properties to settle before calibrating anything:

- The input impedance **changes with the PGA setting** - 6 MΩ at ±2.048 V, 3 MΩ at ±1.024 V.
  Against a ~9 kΩ source impedance that is a ~0.15 % gain error. **Pick the PGA once and never
  change it afterwards**, or an existing calibration silently becomes wrong.
- In single-ended mode only the positive half of the full-scale range is used, so this is **15
  usable bits, not 16**. Still ~62.5 µV per step at ±2.048 V - just set expectations correctly.
- Put **100 nF to GND at every input you use**, not only at A0. It doubles as the charge reservoir
  the switched-capacitor input wants.

Two levels of test:

### Level 1 - always possible, no bench PSU

Build a 2:1 divider from the 3.3 V rail (two equal resistors, e.g. 10 kΩ) and measure the midpoint.

- expected ~1.65 V, verified against the multimeter
- proves I2C, addressing, PGA selection and conversion
- a 10 kΩ potentiometer across the rail is even better - sweep it and check linearity end to end

### Level 2 - if an adjustable bench supply is available

The **battery divider is a separate circuit from the power supply**, and can be tested on its own
long before the protection board exists. Build just the divider on the breadboard:

```
bench PSU +  --- 100 kOhm ---+--- 1 kOhm --- ADS1115 A0
                             |
                          10 kOhm      +-- 100 nF -- GND
                             |
                            GND
```

Sweep the PSU from 11 V to 15.5 V and confirm the ADC tracks linearly. That gets the calibration
factor established early, and the reading can then be cross-checked once the real supply is built.

> **Safety.** This is the one step in phase A with a voltage that can destroy the ESP. Build the
> divider on a **separate part of the breadboard**, apply the PSU, and **verify with a multimeter
> that the tap really sits near 1.1 V at 12 V in, and never above 1.5 V, before connecting
> anything to the ADS1115.** A slipped
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
- [ ] Each DS18B20 detected individually, family code 0x28, **CRC verified on every read**
- [ ] DS18B20 wire colours metered out before connecting - red/black/yellow is common, not universal
- [ ] Pull-up value chosen using the **full 5 m cables**
- [ ] SHT31 responds at 0x44, plausible temperature and humidity
- [ ] All three ADS1115 on 0x48 / 0x49 / 0x4A via I2C scanner
- [ ] ADS1115 reading matches the multimeter on a known divider
- [ ] PGA fixed at the value that will be used in service
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

When phase B is built, the transition is short:

1. Flash the final firmware **over USB**.
2. With everything switched off, seat the DevKit in the main board.
3. Connect the 12 V side.

USB may stay connected through all of it. USB and the 5V pin each feed the DevKit through a diode
of its own, so they can supply it at the same time and neither drives the other
([A-001](A-001-devkit-and-carrier.md) section 5). On the main board the converter's output is wired
to the 5V pin, and that pin's diode is what keeps a DevKit on USB from driving the converter
backwards - which RECOM warns can damage it while it is unpowered ([B-001](B-001-power-supply.md)).
The protection depends on the DevKit's IN-OUT solder pad staying open, and it is measured before U2
is soldered: the board is first brought up on USB without the converter - see
[B-002](B-002-main-board.md) section 9.

**3V3 is never fed from outside.** It is the regulator's output, not an input with a diode in
front of it, and 5 V there reaches the module directly. This is the rule most likely to be broken
in a moment of impatience, and it is the one that costs a board.

## 9. References

- [B-001-power-supply.md](B-001-power-supply.md) - what gets built after this phase
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - ports, pin mapping, terminals to avoid
