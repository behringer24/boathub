# Bill of materials

Parts, tools and rough prices for the ESP32 BoatHub, taken from the project guide v0.1 of
2026-09-13.

Prices are rough guide values in EUR. The links point to the German Amazon listings or searches
used while planning; they are a sourcing hint only. **What matters is the technical data in the
"Purpose / requirement" column** - any equivalent part will do.

Where a part was bought as a multi-pack, only some of it ends up in the build. The leftovers are
deliberate: they cover later stages and mistakes.

**Status values:** `needed` · `ordered` · `in stock` · `fitted` · `optional` · `later`

> Parts marked **[review]** were added by the design review in
> [design/000-design-review.md](design/000-design-review.md). The finding ID in the note column
> explains why.

---

## Stage 1 - base monitoring

### Controller and sensors

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| ESP32-S3 N16R8 DevKitC-1 + carrier (Heemol set) | 1 | main controller, 16 MB flash / 8 MB octal PSRAM, MRD076A terminal adapter included. Third-party module (sparkleIoT XH-S3E); bundled SMA antenna is **inert until a solder jumper is moved** (M9a, see [A-001](design/A-001-devkit-and-carrier.md)) | ~18 | in stock | [Amazon](https://www.amazon.de/dp/B0GJZS3P1J) |
| GERUI ADS1115 16-bit I2C, 3-pack | 1 pack | ADC for battery and 4-20 mA, plus spares | 8.99 | in stock | [Amazon](https://www.amazon.de/dp/B0F1TJ16Q6) |
| DS18B20-compatible 1-Wire probes, 5 m, 3-pack | 1 pack | engine bay, bilge, fridge; potted probe | ~10-20 | in stock | [Amazon](https://www.amazon.de/gp/product/B0D8VMY5ZM) |
| SHT31-D breakout, ADR 0x44/0x45 | 1 | cabin temperature and relative humidity; mounted outside box and cabinet (I1a) | ~7-12 | needed | [Amazon search](https://www.amazon.de/s?k=SHT31-D+I2C+0x44+0x45) |

### Power supply and protection

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| DC/DC converter 9-36 V to 5 V, min. 3 A | 1 | ESP supply, wide input for house-supply swings | ~9-15 | needed | [Amazon search](https://www.amazon.de/s?k=9-36V+5V+3A+DC+DC+wasserdicht) |
| Automotive blade fuse holder + 2 A fuses | 1 set | cable protection close to the source | ~6-10 | needed | [Amazon search](https://www.amazon.de/s?k=wasserdichter+KFZ+Flachsicherungshalter+2A) |
| 1N5822 Schottky diode | 1 (pack) | simple reverse-polarity protection | ~4-7 | needed | [Amazon search](https://www.amazon.de/s?k=1N5822+Schottky+Diode) |
| TVS diode 1.5KE20A | 1 (pack) | transient protection on the 12 V side | ~5-8 | needed | [Amazon search](https://www.amazon.de/s?k=1.5KE20A+TVS) |
| 100 µF / 35 V, 470 µF / 16 V, 100 nF / 50 V | 1 each, several 100 nF | supply filtering before and after the DC/DC | ~8-15 | needed | [Amazon search](https://www.amazon.de/s?k=Kondensator+Sortiment+100nF+100uF+470uF) |

### Measurement and passives

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| 82 kΩ 0.1 % + 10 kΩ 0.1 % | 1 each | battery voltage divider, factor 9.2 | ~5-10 (pack) | needed | [Amazon search](https://www.amazon.de/s?k=82k+10k+0.1%25+Praezisionswiderstand) |
| 4.7 kΩ resistors | 3-4 | DS18B20 pull-ups per the original guide - **superseded by 2.2 kΩ below** for the 5 m probes (I3) | from assortment | needed | [Amazon search](https://www.amazon.de/s?k=Metallschicht+Widerstand+Sortiment+1%25) |

### Assembly and enclosure

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| Perfboard 2.54 mm, individual pads | 1 | protection and distribution board | ~7-10 | needed | [Amazon search](https://www.amazon.de/s?k=Punktrasterplatine+2.54mm) |
| Screw terminals 5.08 mm | set | detachable sensor leads | ~7-12 | needed | [Amazon search](https://www.amazon.de/s?k=Schraubklemme+5.08mm+PCB) |
| ABS enclosure IP65/IP67, approx. 200 x 120 x 75 mm | 1 | electronics box | ~12-20 | needed | [Amazon search](https://www.amazon.de/s?k=ABS+Gehaeuse+IP65+200x120x75) |
| Cable glands M12/M16, IP68 | set | cable entries, fitted pointing down or sideways | ~7-10 | needed | [Amazon search](https://www.amazon.de/s?k=Kabelverschraubung+IP68+M12+M16) |
| Tinned copper stranded wire 0.5-0.75 mm² | as needed | 12 V / 5 V and sensor wiring, marine grade | ~15-25 | needed | [Amazon search](https://www.amazon.de/s?k=verzinnte+Kupferlitze+Boot+0.75mm2) |

### Optional - continuous bilge level

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| Bilge pressure probe 0-1 m, 4-20 mA, IP68 | 1 | hydrostatic water level, small range beats 0-5 m; **check its minimum supply voltage** (I2) | ~30-45 | optional | [Amazon search](https://www.amazon.de/s?k=4-20mA+Wasserstandssensor+0-1m+IP68+316L) |
| 100 Ω 0.1 % / 0.25 W | 2 | 4-20 mA shunt into ADS1115 A1; **two in parallel give 50 Ω**, halving the burden voltage at identical resolution (I2) | ~5 (pack) | optional | [Amazon search](https://www.amazon.de/s?k=100+Ohm+0.1%25+Praezisionswiderstand) |

### Optional - water tank temperature

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| Fourth DS18B20 probe | 1 | tank wall temperature on GPIO7 | from the 3-pack | deferred | - |
| Aluminium tape + insulation | as needed | thermal contact on the stainless tank, insulated over the top | small | deferred | - |

### Additions from the design review **[review]**

| Part | Qty | Purpose / requirement | Price | Status | Finding |
|------|-----|-----------------------|-------|--------|---------|
| 2.2 kΩ resistors | 3-4 | **1-Wire pull-ups, the value to fit** - 4.7 kΩ is the textbook value but marginal at 5 m | from assortment | needed | I3 |
| 3.3 kΩ resistors | 3-4 | 1-Wire pull-up fallback, if 2.2 kΩ ever proves too strong | from assortment | needed | I3 |
| 100 Ω resistors | 3-4 | series protection in the DS18B20 DATA lines | from assortment | needed | I3 |
| 4-core twisted/shielded cable, max 3 m | 1 | I2C run to the SHT31 outside box and cabinet: 3.3 V, GND, SDA, SCL | ~5-10 | needed | I1a |
| Pressure-equalisation vent membrane (Gore-type) | 1 | stops condensation inside the sealed box | ~8-15 | needed | I7 |
| Electrolytics rated 105 °C, not 85 °C | - | specification of the caps above, not an extra part | - | needed | I7 |
| 3.3 kΩ resistors | 2 | extra I2C pull-up pair, only if the SHT31 run exceeds ~3 m | from assortment | conditional | I1a |
| ~~DC/DC with a specified low quiescent current~~ | - | **not needed** - the boat is permanently on shore power | - | dropped | B1 |
| Small-signal transistor + 1 kΩ + 10 kΩ | 1 set | buzzer driver; a buzzer must not hang directly on GPIO21 | ~3 | optional | M5 |

### Cost frame

Roughly **100-170 EUR** for stage 1, depending on the bilge sensor, the enclosure and installation
material, plus about **15-30 EUR** for the review additions above. Many small parts remain for
later stages.

---

## Tools

| Tool | Note |
|------|------|
| Soldering iron approx. 320-350 °C, electronics solder, desoldering braid | |
| Multimeter with DC voltage measurement | also the calibration reference for the battery divider |
| Side cutters, wire strippers, small pliers, screwdrivers | |
| Heat-shrink tubing and hot air or a lighter | use carefully |
| Breadboard and Dupont cables | for the first bench tests |
| Laptop with Arduino IDE or PlatformIO | |
| Adjustable 12 V bench supply | optional, useful for bench tests |
| Oscilloscope or logic analyser | optional now, strongly recommended for SeaTalk in stage 2 |

---

## Stage 2 - SeaTalk1 (preliminary, do not order yet)

The RX/TX stage is deliberately not finalised. It gets its own schematic revision and a bench test
before anything is connected to the Raymarine S1. The design review turned the guide's vague
"resistors / Zener / protection parts" into the concrete list below **[review]**.

| Part | Qty | Purpose | Status | Finding |
|------|-----|---------|--------|---------|
| 3-pole screw terminal 5.08 mm | 1 | SeaTalk +12 V / DATA / GND | later | - |
| PC817 optocoupler (or 6N137) | 1-2 | galvanic isolation of SeaTalk RX; PC817's ~4 µs edges are fine against a 208 µs bit at 4800 baud | later | - |
| 1-2 kΩ resistor | 1 | LED series resistor on the SeaTalk side of the opto | later | - |
| 10 kΩ resistor | 1 | pull-up on the ESP side of the opto output | later | - |
| 2N7002 or BSS138 N-MOSFET | 1 | SeaTalk TX open-drain driver; **replaces the 74LS07** - no 5 V rail needed, open-drain by nature | later | I5 |
| **10 kΩ resistor** | 1 | **gate pull-down - keeps TX off while the ESP boots or after a crash. Not optional.** | later | I5 |
| 100-470 Ω resistor | 1 | series resistor in the TX drain line | later | I5 |
| SMBJ15A or similar TVS | 1 | protection on the SeaTalk DATA line | later | - |
| Oscilloscope or logic analyser | 1 | bench test at 4800 baud, 9th bit | strongly recommended | - |

**Firmware note:** the ESP32 UART has no 9-bit mode, so the SeaTalk command bit has to be recovered
through the parity-error trick or a bit-banged receiver. Prove this on the bench before the
interface hardware is finalised - see the review, section 4.

---

## Stage 3 - NMEA2000 (future)

| Part | Qty | Purpose | Status | Finding |
|------|-----|---------|--------|---------|
| SN65HVD230 or SN65HVD232 breakout | 1 | TWAI transceiver on GPIO17/18. **Must be a 3.3 V part - not the MCP2551**, whose 5 V RX output would sit on a GPIO rated 3.6 V absolute maximum | later | I6 |
| or ISO1050 / TJA1052i | 1 | isolated alternative, preferred for N2K because the backbone carries its own power and ground reference | later | I6 |
| N2K drop cable + T-piece | 1 | backbone connection | later | - |
| **No** 120 Ω terminator | - | the BoatHub is a drop, not a bus end. Terminators belong at the two ends of the backbone only | later | I6 |

---

## Notes on ordering

- Multi-packs are intentional: a spare ESP32 and spare ADS1115 modules save a whole build evening
  when something dies.
- Precision resistors (0.1 %) only matter for the battery divider and the 4-20 mA shunt. Everything
  else can come out of a standard assortment.
- On the bilge probe the exact stainless alloy is often unspecified. In salt or brackish water check
  it for corrosion regularly and mount it so it can be swapped out easily.
- Many hydrostatic probes carry a vent tube inside the cable. **It must not be sealed airtight** at
  the enclosure with glue or potting compound.
