# Design documents

One planning document per feature. Meant for thinking **before** soldering and coding: what the
feature has to do, how it is solved electrically and in software, how it is tested, and what can go
wrong.

## Process

1. Copy a new document from [TEMPLATE.md](TEMPLATE.md).
2. File name: `NNN-short-name.md`, running number, lower case, hyphens.
   Example: `001-ds18b20-temperature-sensors.md`
3. Add it to the index below.
4. Keep the status current: `Draft` → `In review` → `Accepted` → (`Implemented` | `Rejected` |
   `Superseded by NNN`).
5. Record accepted hardware decisions in [../CHANGELOG.md](../CHANGELOG.md) with the **[HW]**
   prefix, update [../MATERIAL.md](../MATERIAL.md) if the bill of materials changes, and carry the
   status over into [../ROADMAP.md](../ROADMAP.md).

Accepted documents are not quietly rewritten. If a decision changes, it gets a new document that
supersedes the old one - that way it stays traceable why something on board is wired the way it is.

## Index

| No. | Feature | Stage | Status | Document |
|-----|---------|-------|--------|----------|
| 000 | Design review and spec validation | all | Accepted | [000-design-review.md](000-design-review.md) |
| 001 | Power supply and battery measurement | 1 | Draft | [001-power-supply.md](001-power-supply.md) |
| 002 | DevKit, carrier board and antenna | 1 | Draft | [002-devkit-and-carrier.md](002-devkit-and-carrier.md) |
| 003 | Bench setup on USB power | 1 | Draft | [003-bench-setup-usb.md](003-bench-setup-usb.md) |

Documents are numbered in **creation order, not build order**. The build sequence lives in
[../ROADMAP.md](../ROADMAP.md) - phase 1A starts with 003, and 001 is deliberately last in the
bench phase.

## Planned documents

Ordered along the roadmap; moved into the index above when created.

**Stage 1**

- DS18B20 temperature sensors on three separate 1-Wire GPIOs
- SHT31-D cabin climate on the shared I2C bus
- ADS1115 channel allocation and value conditioning
- Bilge level 4-20 mA (optional)
- Wi-Fi operation: SoftAP `BOOT-NETZ` plus marina station, reconnect behaviour
- Configuration and secrets in NVS/Preferences, local web UI
- Server uplink: MQTT over TLS, telemetry schema, heartbeat, last will
- Alarm and threshold logic
- Fault handling and watchdog: decoupling sensor and network failures

**Stage 2 and later**

- SeaTalk1 RX stage: level shifting and isolation (schematic revision)
- SeaTalk1 decoding: datagrams, 4800 baud, 9th bit
- SeaTalk1 TX output stage (open collector) and safety interlock
- Autopilot operation on the on-board Wi-Fi: arming logic and state machine
- Track logger: record format, LittleFS ring buffer, trip detection
- Track synchronisation and server-side logbook
- NMEA2000: CAN transceiver, isolation, PGN selection
