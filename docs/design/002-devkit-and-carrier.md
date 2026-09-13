# 002 - DevKit, carrier board and antenna

| | |
|---|---|
| **Status** | Draft |
| **Stage** | 1, phases 1A and 1C |
| **Roadmap package** | 1A.1, 1C.2 |
| **Created** | 2026-09-13 |
| **Last changed** | 2026-09-13 |
| **Touches hardware** | yes |

## 1. Goal

Record what the delivered board actually is, map the project pin plan onto the carrier board's
screw terminals, and settle the antenna question.

Answers open question 4 from [000-design-review.md](000-design-review.md) and replaces the
assumption there that the board is an ESP32-S3-WROOM-1**U**.

## 2. What was actually delivered

Heemol set, Amazon ASIN **B0GJZS3P1J**, "ESP32-S3 N16R8 DevKitC-1 with expansion board".

Source: the seller's own product images. The Amazon listing itself could not be retrieved
programmatically, so **everything below should be confirmed against the physical board** on the
first bench evening - particularly the pin order on the carrier terminals, which is transcribed
from a photograph.

| Item | Detail |
|------|--------|
| Module | Marked **sparkleIoT XH-S3E**, WiFi+BT, N16R8. The listing calls it "ESP32-S3-WROOM-1-N16R8", but the silkscreen shows a **third-party module**, not a genuine Espressif WROOM-1. Pin-compatible. |
| Antenna | **Both** a PCB antenna and a U.FL/IPEX socket, selected by a solder jumper. See section 4. |
| Regulator | AMS1117-3.3 (SOT-223) |
| USB | **Two** Type-C ports: one native ESP32-S3 USB/OTG on GPIO19/20, one USB-serial via **CH343P** |
| Buttons | RST and BOOT |
| LEDs | PWR, TX, RX, plus a **WS2812 RGB** LED |
| Carrier | MRD076A "Terminal Adapter for ESP32-S3", 84.5 x 73.7 mm, all pins on screw terminals |
| In the box | DevKit, carrier board, U.FL-to-SMA pigtail, 2 dBi SMA antenna |

### The module is a clone

Not a problem in itself - it is pin-compatible and carries the same N16R8 configuration - but it
means the Espressif datasheet is a **reference, not a guarantee** for this specific board. RF
performance and the seller's "2 dB" antenna gain claim are unverified. If anything RF-related
behaves oddly, this is the first thing to suspect.

## 3. Pin plan mapped to the carrier terminals

The carrier breaks every pin out to screw terminals in two rows. Conveniently, **the entire project
pin plan except GPIO21 sits on the top row.**

### Top row (left to right as printed)

`GND · 5V · IO14 · IO13 · IO12 · IO11 · IO10 · IO9 · IO46 · IO3 · IO8 · IO18 · IO17 · IO16 · IO15 · IO7 · IO6 · IO5 · IO4 · RST · 3.3V · 3.3V`

### Bottom row

`GND · GND · IO19 · IO20 · IO21 · IO47 · IO48 · IO45 · IO0 · IO35 · IO36 · IO37 · IO38 · IO39 · IO40 · IO41 · IO42 · IO2 · IO1 · RX · TX · GND`

### Project allocation

| Terminal | Row | Function | Stage |
|----------|-----|----------|-------|
| `5V` | top | DC/DC 5 V feed in, routes to the DevKit 5Vin pin | 1 |
| `3.3V` x2 | top | sensor rail for DS18B20, SHT31, ADS1115 | 1 |
| `GND` x4 | both | star point (M8) | 1 |
| `IO4` | top | DS18B20 engine bay | 1 |
| `IO5` | top | DS18B20 bilge water | 1 |
| `IO6` | top | DS18B20 fridge | 1 |
| `IO7` | top | spare, optional tank DS18B20 | 1 |
| `IO8` | top | I2C SDA | 1 |
| `IO9` | top | I2C SCL | 1 |
| `IO15` | top | reserved, SeaTalk RX | 2 |
| `IO16` | top | reserved, SeaTalk TX | 2B |
| `IO17` | top | reserved, TWAI TX | 3 |
| `IO18` | top | reserved, TWAI RX | 3 |
| `IO21` | bottom | spare, optional buzzer via a transistor (M5) | 1 |
| `RX` / `TX` | bottom | debug UART (GPIO44/43), keep free for service | - |

### Terminals that must not be used

The carrier exposes several pins that are unusable on this board. They sit on labelled screw
terminals, which makes them tempting - **label them off or note it on the enclosure lid.**

| Terminal | Why |
|----------|-----|
| `IO35` `IO36` `IO37` | **Octal PSRAM on the N16R8.** Using them crashes the board. The most dangerous trap on this carrier, because nothing on the silkscreen warns you. |
| `IO0` `IO3` `IO45` `IO46` | strapping pins - affect boot mode |
| `IO19` `IO20` | native USB D-/D+, used by the direct USB-C port |

### Genuinely free spares

`IO1` `IO2` `IO10` `IO11` `IO12` `IO13` `IO14` `IO38` `IO39` `IO40` `IO41` `IO42` `IO47` `IO48`

That is plenty of expansion headroom. Note that the **WS2812 RGB LED is wired to either GPIO38 or
GPIO48** depending on board revision - determine which by test before using either as a spare.

## 4. Antenna - the external antenna does not work out of the box

**This is the finding that changes the install plan.**

The seller's note states: the onboard antenna is selected at position ① by default, and *"if you
want to use an external antenna at position ②, you must solder it yourself"*. A small solder jumper
next to the U.FL socket routes the module's RF output either to the PCB antenna trace or to the
U.FL connector.

**Consequences:**

1. **As delivered, the supplied SMA antenna is inert.** Plugging the pigtail into the U.FL socket
   changes nothing - the RF path still goes to the PCB antenna.
2. Activating it means moving a 0402/0603-sized solder blob **in the RF path**, on the most
   expensive component in the build. It is the most delicate soldering in the whole project.
3. After the rework the PCB antenna is disconnected, so the external antenna must **always** be
   fitted before transmitting, or the PA drives into an open circuit.

This invalidates the guide's plan of mounting an external antenna high in the compartment as a
given. It is now a conditional step.

### Decision: measure first, rework only if needed

Both the ABS enclosure and the GFK deck are RF-transparent, so the onboard antenna may well be
sufficient. The honest way to decide is to measure rather than to assume.

1. Assemble with the **onboard antenna, no rework**.
2. At the real installation point, inside the closed enclosure, in the actual compartment, log the
   **RSSI to the marina AP** over a few hours.
3. Judge: better than **-70 dBm** is comfortable, -70 to -80 dBm is workable, below -80 dBm needs
   the external antenna.
4. Only if it falls short: do the rework, then repeat the same measurement to confirm it actually
   helped.

Doing it in this order means the risky rework is only attempted when there is evidence it is
needed, and there is a before/after number to prove it worked.

## 5. Consequences for the rest of the build

### The carrier replaces part of the perfboard

The guide's plan was a perfboard carrying screw terminals plus the protection circuit. The carrier
already provides the screw terminals and a socketed, replaceable DevKit - which is exactly what the
guide asked for ("do not solder the DevKit in permanently").

**Revised split:**

| Board | Carries |
|-------|---------|
| MRD076A carrier | DevKit socket, all signal and 3.3 V / 5 V terminals |
| Perfboard (smaller than planned) | fuse, TVS, 1N5822, DC/DC, bulk caps, battery divider, 1-Wire pull-ups and series resistors, 4-20 mA shunt |

The perfboard still earns its place: **the carrier's terminals connect straight to bare GPIOs with
no protection**, so every pull-up, series resistor and filter from [001](001-power-supply.md) and
the sensor documents still has to live somewhere.

### Mechanical

Carrier 84.5 x 73.7 mm inside a 200 x 120 x 75 mm enclosure - fits with room for the perfboard
alongside. Stack height with the socketed DevKit is roughly 20 mm against 75 mm of depth. Both USB-C
ports face sideways; leave access to them or accept opening the box to reflash.

### Power budget refinement

Two items to fold into the B1 estimate:

- the **AMS1117-3.3 ground current** is roughly 5-10 mA on its own
- the **WS2812 draws around 1 mA even with no data**, and the CH343P plus the PWR LED add more

This does not change the conclusion - shore power is confirmed - but it does mean the DevKit
overhead is nearer 10-18 mA than the 5-8 mA assumed in B1. The measurement in the
[001](001-power-supply.md) test plan settles it either way.

For a permanent installation the PWR LED and the CH343P are wasted current and can be removed, but
with shore power there is no reason to bother.

### Flashing

Use the **CH343P port** (the USB-serial one) for flashing; the native USB port occupies GPIO19/20.
The guide's rule stands for both ports: **external 5 V off while USB is connected.**

## 6. Test

- [ ] Read the module silkscreen and confirm N16R8
- [ ] **Verify the carrier terminal order against section 3** - it was transcribed from a product
      photo, not from the board in hand
- [ ] Blink and serial test over the CH343P port
- [ ] Confirm the board boots and PSRAM is detected (proves IO35-37 are in use and off limits)
- [ ] Verify which GPIO drives the WS2812
- [ ] Continuity-check each project terminal on the carrier through to the right DevKit pin before
      wiring sensors
- [ ] RSSI measurement per section 4, in the closed enclosure at the real mounting point

## 7. Open points

| Point | Decide by | Who |
|-------|-----------|-----|
| Onboard antenna sufficient, or do the rework? | after the RSSI measurement | both |
| Which GPIO carries the WS2812 | first bench evening | both |
| Whether to fit the carrier at all, or keep the original all-perfboard plan | before drilling the enclosure | both |

## 8. References

- [000-design-review.md](000-design-review.md) - open question 4, findings M8, M9, M5
- [001-power-supply.md](001-power-supply.md) - 5 V feed and current budget
- [ESP32-S3-DevKitC-1 hardware reference](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
