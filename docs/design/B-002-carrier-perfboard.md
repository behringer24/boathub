# B-002 - Carrier perfboard: nets and build

| | |
|---|---|
| **Phase** | B |
| **Software version** | - |
| **Touches hardware** | yes |

## 1. Goal

The one piece of self-built hardware in the system: a small perfboard carrying the 12 V protection
and conversion from [B-001](B-001-power-supply.md), the battery divider, and the 1-Wire passives
from [A-003](A-003-ds18b20-temperature-sensors.md).

This document is the **logical layout** - every component, every net, every connector pin. It is
the wiring list you build from and check against. Physical placement is not prescribed, because a
perfboard is laid out around the parts actually in hand.

A machine-readable form of the same list lives in
[`hardware/kicad/boathub-carrier.net`](../../hardware/kicad/boathub-carrier.net), a KiCad netlist
that Pcbnew can import - see section 7.

**Out of scope:** the DevKit socket and the GPIO screw terminals, which the Heemol carrier already
provides ([A-001](A-001-devkit-and-carrier.md)); the sensor breakout boards, which are bought
assembled; and the 4-20 mA shunt, which has no circuit defined yet - section 9.

## 2. What the board does and does not carry

| On the board | Why there |
|--------------|-----------|
| TVS, reverse-polarity diode, bulk and bypass capacitors, DC/DC module | the 12 V entry has to be protected before anything else sees it |
| Battery divider and its series resistor | the tap must sit ahead of the reverse-polarity diode, so it belongs with the protection |
| Three 1-Wire pull-ups and series resistors | they belong at the cable end, not at the GPIO - section 5 |
| Detachable probe connections | cables have to come off for service |

| Off the board | Where instead |
|---------------|---------------|
| 2 A fuse | inline, **close to the supply**, because it protects the cable rather than the load |
| DevKit, GPIO breakout | the carrier |
| SHT31, ADS1115, IMU | bought breakouts, wired to the carrier's I2C and 3.3 V terminals |

## 3. Components

Values and rationale come from [B-001](B-001-power-supply.md) section 3 and
[A-003](A-003-ds18b20-temperature-sensors.md) section 3; this table is the build list.

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
| U1 | DC/DC 9-36 V → 5 V, min. 3 A | ESP supply |
| C4 | 470 µF / 16 V, 105 °C | bulk at the 5 V output |
| C5 | 100 nF / 50 V | HF bypass at the output |
| R4, R5, R6 | 2.0 kΩ | 1-Wire pull-ups, one per probe |
| R7, R8, R9 | 100 Ω | 1-Wire series protection, one per probe |

### Connectors

| Ref | Pins | Carries | Type |
|-----|------|---------|------|
| J1 | 2 | 12 V in, already fused | screw terminal |
| J2 | 2 | 5 V out to the carrier's `5V` terminal | pin header or soldered |
| J3 | 2 | 3.3 V in from the carrier's `3.3V` terminal | pin header or soldered |
| J4 | 3 | probe `MOTOR-T` | **screw terminal** |
| J5 | 3 | probe `BILGE-T` | **screw terminal** |
| J6 | 3 | probe `FRIDGE-T` | **screw terminal** |
| J7 | 4 | 1-Wire DATA to the carrier's `IO4` `IO5` `IO6`, plus GND | pin header or soldered |
| J8 | 2 | divider output to ADS1115 A0 | pin header |

The three probe connectors must be **screw terminals**: the probe cables come off for service, and
that is the whole reason they are connectors rather than solder joints. The rest are internal links
a few centimetres long and may simply be soldered wire.

## 4. The nets

Thirteen nets. This is the complete electrical description of the board.

| Net | Nodes |
|-----|-------|
| `GND` | J1.2, D1.2, C1.2, R2.2, C2.2, J8.2, C3.2, U1.2, U1.4, C4.2, C5.2, J2.2, J3.2, J4.3, J5.3, J6.3, J7.4 |
| `+12V_FUSED` | J1.1, D1.1, C1.1, R1.1, D2.2 |
| `VBAT_SENSE` | R1.2, R2.1, R3.1 |
| `ADS_A0` | R3.2, C2.1, J8.1 |
| `+12V_PROT` | D2.1, C3.1, U1.1 |
| `+5V` | U1.3, C4.1, C5.1, J2.1 |
| `+3V3` | J3.1, R4.1, R5.1, R6.1, J4.1, J5.1, J6.1 |
| `OW1_BUS` | R4.2, R7.1, J4.2 |
| `OW1_GPIO` | R7.2, J7.1 |
| `OW2_BUS` | R5.2, R8.1, J5.2 |
| `OW2_GPIO` | R8.2, J7.2 |
| `OW3_BUS` | R6.2, R9.1, J6.2 |
| `OW3_GPIO` | R9.2, J7.3 |

Read the order of the power chain off `+12V_FUSED` → `+12V_PROT` → `+5V`: the clamp and the battery
tap sit on the **fused** net, ahead of the diode, and everything downstream of the diode is on
`+12V_PROT`. [B-001](B-001-power-supply.md) explains why that order and not another.

### Connector pin assignment

| | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|
| J1 | +12 V | GND | | |
| J2 | +5 V | GND | | |
| J3 | +3.3 V | GND | | |
| J4, J5, J6 | +3.3 V (probe red) | DATA (probe yellow) | GND (probe black) | |
| J7 | `IO4` | `IO5` | `IO6` | GND |
| J8 | A0 | GND | | |

The three probe connectors share one pin order, deliberately. Three identical connectors wired
three different ways is a fault waiting for the first service visit.

## 5. What a netlist cannot say

Two things decide whether this board works, and neither is expressible as a net.

### The 1-Wire passives sit at the cable, not at the pin

`OW1_BUS` is one net, so the netlist cannot tell you that the pull-up belongs at the connector and
the 100 Ω between it and the GPIO. Electrically it matters: the pull-up has to drive 500 pF of
5 m cable directly, while the 100 Ω sees only the pin's few pF.

Build the other way round - 100 Ω towards the cable, pull-up at the GPIO - and the series resistor
lands in the path that charges the cable capacitance. Same parts, a worse edge, and CRC errors that
look like a bad probe.

### GND is one net, but not one piece of wire

The DC/DC return carries the ESP's supply current, which peaks at several hundred milliamps when
the radio transmits. The divider's return carries 148 µA and is being measured to the millivolt.

Share a wire between them and the transmit peaks appear in the battery reading.

So: **GND meets at one point**, at the DC/DC output. The sensor and divider grounds run there
directly rather than being daisy-chained along the power return. On a perfboard this costs nothing
but attention while soldering, and it cannot be retrofitted once everything is chained.

## 6. Orientation and polarity

Four parts have a direction, and three of them fail expensively if reversed.

| Part | Correct orientation | If reversed |
|------|--------------------|--------------|
| D1 (TVS) | **banded end (cathode) to `+12V_FUSED`**, body to GND | conducts at 0.7 V and blows the fuse as soon as 12 V arrives |
| D2 (1N5822) | banded end (cathode) to `+12V_PROT`, body to `+12V_FUSED` | no supply reaches the DC/DC - harmless but baffling |
| C3, C4 | **minus stripe to GND** | electrolytics vent |
| U1 | check the module's own IN/OUT silkscreen | 12 V into the 5 V output destroys it |

In the KiCad netlist, **pin 1 of both diodes is the cathode**, following the standard symbol. That
convention is worth knowing before checking the board against the list.

DC/DC modules are not standardised. The netlist assumes `1 = IN+`, `2 = IN−`, `3 = OUT+`,
`4 = OUT−`. Verify against the part actually bought and correct the generator if it differs.

## 7. The files

```
hardware/kicad/
  boathub-carrier.net      the netlist, importable into KiCad Pcbnew
  generate-netlist.py      what produces it
```

The netlist is generated rather than drawn, so the component and net tables exist once and cannot
drift apart. The generator validates before writing: every reference unique, every declared pin on
exactly one net, no net with fewer than two nodes. A netlist that assembles cleanly is not
necessarily correct, but one that does not assemble is certainly wrong, and the check is free.

```bash
python hardware/kicad/generate-netlist.py
```

**In KiCad:** create an empty project, open Pcbnew, and use *File → Import → Netlist*. Every
component appears with a ratsnest showing what must connect to what. On a perfboard that ratsnest
is the wiring checklist; the board outline and routing are only meaningful if this is ever
fabricated.

**Footprints are placeholders.** Pin headers stand in for the connectors because they match the
2.54 mm perfboard grid. For a fabricated board they have to be replaced with the real parts -
the 5.08 mm screw terminals, and whatever pad pattern the DC/DC module actually has.

## 8. Verification

Before the DevKit goes near the board, with no supply connected:

- [ ] Every net checked through with a meter against the table in section 4
- [ ] `+12V_FUSED` to `GND` **not** continuous - a reversed TVS shows up here, before it takes the fuse
- [ ] `+12V_PROT` to `GND` not continuous
- [ ] `+5V` to `GND` not continuous
- [ ] `+3V3` to `GND` not continuous
- [ ] Divider ratio measured across R1 and R2 in place: (R1+R2)/R2 within a percent of 9.2
- [ ] Electrolytic stripes against the table in section 6
- [ ] Both diode bands against the table in section 6

Then on the bench supply, still with nothing connected to J2:

- [ ] 12.0 V in gives 5.0 V ±0.15 V at J2
- [ ] `ADS_A0` reads 1.304 V ±10 mV at 12.0 V in - that is the divider proving itself against a
      meter before any firmware trusts it
- [ ] Supply swept 11 to 15 V: the 5 V rail holds, the A0 reading tracks linearly
- [ ] **Supply reversed, briefly:** the fuse blows. This is the intended behaviour from
      [B-001](B-001-power-supply.md), and it is worth seeing once on the bench rather than
      discovering in the boat
- [ ] Board runs an hour at 13.6 V with the DevKit connected; nothing on it is more than hand-warm

## 9. Open points

| Point | Decide by |
|-------|-----------|
| The 4-20 mA shunt has no circuit document, so it is not in the netlist. Reserve board area for two 100 Ω 0.1 % in parallel and a 100 nF at A1 | when the bilge level probe is specified |
| Whether J7 and J2 are headers or soldered wire | when the enclosure layout is known - a plug is worth more if the board has to come out |
| Whether the board carries the optional buzzer transistor on `IO21` | when the alarm logic is written |

## 10. References

- [B-001-power-supply.md](B-001-power-supply.md) - the power circuit and its component values
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the 1-Wire
  passives and why 2.0 kΩ
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - the carrier terminals this board
  connects to
- [`hardware/kicad/boathub-carrier.net`](../../hardware/kicad/boathub-carrier.net) - the netlist
