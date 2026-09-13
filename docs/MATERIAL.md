# Bill of materials

Parts, tools and rough prices for the ESP32 BoatHub, taken from the project guide v0.1 of
2026-09-13.

Prices are rough guide values in EUR. The links point to the German Amazon listings or searches
used while planning; they are a sourcing hint only. **What matters is the technical data in the
"Purpose / requirement" column** - any equivalent part will do.

Where a part was bought as a multi-pack, only some of it ends up in the build. The leftovers are
deliberate: they cover later stages and mistakes.

**Status values:** `needed` · `ordered` · `in stock` · `fitted` · `optional` · `later`

---

## Stage 1 - base monitoring

### Controller and sensors

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| ESP32-S3 DevKitC-1 N16R8, external antenna | 1 | main controller, 16 MB flash / 8 MB octal PSRAM | ~18 | in stock | [Amazon](https://amzn.eu/d/06g369jW) |
| GERUI ADS1115 16-bit I2C, 3-pack | 1 pack | ADC for battery and 4-20 mA, plus spares | 8.99 | in stock | [Amazon](https://www.amazon.de/dp/B0F1TJ16Q6) |
| DS18B20-compatible 1-Wire probes, 5 m, 3-pack | 1 pack | engine bay, bilge, fridge; potted probe | ~10-20 | in stock | [Amazon](https://www.amazon.de/gp/product/B0D8VMY5ZM) |
| SHT31-D breakout, ADR 0x44/0x45 | 1 | cabin temperature and relative humidity | ~7-12 | needed | [Amazon search](https://www.amazon.de/s?k=SHT31-D+I2C+0x44+0x45) |

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
| 4.7 kΩ resistors | 3-4 | DS18B20 pull-ups to 3.3 V | from assortment | needed | [Amazon search](https://www.amazon.de/s?k=Metallschicht+Widerstand+Sortiment+1%25) |

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
| Bilge pressure probe 0-1 m, 4-20 mA, IP68 | 1 | hydrostatic water level, small range beats 0-5 m | ~30-45 | optional | [Amazon search](https://www.amazon.de/s?k=4-20mA+Wasserstandssensor+0-1m+IP68+316L) |
| 100 Ω 0.1 % / 0.25 W | 1 | 4-20 mA shunt into ADS1115 A1 | ~5 (pack) | optional | [Amazon search](https://www.amazon.de/s?k=100+Ohm+0.1%25+Praezisionswiderstand) |

### Optional - water tank temperature

| Part | Qty | Purpose / requirement | Price | Status | Source |
|------|-----|-----------------------|-------|--------|--------|
| Fourth DS18B20 probe | 1 | tank wall temperature on GPIO7 | from the 3-pack | deferred | - |
| Aluminium tape + insulation | as needed | thermal contact on the stainless tank, insulated over the top | small | deferred | - |

### Cost frame

Roughly **100-170 EUR** for stage 1, depending on the bilge sensor, the enclosure and installation
material. Many small parts remain for later stages.

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
before anything is connected to the Raymarine S1.

| Part | Purpose | Status |
|------|---------|--------|
| 3-pole screw terminal 5.08 mm | SeaTalk +12 V / DATA / GND | later |
| Open-collector driver, e.g. 74LS07 as a reference | SeaTalk TX | design under review |
| Level shifting / isolation stage | SeaTalk RX down to 3.3 V | design under review |
| Resistors, Zener and protection parts | levels and protection | per final schematic |
| Oscilloscope or logic analyser | bench test at 4800 baud, 9th bit | strongly recommended |

---

## Stage 3 - NMEA2000 (future)

| Part | Purpose | Status |
|------|---------|--------|
| External CAN transceiver, preferably isolated | TWAI on GPIO17/18 | not dimensioned yet |
| NMEA2000 backbone, drop cables, terminators | bus wiring | planned with stage 3 |

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
