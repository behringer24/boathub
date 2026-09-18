# Bill of materials

Parts, tools and rough prices for the ESP32 BoatHub. The single orderable list; the design
documents in [design/](design/) carry the reasoning behind each choice.

Prices are rough guide values in EUR. The links point to the German Amazon listings or searches
used while planning; they are a sourcing hint only. **What matters is the technical data in the
"Purpose / requirement" column** - any equivalent part will do.

**Phase** says when the part is first needed, matching the build phases in
[ROADMAP.md](ROADMAP.md):

| Phase | | |
|-------|---|---|
| **A** | bench build on USB power | nothing runs on 12 V yet |
| **B** | power supply and perfboard | the 12 V side gets built |
| **C** | installation in the boat | enclosure, cable routing |

---

## Stage 1 - base monitoring

### Controller and sensors

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| ESP32-S3 N16R8 DevKitC-1 + carrier (Heemol set) | **2** | A | main controller, 16 MB flash / 8 MB octal PSRAM, MRD076A terminal adapter included. Unbranded third-party module; bundled SMA antenna is **inert until a solder jumper is moved** (M9a, see [A-001](design/A-001-devkit-and-carrier.md)). The second board is the guide's reserve - and makes the antenna rework an A/B comparison rather than a one-way bet | ~18 each | [Amazon](https://www.amazon.de/dp/B0GJZS3P1J) |
| GERUI ADS1115 16-bit I2C, 3-pack | 1 pack | A | ADC for battery and 4-20 mA, plus spares. Addresses 0x48/0x49/0x4A | 8.99 | [Amazon](https://www.amazon.de/dp/B0F1TJ16Q6) |
| DS18B20-compatible 1-Wire probes, 5 m, 3-pack | 1 pack | A | engine bay, bilge, fridge; potted. Clones - verify family code 0x28 and CRC ([A-003](design/A-003-ds18b20-temperature-sensors.md)) | ~10-20 | [Amazon](https://www.amazon.de/gp/product/B0D8VMY5ZM) |
| LSM6DSOX breakout (6-axis IMU) | 1 | A | heel and pitch under sail, motion and impact at the berth. 0x6A, no clash on this bus. **Mounted rigidly inside the enclosure**, unlike the SHT31 - an IMU on a flying lead measures the lead ([A-009](design/A-009-imu-heel-and-motion.md)) | ~4-8 | **needed** | [Amazon search](https://www.amazon.de/s?k=LSM6DSOX+breakout) |
| SHT3x-D breakout, ADR 0x44/0x45 | **3** | A | cabin temperature and relative humidity; mounted outside box and cabinet. Only one is fitted; the bus supports two (0x44/0x45), the third is a spare. **Confirm it is an SHT31** - SHT30/31/35 are pin- and protocol-compatible but differ in RH accuracy (±3 / ±2 / ±1.5 %) | ~7-12 each | [Amazon search](https://www.amazon.de/s?k=SHT31-D+I2C+0x44+0x45) |

### Resistors from a 1 % assortment

One metal-film assortment covers every value below. Buy it for phase A - the 1-Wire pull-ups and
the bench reference divider both come out of it.

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Resistor assortment, 1 % metal film | 1 | A | source for all values below | ~10-15 | [Amazon search](https://www.amazon.de/s?k=Metallschicht+Widerstand+Sortiment+1%25) |

Values drawn from it:

| Value | Qty | Phase | Purpose |
|-------|-----|-------|---------|
| **2.0-2.2 kΩ** | 3 | A | **1-Wire pull-ups, the value to fit** - either works, take what the assortment holds. 4.7 kΩ is the textbook value but marginal over 5 m ([A-003](design/A-003-ds18b20-temperature-sensors.md)) |
| 3.3 kΩ | 3 | A | 1-Wire pull-up fallback; also the extra I2C pull-up pair if the SHT31 run exceeds ~3 m |
| 4.7 kΩ | 3 | A | 1-Wire fallback, the original guide's value. Keep a few, do not fit them first |
| 100 Ω | 3 | A | series protection in each DS18B20 DATA line |
| 10 kΩ | 2 | A | bench reference divider from the 3.3 V rail, to prove the ADS1115 without a 12 V supply ([A-002](design/A-002-bench-setup-usb.md) section 4) |
| **1 kΩ** | 1 | B | **series into ADS1115 A0 - a safety part.** It limits current into the ESD clamp to ~0.1 mA on reversed polarity. Do not omit ([B-001](design/B-001-power-supply.md)) |
| 10 kΩ + 1 kΩ | 1 each | opt | buzzer driver on GPIO21, if a buzzer is fitted |

A 10 kΩ potentiometer instead of the two 10 kΩ resistors makes the ADS1115 test better - sweep it
and check linearity end to end.

### Precision resistors, 0.1 %

Not found in a standard assortment; order separately. Tolerance only matters here.

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| 82 kΩ 0.1 % + 10 kΩ 0.1 % | 1 each | B | battery voltage divider, factor 9.2 | ~5-10 (pack) | [Amazon search](https://www.amazon.de/s?k=82k+10k+0.1%25+Praezisionswiderstand) |
| 100 Ω 0.1 % / 0.25 W | 2 | opt | 4-20 mA shunt into ADS1115 A1; **two in parallel give 50 Ω**, halving the burden voltage at identical resolution | ~5 (pack) | [Amazon search](https://www.amazon.de/s?k=100+Ohm+0.1%25+Praezisionswiderstand) |

### Power supply and protection

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| DC/DC converter 9-36 V to 5 V, min. 3 A | 1 | B | ESP supply, wide input for house-supply swings | ~9-15 | [Amazon search](https://www.amazon.de/s?k=9-36V+5V+3A+DC+DC+wasserdicht) |
| Automotive blade fuse holder + 2 A fuses | 1 set | B | cable protection close to the source | ~6-10 | [Amazon search](https://www.amazon.de/s?k=wasserdichter+KFZ+Flachsicherungshalter+2A) |
| TVS diode 1.5KE20A | 1 (pack) | B | transient clamp, **fitted ahead of the Schottky**. 17.1 V standoff, clamps 27.7 V at 54 A | ~5-8 | [Amazon search](https://www.amazon.de/s?k=1.5KE20A+TVS) |
| 1N5822 Schottky diode | 1 (pack) | B | reverse-polarity protection, 3 A / 40 V | ~4-7 | [Amazon search](https://www.amazon.de/s?k=1N5822+Schottky+Diode) |
| 100 nF / 50 V | **at least 5** | B | 12 V input, DC/DC output, ADS1115 A0, ADS1115 A1, and the SHT31 far end | ~8-15 (assortment) | [Amazon search](https://www.amazon.de/s?k=Kondensator+Sortiment+100nF+100uF+470uF) |
| 100 µF / 35 V, **105 °C** | 1 | B | bulk at the DC/DC input. 105 °C, not 85 °C | with the above | as above |
| 470 µF / 16 V, **105 °C** | 1 | B | bulk on the 5 V output | with the above | as above |

### Assembly and enclosure

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Fabricated main board, 2-layer | 5 (minimum run) | B | carries the protection, the DC/DC, the divider, the 1-Wire passives, the I2C distribution and the DevKit socket, and replaces the bundled carrier. Component and net list in [B-002](design/B-002-main-board.md) | ~30-60 | JLCPCB, Aisler, PCBWay |
| 3-pole screw terminals, 5.08 mm | 3 | B | detachable probe cables (J4-J6) | ~7-12 | [Amazon search](https://www.amazon.de/s?k=Schraubklemme+5.08mm+PCB) |
| 2-pole screw terminal, 5.08 mm | 1 | B | 12 V entry (J3) | with the above | as above |
| 4-pole screw terminals, 5.08 mm | 2 | B | SHT31 cable and the ADS1115 analog inputs (J7, J8) | with the above | as above |
| Socket strip 2.54 mm, 1x22 | 2 | B | the DevKit plugs into these (J1, J2). Sockets, not pin headers - the DevKit has to come out | ~6-10 | [Amazon search](https://www.amazon.de/s?k=Buchsenleiste+2.54mm+22polig) |
| Socket strip 2.54 mm, 1x10 | 1 | B | ADS1115 breakout (U2) | with the above | as above |
| Pin header strip 2.54 mm, straight | 1 strip | B | IMU, I2C expansion, and the reserved and spare GPIO headers (J9-J12) | ~5-8 | [Amazon search](https://www.amazon.de/s?k=Stiftleiste+2.54mm+Sortiment) |
| ABS enclosure IP65/IP67, approx. 200 x 120 x 75 mm | 1 | C | electronics box; carrier is 84.5 x 73.7 mm and fits alongside the perfboard | ~12-20 | [Amazon search](https://www.amazon.de/s?k=ABS+Gehaeuse+IP65+200x120x75) |
| Pressure-equalisation vent membrane (Gore-type) | 1 | C | stops condensation inside the sealed box; fitted pointing down | ~8-15 | [Amazon search](https://www.amazon.de/s?k=Druckausgleichselement+Gehaeuse+IP67+Membran) |
| Cable glands M12/M16, IP68 | set | C | cable entries, fitted pointing down or sideways, with drip loops | ~7-10 | [Amazon search](https://www.amazon.de/s?k=Kabelverschraubung+IP68+M12+M16) |
| Tinned copper stranded wire 0.5-0.75 mm² | as needed | B, C | 12 V / 5 V and sensor wiring, marine grade | ~15-25 | [Amazon search](https://www.amazon.de/s?k=verzinnte+Kupferlitze+Boot+0.75mm2) |
| 4-core twisted or shielded cable, max 3 m | 1 | C | I2C run to the SHT31 outside box and cabinet: 3.3 V, GND, SDA, SCL. Twist SDA and SCL each with a ground wire, not with each other | ~5-10 | [Amazon search](https://www.amazon.de/s?k=Steuerleitung+geschirmt+4-adrig+LIYCY) |
| Clamp-on ferrites | 3 | C | optional, conducted noise on the probe cables at the box entry ([A-003](design/A-003-ds18b20-temperature-sensors.md)) | ~5-8 | [Amazon search](https://www.amazon.de/s?k=Klappferrit+Kabel+5mm) |

### Optional - continuous bilge level

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Bilge pressure probe 0-1 m, 4-20 mA, IP68 | 1 | B | hydrostatic water level, small range beats 0-5 m. **Buy one specified from 9-10 V up** - the compliance budget is tight on a discharged battery | ~30-45 | [Amazon search](https://www.amazon.de/s?k=4-20mA+Wasserstandssensor+0-1m+IP68+316L) |

Shunt resistors for it are in the precision-resistor table above.

### Optional - fourth temperature probe

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Fourth DS18B20 probe | 1 | opt | either water tank wall, or battery temperature for compensated AGM thresholds. **These compete for GPIO7, but need not** - the carrier has plenty of free pins ([A-003](design/A-003-ds18b20-temperature-sensors.md) section 8) | from the 3-pack | - |
| Aluminium tape + insulation | as needed | opt | thermal contact on a stainless tank, insulated over the top | small | - |

### Cost frame

Roughly **110-190 EUR** for stage 1 complete, depending on the bilge probe, the enclosure and
installation material. Spread across the phases:

| Phase | Outstanding spend |
|-------|-------------------|
| A | ~10-15 EUR - resistor assortment, and a breadboard if not already on hand |
| B | ~45-75 EUR, plus ~35-50 EUR if the bilge probe is fitted |
| C | ~40-70 EUR - enclosure, glands, membrane, wire |

Many small parts remain over for later stages.

---

## Tools

| Tool | Phase | Note |
|------|-------|------|
| Multimeter with DC voltage measurement | A | also the calibration reference for the battery divider, and used to meter out the probe wire colours before connecting anything |
| Breadboard and Dupont cables | A | phase A is built entirely on a breadboard |
| Laptop with PlatformIO | A | VS Code with the PlatformIO IDE extension. The firmware project is `board/` |
| Adjustable bench supply, 0-15 V, current-limited | A | strictly optional, but it allows the battery divider and its calibration factor to be proven in phase A instead of waiting for phase B ([A-002](design/A-002-bench-setup-usb.md) section 4) |
| Soldering iron approx. 320-350 °C, electronics solder, desoldering braid | **A** | needed in phase A already: the SHT31 and ADS1115 breakouts ship with loose pin headers that must be soldered on before they will sit in a breadboard or a terminal |
| Side cutters, wire strippers, small pliers, screwdrivers | B | |
| Heat-shrink tubing and hot air or a lighter | B | use carefully |
| Oscilloscope or logic analyser | stage 2 | optional now, strongly recommended for SeaTalk |

---

## Stage 2 - SeaTalk1 (preliminary, do not order yet)

The RX/TX stage is deliberately not finalised. It gets its own schematic revision and a bench test
before anything is connected to the Raymarine S1. The parts below are what that stage needs.

| Part | Qty | Purpose |
|------|-----|---------|
| 3-pole screw terminal 5.08 mm | 1 | SeaTalk +12 V / DATA / GND |
| PC817 optocoupler (or 6N137) | 1-2 | galvanic isolation of SeaTalk RX; PC817's ~4 µs edges are fine against a 208 µs bit at 4800 baud |
| 1-2 kΩ resistor | 1 | LED series resistor on the SeaTalk side of the opto |
| 10 kΩ resistor | 1 | pull-up on the ESP side of the opto output |
| 2N7002 or BSS138 N-MOSFET | 1 | SeaTalk TX open-drain driver; **replaces the 74LS07** - no 5 V rail needed, open-drain by nature |
| **10 kΩ resistor** | 1 | **gate pull-down - keeps TX off while the ESP boots or after a crash. Not optional.** |
| 100-470 Ω resistor | 1 | series resistor in the TX drain line |
| SMBJ15A or similar TVS | 1 | protection on the SeaTalk DATA line |

**Firmware note:** the ESP32 UART has no 9-bit mode, so the SeaTalk command bit has to be recovered
through the parity-error trick or a bit-banged receiver. Prove this on the bench before the
interface hardware is finalised - see the review, section 4.

---

## Stage 3 - NMEA2000 (future)

| Part | Qty | Purpose |
|------|-----|---------|
| SN65HVD230 or SN65HVD232 breakout | 1 | TWAI transceiver on GPIO17/18. **Must be a 3.3 V part - not the MCP2551**, whose 5 V RX output would sit on a GPIO rated 3.6 V absolute maximum |
| or ISO1050 / TJA1052i | 1 | isolated alternative, preferred for N2K because the backbone carries its own power and ground reference |
| N2K drop cable + T-piece | 1 | backbone connection |
| **No** 120 Ω terminator | - | the BoatHub is a drop, not a bus end. Terminators belong at the two ends of the backbone only |

A bus-powered battery monitor with a shunt was considered here and **rejected** - it is unpowered
exactly when the marina alarm matters. See [B-001](design/B-001-power-supply.md) section 8.

---

## Notes on ordering

- Multi-packs are intentional: a spare ESP32 and spare ADS1115 modules save a whole build evening
  when something dies.
- Precision resistors (0.1 %) only matter for the battery divider and the 4-20 mA shunt. Everything
  else comes out of the 1 % assortment.
- **Electrolytics must be 105 °C parts.** The sealed box reaches 55-60 °C in summer.
- On the bilge probe the exact stainless alloy is often unspecified. In salt or brackish water check
  it for corrosion regularly and mount it so it can be swapped out easily - treat it as a
  consumable.
- Many hydrostatic probes carry a vent tube inside the cable. **It must not be sealed airtight** at
  the enclosure with glue or potting compound.
