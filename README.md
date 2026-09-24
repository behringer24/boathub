# ESP32 BoatHub

Boat monitoring, SeaTalk1 gateway, track logger and NMEA2000 groundwork, built around an
ESP32-S3 N16R8.

A permanently powered ESP32 system monitors the boat in the marina, provides its own on-board
Wi-Fi, pushes measurements over an outbound TLS connection to a self-hosted Docker server, and is
later extended into a gateway for SeaTalk1, autopilot control, track logging and NMEA2000.

Based on the project guide v0.1 of 2026-09-13
([PDF, German](docs/reference/ESP32_BoatHub_Projektanleitung.pdf)).

## What it does, in the order it is built

| | Scope |
|---|-------|
| **v1** | Base monitoring: temperatures, humidity, battery, optional bilge level, on-board Wi-Fi, marina Wi-Fi, server uplink |
| **v2** | Read SeaTalk1: log, depth, compass, GPS, autopilot status |
| **v2.5** | Store GPS tracks locally and sync them as a digital logbook |
| **v2B** | Write SeaTalk1: local autopilot control - only after RX runs reliably |
| **v3** | NMEA2000 over TWAI/CAN |
| **v4** | Android tablet or phone running OpenCPN as a plotter |

Software scope in detail: [docs/ROADMAP.md](docs/ROADMAP.md).
Build order: [docs/design/README.md](docs/design/README.md).

## Architecture (target)

```
Sensors (1-Wire / I2C / 4-20 mA)
        |
     ESP32-S3 N16R8  ──SoftAP──>  BOOT-NETZ (Pixel, tablet, OpenCPN)
        |   |
        |   └──STA──> marina Wi-Fi ──TLS──> self-hosted Docker server
        |                                     ├── Mosquitto (MQTT)
   SeaTalk1 (stage 2)                         ├── Backend/API
   NMEA2000 (stage 3)                         ├── PostgreSQL + PostGIS
                                              └── Grafana / web dashboard
```

The ESP opens the internet connection outbound. No inbound ports are needed in the marina network,
and the board does not route between its two networks - `BOOT-NETZ` reaches the board, not the
internet.

**The two networks share one radio.** In AP+STA mode the SoftAP is forced onto whatever channel the
station connects to, so when the marina access point changes channel - many do so automatically -
every device on `BOOT-NETZ` is disconnected. This is normal behaviour, not a fault: clients have to
tolerate reconnects, and no local UI may treat a dropped socket as anything unusual.

## Hardware at a glance

- **Controller:** 1 x ESP32-S3 N16R8 DevKitC-1 (third-party module). It sits on the bundled
  screw-terminal carrier for the bench build and in a socket on the main board once installed -
  [A-001](docs/design/A-001-devkit-and-carrier.md),
  [B-002](docs/design/B-002-main-board.md). Ships with an onboard PCB antenna; the bundled external
  antenna needs a solder rework to activate
- **Temperature:** 3 x DS18B20 (engine bay, bilge water, fridge), each on its own 1-Wire GPIO
- **Cabin climate:** SHT31-D (I2C, address 0x44)
- **Analog:** 2 x ADS1115 on the shared I2C bus, 0x48 and 0x49. The second is socketed and may
  stay empty until its channels are specified
- **Attitude and motion:** LSM6DSOX IMU on the I2C bus at 0x6A, with its interrupt on GPIO2 -
  heel and pitch under sail, impacts at the berth
- **Battery:** 100 kΩ / 10 kΩ divider (factor 11.0) into ADS1115 A0, tapped **upstream of** the
  reverse-polarity diode, calibrated against a multimeter
- **Bilge level (optional):** hydrostatic 0-1 m probe, 4-20 mA, 100 Ω burden into ADS1115 A1
- **Power:** 12 V house supply → 2 A fuse → TVS 1.5KE20A → 1N5822 → DC/DC 9-36 V to 5 V

The TVS sits ahead of the Schottky diode, and the battery tap ahead of both - see
[docs/design/B-001-power-supply.md](docs/design/B-001-power-supply.md) for why the order matters.

Full parts list with prices and sources: [docs/MATERIAL.md](docs/MATERIAL.md)

### Pin assignment

| GPIO | Function today | Later / note |
|------|----------------|--------------|
| 2 | IMU interrupt | impact detection, [A-009](docs/design/A-009-imu-heel-and-motion.md) |
| 4 | DS18B20 engine bay | dedicated 1-Wire bus |
| 5 | DS18B20 bilge water | dedicated 1-Wire bus |
| 6 | DS18B20 fridge | dedicated 1-Wire bus |
| 7 | spare | optional water tank DS18B20 |
| 8 | I2C SDA | SHT31, both ADS1115, IMU |
| 9 | I2C SCL | SHT31, both ADS1115, IMU |
| 15 | reserved | SeaTalk RX (stage 2) |
| 16 | reserved | SeaTalk TX (stage 2) |
| 17 | reserved | TWAI TX (stage 3) |
| 18 | reserved | TWAI RX (stage 3) |
| 21 | spare | optional local buzzer - **through a transistor**, 20-30 mA exceeds the GPIO limit |
| 43/44 | debug UART | keep free for servicing |
| 48 | onboard WS2812 RGB LED | DevKit-internal, confirmed on the delivered board - do not reuse |

Avoided: strapping pins GPIO0/3/45/46, USB pins GPIO19/20, and GPIO33-37 on the N16R8 (octal
PSRAM). Always cross-check the silkscreen of the delivered DevKit board before soldering.

## Safety rules

- **12 V is not harmless.** A boat battery can deliver very high short-circuit currents. Every new
  feed gets its own fuse close to the source.
- Switch off the external 5 V supply while flashing over USB. Never feed 5 V into 3V3.
- **SeaTalk TX stays disabled** until RX runs reliably and the output stage has been tested
  separately on the bench. After a reset or a lost connection the transmit path is passive.
- **No autopilot commands from the internet.** Control lives on the local on-board Wi-Fi only; the
  server receives telemetry.
- No Wi-Fi or server passwords in source code. Configuration lives in NVS/Preferences.
- **Sensor and network faults are decoupled.** A broken probe, a silent broker or a marina access
  point that vanishes may cost its own reading or its own connection, and nothing else. The
  watchdog is there for what gets past that.
- The boat is permanently on shore power in the marina, so the BoatHub runs continuously. On the
  2 x 100 Ah AGM bank (~100 Ah usable, no solar or wind) it would still draw 40-60 Ah per month on
  its own, so a **low-voltage backstop stays mandatory** for the case shore power fails and stays
  failed - the remaining capacity belongs to the bilge pump.
- The shore-power-loss alarm is gated on at least 6 hours of prior charging, so it stays quiet
  underway where running on the battery is normal. See
  [docs/design/B-001-power-supply.md](docs/design/B-001-power-supply.md).

## Repository layout

```
board/                  PlatformIO firmware project - ESP32-S3 N16R8, see board/platformio.ini
hardware/
├── main-board/         KiCad project, see docs/design/B-002-main-board.md
│   └── fab/<rev>/      what was actually sent to the fabricator, one directory per revision
└── case/               parametric FreeCAD model of a printed enclosure, run rather than
                        drawn, see hardware/case/README.md
server/                 MQTT broker in Docker, see server/README.md
docs/
├── ROADMAP.md          planned software functionality, board and server
├── CHANGELOG.md        software change log
├── MATERIAL.md         bill of materials and tools
├── design/             build guide and one document per feature (+ TEMPLATE.md)
│   ├── A-NNN-*.md      phase A: the bench build on USB power
│   ├── B-NNN-*.md      phase B: 12 V supply, protection, the main board
│   └── D-NNN-*.md      phase D: SeaTalk1
├── reference/          source documents (project guide PDF)
```


## Where to start

Read [docs/design/A-001-devkit-and-carrier.md](docs/design/A-001-devkit-and-carrier.md) to identify
the board and its carrier, then build along
[docs/design/A-002-bench-setup-usb.md](docs/design/A-002-bench-setup-usb.md).

**Phase A runs entirely on USB power - no 12 V anywhere.** The 12 V supply is built only once the
whole sensor and network stack has run 24 hours on USB without intervention, so that a brownout
later can be blamed on the converter rather than on the firmware. The full build order is in
[docs/design/README.md](docs/design/README.md).
