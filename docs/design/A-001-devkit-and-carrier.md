# A-001 - DevKit, carrier board and antenna

| | |
|---|---|
| **Phase** | A and C |
| **Software version** | v1 |
| **Touches hardware** | yes |

## 1. Goal

Record what the delivered board actually is, map the project pin plan onto the carrier board's
screw terminals, and settle the antenna question.

## 2. The board

Heemol set, Amazon ASIN **B0GJZS3P1J**, "ESP32-S3 N16R8 DevKitC-1 with expansion board". Sets sold
under this description vary, so check yours against the table below - section 6 lists how.

| Item | Detail |
|------|--------|
| Module | The shield reads only `ESP32-S3-N16R8` / `WIFI+BT Model` / `ISM 2.4G 802.11 b/g/n` - no manufacturer, no WROOM designation. An **unbranded third-party module**, pin-compatible with the WROOM-1 and carrying the same 16 MB flash / 8 MB octal PSRAM. |
| Antenna | **Both** a PCB antenna and a U.FL/IPEX socket, selected by a solder jumper. See section 4. |
| Regulator | AMS1117-3.3 (SOT-223) |
| USB | **Two** Type-C ports: one native ESP32-S3 USB/OTG on GPIO19/20, one USB-serial via **CH343P** - which socket is which is in section 5 |
| Buttons | RST and BOOT |
| LEDs | PWR, TX, RX, plus a **WS2812 RGB** LED |
| Carrier | MRD076A "Terminal Adapter for ESP32-S3", 84.5 x 73.7 mm, all pins on screw terminals |
| In the box | DevKit, carrier board, U.FL-to-SMA pigtail, 2 dBi SMA antenna |

### The module is an unbranded clone

Not a problem in itself - it is pin-compatible and carries the same N16R8 configuration - but it
means the Espressif datasheet is a **reference, not a guarantee** for this specific board. RF
performance and the seller's "2 dB" antenna gain claim are unverified. If anything RF-related
behaves oddly, this is the first thing to suspect.

There is no manufacturer label to look up, so **the antenna variant cannot be established from a
datasheet.** Section 4 answers that question by looking at the board instead.

## 3. Pin plan mapped to the carrier terminals

The carrier breaks every pin out to screw terminals in two rows. Conveniently, **the entire project
pin plan except GPIO21 sits on the top row.**

The carrier is what the bench build wires into. In the finished system the main board
([B-002](B-002-main-board.md)) takes its place, socketing the DevKit directly and carrying the
circuitry as well - but it has to match these same two rows, so the tables below outlive it.

Both rows below are listed **starting at the USB end** of the DevKit. That is the only unambiguous
reference point: "left" depends on which way up the board is held, while the two USB-C sockets sit
at one end and the antenna at the other, and no amount of rotating changes which is which.

### Top row - the column carrying IO4 to IO9

`GND · 5V · IO14 · IO13 · IO12 · IO11 · IO10 · IO9 · IO46 · IO3 · IO8 · IO18 · IO17 · IO16 · IO15 · IO7 · IO6 · IO5 · IO4 · RST · 3.3V · 3.3V`

### Bottom row - the column carrying IO19 to IO21 and IO2

`GND · GND · IO19 · IO20 · IO21 · IO47 · IO48 · IO45 · IO0 · IO35 · IO36 · IO37 · IO38 · IO39 · IO40 · IO41 · IO42 · IO2 · IO1 · RX · TX · GND`

So the `3.3V` pair and the lone `GND` sit at the **antenna** end, and `GND`/`5V` and the `GND` pair
at the **USB** end. The carrier's own screw terminals may be silkscreened in either direction; the
DevKit's numbering is the authority, because that is what a socket has to mate with.

### Project allocation

| Terminal | Row | Function | Stage |
|----------|-----|----------|-------|
| `5V` | top | DC/DC 5 V feed in, routes to the DevKit 5Vin pin | 1 |
| `3.3V` x2 | top | sensor rail for DS18B20, SHT31, ADS1115, IMU | 1 |
| `GND` x4 | both | star point | 1 |
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
| `IO2` | bottom | IMU interrupt line ([A-009](A-009-imu-heel-and-motion.md)) | 1 |
| `IO21` | bottom | spare, optional buzzer via a transistor | 1 |
| `RX` / `TX` | bottom | debug UART (GPIO44/43), keep free for service | - |

### Checking the carrier against your own board

The tables above are read off a silkscreen. What the silkscreen promises and what the copper does
are two different claims, and only the second one matters once a sensor is wired on. **Check it
before the first sensor, not after a reading looks wrong.**

**Power off, USB unplugged.** Continuity testing a live board gives meaningless readings.

Clamp a short piece of stiff wire into each terminal you want to test. Screw terminals are awkward
to probe directly, and a slipping probe shorts the neighbour.

Leave the DevKit **in its socket**: the socket contact is itself a failure point and belongs in the
test. Put one probe on the terminal and the other on the pin as labelled **on the DevKit**, not on
the carrier - whether those two labels agree is the whole question.

For phase A these are the ones that matter:

| Terminal | Must reach DevKit pin | Used for |
|----------|----------------------|----------|
| `3.3V` x2 | `3V3` | sensor rail |
| `GND` | `GND` | ground |
| `IO4` `IO5` `IO6` | `4` `5` `6` | the three DS18B20 |
| `IO8` `IO9` | `8` `9` | I2C SDA and SCL |
| `5V` | `5Vin` | the DC/DC feed in phase B |

Under about 1 Ω, with the beeper sounding, is a pass.

**Then test the opposite.** Put one probe on each of two *neighbouring* terminals: it must stay
silent. A solder bridge or a swapped track shows up nowhere else. `IO8` and `IO9` sit next to each
other and are the I2C bus - a bridge there means no device answers at all, and the search for it
goes looking in the firmware.

| Result | Meaning |
|--------|---------|
| no continuity | poor socket contact - reseat the DevKit - or the terminal goes somewhere else |
| continuity to the wrong pin | the carrier's silkscreen is wrong. **Your measurement wins**, correct the tables above |
| neighbouring terminals beep | short circuit |

Power up once afterwards and confirm the board still boots normally before wiring anything.

### Terminals that must not be used

The carrier exposes several pins that are unusable on this board. They sit on labelled screw
terminals, which makes them tempting - **label them off or note it on the enclosure lid.**

| Terminal | Why |
|----------|-----|
| `IO35` `IO36` `IO37` | **Octal PSRAM on the N16R8.** Using them crashes the board. The most dangerous trap on this carrier, because nothing on the silkscreen warns you. |
| `IO0` `IO3` `IO45` `IO46` | strapping pins - affect boot mode |
| `IO19` `IO20` | native USB D-/D+, used by the direct USB-C port |

### Genuinely free spares

`IO1` `IO10` `IO11` `IO12` `IO13` `IO14` `IO38` `IO39` `IO40` `IO41` `IO42` `IO47` `IO48`

`IO2` is spoken for: the IMU's interrupt line
([A-009](A-009-imu-heel-and-motion.md)). `IO10`-`IO13` are the ESP32-S3's default SPI pins, so they
are the natural choice if anything ever needs SPI.

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

**Two boards make this much better.** With a second identical DevKit on hand, the rework stops
being a one-way bet: rework one board, leave the other stock, and measure both at the same spot at
the same time. That is a direct A/B comparison instead of a before/after one, so it is not confused
by the marina AP changing channel or a neighbour's boat moving between the two measurements. It
also means a botched rework costs nothing - the stock board is still there.

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
no protection**, so every pull-up, series resistor and filter from [B-001](B-001-power-supply.md) and
the sensor documents still has to live somewhere.

### Mechanical

Carrier 84.5 x 73.7 mm inside a 200 x 120 x 75 mm enclosure - fits with room for the perfboard
alongside. Stack height with the socketed DevKit is roughly 20 mm against 75 mm of depth. Both USB-C
ports face sideways; leave access to them or accept opening the box to reflash.

### DevKit overhead

Two items belong in the power budget in [B-001](B-001-power-supply.md):

- the **AMS1117-3.3 ground current** is roughly 5-10 mA on its own
- the **WS2812 draws around 1 mA even with no data**, and the CH343P plus the PWR LED add more

The DevKit's own overhead is therefore around **10-18 mA**. On permanent shore power that changes
nothing, but it is the figure the budget should carry.

For a permanent installation the PWR LED and the CH343P are wasted current and can be removed, but
with shore power there is no reason to bother.

### Flashing

Use the **CH343P port** (the USB-serial one) for flashing; the native USB port occupies GPIO19/20.
The guide's rule stands for both ports: **external 5 V off while USB is connected.**

**Which of the two sockets.** Hold the board with the module at the top and both USB-C sockets along
the bottom edge: the **right-hand socket is the CH343P**, the left-hand one is the native ESP32-S3
USB. Nothing on the silkscreen says so. Identify it by the USB ID rather than by position, because
a board revision could swap them:

| Enumerates as | Port | Use it? |
|---------------|------|---------|
| `1A86:55D3`, "USB-Enhanced-SERIAL CH343" | CH343P | **yes** |
| `303A:....`, Espressif | native ESP32-S3 USB | no |

`pio device list` prints the VID:PID of every serial device, which is the quickest way to tell
the two apart.

Picking the wrong socket does not announce itself. The firmware in `board/` is built with
`ARDUINO_USB_CDC_ON_BOOT=0` and therefore never creates a native USB serial port - so an upload over
the native socket can still succeed, after which the port vanishes on reboot and the serial monitor
stays silent for good.

## 6. Test

- [ ] Read the module silkscreen and confirm it says `ESP32-S3-N16R8`
- [ ] Locate the U.FL socket and the antenna solder jumper, and record which position it ships in
- [ ] **Check the carrier terminal order against section 3** - all 22 labels per block, `IO14`
      included. This confirms the silkscreen only; the continuity check below proves the routing
- [ ] Blink and serial test over the CH343P port
- [ ] Confirm the board boots and PSRAM is detected. A working octal PSRAM is the proof that
      IO35-37 are in use and must stay off the pin plan
- [ ] Confirm the WS2812 responds on GPIO48
- [ ] Continuity-check each project terminal on the carrier through to the right DevKit pin before
      wiring sensors
- [ ] RSSI measurement per section 4, in the closed enclosure at the real mounting point

## 7. Open points

| Point | Decide by | Who |
|-------|-----------|-----|
| Which position the antenna jumper ships in on this board | first bench evening | Andreas |
| Onboard antenna sufficient, or do the rework? | after the RSSI measurement | both |
| Which GPIO carries the WS2812 | first bench evening | both |
| Whether to fit the carrier at all, or keep the original all-perfboard plan | before drilling the enclosure | both |

## 8. References

- [B-001-power-supply.md](B-001-power-supply.md) - 5 V feed and current budget
- [ESP32-S3-DevKitC-1 hardware reference](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html)
