# Design documents

One planning document per feature. Meant for thinking **before** soldering and coding: what the
feature has to do, how it is solved electrically and in software, how it is tested, and what can go
wrong.

## Process

1. Copy a new document from [TEMPLATE.md](TEMPLATE.md).
2. File name: `NNN-short-name.md`, **next free number**, lower case, hyphens.
   Example: `004-ds18b20-temperature-sensors.md`. Numbers are never reused and never reassigned.
3. Add it to the index below, **in build order** - the number decides the filename, the roadmap
   decides the position in the table.
4. Keep the status current: `Draft` → `In review` → `Accepted` → (`Implemented` | `Rejected` |
   `Superseded by NNN`).
5. Record accepted hardware decisions in [../CHANGELOG.md](../CHANGELOG.md) with the **[HW]**
   prefix, update [../MATERIAL.md](../MATERIAL.md) if the bill of materials changes, and carry the
   status over into [../ROADMAP.md](../ROADMAP.md).

Accepted documents are not quietly rewritten. If a decision changes, it gets a new document that
supersedes the old one - that way it stays traceable why something on board is wired the way it is.

## Index

> **The number is an identifier, not a step number.** Documents are numbered in the order they were
> written and keep that number for life. This table is sorted by **build order** - read it top to
> bottom. Why it works this way: see below.

| Read | No. | Feature | Phase | Status | Document |
|------|-----|---------|-------|--------|----------|
| first | 000 | Design review and spec validation | all | Accepted | [000-design-review.md](000-design-review.md) |
| then | 002 | DevKit, carrier board and antenna | 1A, 1C | Draft | [002-devkit-and-carrier.md](002-devkit-and-carrier.md) |
| **start building** | 003 | Bench setup on USB power | 1A | Draft | [003-bench-setup-usb.md](003-bench-setup-usb.md) |
| last, on the bench | 001 | Power supply and battery measurement | 1B | Draft | [001-power-supply.md](001-power-supply.md) |

### Why the numbers look out of order

`001` is the power supply because it was the first document written, not because it is the first
thing to build. Renumbering to match build order would look tidier once and then break
permanently: eight more phase-1A documents are planned - DS18B20, SHT31, ADS1115, Wi-Fi, NVS
config, MQTT uplink, alarms, watchdog - and **every one of them is built before the power supply.**
Making the power supply `003` today means making it `011` by the time phase 1A is documented, and
every insertion in between would shift everything after it.

Stable numbers mean a reference written down today - in a commit message, in the changelog, on a
label inside the enclosure - still points at the same document in two years. The build order lives
in [../ROADMAP.md](../ROADMAP.md), which is free to be reordered because nothing cites it by
position.

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
