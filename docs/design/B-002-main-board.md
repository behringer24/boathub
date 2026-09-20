# B-002 - Main board

| | |
|---|---|
| **Phase** | B |
| **Software version** | - |
| **Touches hardware** | yes |

## 1. Goal

One fabricated board carrying everything between the 12 V supply and the sensors: the protection
and conversion from [B-001](B-001-power-supply.md), the battery divider, the 1-Wire passives from
[A-003](A-003-ds18b20-temperature-sensors.md), the I2C distribution, the SeaTalk stages from
[D-001](D-001-seatalk-rx-stage.md) and [D-003](D-003-seatalk-tx-stage.md), and a socket for the
DevKit.

It **replaces the bundled carrier**, which breaks out the GPIO but carries no circuitry of its own.
Everything the carrier does, this board does as well, and it does the rest too - so there is no
reason to keep both in the enclosure.

This document is the **logical layout**: every component, every net, every connector pin. Physical
placement and routing are not prescribed here; they follow from the enclosure.

**Reference designators are the ones in the KiCad project** - see section 8. A design document
whose numbering disagrees with the board it describes turns every check into a translation, so
where the two differed, the board won.

**Out of scope:** the sensor breakouts, which are bought assembled, and the channel allocation of
the second converter, which waits on its own document - section 10.

## 2. The DevKit stays in a socket

The module could sit on the board directly, as a bare ESP32-S3-WROOM-1. It does not, and the
reasons are worth writing down because the opposite choice looks tempting.

Soldering the module down means the board also has to provide everything the DevKit quietly
supplies: the 3.3 V regulator, decoupling at the module, the EN pull-up with its RC delay, reset
and BOOT buttons, the strapping resistors, the USB path for flashing, and the RGB status LED the
firmware drives. That is roughly fifteen additional parts, each of which is a reason a board does
not boot - and none of which has ever been debugged in this project, because the DevKit was always
correct.

Against that, a socket costs: board area, a few millimetres of height, and the DevKit's own
quiescent draw for its regulator, USB bridge and power LED - some 10-18 mA, as
[B-001](B-001-power-supply.md) accounts for.

On permanent shore power that draw is irrelevant, and a DevKit that dies is a part swap rather than
a rework. **Socket it.**

### What the socket must respect

| | |
|---|---|
| Row spacing | **25.4 mm**, 2 x 22 pins at 2.54 mm. That is 10 pitches, a full inch - not the 22.86 mm that a DevKitC-1 of the original width would give. Confirm it by counting: **nine free grid positions** between the two rows |
| Orientation | **pin 1 of both sockets sits at the antenna end.** J7.1 is the first of the `3V3` pair, J11.1 is the `GND` opposite it; `5V` and `GND` land on pins 21 and 22 at the USB end - see [A-001](A-001-devkit-and-carrier.md) section 3 |
| Antenna | **no copper pour beyond pin 1.** That is where the module's antenna sits; a ground plane beneath it detunes the antenna, and the board would be the reason for poor range |
| USB sockets | both DevKit USB connectors must stay reachable at the board edge beside pin 22 |
| Buttons | the DevKit's own BOOT and RESET buttons must stay pressable with the board installed |

The orientation line is the one to get right. Both sockets run in the same direction, pin 1 to pin
22 from the antenna end towards the USB end. One socket footprint placed 180° out puts 5 V where
GND belongs, and nothing in the netlist can catch it - the ratsnest is just as happy either way.

**Measure the actual DevKit before the board is ordered.** [A-001](A-001-devkit-and-carrier.md)
establishes that boards sold under this description vary, and a socket is unforgiving: a row
spacing or a pin order that is out by one is a scrapped run.

## 3. What the board carries

| On the board | Why there |
|--------------|-----------|
| TVS, reverse-polarity diode, bulk and bypass capacitors, DC/DC module | the 12 V entry has to be protected before anything else sees it |
| Battery divider and its series resistor | the tap must sit ahead of the reverse-polarity diode |
| Three 1-Wire pull-ups and series resistors, with detachable probe terminals | they belong at the cable end, not at the GPIO - section 6 |
| Sockets for two ADS1115 breakouts, one left empty | the battery channel is the one whose use is defined; the rest come out on a socket header |
| The 4-20 mA burden for the bilge level channel | it is the only analogue channel with a defined sender |
| I2C distribution and the IMU interrupt line | so the bus has one origin instead of a daisy chain of jumpers |
| The SeaTalk receive and transmit stages with their isolated ground island | one terminal, one clamp, one cable serving both directions |

| Off the board | Where instead |
|---------------|---------------|
| 2 A fuse | inline, **close to the supply** - it protects the cable, not the load |
| SHT31 | outside the enclosure entirely, on its own cable ([A-008](A-008-sht31-cabin-climate.md)) |
| IMU | inside the enclosure, bolted down, on a five-core cable ([A-009](A-009-imu-heel-and-motion.md)) |
| Conditioning for any future analogue sender | on a small adapter plugged into J1, designed when that sender is known |

**The unallocated GPIO do not come out on headers.** An earlier revision put all of them on pin
headers, on the argument that breaking out the GPIO is what made the carrier useful. The board
filled up first. The consequence is worth stating plainly rather than leaving to be discovered:
`IO7`, `IO17`, `IO18` and `IO21` terminate at a socket pin and nothing else, so reaching one later
means soldering to that pin with the DevKit lifted out. Section 10 keeps the question open, because
it is not a decision that can be revisited after the board is made.

## 4. Components

Values and rationale are in [B-001](B-001-power-supply.md) section 3,
[A-003](A-003-ds18b20-temperature-sensors.md) section 3 and [D-001](D-001-seatalk-rx-stage.md).
This table is the build list; [MATERIAL.md](../MATERIAL.md) is the orderable one, with Reichelt
codes.

### Power entry and conversion

| Ref | Value | Function |
|-----|-------|----------|
| D1 | 1.5KE20A | transient clamp, unidirectional, 17.1 V standoff |
| C1 | 100 nF / 50 V | HF bypass at the input |
| D2 | 1N5822 | reverse polarity, 3 A / 40 V Schottky |
| C3 | 100 µF / 35 V, 105 °C | bulk at the DC/DC input |
| U2 | RECOM **R-78K5.0-1.0** | DevKit supply. 6.5-36 V in, 5 V / 1 A out, 1 mA quiescent, SIP-3 with three pins at 2.54 mm |
| C5 | 470 µF / 16 V, 105 °C | bulk at the 5 V output |
| C4 | 100 nF / 50 V | HF bypass at the output |

### Battery measurement

| Ref | Value | Function |
|-----|-------|----------|
| R7 | 100 kΩ 0.1 % | battery divider, top leg |
| R8 | 10 kΩ 0.1 % | battery divider, bottom leg |
| R9 | 1 kΩ | series into ADS1115 A0 - **do not omit, and keep it in series with the pin.** The last barrier if the divider faults ([B-001](B-001-power-supply.md)) |
| C2 | 100 nF / 50 V | at A0 to GND, also feeds the converter's switched-capacitor input |

### 1-Wire

| Ref | Value | Function |
|-----|-------|----------|
| R1, R3, R5 | 2.0 kΩ | pull-ups, one per probe - engine, bilge, fridge in that order |
| R2, R4, R6 | 100 Ω | series protection, one per probe, same order |

### Analogue channels

| Ref | Value | Function |
|-----|-------|----------|
| R22 | 100 Ω | the 4-20 mA burden, from J12.1 to GND. 0.4-2.0 V out of 4-20 mA |
| R15 | 1 kΩ | series into ADS1115 A1, the bilge level channel |
| C6 | 100 nF / 50 V | at A1 to GND |
| R21, R20 | 1 kΩ | series into ADS1115 A2, A3 |
| C12, C11 | 100 nF / 50 V | at A2, A3 - **pads only, not fitted** |
| R19, R18, R17, R16 | 1 kΩ | series into the second converter's A0-A3. **Fitted**, although the module is not - the resistor is the protection, and the moment it is needed is the moment somebody plugs a module in |
| C7, C8, C9, C10 | 100 nF / 50 V | at the second converter's A0-A3 - **pads only, not fitted** |

### SeaTalk

| Ref | Value | Function |
|-----|-------|----------|
| D3 | 1.5KE20A | transient clamp on `ST_DATA`, cathode to `ST_DATA` |
| R10 | 4.7 kΩ | series into the optocoupler's LED, on the instrument side |
| U1 | PC817 | the isolation itself |
| R11 | 10 kΩ | pull-up on the receive output, on the board side |
| Q1 | BC337-25 | transmit stage, pulls the bus low |
| R12 | 1 kΩ | base resistor from IO16 |
| R13 | 10 kΩ | base to emitter - **the safety part.** It holds Q1 off while IO16 floats, which it does from power-on until the firmware claims the pin |
| R14 | 100 Ω | collector, limits the current into the bus |
| JP2 | wire link | joins `ST_GND` to `GND`. Closed for transmitting - see [D-003](D-003-seatalk-tx-stage.md) |

### Mechanical

| Ref | Function |
|-----|----------|
| H1-H4 | M3 mounting holes, **pads tied to `GND`** - metal standoffs then become part of the return, plastic ones change nothing |

### Connectors and sockets

| Ref | Pins | Carries | Type |
|-----|------|---------|------|
| J7, J11 | 22 each | the DevKit, left and right rows | **socket strip**, 2.54 mm |
| J9 | 2 | 12 V in, already fused | **screw terminal** |
| J16 | 4 | probe `MOTOR-T`, one pole spare | **screw terminal** |
| J15 | 4 | probe `BILGE-T`, one pole spare | **screw terminal** |
| J14 | 4 | probe `FRIDGE-T`, one pole spare | **screw terminal** |
| J2 | 4 | SHT31: 3.3 V, GND, SCL, SDA | **screw terminal** |
| J4 | 6 | IMU: 3.3 V, GND, SDA, SCL, INT, one pole spare | **screw terminal** - it is on the I2C bus |
| J12 | 2 | bilge level sender: the loop return and +12 V | **screw terminal** |
| J17 | 4 | SeaTalk: `ST_GND`, `ST_DATA`, the bus 12 V parked, one spare | **screw terminal** |
| J6 | 10 | ADS1115 breakout, 0x48 | socket strip |
| J10 | 10 | ADS1115 breakout, 0x49 - **socket left empty** | socket strip |
| J1 | 8 | the spare analogue channels: 3.3 V, A2 and A3 of J6, A0-A3 of J10, GND | socket strip |

J1 is a **socket** rather than a pin header, so the exposed pins sit on whatever adapter plugs into
it instead of standing upright on the board. A board covered in bare pins is a board that shorts
against a lid.

A connector is chosen by **what a bad contact costs**, not by which side of the enclosure wall it
sits on.

- **Anything cabled onto the I2C bus gets a screw terminal.** A contact that degrades there does
  not merely lose its own sensor: SDA held low takes the SHT31 and both converters with it. That is
  out of all proportion to the one device at fault, and it is why the IMU *inside* the box is
  terminated the same way as the probes outside it.
- **A single analogue channel, or a spare, gets a socket.** A bad contact costs exactly the one
  measurement, and it is easy to find.
- A socket onto which a board plugs directly is not a cabled connection and has nothing to work
  loose.

Nothing that has to come off for service is soldered down - the probe cables, the SHT31 run, the
IMU cable and the SeaTalk cable all do.

**The terminal footprints are Metz Connect, the parts are CamdenBoss.** `CTB0509` at 5.08 mm has
the same single row of pads, and the Metz footprint was the geometric match available in the
library. Nobody should "correct" the footprint to the manufacturer on the order - what had to match
is the pitch and the drill, and it does.

## 5. The nets

This is the complete electrical description of the board. Net names are as the KiCad project
exports them; several are auto-generated from the pin they begin at, so the meaning is given
beside.

### Power

| Net | Nodes |
|-----|-------|
| `GND` | J7.22, J11.1, J11.21, J11.22, J9.1, D1.2, C1.1, C2.1, C3.2, C4.1, C5.2, U2.2, R8.2, C6.1, C7.1, C8.1, C9.1, C10.1, C11.1, C12.1, R22.1, J14.3, J15.3, J16.3, J2.2, J4.2, J6.2, J6.5, J10.2, J1.8, U1.3, Q1.3, R13.1, JP2.2, H1.1, H2.1, H3.1, H4.1 |
| `+12V_FUSED` | J9.2, D1.1, C1.2, R7.1, D2.2, J12.2 |
| `+12V_PROT` | D2.1, C3.1, U2.1 (+VIN) |
| `+5V` | U2.3 (+VOUT), C5.1, C4.2, J7.21 |
| `3.3V` | J7.1, J7.2, R1.2, R3.2, R5.2, J14.2, J15.2, J16.2, J2.1, J4.1, J6.1, J10.1, J10.5, J1.1, R11.1 |

Read the power chain off the net names: the clamp and the battery tap sit on the **fused** net,
ahead of the diode; everything downstream of the diode is on `+12V_PROT`.
[B-001](B-001-power-supply.md) explains why that order and not another. The 3.3 V rail is produced
by the DevKit's own regulator and distributed from the socket - the board consumes it, it does not
make it.

### Measurement and bus

| Net | Nodes | Meaning |
|-----|-------|---------|
| `VBAT_SENSE` | R7.2, R8.1, R9.1 | the divider tap |
| `ADS_A0` | R9.2, C2.1, J6.7 | battery voltage at the converter |
| `Net-(J12-Pin_1)` | J12.1, R22.2, R15.1 | the bilge loop return, across the burden |
| `Net-(J6-Pin_8)` | R15.2, C6.2, J6.8 | bilge level at A1 |
| `Net-(J6-Pin_9)` | R21.1, C12.2, J6.9 | A2 |
| `Net-(J6-Pin_10)` | R20.1, C11.2, J6.10 | A3 |
| `Net-(J1-Pin_2)`, `Net-(J1-Pin_3)` | J1.2/R21.2, J1.3/R20.2 | A2, A3 at the header |
| `Net-(J10-Pin_7)` … `Net-(J10-Pin_10)` | R19.2/R18.2/R17.2/R16.2 with C7.2/C8.2/C9.2/C10.2 and J10.7-J10.10 | the second converter's A0-A3 |
| `Net-(J1-Pin_4)` … `Net-(J1-Pin_7)` | J1.4-J1.7 with R19.1/R18.1/R17.1/R16.1 | the same four at the header |
| `SDA` | J7.12 (IO8), J2.4, J4.3, J6.4, J10.4 | |
| `SCL` | J7.15 (IO9), J2.3, J4.4, J6.3, J10.3 | |
| `Net-(J11-GPIO2)` | J11.5 (IO2), J4.5 | the IMU interrupt |

`J6.5` is the first ADS1115's ADDR pin, tied to GND for **0x48**. `J10.5` is tied to 3.3 V for
**0x49**. `J6.6` and `J10.6` are the ALRT pins, unused.

Several of these carry auto-generated names. The netlist is unambiguous either way, but a label in
the schematic would make the net-class assignment and every later reading of the ratsnest easier,
and costs nothing.

### 1-Wire

| Net | Nodes |
|-----|-------|
| `Net-(J16-Pin_1)` | R1.1, R2.2, J16.1 - engine probe, at the terminal |
| `Net-(J7-GPIO4)` | R2.1, J7.4 (IO4) |
| `Net-(J15-Pin_1)` | R3.1, R4.2, J15.1 - bilge probe, at the terminal |
| `Net-(J7-GPIO5)` | R4.1, J7.5 (IO5) |
| `Net-(J14-Pin_1)` | R5.1, R6.2, J14.1 - fridge probe, at the terminal |
| `Net-(J7-GPIO6)` | R6.1, J7.6 (IO6) |

### SeaTalk

| Net | Nodes | Meaning |
|-----|-------|---------|
| `ST_GND` | J17.1, D3.2, U1.2, JP2.1 | the instrument side's ground, an island |
| `ST_DATA` | J17.2, D3.1, R10.2, R14.1 | the bus itself |
| `Net-(R10-Pad1)` | R10.1, U1.1 | the optocoupler's LED anode |
| `Net-(J7-GPIO15)` | U1.4, R11.2, J7.8 (IO15) | receive, into the ESP |
| `Net-(J7-GPIO16)` | J7.9 (IO16), R12.2 | transmit, out of the ESP |
| `Net-(Q1-B)` | Q1.2, R12.1, R13.2 | the base, held down by R13 |
| `Net-(Q1-C)` | Q1.1, R14.2 | the collector, into R14 |

`J17.3` and `J17.4` are deliberately unconnected: pin 3 is where the cable's own 12 V core is
parked, and pin 4 is the spare pole of a four-way block. Both want a no-connect flag, or ERC
reports a pin unconnected that is unconnected on purpose.

### Deliberately unconnected socket pins

`RST`, `IO43`/`IO44` (the debug UART), `IO0`, `IO3`, `IO45`, `IO46` (strapping pins and BOOT),
`IO19`/`IO20` (USB), `IO35`-`IO37` (octal PSRAM) and `IO48` (the RGB LED) stay with the DevKit.

`IO7`, `IO17`, `IO18`, `IO21`, `IO1`, `IO10`-`IO14`, `IO38`-`IO42` and `IO47` are unallocated and
reach no connector - see the note at the end of section 3.

### Connector pin assignment

| | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 |
|---|---|---|---|---|---|
| J9 | GND | **+12 V** | | | |
| J16, J15, J14 | DATA (probe yellow) | +3.3 V (probe red) | GND (probe black) | - | |
| J2 | +3.3 V | GND | SCL | SDA | |
| J4 | +3.3 V | GND | SDA | SCL | INT |
| J12 | loop return | **+12 V** | | | |
| J17 | `ST_GND` (black) | `ST_DATA` (yellow) | bus 12 V, parked (red) | - | |
| J1 | +3.3 V | A2 | A3 | A0 of J10 | A1 of J10 |

J1 continues: pin 6 is A2 of J10, pin 7 is A3 of J10, pin 8 is GND.

The three probe connectors share one pin order, deliberately. Three identical connectors wired
three different ways is a fault waiting for the first service visit.

**J12 is two poles, not four**, because a loop-powered sender has two wires and the 2-pole of this
series is stocked - the spare pole on the probe terminals exists only because the 3-pole is not.
That leaves J12 looking exactly like J9: same part, same pitch, 12 V on one pole of each.

The remedy is placement and legend rather than a different part. J9 belongs at the supply gland,
J12 among the sensor cables, and both get the voltage written beside the pole that carries it. It
is also the milder of the two confusions available here: the supply wired into J12 leaves the board
dead and announces itself, where 12 V onto a probe terminal destroys a DS18B20 and is not noticed
until somebody looks at the data.

This assumes a **two-wire, loop-powered** sender - which is what the requirement in
[MATERIAL.md](../MATERIAL.md) to buy one specified from 9-10 V upwards is selecting for, since
supply compliance only constrains a two-wire part. A three-wire sender would need the 4-pole after
all.

## 6. What a netlist cannot say

Seven things decide whether this board works, and none is expressible as a net.

### The 1-Wire passives sit at the cable, not at the pin

`Net-(J16-Pin_1)` is one net, so the netlist cannot say that the pull-up belongs at the connector
and the 100 Ω between it and the socket. Electrically it matters: the pull-up has to drive 500 pF
of 5 m cable directly, while the 100 Ω sees only the pin's few pF.

Lay it out the other way round and the series resistor lands in the path that charges the cable
capacitance. Same parts, a worse edge, and CRC errors that look like a bad probe.

### GND is a plane, and the job is not to cut it

The DC/DC return carries the DevKit's supply current, which peaks at several hundred milliamps when
the radio transmits. The divider's return carries 124 µA and is being measured to the millivolt. On
a wired ground those two sharing a conductor would put the transmit peaks straight into the battery
reading, and the classic answer is a star point.

**A ground plane makes that answer unnecessary.** 35 µm copper is half a milliohm per square, so
twenty millimetres across the plane is a fraction of a milliohm and the worst case is a tenth of a
millivolt - against an ADS1115 LSB of 62.5 µV and half a percent of overall accuracy. A separate
star-point conductor would be worse: it carries no useful current and behaves as an antenna.

With a plane the rule is not where to join it, but **not to cut it**:

- Bottom-layer traces slit the plane and send return current the long way round. Keep the bottom
  layer for the plane and route on top.
- Nothing crosses beneath R7, R8, C2 and J6. That is where a slit costs the most.
- **D1's anode needs no stitching vias.** It is a through-hole pad, so its own plated barrel
  reaches the plane: a 1.6 mm hole plated to 25 µm carries about 0.13 mm² of copper against the
  0.07 mm² of a 2 mm track. The barrel is the strongest link in that chain, not the weakest.
- **Thermal relief on it is fine**, and probably better than the alternative. KiCad's default four
  spokes at 0.5 mm add up to 2 mm of width, which is exactly the track feeding the pad, so nothing
  is given away. A 3.2 mm pad tied solidly into a ground plane is a fight with a soldering iron,
  and this board is soldered by hand. **Check the spoke width against the track width** rather than
  removing the relief - that is the number that matters.
- The pour is **excluded at the DevKit's antenna end**. A plane fills everything it is not
  forbidden to fill, so that exclusion has to exist as a rule area - it does not follow from the
  note in section 2 by itself.
- The `ST_GND` island is a second pour of its own, at a higher priority, inside the first. What
  keeps it separate is that no other net's pad or track enters it - see
  [D-001](D-001-seatalk-rx-stage.md).

### Track width is set by the fault, not by the load

In normal operation the 12 V side carries almost nothing. The ESP draws some 500 mA at 5 V while
transmitting, which is about 245 mA at the 12 V input, against roughly 0.75 A that a 0.2 mm track
in 35 µm copper handles at a 10 K rise. Threefold margin, and the drop over the whole board is tens
of millivolts.

The case that decides the width is **reverse polarity**. The TVS then conducts forward and shorts
the input until the fuse clears - intended behaviour, see [B-001](B-001-power-supply.md) - and a
200 Ah bank through a couple of metres of cable really does deliver the 200 A the 1.5KE20A is rated
to pass for 8.3 ms.

That current runs through exactly two paths: **J9 to D1, and D1 to ground.** A 0.2 mm track does
not survive it, and it would fail in the one event the circuit exists to survive.

The branch out to J12 looks like an exception and is not. It feeds a sender drawing 20 mA, but it
leaves the enclosure on a cable lying in the bilge, and a chafed conductor there is limited by
nothing except the fuse. It stays at the class width for the same reason as the rest of the net:
what sizes a track is the fault it has to survive, not the load it usually carries.

| Net class | Nets | Width |
|-----------|------|-------|
| fault path | `+12V_FUSED` | **1.5-2 mm** |
| power | `+12V_PROT`, `+5V` | **1.0 mm** |
| SeaTalk clamp | `ST_DATA`, between J17.2 and D3.1 only | **1.0 mm** |
| default | everything else | 0.2 mm |

`GND` is not in that table, because it is a pour and a net class width says nothing about a pour.
What carries the return at 200 A is the **number of vias at D1's anode** - four to six rather than
one - and an unbroken plane beneath them. A ground plane sliced by other tracks at that point sends
the current the long way round, and the via count stops mattering.

`VBAT_SENSE` and `ADS_A0` stay at the default width: they carry 124 µA, and what they need is not
copper but distance from the DC/DC module, which radiates into a deliberately high-impedance
measurement path.

### The bilge channel has two poles and no ground

A loop-powered sender has two wires and nothing else, because the quantity it sends is a **current**
rather than a voltage. It regulates 4-20 mA through the loop irrespective of the potential across
it, which is what makes the standard survive a long, wet cable: line resistance and a poor contact
change the voltage along the loop and not the current in it.

So there is no third wire to reference against, and nothing at the sender to call ground.
**Ground appears on this board**, at the bottom of the burden resistor:

```
   J12.2 ─────────────────────────► +12 V     ─┐
                                                │  sender in the bilge
   J12.1 ◄─────────────────────────  loop back ─┘   (4-20 mA)
     │
     ├──[ R22  100 Ω ]── GND      the voltage appears here
     │
     └──[ R15  1 kΩ  ]──┬──► A1
                        └──[ C6 ]── GND
```

Running a ground wire out to the sender as well would not merely be spare copper, it would put a
second return path in parallel with the loop.

**R22 is 100 Ω**, and that does not depend on which sender is bought: 4-20 mA across it is
0.4-2.0 V, which fills the ADS1115's +/-2.048 V range without crossing it. What does depend on the
sender is whether it works on the roughly 10 V left after the burden - a purchasing criterion,
recorded in [MATERIAL.md](../MATERIAL.md), not a component value.

R22 is also the part that dies first. Twelve volts onto the loop - a chafed cable is the realistic
way - puts 1.4 W into it, and it opens. That is the right order of failure: from then on R15 holds
the converter's input clamp to the same 8 mA as a bridged divider, and a through-hole resistor is a
two-minute repair where the converter is not.

### What each analogue channel ends in

Eight analogue inputs across two sockets, and they do not all deserve the same connector.

| Channel | Ends in | Because |
|---------|---------|---------|
| J6 A0 | nothing - the divider is on the board | it measures the board's own supply |
| **J6 A1** | **screw terminal J12**, through R22 | the bilge level sender is a cable through the enclosure wall |
| J6 A2, A3 | socket J1, pins 2 and 3 | nothing is specified for them yet |
| J10 A0-A3 | socket J1, pins 4 to 7 | the converter itself is not fitted yet |

Every one of them carries the same shape, **per signal pin** - the ground and 3.3 V poles on J1 are
supply for a future adapter and carry nothing:

```
J12.1 / a J1 signal pin ──[ 1 kΩ ]──┬──► converter input
                                    └── (100 nF) ── GND
```

- **The 1 kΩ is fitted and it lives here, on this board.** It is not conditioning, it is the last
  barrier in front of the converter, and the ADS1115 takes VDD + 0.3 V on an input regardless of
  what its gain is set to. The bilge channel makes the case concretely: a 4-20 mA loop is powered
  from the same terminal it measures into, so the fault to design against is the loop's own +12 V
  arriving on the return pole. The 1 kΩ holds that to about 8 mA into the input clamp - the same
  number, and the same argument, as the bridged divider in [B-001](B-001-power-supply.md).
- **The 100 nF is fitted where the source is known and pads only where it is not.** It is a charge
  reservoir for the converter's switched-capacitor input, wanted behind a high-impedance source -
  the divider, and the burden - and unwelcome on a fast signal, where 1 kΩ and 100 nF make a 100 µs
  filter nobody asked for.

**Conditioning proper belongs on a small adapter designed once the sender is known.** Putting screw
terminals on the spare channels now would mean guessing the circuit an unspecified sender needs,
and guessing it on the board that is hardest to change. A 30 x 20 mm adapter costs a few euro to
respin; this one does not. A socket also need not sit at the board edge, since nothing cabled
leaves through it - which is where the space is actually saved, not in the pitch.

### The second converter is socketed and left empty

J10 has its socket and its four pins on J1, and **no module in it** until its channels are
specified. An empty socket is holes: no device answers at 0x49, the bus scan finds nothing there,
and the firmware reports only the channels that produced a value.

Its four series resistors are fitted all the same. They do nothing while the socket is empty - they
lead from J1 to pins that go nowhere - and they are the one part of that channel that must not
depend on somebody remembering it later. The capacitors stay as bare pads for the opposite reason:
what they should be is a property of a sender nobody has chosen.

Mark the socket **optional** on the silkscreen, or somebody will go looking for the missing module.

One second-order effect worth knowing, because it is the sort that surprises later: the I2C
pull-ups sit on the modules, not on this board. With the SHT31 and both converters fitted the bus
sees about 3.3 kΩ; with J10 empty, 5 kΩ. Both are comfortable at 100 kHz - 5 kΩ against a few
hundred picofarads is an edge of about 2 µs against a 10 µs half-period. The IMU board carries pads
for pull-ups and no resistors, so it does not enter the sum either way.

### The I2C bus is a bus, not a star

`SDA` and `SCL` reach five places. Routed as a star from the socket, each branch is a stub, and
stubs add capacitance where the rise time is already set by roughly 2.5 kΩ of pull-up on the
breakouts ([A-002](A-002-bench-setup-usb.md)). Route it as a chain instead - socket, J6, J10, the
IMU terminal, and the SHT31 terminal last, since that one leaves the box and is the longest branch
by far.

## 7. Orientation and polarity

| Part | Correct orientation | If reversed |
|------|--------------------|--------------|
| D1 (TVS) | **banded end (cathode) to `+12V_FUSED`**, body to GND | conducts at 0.7 V and blows the fuse as soon as 12 V arrives |
| D3 (TVS) | **banded end to `ST_DATA`**, body to `ST_GND` | holds the boat's whole instrument bus low |
| D2 (1N5822) | banded end (cathode) to `+12V_PROT`, body to `+12V_FUSED` | no supply reaches the DC/DC - harmless but baffling |
| C3, C5 | **minus stripe to GND** | electrolytics vent |
| U2 | pin 1 = +VIN, 2 = GND, 3 = +VOUT, flat face and printed pin numbers to orient by | 12 V into the 5 V output destroys it |
| U1 (PC817) | **pin 1 is the LED anode**, marked by the dot. Pins 1-2 are the instrument side, 3-4 the board side | the isolation is bridged the wrong way round and nothing is received |
| Q1 (BC337-25) | the KiCad symbol numbers its pins **C-B-E**. Verify against the datasheet of the part actually bought | emitter and collector swapped puts the base-emitter junction across 11.3 V, against a V_EBO of 5 V - it fails on the first byte |

In the netlist, **pin 1 of both TVS diodes is the cathode**. They use `Device:D_Zener` rather than
`Device:D_TVS` for exactly that reason: a unidirectional TVS *is* a large zener, and the zener
symbol has an unambiguous cathode on pin 1, where the TVS symbol is drawn back to back and numbers
its pins as two anodes.

### Footprints

| Ref | Footprint |
|-----|-----------|
| R7-R21 | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| R1-R6, R22 | `Resistor_THT:R_Axial_DIN0309_L9.0mm_D3.2mm_P12.70mm_Horizontal` |
| C1, C2, C4, C6-C12 | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| C3 | `Capacitor_THT:CP_Radial_D8.0mm_P3.50mm` |
| C5 | `Capacitor_THT:CP_Radial_D10.0mm_P5.00mm` |
| D1, D3 | `Diode_THT:D_DO-201AE_P15.24mm_Horizontal` |
| D2 | `Diode_THT:D_DO-201AD_P15.24mm_Horizontal` |
| Q1 | `Package_TO_SOT_THT:TO-92_Inline` |
| U1 | `Package_DIP:DIP-4_W7.62mm` |
| U2 | `Converter_DCDC:Converter_DCDC_RECOM_R-78E-0.5_THT` |
| J7, J11 | `Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical` |
| J6, J10 | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` |
| J1 | `Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical` |
| J9, J12 | `TerminalBlock_MetzConnect:TerminalBlock_MetzConnect_Type101_RT01602HBWC_1x02_P5.08mm_Horizontal` |
| J2, J14, J15, J16, J17 | `TerminalBlock_MetzConnect:TerminalBlock_MetzConnect_Type101_RT01604HBWC_1x04_P5.08mm_Horizontal` |
| J4 | `TerminalBlock_MetzConnect:TerminalBlock_MetzConnect_Type101_RT01606HBWC_1x06_P5.08mm_Horizontal` |
| JP2 | `TestPoint:TestPoint_2Pads_Pitch2.54mm_Drill0.8mm` |
| H1-H4 | `MountingHole:MountingHole_3.2mm_M3_Pad` |

Three of these are assumptions rather than confirmed geometry, and each has to be checked against
the part in hand before the board is ordered:

| Part | To confirm |
|------|------------|
| U2 | an R-78E footprint is standing in for an R-78K. Three pads at 2.54 mm is right; **the body outline and courtyard are the question**, and on a densely packed board that is what decides whether a neighbour ends up under the module |
| Q1 | the symbol's pin order is C-B-E. Some BC337 symbols number it E-B-C |
| J6, J10 | the ADS1115 breakout's pin order: VDD, GND, SCL, SDA, ADDR, ALRT, A0, A1, A2, A3 |

JP2 uses a two-pad test point rather than a jumper footprint because it is closed with a **wire
link**, not a shunt. A shunt on a boat is a contact that vibrates and corrodes - see
[D-001](D-001-seatalk-rx-stage.md).

## 8. The files

The KiCad project is the source of truth for this board:

```
BoatHub.kicad_sch    the schematic
BoatHub.kicad_pcb    the layout
BoatHub-main.net     the exported netlist
```

The tables in sections 4, 5 and 7 are read from that export. **Where the two disagree, the project
is right and this document is stale** - which is the only workable rule once a board is being
routed rather than specified.

An earlier revision generated the netlist from a script, so that the tables here and the importable
file could not drift apart. That served its purpose - it bootstrapped the schematic - and then
became the drift it was meant to prevent, once the schematic acquired designators of its own. The
generator is gone; the export is the netlist.

**Export the netlist and run DRC from the same save as the layout being checked.** A netlist that
is minutes older than the board describes a board that no longer exists, and the disagreement is
silent.

### Having it made

Two layers, 1 oz copper, and nothing on it that a cheap process cannot do. Set the design rules
before routing rather than discovering them afterwards; the values below sit inside every common
fabricator's standard 2-layer process, and are worth confirming against the current capability page
of whoever makes it.

| | Set to |
|---|--------|
| Track width and clearance | 0.2 mm |
| Via drill / diameter | 0.3 / 0.6 mm |
| Smallest hole | 0.3 mm |
| Copper to board edge | 0.3 mm |
| Silkscreen line / character height | 0.2 / 1.2 mm |

Send Gerbers plus an Excellon drill file: `F.Cu`, `B.Cu`, `F.Mask`, `B.Mask`, `F.Silkscreen`,
`B.Silkscreen` and `Edge.Cuts`, which has to be a closed outline. `F.Paste` is only needed for
assembly.

**Open the Gerbers in a Gerber viewer before uploading them**, not just the layout editor. That is
what the fabricator sees, and it is the only reliable way to confirm that the exclusion under the
antenna is actually absent from the copper rather than merely present as a rule.

Lead-free HASL is enough here: the board lives in a sealed enclosure with a vent membrane, and the
enclosure is the corrosion measure. ENIG is flatter and more corrosion resistant if the surcharge
does not matter.

### It is hand-soldered

The board is entirely through-hole, none of it fine-pitch, and it stays that way. The reason is
structural rather than a preference: **this is a connector board.**

| | Joints | Could be surface-mount |
|---|--------|------------------------|
| Socket strips J7, J11, J6, J10, J1 | 72 | no |
| Screw terminals | 28 | no |
| Converter, optocoupler, transistor, link | 13 | no |
| Resistors, capacitors, diodes | 71 | yes |

Connectors are through-hole because they have to hold when somebody pulls a cable. Converting the
passives to surface-mount would move 71 joints to a machine and leave 113 to be soldered by hand
anyway - and the assembly setup, plus a feeder charge for each part that is not shelf stock, costs
more than the time it saves on the one board that actually gets populated.

Through-hole also keeps every part reworkable, which matters while values are still provisional:
the 1-Wire pull-ups are specified as a range, and a wire-ended resistor is a thirty-second swap.

Surface mount would earn its place if this board ever carried something that cannot be soldered by
hand - a fine-pitch converter IC in place of the module, say. It does not.

## 9. Verification

### Before the board is ordered

- [ ] Socket row spacing counted on the physical DevKit - nine free grid positions between the
      rows - and the pin order read off the DevKit rather than off the carrier's silkscreen
- [ ] ADS1115 breakout pin order confirmed against the part in hand
- [ ] **U2's body outline checked against the R-78K drawing**, not only its three pads
- [ ] **Q1's pin order checked against the datasheet of the BC337 actually bought**
- [ ] No copper pour under the DevKit's antenna end - which is the **pin 1** end of both sockets
- [ ] Both DevKit USB sockets and both its buttons reachable once installed
- [ ] Board outline and mounting holes checked against the enclosure
- [ ] Every terminal that takes a cable - J9, J16, J15, J14, J2, J4, J12, J17 - sits at the board
      edge with its opening facing outwards, and they are grouped towards the enclosure side that
      carries the cable glands. A terminal facing inwards cannot be fixed after fabrication
- [ ] Silkscreen carries what somebody needs in a dark locker with a torch: the cable label at each
      terminal, the wire colours at the probe and SeaTalk terminals, `5V`, `GND`, `3V3`, `IO4`-`IO6`,
      `SDA` and `SCL` beside the sockets, a pin 1 marker on each socket, and the fuse rating at J9
      with the note that reversing the supply blows it. None of it under a module, where a fitted
      board hides it
- [ ] **J9 and J12 are the same 2-pole part with 12 V on one pole of each, so both carry a legend** -
      supply in at one, sender loop at the other - and they are not placed side by side
- [ ] **J1's channel labels use the converter's own names, `A0`-`A3`.** The breakout is printed
      `A0 A1 A2 A3` and the firmware says the same; a legend that counts from one is an off-by-one
      against both
- [ ] **J10's socket is marked optional.** An empty footprint with no note reads as a missing part
- [ ] **Which way each screw terminal opens is drawn on the silkscreen.** A single row of pads with
      no alignment pegs accepts the block either way round, and the stock footprints draw a
      symmetric body, so nothing in the design records the intended direction. An asymmetric
      outline - 4.55 mm towards the opening, 3.75 mm behind the pins - says it without a legend
- [ ] **Terminal drill diameter is 1.2 mm.** That is the manufacturer's recommendation for a
      1.0-1.1 mm pin, and it is a fit rather than a minimum: the block stands square by itself while
      it is soldered. A footprint borrowed from a maker with heavier pins drills wider and leaves it
      loose
- [ ] No dimension objects or fabrication notes left on a silkscreen layer - they are printed on
      the finished board
- [ ] No silkscreen over a pad. Module outlines that cross their own socket pads belong on `F.Fab`
- [ ] DRC clean of everything but silkscreen warnings

### On the bare board, nothing fitted

- [ ] `+12V_FUSED`, `+12V_PROT`, `+5V` and `3.3V` each not continuous to `GND`
- [ ] `ST_GND` not continuous to `GND` with JP2 open, and continuous with it closed
- [ ] Socket pin 1 of J7 and J11 continuous to the nets section 5 claims
- [ ] Neighbouring socket pins not continuous to each other

### Populated, on a bench supply

- [ ] Divider ratio measured across R7 and R8 in place: (R7+R8)/R8 within a percent of 11.0
- [ ] 12.0 V in gives 5.0 V ±0.15 V at the socket's 5V pin
- [ ] `ADS_A0` reads 1.091 V ±10 mV at 12.0 V in - the divider proving itself against a meter
      before any firmware trusts it
- [ ] Supply swept 11 to 15 V: the 5 V rail holds, the A0 reading tracks linearly
- [ ] **A current source on J12 reads back:** 4 mA gives 0.4 V at A1, 20 mA gives 2.0 V. This can
      be done long before the sender is bought, and it proves R22 and R15 together
- [ ] **Supply reversed, briefly:** the fuse blows. Intended behaviour from
      [B-001](B-001-power-supply.md), worth seeing once on the bench rather than in the boat
- [ ] DevKit fitted: boots, reports 16 MB flash and 8 MB PSRAM, all three probes enumerate, and the
      I2C scan finds every device the bench build found - 0x48 answers, 0x49 does not while J10 is
      empty
- [ ] RSSI within a few dB of the breadboard build - a worse figure means the antenna keepout
- [ ] An hour at 13.6 V; nothing on the board more than hand-warm

## 10. Open points

| Point | Decide by |
|-------|-----------|
| Channel allocation for the second converter's four inputs. The socket, its pins on J1 and the resistor positions exist; nothing is fitted | when the analog inputs are specified |
| Whether the unallocated GPIO get a header after all. As drawn, `IO7`, `IO17`, `IO18` and `IO21` end at a socket pin, and reaching one later means soldering with the DevKit lifted out | **before the board is ordered** - it cannot be added afterwards |
| Whether to fit a fuse holder on the board as well as inline | when the enclosure layout is known |

## 11. References

- [B-001-power-supply.md](B-001-power-supply.md) - the power circuit and its component values
- [D-001-seatalk-rx-stage.md](D-001-seatalk-rx-stage.md) - the receive stage and its ground island
- [D-003-seatalk-tx-stage.md](D-003-seatalk-tx-stage.md) - the transmit stage and the link JP2
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - the DevKit pin rows this socket has
  to match, and how to check them
- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the I2C bus, its pull-ups and the ADS1115
  addresses
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the 1-Wire
  passives and why 2.0 kΩ
- [A-009-imu-heel-and-motion.md](A-009-imu-heel-and-motion.md) - the interrupt line on IO2
- [MATERIAL.md](../MATERIAL.md) - the orderable list, with Reichelt codes
