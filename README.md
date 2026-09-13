# ESP32 BoatHub

Boat monitoring, SeaTalk1 gateway, track logger and NMEA2000 groundwork, built around an
ESP32-S3 N16R8.

A permanently powered ESP32 system monitors the boat in the marina, provides its own on-board
Wi-Fi, pushes measurements over an outbound TLS connection to a self-hosted Docker server, and is
later extended into a gateway for SeaTalk1, autopilot control, track logging and NMEA2000.

Based on the project guide v0.1 of 2026-09-13
([PDF, German](docs/reference/ESP32_BoatHub_Projektanleitung.pdf)).

## Status

| Stage | Goal | Status |
|-------|------|--------|
| 1 | Base monitoring: temperatures, humidity, battery, optional bilge level, on-board Wi-Fi, marina Wi-Fi, server uplink | in progress |
| 2 | Read SeaTalk1: log, depth, compass, GPS, autopilot status | planned |
| 2.5 | Store GPS tracks locally and sync them as a digital logbook | planned |
| 2B | Write SeaTalk1: local autopilot control | after RX test |
| 3 | NMEA2000 over TWAI/CAN | future |
| 4 | Android tablet / Pixel running OpenCPN as a plotter | future |

Details and acceptance criteria: [docs/ROADMAP.md](docs/ROADMAP.md)

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

The ESP opens the internet connection outbound. No inbound ports are needed in the marina
network.

## Hardware at a glance

- **Controller:** 1 x ESP32-S3 DevKitC-1 N16R8 with external antenna
- **Temperature:** 3 x DS18B20 (engine bay, bilge water, fridge), each on its own 1-Wire GPIO
- **Cabin climate:** SHT31-D (I2C, address 0x44)
- **Analog:** 3 x ADS1115 (0x48 / 0x49 / 0x4A) on the shared I2C bus
- **Battery:** 82 kΩ / 10 kΩ divider (factor 9.2) into ADS1115 A0, calibrated against a multimeter
- **Bilge level (optional):** hydrostatic 0-1 m probe, 4-20 mA, 100 Ω shunt into ADS1115 A1
- **Power:** 12 V house supply → 2 A fuse → 1N5822 → TVS 1.5KE20A → DC/DC 9-36 V to 5 V

Full parts list with prices and sources: [docs/MATERIAL.md](docs/MATERIAL.md)

### Pin assignment

| GPIO | Function today | Later / note |
|------|----------------|--------------|
| 4 | DS18B20 engine bay | dedicated 1-Wire bus |
| 5 | DS18B20 bilge water | dedicated 1-Wire bus |
| 6 | DS18B20 fridge | dedicated 1-Wire bus |
| 7 | spare | optional water tank DS18B20 |
| 8 | I2C SDA | SHT31 + all ADS1115 |
| 9 | I2C SCL | SHT31 + all ADS1115 |
| 15 | reserved | SeaTalk RX (stage 2) |
| 16 | reserved | SeaTalk TX (stage 2) |
| 17 | reserved | TWAI TX (stage 3) |
| 18 | reserved | TWAI RX (stage 3) |
| 21 | spare | optional local buzzer |
| 43/44 | debug UART | keep free for servicing |

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

## Repository layout

```
docs/
├── ROADMAP.md          stages, acceptance criteria, build evenings
├── CHANGELOG.md        project change log
├── MATERIAL.md         bill of materials and tools
├── design/             per-feature design documents (+ TEMPLATE.md)
└── reference/          source documents (project guide PDF)
```

Conventions for working in this repository: [CLAUDE.md](CLAUDE.md)

## Next step

Build stage 1 on the bench: ESP32 over USB, then DS18B20, SHT31 and ADS1115. Only once the sensor
side is stable does the 12 V supply get added.
