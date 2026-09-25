# Bill of materials

Parts, tools and rough prices for the ESP32 BoatHub. The single orderable list; the design
documents in [design/](design/) carry the reasoning behind each choice.

**One supplier where possible.** Everything Reichelt stocks is sourced from
[Reichelt](https://www.reichelt.de), so nearly the whole list goes into a single order. The source
column gives the Reichelt article number in `code` form, linked to the product page - the article
number is also what the Reichelt search box takes. The base modules - the DevKit set, the ADS1115
and SHT31 breakouts and the temperature probes - stay with the Amazon listings the design was built
and tested with. Beyond those, Amazon is the fallback only where Reichelt has no part that meets
the requirement, and the row says why.

**The Reichelt order as a file.** [`reichelt.csv`](reichelt.csv) holds every Reichelt part of
stage 1 with its quantity: article number and count, separated by a semicolon, one part per line.
That is the format myReichelt imports as a list, which then goes into the basket in one step. The
clamp-on ferrites are optional and in the file; delete their line if they are not wanted.

**Prices are an order of magnitude, not a quote.** They are rounded guide values in EUR to show
where the money goes; check the shop when ordering. What matters is the technical data in the
"Purpose / requirement" column - any equivalent part will do.

**Phase** says when the part is first needed, matching the build phases in
[design/README.md](design/README.md):

| Phase | | |
|-------|---|---|
| **A** | bench build on USB power | nothing runs on 12 V yet |
| **B** | power supply and main board | the 12 V side gets built |
| **C** | installation in the boat | enclosure, cable routing |
| **D** | SeaTalk1 | the boat's instrument bus |

---

## Stage 1 - base monitoring

### Controller and sensors

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| ESP32-S3 N16R8 DevKitC-1 + carrier (Heemol set) | **2** | A | main controller, 16 MB flash / 8 MB octal PSRAM, MRD076A terminal adapter included. Unbranded third-party module; bundled SMA antenna is **inert until a solder jumper is moved** (M9a, see [A-001](design/A-001-devkit-and-carrier.md)). The second board is the guide's reserve - and makes the antenna rework an A/B comparison rather than a one-way bet | ~20 each | [Amazon](https://www.amazon.de/dp/B0GJZS3P1J) |
| GERUI ADS1115 16-bit I2C, 3-pack | 1 pack | A | ADC for battery and 4-20 mA, plus spares. Addresses 0x48/0x49/0x4A | ~10 | [Amazon](https://www.amazon.de/dp/B0F1TJ16Q6) |
| DS18B20-compatible 1-Wire probes, 5 m, 3-pack | 1 pack | A | engine bay, bilge, fridge; potted. Clones - verify family code 0x28 and CRC ([A-003](design/A-003-ds18b20-temperature-sensors.md)) | ~15 | [Amazon](https://www.amazon.de/gp/product/B0D8VMY5ZM) |
| **Arduino Modulino Movement** (LSM6DSOXTR) | 1 | A | heel and pitch under sail, motion and impact at the berth. 0x6A, no clash. Chosen because its second 1x10 header carries **INT1**, which the impact detection depends on, while CS and the address pin are already tied for I2C. No pull-ups fitted, which suits a bus that already has three sets. Its on-board STM32 answers at 0x7E - expect it in a scan. **Mounted rigidly**, unlike the SHT31: an IMU on a flying lead measures the lead ([A-009](design/A-009-imu-heel-and-motion.md)) | ~15 | [Reichelt `ARD MOD MOVE`](https://www.reichelt.de/de/de/shop/produkt/arduino_modulino_node_-_movement_6-achsen-bewegungssensor-402982) |
| SHT3x-D breakout, ADR 0x44/0x45 | **3** | A | cabin temperature and relative humidity; mounted outside box and cabinet. Only one is fitted; the bus supports two (0x44/0x45), the third is a spare. **Confirm it is an SHT31** - SHT30/31/35 are pin- and protocol-compatible but differ in RH accuracy (±3 / ±2 / ±1.5 %) | ~10 each | [Amazon search](https://www.amazon.de/s?k=SHT31-D+I2C+0x44+0x45) |

### Resistors from a 1 % assortment

One metal-film assortment covers almost every value below. Buy it for phase A - the 1-Wire
pull-ups and the bench reference divider both come out of it.

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Resistor assortment, 1 % metal film, 0.25 W | 1 | A | source for all values below except 3.3 kΩ. 20 values from 1 Ω to 1 MΩ, 20 of each | ~10 | [Reichelt `QUAD 201711P002`](https://www.reichelt.de/de/de/shop/produkt/metallschicht-widerstand_0_25_w_1_ohm_-_1m_ohm_400_stueck-280148) |
| 3.3 kΩ 1 %, 0207 | 5 | A | not in the assortment; see the values table | under 1 | [Reichelt `METALL 3,30K`](https://www.reichelt.de/de/de/shop/produkt/widerstand_metallschicht_3_30_kohm_0207_0_6_w_1_-11693) |

Values drawn from it:

| Value | Qty | Phase | Purpose |
|-------|-----|-------|---------|
| **2.0-2.2 kΩ** | 3 | A | **1-Wire pull-ups, the value to fit** - either works, take what the assortment holds. 4.7 kΩ is the textbook value but marginal over 5 m ([A-003](design/A-003-ds18b20-temperature-sensors.md)) |
| 3.3 kΩ | 3 | A | 1-Wire pull-up fallback; also the extra I2C pull-up pair if the SHT31 run exceeds ~3 m. Ordered as single parts |
| 4.7 kΩ | 3 | A | 1-Wire fallback, the original guide's value. Keep a few, do not fit them first |
| 100 Ω | 3 | A | series protection in each DS18B20 DATA line |
| 10 kΩ | 2 | A | bench reference divider from the 3.3 V rail, to prove the ADS1115 without a 12 V supply ([A-002](design/A-002-bench-setup-usb.md) section 4) |
| **1 kΩ** | **4** | B | **one in series with each analogue input of the first converter - a safety part.** It holds the current into the ADS1115's input clamp to about 8 mA whatever arrives outside: a bridged divider top leg on A0, or the loop's own 12 V on the bilge channel. The converter takes VDD + 0.3 V on an input regardless of its gain setting. Do not omit ([B-001](design/B-001-power-supply.md), [B-002](design/B-002-main-board.md)) |
| 1 kΩ | **4 more** | B | the same again for the second converter's inputs. Fitted even though that module is not - it is the protection, and it must not wait on somebody remembering it when the module goes in |
| 10 kΩ + 1 kΩ | 1 each | opt | buzzer driver on GPIO21, if a buzzer is fitted |

A 10 kΩ potentiometer instead of the two 10 kΩ resistors makes the ADS1115 test better - sweep it
and check linearity end to end.

### Precision resistors, 0.1 %

Not found in a standard assortment; order separately. Tolerance only matters here.

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| 100 kΩ 0.1 %, 0.25 W | 1 | B | battery voltage divider, top leg. Factor 11.0 together with the 10 kΩ. **100 kΩ rather than 82 kΩ because both values are stocked as 0.1 % parts and 82 kΩ is not** - it costs 0.1 mV of resolution and saves a second order ([B-001](design/B-001-power-supply.md)) | ~3 | [Reichelt `WEL RC55Y-100KB`](https://www.reichelt.de/de/de/shop/produkt/widerstand_metallschicht_100_kohm_axial_250_mw_0_1_-237002) |
| 10 kΩ 0.1 %, 0.25 W | 1 | B | battery voltage divider, bottom leg | ~2 | [Reichelt `WEL RC55Y-10KBI`](https://www.reichelt.de/de/de/shop/produkt/widerstand_metallschicht_10_kohm_axial_250_mw_0_1_-237000) |
| **100 Ω 0.1 %, 0.25 W** | 1 | B | **the 4-20 mA burden for the bilge channel.** 4-20 mA across it is 0.4-2.0 V, which fills the ADS1115's +/-2.048 V range without exceeding it. Also the sacrificial part: 12 V onto the loop puts 1.4 W into it, and once it opens the 1 kΩ holds the converter's input clamp to about 8 mA ([B-002](design/B-002-main-board.md)) | ~3 | [Reichelt `WEL RC55Y-100RB`](https://www.reichelt.de/de/de/shop/produkt/widerstand_metallschicht_100_ohm_axial_250_mw_0_1_-237001) |

### Power supply and protection

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| **RECOM R-78K5.0-1.0** switching regulator | 1 | B | ESP supply. 6.5-36 V in, 5 V / 1 A out, **1 mA quiescent** - which is what keeps the standby draw down, since the converter would otherwise set the floor ([B-001](design/B-001-power-supply.md)). SIP-3, three pins at 2.54 mm | ~4 | [Reichelt `R-78K50-10`](https://www.reichelt.de/de/de/shop/produkt/dc_dc-wandler_r-78_6_5-36_vin_single_5_vout_1000_ma_sip3-422392) |
| Inline blade fuse holder, standard size (ATO) | 1 | B | cable protection close to the source. The holder is open, not sealed: mount it where it stays dry, or use a sealed inline holder instead | ~3 | [Reichelt `KFZ 019031`](https://www.reichelt.de/de/de/shop/produkt/kfz_-_sicherungshalter_ato_offene_enden-325426) |
| Blade fuse 2 A, standard size | 2 | B | one fitted, one spare | under 1 each | [Reichelt `FKS 2A`](https://www.reichelt.de/de/de/shop/produkt/kfz-flachsicherung_2a_grau-7787) |
| TVS diode 1.5KE20A | 1 | B | transient clamp, **fitted ahead of the Schottky**. 17.1 V standoff, clamps 27.7 V at 54 A. Unidirectional - the bidirectional `CA` suffix is the wrong part | under 1 | [Reichelt `1,5KE20A`](https://www.reichelt.de/de/de/shop/produkt/tvs-diode_unidirektional_17_1_v_1500_w_do-201-272804) |
| 1N5822 Schottky diode | 1 | B | reverse-polarity protection, 3 A / 40 V, DO-201AD | under 1 | [Reichelt `1N 5822`](https://www.reichelt.de/de/de/shop/produkt/schottkydiode_40_v_3_a_do-201ad-41852) |
| 100 nF / 50 V ceramic, X7R, 5 mm pitch | **10** | B | 12 V input, DC/DC output, the SHT31 far end, and one at each analogue input that has a known source - A0 and the bilge channel. The positions on the unspecified channels stay empty pads: the capacitor suits a high-impedance source and gets in the way of a fast one. Six are fitted; the rest are spares, since they cost next to nothing | under 1 each | [Reichelt `X7R-5 100N`](https://www.reichelt.de/de/de/shop/produkt/vielschicht-kerko_100_nf_50_100_v_x7r_10_rm_5_0-22865) |
| 100 µF / 35 V, **105 °C**, 8 mm, 3.5 mm pitch | 1 | B | bulk at the DC/DC input. 105 °C, not 85 °C. Diameter and pitch match the board's footprint (Panasonic FR) | under 1 | [Reichelt `RAD FR 100/35`](https://www.reichelt.de/de/de/shop/produkt/elko_radial_100_f_35v_105_c_low_esr-140121) |
| 470 µF / 16 V, **105 °C**, 10 mm, 5 mm pitch | 1 | B | bulk on the 5 V output. Diameter and pitch match the board's footprint (Panasonic FC) | under 1 | [Reichelt `RAD FC 470/16`](https://www.reichelt.de/de/de/shop/produkt/elko_radial_470_f_16_v_105_c_low_esr_10_x_12_5_mm_rm_5_0-84668) |

### Assembly and enclosure

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Fabricated main board, 2-layer | 5 (minimum run) | B | carries the protection, the DC/DC, the divider, the 1-Wire passives, the I2C distribution and the DevKit socket, and replaces the bundled carrier. Component and net list in [B-002](design/B-002-main-board.md) | ~30-60 | JLCPCB, Aisler, PCBWay |
| Screw terminals 5.08 mm, **4-pole** | 4 | B | the three probe cables and the SeaTalk cable, three wires each. **Four poles for three wires on purpose**: the 3-pole of this series is not stocked, and one spare pole costs 5 mm of board edge against a wait of months | under 1 each | [Reichelt `CTB0509-4`](https://www.reichelt.de/de/de/shop/produkt/loetbare_schraubklemme_-_4-pol_rm_5_08_mm_90_-292703) |
| Screw terminal 5.08 mm, **6-pole** | 1 | A | the IMU cable, five wires. A screw terminal although it stays inside the box, because it is on the I2C bus and a poor contact there takes every sensor with it ([B-002](design/B-002-main-board.md)). Six poles because the series has no 5-pole | ~1 | [Reichelt `CTB0509-6`](https://www.reichelt.de/de/de/shop/produkt/loetbare_schraubklemme_-_6-pol_rm_5_08_mm_90_-292704) |
| Screw terminal 5.08 mm, 2-pole | 2 | B | 12 V entry, and the bilge level sender - a loop-powered 4-20 mA probe is two wires. The bilge terminal is fitted whether or not that probe is bought; a few cents keep the channel usable without another board revision | under 1 each | [Reichelt `CTB0509-2`](https://www.reichelt.de/de/de/shop/produkt/loetbare_schraubklemme_-_2-pol_rm_5_08_mm_90_-292701) |
| Screw terminal 5.08 mm, 4-pole | 1 | B | the SHT31 cable. One pitch across the whole board, so there is nothing to confuse when ordering or when soldering | under 1 | [Reichelt `CTB0509-4`](https://www.reichelt.de/de/de/shop/produkt/loetbare_schraubklemme_-_4-pol_rm_5_08_mm_90_-292703) |
| Socket strip 2.54 mm, 1x40 | 3 | B | cut to 1x22 for the DevKit sockets J7 and J11, 1x10 for the ADS1115 breakouts J6 and J10, and 1x08 for the spare analogue channels J1 - 72 positions in all. Three strips rather than two, because cutting a socket strip costs the position it is cut at. Sockets throughout, never pin headers: the DevKit has to come out, and bare pins standing on the board short against a lid. J10 is fitted and left empty until its channels are specified - an empty socket costs nothing and loads nothing. J1 is a socket for the same reason it is not a terminal: the conditioning an unspecified sender needs belongs on a small adapter, not on the board that is hardest to change | ~3 each | [Reichelt `BKL 10120978`](https://www.reichelt.de/de/de/shop/produkt/buchsenleiste_2_54mm_1x40_gerade-266702) |
| ABS enclosure IP65, 200 x 120 x 75 mm | 1 | C | electronics box. **Internal floor at least 151 x 108 mm** - the board is 134.62 x 91.44 and each gland nut stands about 5 mm proud of the wall it passes through. Internal height at least 35 mm: 6 mm under the board for trimmed leads, and the DevKit standing in its sockets needs roughly 14 of the rest. This one (BOXEXPERT Elbe) gives 191 x 111 x 68 mm inside and has mounting bosses in the floor. Measure the wall where the glands go - see the thread length below | ~25 | [Reichelt `ELBE 20012075 SW`](https://www.reichelt.de/de/de/shop/produkt/industriegehaeuse_elbe_200_x_120_x_75_mm-285710) |
| Pressure-equalisation vent, M12 x 1.5, with SW 17 nut | 1 | C | stops condensation inside the sealed box; fitted pointing down. The same thread as the cable glands, so it takes the same hole - an eighth one. Its thread is 10 mm long, so it reaches through any wall the glands do | ~8 | [Reichelt `HGR 595.15.AS`](https://www.reichelt.de/de/de/shop/produkt/druckausgleichselement_m12x1_5-285020) |
| Tinned copper stranded wire 0.5-0.75 mm² | as needed | B, C | 12 V / 5 V and sensor wiring, marine grade. **Amazon because Reichelt has no tinned-strand single wire** | ~15-25 | [Amazon search](https://www.amazon.de/s?k=verzinnte+Kupferlitze+Boot+0.75mm2) |
| 4-core twisted or shielded cable, max 3 m | 1 | C | I2C run to the SHT31 outside box and cabinet: 3.3 V, GND, SDA, SCL. Twist SDA and SCL each with a ground wire, not with each other. **Amazon because Reichelt sells LiYCY only in 100 m rolls** (`LIYCY 4X0,25-100`); worth it only if the roll has other uses | ~5-10 | [Amazon search](https://www.amazon.de/s?k=Steuerleitung+geschirmt+4-adrig+LIYCY) |
| Clamp-on ferrites for 5 mm cable | 3 | C | optional, conducted noise on the probe cables at the box entry ([A-003](design/A-003-ds18b20-temperature-sensors.md)) | ~2 each | [Reichelt `FR 0431173951`](https://www.reichelt.de/de/de/shop/produkt/klappferrit_fuer_5_mm_180_ohm_100_mhz-434770) |

### Cable entries

Seven cables leave the box, and every one of them uses the same hole. The gland
decides what cable fits, not the enclosure, so one diameter serves the 3-4 mm
probe cables, the 12 V feed and the SeaTalk run alike.

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Cable gland M12 x 1.5, clamping range 3.5-7 mm, IP69K | 7 | C | one per cable leaving the box, fitted pointing down or sideways with a drip loop | under 1 each | [Reichelt `LAPP 53111000`](https://www.reichelt.de/de/de/shop/produkt/kabelverschraubung_m_12_x_1_5_3_5_-_7_mm_silbergrau_ip69k-158605) |
| Lock nut M12 x 1.5, SW 17, glass-filled PA | 7 | C | holds each gland in the wall. Ordered separately because it is not always in the bag | under 1 each | [Reichelt `LAPP 53119000`](https://www.reichelt.de/de/de/shop/produkt/gegenmutter_sechskant_m_12_x_1_5_mm_sw_17_mm_silbergrau-158699) |

**Which seven.** The three temperature probes on J14, J15 and J16, the SHT31 on
J2, 12 V in on J9, SeaTalk on J17, and the bilge level sender on J12. The IMU
gets none: it is terminated on J4 like the probes, but it lives inside the box
and its cable never leaves.

**Plastic, not brass.** Both are stocked and the brass ones are better made. A
boat in salt air already has more dissimilar metals in contact than anyone
wants, and nothing here needs the strength.

Two of the gland's own dimensions are requirements on whatever encloses the
board - bought or printed, they apply either way:

- **The thread is 8 mm long.** The wall it passes through can be about 4 mm
  thick at most, or the nut has nothing left to catch.
- **The nut is SW 17** and stands roughly 5 mm proud of the inside face. The
  board has to sit at least that far back from every wall a gland goes through,
  which is where the 151 x 108 mm internal floor above comes from.

A parametric model of a printed alternative lives in
[`hardware/case/`](../hardware/case/README.md). It is a bench housing and a way
of checking reach in three dimensions, not a replacement for the bought box: an
FDM print leaks along its layer lines, and on a boat the enclosure is the
corrosion measure.

### Optional - continuous bilge level

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Bilge pressure probe 0-1 m, 4-20 mA, IP68 | 1 | B | hydrostatic water level, small range beats 0-5 m. **Buy one specified from 9-10 V up** - the compliance budget is tight on a discharged battery. Amazon because Reichelt carries no submersible level probe | ~30-45 | [Amazon search](https://www.amazon.de/s?k=4-20mA+Wasserstandssensor+0-1m+IP68+316L) |

The 100 Ohm burden for it is in the precision-resistor table above, and is fitted whether or not
the probe is bought - it costs cents and it is what makes the channel testable with a current
source on the bench.

### Optional - fourth temperature probe

| Part | Qty | Phase | Purpose / requirement | Price | Source |
|------|-----|-------|-----------------------|-------|--------|
| Fourth DS18B20 probe | 1 | opt | either water tank wall, or battery temperature for compensated AGM thresholds. **These compete for GPIO7, but need not** - the carrier has plenty of free pins ([A-003](design/A-003-ds18b20-temperature-sensors.md) section 8) | from the 3-pack | - |
| Aluminium tape + insulation | as needed | opt | thermal contact on a stainless tank, insulated over the top | small | - |

### Cost frame

Roughly **250-350 EUR** for stage 1 complete, depending on the bilge probe, the board run and
installation material. Spread across the phases:

| Phase | Spend |
|-------|-------|
| A | ~100-130 EUR - the two DevKit sets, the ADS1115 pack, the probes, the IMU, the SHT31 breakouts and the resistor assortment, plus a breadboard if not already on hand |
| B | ~60-100 EUR, most of it the board run, plus ~30-45 EUR if the bilge probe is fitted |
| C | ~60-80 EUR - enclosure, glands, vent, wire |

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
| Oscilloscope or logic analyser | D | **not optional for SeaTalk.** Bit timing, the command bit and the optocoupler's edges are all measurements, not guesses ([D-002](design/D-002-seatalk-decoding.md)) |

---

## Stage 2 - SeaTalk1 (preliminary, do not order yet)

The receive stage is specified in [D-001](design/D-001-seatalk-rx-stage.md), the transmit stage in
[D-003](design/D-003-seatalk-tx-stage.md). **Nothing is connected to the Raymarine S1 until both
have passed a bench test against a simulated bus.**

| Part | Qty | Purpose |
|------|-----|---------|
| 4-pole screw terminal, 5.08 mm | 1 | SeaTalk +12 V / DATA / GND, one pole unused. Shared by both directions; same part as the probe terminals |
| PC817 optocoupler (or 6N137) | 1-2 | galvanic isolation of SeaTalk RX; PC817's ~4 µs edges are fine against a 208 µs bit at 4800 baud. Reichelt [`PC817X1NSZ1B`](https://www.reichelt.de/de/de/shop/produkt/optokoppler_5kv_80v_4_8ma_80_dip-4-319088), DIP-4 |
| 4.7 kΩ resistor | 1 | LED series resistor on the SeaTalk side of the opto. ~2.3 mA is plenty for a PC817 and keeps the load off the instrument bus, which is held high by pull-ups inside the instruments. A 6N137 would want 1-2 kΩ instead ([D-001](design/D-001-seatalk-rx-stage.md)) |
| 10 kΩ resistor | 1 | pull-up on the ESP side of the opto output |
| **BC337-25** NPN, TO-92 | 1 | SeaTalk TX driver, Reichelt [`BC 337-25`](https://www.reichelt.de/de/de/shop/produkt/bipolartransistor_npn_45v_0_8a_0_625w_to-92-4986). hFE 160 minimum against the 100 of a 2N3904, and four times the current headroom. **Not a small MOSFET**: the common logic-level types are surface mount, and the through-hole ones specify a gate threshold of up to 3 V, which a 3.3 V pin barely clears ([D-003](design/D-003-seatalk-tx-stage.md)) |
| 1 kΩ resistor | 1 | base resistor from IO16 - 2.6 mA of base current, ample for a bus that needs ten |
| **10 kΩ resistor** | 1 | **base to emitter - keeps the transmitter off while the ESP boots, crashes or is unpowered. Not optional**: it is the only thing between a dead board and a dead instrument network |
| 100 Ω resistor | 1 | series resistor in the collector line. The low end of the usual range on purpose: it divides against the bus pull-up, and 470 Ω would leave the low level too high to be read as low |
| 1.5KE20A TVS | 1 | protection on the SeaTalk DATA line. **The same part as the supply input's**: a 15 V device would sit at its threshold whenever the bank is in absorption ([D-001](design/D-001-seatalk-rx-stage.md)) |

The resistors come out of the 1 % assortment.

**Firmware note:** the ESP32 UART has no 9-bit mode, so the SeaTalk command bit has to be recovered
another way - [D-002](design/D-002-seatalk-decoding.md) settles on a bit-banged receiver and says
why. Prove it on the bench before the interface hardware is finalised: a receiver that measures
edges rather than sampling mid-bit would want the faster 6N137 instead of the PC817.

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
- **Radial parts must match the board's footprints**: 100 nF at 5 mm pitch, the 100 µF at 8 mm
  diameter and 3.5 mm pitch, the 470 µF at 10 mm and 5 mm. An equivalent from another series has to
  match these too ([B-002](design/B-002-main-board.md) section 7).
- On the bilge probe the exact stainless alloy is often unspecified. In salt or brackish water check
  it for corrosion regularly and mount it so it can be swapped out easily - treat it as a
  consumable.
- Many hydrostatic probes carry a vent tube inside the cable. **It must not be sealed airtight** at
  the enclosure with glue or potting compound.
