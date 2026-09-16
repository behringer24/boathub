# Design documents

One document per feature: what it has to do, how it is solved electrically and in software, how it
is verified, and what can go wrong. Read these before soldering or writing code.

## How the system is built

The build runs in three phases. **Nothing is soldered to 12 V until the whole sensor and network
stack runs on USB power.** That way, when something browns out later, the converter is the suspect
and not the firmware.

| Phase | Covers |
|-------|--------|
| **A** | bench build on USB power: sensors, Wi-Fi, server uplink. No 12 V anywhere |
| **B** | 12 V supply, protection, battery measurement |
| **C** | installation in the boat |

### Phase A - on the bench, USB power only

| # | Step | Document |
|---|------|----------|
| 1 | Toolchain, blink and serial over the CH343P port; confirm the module is an N16R8 and check the carrier terminals | [A-001](A-001-devkit-and-carrier.md), [A-002](A-002-bench-setup-usb.md) |
| 2 | Three DS18B20 on GPIO4/5/6, pull-ups proven with the real 5 m cables | [A-003](A-003-ds18b20-temperature-sensors.md) |
| 3 | SHT31-D cabin climate on I2C 0x44 | [A-002](A-002-bench-setup-usb.md), [A-008](A-008-sht31-cabin-climate.md) |
| 4 | Three ADS1115 on 0x48/0x49/0x4A against a known reference voltage, PGA fixed | [A-002](A-002-bench-setup-usb.md) |
| 5 | SoftAP `BOOT-NETZ` and the local configuration web UI | [A-004](A-004-wifi-and-configuration-portal.md) |
| 6 | Station mode, Wi-Fi credentials in NVS | [A-004](A-004-wifi-and-configuration-portal.md) |
| 7 | Server uplink: MQTT, telemetry, heartbeat, last will | [A-005](A-005-server-uplink.md) |
| 8 | Store the telemetry and put it on a dashboard | [A-006](A-006-telemetry-storage.md) |
| 9 | Buffer measurements in flash and drain them when a connection exists | [A-007](A-007-store-and-forward.md) |
| 10 | Alarm and threshold logic for the sensors that exist by then | planned |
| 11 | Fault handling and watchdog: a dead sensor must not take the network path with it | planned |

The milestone is a monitor that runs off any USB charger and reports to the server. Not the final
system - no battery measurement, no 12 V robustness - but a real, testable one.

### Phase B - power supply, still on the bench

| # | Step | Document |
|---|------|----------|
| 1 | 12 V supply board: fuse, TVS, reverse-polarity protection, DC/DC | [B-001](B-001-power-supply.md) |
| 2 | Battery divider and its calibration factor | [B-001](B-001-power-supply.md) |
| 3 | Battery state machine and debouncing | [B-001](B-001-power-supply.md) |
| 4 | Shore-power-loss alarm, gated so it stays quiet underway | [B-001](B-001-power-supply.md) |
| 5 | Changeover from USB to 12 V - never both at once | [B-001](B-001-power-supply.md) |
| 6 | Bilge level 4-20 mA, optional | planned |

### Phase C - into the boat

| # | Step |
|---|------|
| 1 | Enclosure, cable labelling, and a pressure-equalisation vent membrane fitted pointing down |
| 2 | RSSI at the real mounting point; rework the antenna jumper only if it falls short |
| 3 | Marina Wi-Fi credentials, server reachable from home |

**Why the vent membrane.** A sealed IP65 box on a boat goes through daily temperature cycles and
the air inside carries moisture, which condenses on the coldest surface - usually the board. IP65
keeps spray out and moisture in. Specify **105 °C electrolytics** rather than 85 °C parts for the
same reason: inside the box at summer ambient the internal temperature reaches 55-60 °C.

## The documents

In reading order.

| Read | ID | Feature |
|------|----|---------|
| first | [A-001](A-001-devkit-and-carrier.md) | DevKit, carrier board and antenna |
| **start building** | [A-002](A-002-bench-setup-usb.md) | Bench setup on USB power |
| then | [A-003](A-003-ds18b20-temperature-sensors.md) | DS18B20 temperature sensors |
| then | [A-008](A-008-sht31-cabin-climate.md) | SHT31 cabin climate |
| then | [A-004](A-004-wifi-and-configuration-portal.md) | Wi-Fi operation and configuration portal |
| then | [A-005](A-005-server-uplink.md) | Server uplink |
| then | [A-006](A-006-telemetry-storage.md) | Telemetry storage and dashboard |
| then | [A-007](A-007-store-and-forward.md) | Store and forward |
| last, on the bench | [B-001](B-001-power-supply.md) | Power supply and battery measurement |

## Checking a board

The firmware reports flash size and PSRAM on every boot, and writes and reads a megabyte of PSRAM
to prove the figure rather than trust it:

```
chip:  ESP32-S3 rev 0, 2 cores, 240 MHz
flash: 16 MB
psram: 8 MB, write/read ok
```

Anything else and the board is not the configuration these documents assume - most likely a
different module in a set sold under the same description, or a changed build setting.

A separate `bringup` firmware goes further: it dumps the partition table and identifies the RGB LED
pin. Two occasions justify it. **A board you have not used before**, because sets sold as "ESP32-S3
N16R8 DevKitC-1" vary. And **a board whose firmware will not run**, because `bringup` needs neither
Wi-Fi nor a broker nor stored configuration, so it separates a hardware fault from a firmware one.

```
pio run -d board -e bringup -t upload -t monitor
```

## Writing a new one

1. Copy [TEMPLATE.md](TEMPLATE.md).
2. Name it `<PHASE>-NNN-short-name.md`: the build phase letter, then the next free number **within
   that phase**, lower case, hyphens. Numbering restarts per phase, so inserting a document into
   phase A never disturbs phase B.
3. Add it to the table above, in reading order.
4. Update [../MATERIAL.md](../MATERIAL.md) if the bill of materials changes.

The number is an identifier, not a step number - an `A-005` written later may well be built before
`A-003`. The build order is the tables above.

**When a decision changes, correct the document in place.** These documents describe the system as
it should be built, not how the thinking arrived there; the git history keeps what came before.

## Planned documents

They take the next free number in their phase when written.

**Phase A**

- ADS1115 channel allocation and value conditioning
- Alarm and threshold logic
- Fault handling and watchdog: decoupling sensor and network failures

**Phase B**

- Bilge level 4-20 mA (optional)

**Phase C**

- Enclosure, mounting, cable routing and labelling

**Later stages, phase letters not yet assigned**

- SeaTalk1 RX stage: level shifting and isolation
- SeaTalk1 decoding: datagrams, 4800 baud, 9th bit
- SeaTalk1 TX output stage and safety interlock
- Autopilot operation on the on-board Wi-Fi: arming logic and state machine
- Track logger: record format, LittleFS ring buffer, trip detection
- Track synchronisation and server-side logbook
- NMEA2000: CAN transceiver, isolation, PGN selection
