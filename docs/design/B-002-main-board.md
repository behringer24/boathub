# B-002 - Main board

| | |
|---|---|
| **Phase** | B |
| **Software version** | - |
| **Touches hardware** | yes |

## 1. Goal

One fabricated board carrying everything between the 12 V supply and the sensors: the protection
and conversion from [B-001](B-001-power-supply.md), the battery divider, the 1-Wire passives from
[A-003](A-003-ds18b20-temperature-sensors.md), the I2C distribution, and a socket for the DevKit.

It **replaces the bundled carrier**, which breaks out the GPIO but carries no circuitry of its own.
Everything the carrier does, this board does as well, and it does the rest too - so there is no
reason to keep both in the enclosure.

This document is the **logical layout**: every component, every net, every connector pin. Physical
placement and routing are not prescribed here; they follow from the enclosure.

A machine-readable form of the same list lives in
[`hardware/kicad/boathub-main.net`](../../hardware/kicad/boathub-main.net) - see section 8.

**Out of scope:** the sensor breakouts, which are bought assembled; the 4-20 mA shunt, which has no
circuit defined yet; and the channel allocation of the second and third ADS1115, which waits on its
own document - section 10.

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
| Orientation | **pin 1 of both sockets sits at the USB end.** J1.1 is the `GND` next to `5V`, J2.1 is the first of the `GND` pair - see [A-001](A-001-devkit-and-carrier.md) section 3 |
| Antenna | **no copper pour at the far end from pin 1.** That is where the module's antenna sits; a ground plane beneath it detunes the antenna, and the board would be the reason for poor range |
| USB sockets | both DevKit USB connectors must stay reachable at the board edge beside pin 1 |
| Buttons | the DevKit's own BOOT and RESET buttons must stay pressable with the board installed |

The orientation line is the one to get right. Both sockets run in the same direction, pin 1 to pin
22 from the USB end towards the antenna. One socket footprint placed 180° out puts 5 V where GND
belongs, and nothing in the netlist can catch it - the ratsnest is just as happy either way.

**Measure the actual DevKit before the board is ordered.** [A-001](A-001-devkit-and-carrier.md)
establishes that boards sold under this description vary, and a socket is unforgiving: a row
spacing or a pin order that is out by one is a scrapped run.

## 3. What the board carries

| On the board | Why there |
|--------------|-----------|
| TVS, reverse-polarity diode, bulk and bypass capacitors, DC/DC module | the 12 V entry has to be protected before anything else sees it |
| Battery divider and its series resistor | the tap must sit ahead of the reverse-polarity diode |
| Three 1-Wire pull-ups and series resistors, with detachable probe terminals | they belong at the cable end, not at the GPIO - section 6 |
| A socket for one ADS1115 breakout | the battery channel is the one whose use is defined |
| I2C distribution and the IMU interrupt line | so the bus has one origin instead of a daisy chain of jumpers |
| Every reserved and spare GPIO on a header | this board replaces the carrier, and the carrier's breakout is why it was useful |

| Off the board | Where instead |
|---------------|---------------|
| 2 A fuse | inline, **close to the supply** - it protects the cable, not the load |
| SHT31 | outside the enclosure entirely, on its own cable ([A-008](A-008-sht31-cabin-climate.md)) |
| Second and third ADS1115 | on the I2C expansion header, until their channels are specified |

## 4. Components

Values and rationale are in [B-001](B-001-power-supply.md) section 3 and
[A-003](A-003-ds18b20-temperature-sensors.md) section 3. This table is the build list.

| Ref | Value | Function |
|-----|-------|----------|
| D1 | 1.5KE20A | transient clamp, unidirectional, 17.1 V standoff |
| C1 | 100 nF / 50 V | HF bypass at the input |
| R1 | 82 kΩ 0.1 % | battery divider, top leg |
| R2 | 10 kΩ 0.1 % | battery divider, bottom leg |
| R3 | 1 kΩ | series into ADS1115 A0 - **do not omit**, this is what survives a reversed supply |
| C2 | 100 nF / 50 V | at A0 to GND, also feeds the converter's switched-capacitor input |
| D2 | 1N5822 | reverse polarity, 3 A / 40 V Schottky |
| C3 | 100 µF / 35 V, 105 °C | bulk at the DC/DC input |
| U1 | DC/DC 9-36 V → 5 V, min. 3 A | DevKit supply |
| C4 | 470 µF / 16 V, 105 °C | bulk at the 5 V output |
| C5 | 100 nF / 50 V | HF bypass at the output |
| R4, R5, R6 | 2.0 kΩ | 1-Wire pull-ups, one per probe |
| R7, R8, R9 | 100 Ω | 1-Wire series protection, one per probe |

### Connectors and sockets

| Ref | Pins | Carries | Type |
|-----|------|---------|------|
| J1, J2 | 22 each | the DevKit | **socket strip**, 2.54 mm |
| J3 | 2 | 12 V in, already fused | screw terminal |
| J4 | 3 | probe `MOTOR-T` | **screw terminal** |
| J5 | 3 | probe `BILGE-T` | **screw terminal** |
| J6 | 3 | probe `FRIDGE-T` | **screw terminal** |
| J7 | 4 | SHT31: 3.3 V, SDA, SCL, GND | **screw terminal** |
| U2 | 10 | ADS1115 breakout, 0x48 | socket strip |
| J8 | 4 | ADS1115 A1, A2, A3 and GND | screw terminal |
| J9 | 5 | IMU: 3.3 V, GND, SDA, SCL, INT | pin header |
| J10 | 4 | I2C expansion: 3.3 V, GND, SDA, SCL | pin header |
| J11 | 8 | reserved GPIO | pin header |
| J12 | 14 | spare GPIO | pin header |

Everything that leaves the enclosure gets a **screw terminal**; everything that stays inside gets a
pin header. Probe cables and the SHT31 run have to come off for service, and that is the whole
reason they are connectors rather than solder joints.

## 5. The nets

Thirty-seven nets. This is the complete electrical description of the board.

### Power

| Net | Nodes |
|-----|-------|
| `GND` | J1.1, J2.1, J2.2, J2.22, J3.2, D1.2, C1.2, R2.2, C2.2, C3.2, U1.2, U1.4, C4.2, C5.2, J4.3, J5.3, J6.3, J7.4, U2.2, U2.5, J8.4, J9.2, J10.2, J11.8, J12.14 |
| `+12V_FUSED` | J3.1, D1.1, C1.1, R1.1, D2.2 |
| `+12V_PROT` | D2.1, C3.1, U1.1 |
| `+5V` | U1.3, C4.1, C5.1, J1.2 |
| `+3V3` | J1.21, J1.22, R4.1, R5.1, R6.1, J4.1, J5.1, J6.1, J7.1, U2.1, J9.1, J10.1, J11.7, J12.13 |

Read the power chain off the net names: the clamp and the battery tap sit on the **fused** net,
ahead of the diode; everything downstream of the diode is on `+12V_PROT`.
[B-001](B-001-power-supply.md) explains why that order and not another. The 3.3 V rail is produced
by the DevKit's own regulator and distributed from the socket - the board consumes it, it does not
make it.

### Measurement and bus

| Net | Nodes |
|-----|-------|
| `VBAT_SENSE` | R1.2, R2.1, R3.1 |
| `ADS_A0` | R3.2, C2.1, U2.7 |
| `SDA` | J1.11 (IO8), J7.2, U2.4, J9.3, J10.3 |
| `SCL` | J1.8 (IO9), J7.3, U2.3, J9.4, J10.4 |
| `IMU_INT` | J2.18 (IO2), J9.5 |
| `ADS1_A1` | U2.8, J8.1 |
| `ADS1_A2` | U2.9, J8.2 |
| `ADS1_A3` | U2.10, J8.3 |

`U2.5` is the ADS1115's ADDR pin, tied to GND for address **0x48**. The other two modules set
theirs on the expansion header, per [A-002](A-002-bench-setup-usb.md).

### 1-Wire

| Net | Nodes |
|-----|-------|
| `OW1_BUS` | R4.2, R7.1, J4.2 |
| `OW1_GPIO` | R7.2, J1.19 (IO4) |
| `OW2_BUS` | R5.2, R8.1, J5.2 |
| `OW2_GPIO` | R8.2, J1.18 (IO5) |
| `OW3_BUS` | R6.2, R9.1, J6.2 |
| `OW3_GPIO` | R9.2, J1.17 (IO6) |

### Reserved and spare

Each is a straight link from the socket to a header pin.

| Header | Net | From |
|--------|-----|------|
| J11.1-6 | `IO7`, `IO15`, `IO16`, `IO17`, `IO18`, `IO21` | fourth probe, SeaTalk RX/TX, TWAI TX/RX, buzzer |
| J12.1-12 | `IO1`, `IO10`-`IO14`, `IO38`-`IO42`, `IO47` | unallocated |

J11.7 and J12.13 are 3.3 V, J11.8 and J12.14 are GND, so an experiment on either header needs no
second wire for power.

### Deliberately unconnected

Fourteen socket pins carry nothing, and the netlist says so explicitly rather than leaving it to be
noticed: `IO46`, `IO3`, `IO45` and `IO0` are strapping pins or the BOOT button; `IO19`/`IO20` are
USB; `IO35`-`IO37` belong to the octal PSRAM; `IO48` drives the DevKit's RGB LED; `RST`, `RX` and
`TX` stay with the DevKit for service.

### Connector pin assignment

| | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 |
|---|---|---|---|---|---|
| J3 | +12 V | GND | | | |
| J4, J5, J6 | +3.3 V (probe red) | DATA (probe yellow) | GND (probe black) | | |
| J7 | +3.3 V | SDA | SCL | GND | |
| J8 | A1 | A2 | A3 | GND | |
| J9 | +3.3 V | GND | SDA | SCL | INT |
| J10 | +3.3 V | GND | SDA | SCL | |

The three probe connectors share one pin order, deliberately. Three identical connectors wired
three different ways is a fault waiting for the first service visit.

## 6. What a netlist cannot say

Four things decide whether this board works, and none is expressible as a net.

### The 1-Wire passives sit at the cable, not at the pin

`OW1_BUS` is one net, so the netlist cannot say that the pull-up belongs at the connector and the
100 Ω between it and the socket. Electrically it matters: the pull-up has to drive 500 pF of 5 m
cable directly, while the 100 Ω sees only the pin's few pF.

Lay it out the other way round and the series resistor lands in the path that charges the cable
capacitance. Same parts, a worse edge, and CRC errors that look like a bad probe.

### GND is one net, but not one piece of copper

The DC/DC return carries the DevKit's supply current, which peaks at several hundred milliamps when
the radio transmits. The divider's return carries 148 µA and is being measured to the millivolt.

Share copper between them and the transmit peaks appear in the battery reading.

So: the analog return - R2, C2 and the ADS1115 - joins the plane at **one point**, at the DC/DC
output, rather than anywhere convenient. On a fabricated board this is a deliberate act during
layout, not something that happens by itself.

### Track width is set by the fault, not by the load

In normal operation the 12 V side carries almost nothing. The ESP draws some 500 mA at 5 V while
transmitting, which is about 245 mA at the 12 V input, against roughly 0.75 A that a 0.2 mm track
in 35 µm copper handles at a 10 K rise. Threefold margin, and the drop over the whole board is tens
of millivolts.

The case that decides the width is **reverse polarity**. The TVS then conducts forward and shorts
the input until the fuse clears - intended behaviour, see [B-001](B-001-power-supply.md) - and a
200 Ah bank through a couple of metres of cable really does deliver the 200 A the 1.5KE20A is rated
to pass for 8.3 ms.

That current runs through exactly two paths: **J3 to D1, and D1 to ground.** A 0.2 mm track does
not survive it, and it would fail in the one event the circuit exists to survive.

| Net class | Nets | Width |
|-----------|------|-------|
| fault path | `+12V_FUSED`, and the ground return at the input | **1.5-2 mm** |
| power | `+12V_PROT`, `+5V` | **1.0 mm** |
| default | everything else | 0.2 mm |

D1's return to the ground plane wants **several vias**, not one. At 200 A a single via is the new
weakest point.

`VBAT_SENSE` and `ADS_A0` stay at the default width: they carry 148 µA, and what they need is not
copper but distance from the DC/DC module, which radiates into a deliberately high-impedance
measurement path.

### The I2C bus is a bus, not a star

`SDA` and `SCL` reach five places. Routed as a star from the socket, each branch is a stub, and
stubs add capacitance where the rise time is already set by roughly 2.5 kΩ of pull-up on the
breakouts ([A-002](A-002-bench-setup-usb.md)). Route it as a chain instead - socket, ADS1115, IMU
header, expansion header, SHT31 terminal last, since that one leaves the box and is the longest
branch by far.

## 7. Orientation and polarity

| Part | Correct orientation | If reversed |
|------|--------------------|--------------|
| D1 (TVS) | **banded end (cathode) to `+12V_FUSED`**, body to GND | conducts at 0.7 V and blows the fuse as soon as 12 V arrives |
| D2 (1N5822) | banded end (cathode) to `+12V_PROT`, body to `+12V_FUSED` | no supply reaches the DC/DC - harmless but baffling |
| C3, C4 | **minus stripe to GND** | electrolytics vent |
| U1 | check the module's own IN/OUT silkscreen | 12 V into the 5 V output destroys it |

In the netlist, **pin 1 of both diodes is the cathode**. D1 uses `Device:D_Zener` rather than
`Device:D_TVS` for exactly that reason: a unidirectional TVS *is* a large zener, and the zener
symbol has an unambiguous cathode on pin 1, where the TVS symbol is drawn back to back and numbers
its pins as two anodes.

### KiCad symbols and footprints

| Ref | Symbol | Footprint |
|-----|--------|-----------|
| R1-R9 | `Device:R` | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal` |
| C1, C2, C5 | `Device:C` | `Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm` |
| C3 | `Device:CP` | `Capacitor_THT:CP_Radial_D8.0mm_P3.50mm` |
| C4 | `Device:CP` | `Capacitor_THT:CP_Radial_D10.0mm_P5.00mm` |
| D1 | `Device:D_Zener` | `Diode_THT:D_DO-201AD_P15.24mm_Horizontal` |
| D2 | `Device:D_Schottky` | `Diode_THT:D_DO-201AD_P15.24mm_Horizontal` |
| J1, J2 | `Connector_Generic:Conn_01x22` | `Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical` |
| U2 | `Connector_Generic:Conn_01x10` | `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical` |
| J3-J12 | `Connector_Generic:Conn_01xNN` | `Connector_PinHeader_2.54mm:PinHeader_1xNN_P2.54mm_Vertical` |

Two entries in that table are placeholders and have to be replaced before the board is ordered:

- **The screw terminals.** Filter the footprint chooser to the `TerminalBlock*` libraries and
  search for the pole count at `P5.08mm`. Which one fits depends on the part bought, and library
  names in that family change between KiCad releases - which is why the netlist does not name one.
- **The DC/DC module.** No standard footprint exists. Measure the module's pads and draw one.

Three pinouts in this design are assumptions, not standards, and each has to be checked against the
part actually bought before the board is ordered:

| Part | Assumed |
|------|---------|
| DC/DC module | 1 = IN+, 2 = IN−, 3 = OUT+, 4 = OUT− |
| ADS1115 breakout | VDD, GND, SCL, SDA, ADDR, ALRT, A0, A1, A2, A3 |
| DevKit socket | the row order printed in [A-001](A-001-devkit-and-carrier.md) section 3 |

The last one is the expensive one. A mirrored row puts 5 V where GND belongs.

## 8. The files

```
hardware/kicad/
  boathub-main.net         the netlist, importable into KiCad Pcbnew
  generate-netlist.py      what produces it
```

The netlist is generated rather than drawn, so the component and net tables exist once and cannot
drift apart from each other. The generator validates before writing: every reference unique, every
pin either on exactly one net or explicitly declared a no-connect, no net with fewer than two
nodes.

```bash
python hardware/kicad/generate-netlist.py
```

**In KiCad:** create a project, open Pcbnew, and use *File → Import → Netlist*. Every component
appears with a ratsnest showing what must connect to what.

**Footprints in the netlist are placeholders.** Pin headers and socket strips stand in for the
screw terminals, because their names are stable across KiCad versions while terminal-block library
names are not. Before ordering, replace them with footprints matching the parts in hand: 5.08 mm
pitch for the screw terminals, and whatever pad pattern the DC/DC module has.

The alternative route is to draw the schematic in KiCad, let it generate its own netlist, and
compare the two. Two independent descriptions that agree are worth considerably more than one.

## 9. Verification

### Before the board is ordered

- [ ] Socket row spacing counted on the physical DevKit - nine free grid positions between the
      rows - and the pin order read off the DevKit rather than off the carrier's silkscreen
- [ ] DC/DC module pinout confirmed against the part in hand
- [ ] ADS1115 breakout pin order confirmed against the part in hand
- [ ] No copper pour under the DevKit's antenna end
- [ ] Both DevKit USB sockets and both its buttons reachable once installed
- [ ] Board outline and mounting holes checked against the enclosure
- [ ] Every terminal that takes a cable - J3, J4, J5, J6, J7, J8 - sits at the board edge with its
      opening facing outwards, and they are grouped towards the enclosure side that carries the
      cable glands. A terminal facing inwards cannot be fixed after fabrication
- [ ] Silkscreen carries what somebody needs in a dark locker with a torch: the cable label at each
      terminal, the wire colours at the probe terminals, `5V`, `GND`, `3V3`, `IO4`-`IO6`, `SDA` and
      `SCL` beside the sockets, a pin 1 marker on each socket, and the fuse rating at J3 with the
      note that reversing the supply blows it. None of it under a module, where a fitted board
      hides it
- [ ] Netlist compared against a second description - either the tables in section 5 read through
      by hand, or a schematic drawn independently

### On the bare board, nothing fitted

- [ ] `+12V_FUSED`, `+12V_PROT`, `+5V` and `+3V3` each not continuous to `GND`
- [ ] Socket pin 1 of J1 and J2 continuous to the nets section 5 claims
- [ ] Neighbouring socket pins not continuous to each other

### Populated, on a bench supply

- [ ] Divider ratio measured across R1 and R2 in place: (R1+R2)/R2 within a percent of 9.2
- [ ] 12.0 V in gives 5.0 V ±0.15 V at the socket's 5V pin
- [ ] `ADS_A0` reads 1.304 V ±10 mV at 12.0 V in - the divider proving itself against a meter
      before any firmware trusts it
- [ ] Supply swept 11 to 15 V: the 5 V rail holds, the A0 reading tracks linearly
- [ ] **Supply reversed, briefly:** the fuse blows. Intended behaviour from
      [B-001](B-001-power-supply.md), worth seeing once on the bench rather than in the boat
- [ ] DevKit fitted: boots, reports 16 MB flash and 8 MB PSRAM, all three probes enumerate, the
      I2C scan finds every device the bench build found
- [ ] RSSI within a few dB of the breadboard build - a worse figure means the antenna keepout
- [ ] An hour at 13.6 V; nothing on the board more than hand-warm

## 10. Open points

| Point | Decide by |
|-------|-----------|
| The 4-20 mA shunt has no circuit document, so it is not in the netlist. Reserve area for two 100 Ω 0.1 % in parallel and a 100 nF at A1 | when the bilge level probe is specified |
| Channel allocation for the second and third ADS1115. Until it exists they live on the expansion header rather than in sockets | when the analog inputs are specified |
| Whether the SeaTalk level shifter and its interlock belong on this board or on their own | when the SeaTalk stage is designed |
| Whether to fit a fuse holder on the board as well as inline | when the enclosure layout is known |

## 11. References

- [B-001-power-supply.md](B-001-power-supply.md) - the power circuit and its component values
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - the DevKit pin rows this socket has
  to match, and how to check them
- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the I2C bus, its pull-ups and the ADS1115
  addresses
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the 1-Wire
  passives and why 2.0 kΩ
- [A-009-imu-heel-and-motion.md](A-009-imu-heel-and-motion.md) - the interrupt line on IO2
- [`hardware/kicad/boathub-main.net`](../../hardware/kicad/boathub-main.net) - the netlist
