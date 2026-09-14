# Design documents

One planning document per feature. Meant for thinking **before** soldering and coding: what the
feature has to do, how it is solved electrically and in software, how it is tested, and what can go
wrong.

## Process

1. Copy a new document from [TEMPLATE.md](TEMPLATE.md).
2. File name: `<PHASE>-NNN-short-name.md` - the build phase letter, then the next free number
   **within that phase**, lower case, hyphens.
   Example: `A-003-ds18b20-temperature-sensors.md`.
3. Add it to the index below, in build order.
4. Keep the status current: `Draft` → `In review` → `Accepted` → (`Implemented` | `Rejected` |
   `Superseded by <id>`).
5. Record accepted hardware decisions in [../CHANGELOG.md](../CHANGELOG.md) with the **[HW]**
   prefix, update [../MATERIAL.md](../MATERIAL.md) if the bill of materials changes, and carry the
   status over into [../ROADMAP.md](../ROADMAP.md).

Accepted documents are not quietly rewritten. If a decision changes, it gets a new document that
supersedes the old one - that way it stays traceable why something on board is wired the way it is.

## Phases

Documents are grouped by **build phase**, and the phase letter is part of the identifier. Phases
are the execution timeline; the stages in [../ROADMAP.md](../ROADMAP.md) are the feature scope.

| Phase | Covers | Stage |
|-------|--------|-------|
| **A** | bench build on USB power - sensors, Wi-Fi, server uplink. No 12 V anywhere | 1 |
| **B** | 12 V supply, protection, battery measurement | 1 |
| **C** | installation in the boat | 1 |
| D and on | assigned when a later stage is planned in detail | 2 and later |

Numbering restarts per phase, so a document inserted into phase A never disturbs phase B. Within a
phase the number is still an identifier, not a step number - a `A-005` written later may well be
built before `A-003`. The build order lives in the roadmap.

## Index

In build order.

| Read | ID | Feature | Status | Document |
|------|----|---------|--------|----------|
| first | 000 | Design review and spec validation - project-wide, no phase | Accepted | [000-design-review.md](000-design-review.md) |
| then | A-001 | DevKit, carrier board and antenna | Draft | [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) |
| **start building** | A-002 | Bench setup on USB power | Draft | [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) |
| then | A-003 | DS18B20 temperature sensors | Draft | [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) |
| last, on the bench | B-001 | Power supply and battery measurement | Draft | [B-001-power-supply.md](B-001-power-supply.md) |

A document without a phase letter - currently only `000` - is project-wide reference material
rather than a build step.

## Planned documents

Ordered along the roadmap; they take the next free number in their phase when created.

**Phase A - bench on USB power**

- SHT31-D cabin climate on the shared I2C bus
- ADS1115 channel allocation and value conditioning
- Wi-Fi operation: SoftAP `BOOT-NETZ` plus station, reconnect behaviour
- Configuration and secrets in NVS/Preferences, local web UI
- Server uplink: MQTT over TLS, telemetry schema, heartbeat, last will
- Alarm and threshold logic
- Fault handling and watchdog: decoupling sensor and network failures

**Phase B - power supply**

- Bilge level 4-20 mA (optional)

**Phase C - installation**

- Enclosure, mounting, cable routing and labelling

**Later stages, phase letters not yet assigned**

- SeaTalk1 RX stage: level shifting and isolation (schematic revision)
- SeaTalk1 decoding: datagrams, 4800 baud, 9th bit
- SeaTalk1 TX output stage (open collector) and safety interlock
- Autopilot operation on the on-board Wi-Fi: arming logic and state machine
- Track logger: record format, LittleFS ring buffer, trip detection
- Track synchronisation and server-side logbook
- NMEA2000: CAN transceiver, isolation, PGN selection
