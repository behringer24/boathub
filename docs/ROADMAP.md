# Roadmap

Build stages of the ESP32 BoatHub. Each stage is usable on its own and is only started once the
previous one runs reliably. Based on the project guide v0.1 of 2026-09-13.

**Status values:** `open` · `in progress` · `done` · `blocked` · `deferred`

> The design review in [design/000-design-review.md](design/000-design-review.md) validated the
> whole design against the datasheets. Its findings are folded into the work packages below and
> referenced by ID (B = blocker, I = important, M = minor).

---

## Stage 1 - Base monitoring

**Status:** in progress
**Goal:** Measure temperatures, humidity, battery voltage and optionally bilge level, show them on
the on-board Wi-Fi and push them through the marina Wi-Fi to the self-hosted server.

### Work packages

Stage 1 is built in three phases. **Nothing is soldered to 12 V until the whole sensor and network
stack runs on USB power**, exactly as the project guide sequences it. That way, when something
browns out later, you already know it is not the firmware.

Package numbers carry the phase, so they stay readable as build order.

#### Phase A - bench build on USB power

No 12 V anywhere. The ESP is powered from the CH343P USB-C port, the sensors from its 3.3 V rail.

| # | Package | Status | Design doc |
|---|---------|--------|------------|
| A.0 | Decide the power concept (B1) | **done** - permanently on shore power in the marina, 2 x 100 Ah AGM, no solar or wind | [000](design/000-design-review.md) |
| A.1 | Toolchain set up, blink and serial test on the CH343P port; confirm N16R8 and carrier pinout | **in progress** - toolchain, blink, serial and N16R8 confirmed 2026-09-14; carrier terminal continuity still open | [A-001](design/A-001-devkit-and-carrier.md), [A-002](design/A-002-bench-setup-usb.md) |
| A.2 | DS18B20 engine bay / bilge / fridge (GPIO4/5/6), 2.2 kΩ pull-ups verified with the real 5 m cables (I3) | open | [A-003](design/A-003-ds18b20-temperature-sensors.md) |
| A.3 | SHT31-D cabin climate (I2C 0x44) | open | [A-002](design/A-002-bench-setup-usb.md) |
| A.4 | ADS1115 x3 (0x48/0x49/0x4A) against a known reference voltage, PGA fixed (M2) | open | [A-002](design/A-002-bench-setup-usb.md) |
| A.5 | SoftAP BOOT-NETZ plus local configuration web UI | open | planned |
| A.6 | Station mode, Wi-Fi credentials in NVS (home Wi-Fi for now, not the marina) | open | planned |
| A.7 | Server uplink: MQTT over TLS, telemetry, heartbeat, last will | open | planned |
| A.8 | Alarm and threshold logic for the sensors that exist yet - frost, fridge too warm, humidity | open | planned |
| A.9 | Fault handling and watchdog: decouple sensor failures from the network path | open | planned |

**Milestone:** a working monitor that runs off any USB charger and reports to the server. Not the
final system - no battery measurement, no 12 V robustness - but a real, testable deliverable.

#### Phase B - power supply, still on the bench

Only now does anything get built for 12 V.

| # | Package | Status | Design doc |
|---|---------|--------|------------|
| B.1 | 12 V supply board: fuse, **TVS ahead of the Schottky (B2)**, reverse-polarity protection, DC/DC | open | [B-001](design/B-001-power-supply.md) |
| B.2 | Battery divider 82k/10k, **tapped upstream of the Schottky (B3)**, calibration | open | [B-001](design/B-001-power-supply.md) |
| B.3 | Battery state machine, AGM thresholds, debouncing (B1a) | open | [B-001](design/B-001-power-supply.md) |
| B.4 | Shore-power-loss alarm gated on ≥6 h prior charging, so it stays quiet underway | open | [B-001](design/B-001-power-supply.md) |
| B.5 | **Changeover from USB to 12 V** - never both at once | open | [B-001](design/B-001-power-supply.md) |
| B.6 | Optional: bilge level 4-20 mA, calibrated, shunt value per compliance budget (I2) | open | planned |

#### Phase C - installation in the boat

| # | Package | Status | Design doc |
|---|---------|--------|------------|
| C.1 | Enclosure, cable labelling, vent membrane against condensation (I7) | open | - |
| C.2 | RSSI measurement at the real mounting point; solder the antenna jumper only if it falls short (M9a) | open | [A-001](design/A-001-devkit-and-carrier.md) |
| C.3 | Marina Wi-Fi credentials, server reachable from home | open | planned |
| C.4 | Deferred: NAPT/NAT so clients reach the internet through the ESP | deferred | planned |
| C.5 | Deferred: water tank temperature (GPIO7) | deferred | - |

### Acceptance criteria

- [ ] Each DS18B20 is detected individually and reports plausible values
- [ ] SHT31 reports temperature and relative humidity
- [ ] All three ADS1115 respond on 0x48/0x49/0x4A
- [ ] Battery voltage matches the multimeter after calibration
- [ ] DC/DC delivers a stable 5.0 V without the ESP; ESP 3V3 pin around 3.3 V
- [ ] BOOT-NETZ appears, the Pixel reaches the local web UI
- [ ] Marina Wi-Fi connects, reconnect after an outage works
- [ ] Server shows heartbeat and measurements from home
- [ ] Enclosure and DC/DC thermally unremarkable after 30-60 minutes of operation
- [ ] Watchdog active, sensor and network failures decoupled
- [ ] Average current draw measured against the estimate in B1, low-voltage backstop verified
- [ ] Pulling the shore power cable after a long float period raises the shore-power-loss alarm;
      restoring it clears the alarm
- [ ] An engine run followed by shutdown enters `ON_BATTERY` but raises **no** alarm
- [ ] Charger confirmed on an AGM profile: absorption at or below ~14.7 V, no equalisation step
- [ ] A two-second dip from the bilge pump or fridge produces **no** false alarm
- [ ] `BOOT-NETZ` clients survive a marina channel change (I4)

### Server interface (target)

```
boathub/<boat-id>/telemetry
boathub/<boat-id>/status      last will: "offline"
boathub/<boat-id>/events
```

```json
{
  "ts": "2026-09-13T08:15:00Z",
  "battery_v": 12.73,
  "cabin_temp_c": 8.4,
  "cabin_rh": 72.1,
  "engine_temp_c": 7.9,
  "bilge_temp_c": 6.1,
  "fridge_temp_c": 5.2,
  "bilge_level_cm": 1.3,
  "seatalk_online": false
}
```

Heartbeat roughly every minute. Critical events such as a rising bilge level are sent immediately,
not at the next regular telemetry interval.

---

## Stage 2 - Read SeaTalk1

**Status:** planned
**Goal:** Listen in on the existing SeaTalk1 network through the free port on the Raymarine S1.
**Prerequisite:** Stage 1 running reliably in the boat.

### Work packages

| # | Package | Status |
|---|---------|--------|
| 2.0 | **Prove the 9th-bit recovery on the bench: the ESP32 UART has no 9-bit mode** (parity-error trick or bit-banged receiver) | open |
| 2.1 | Settle the RX level shifting / isolation to 3.3 V as its own schematic revision | open |
| 2.2 | Bench-test the RX stage (scope or logic analyser, 4800 baud, 9th bit) | open |
| 2.3 | Read and log raw bytes (GPIO15) | open |
| 2.4 | Decode datagrams: depth, log speed, compass heading | open |
| 2.5 | GPS position, SOG/COG, time - if present on the bus | open |
| 2.6 | Decode autopilot status and target heading | open |
| 2.7 | Collect unknown datagrams raw and analyse them | open |
| 2.8 | Feed SeaTalk values into telemetry and the on-board Wi-Fi | open |

### Acceptance criteria

- [ ] RX stage bench-tested before it touches the S1
- [ ] Raw data stream stable over hours, with no effect on the bus
- [ ] Depth, speed, heading and - if available - GPS unambiguously identified
- [ ] `seatalk_online` field in telemetry correct

**Open:** The exact RX/TX stage is deliberately not fixed yet. References: APRemote (ESP32 +
SeaTalk1), open-collector circuits using the 74LS07, Signal K autopilot.

---

## Stage 2.5 - Track logging and digital logbook

**Status:** planned
**Goal:** Record trips offline and sync them to the server automatically once back in the marina.
**Prerequisite:** GPS data from stage 2 available.

### Work packages

| # | Package | Status |
|---|---------|--------|
| 2.5.1 | Define the track point record (time, lat/lon, SOG/COG, heading, depth, AP status, battery) | open |
| 2.5.2 | Ring buffer in LittleFS, block writes out of the RAM buffer | open |
| 2.5.3 | Trip detection: start on GPS fix plus movement above a threshold, end after a quiet period | open |
| 2.5.4 | Time base: GPS time, otherwise NTP over marina Wi-Fi | open |
| 2.5.5 | Upload of not-yet-transferred trips, acknowledged and resumable | open |
| 2.5.6 | Server side: map view, logbook entries, GPX/CSV export | open |

### Acceptance criteria

- [ ] Interval 5-10 s, flash write cycles reduced by the RAM buffer
- [ ] A complete trip recorded without internet and afterwards uploaded in full
- [ ] Transferred tracks are discarded first when space runs low, current ones never
- [ ] Server shows start, destination, duration, distance, average and maximum speed

---

## Stage 2B - Autopilot control (SeaTalk1 TX)

**Status:** blocked - only after reliable RX operation and a tested output stage
**Goal:** Operate the Raymarine S1 locally from the on-board Wi-Fi.

### Work packages

| # | Package | Status |
|---|---------|--------|
| 2B.1 | Design the open-collector / open-drain output stage and bench-test it; N-MOSFET rather than 74LS07 (I5) | blocked |
| 2B.2 | Guarantee a high-impedance TX during boot, reset and faults - **10 kΩ gate pull-down (I5)** | blocked |
| 2B.3 | Commands +1 / -1 / +10 / -10 degrees | blocked |
| 2B.4 | AUTO / STANDBY | blocked |
| 2B.5 | Arming logic: local on-board Wi-Fi only, locked after a restart | blocked |
| 2B.6 | TRACK / route - only after a navigation test, with user confirmation | blocked |

### Safety rules

- The bus must never be driven actively to 12 V.
- STANDBY is always directly reachable.
- AUTO only through an explicit user action, never automatically after a restart.
- The existing Raymarine control head always stays in place and functional.
- **No autopilot commands from the internet or the server.**

---

## Stage 3 - NMEA2000

**Status:** future
**Goal:** TWAI/CAN connection as a gateway to modern marine electronics.
**Prerequisite:** Stage 1 and SeaTalk stable. Only then is the CAN interface dimensioned and the
backbone planned.

| # | Package | Status |
|---|---------|--------|
| 3.1 | Pick an external CAN transceiver, isolated interface on GPIO17/18 - **3.3 V part, not MCP2551; no terminator on a drop (I6)** | open |
| 3.2 | NMEA2000 to Wi-Fi for tablet and server | open |
| 3.3 | SeaTalk1 to NMEA2000 for legacy Raymarine data | open |
| 3.4 | Own sensor values to NMEA2000 where sensible PGNs exist | open |
| 3.5 | Optional: AIS, modern sensors, Orca Core and similar | open |

---

## Stage 4 - OpenCPN on tablet / Pixel

**Status:** future
**Goal:** The on-board Wi-Fi distributes navigation data to OpenCPN. Tablet as the main screen, the
Pixel as backup. The ESP stays a gateway, not a chart plotter.

---

## Build evenings

Order of work for building the system together, taken from the project guide.

| Evening | Goal | Done when... | Status |
|---------|------|--------------|--------|
| 1 | Get to know the ESP | serial monitor and first test running | open |
| 2 | Three DS18B20 | all three temperatures stable individually | open |
| 3 | SHT31 + I2C | cabin temperature and humidity visible | open |
| 4 | ADS1115 | I2C addresses found and test voltage measurable | open |
| 5 | 12 V supply | clean 5 V, protection parts fitted | open |
| 6 | Battery measurement | value calibrated against the multimeter | open |
| 7 | On-board Wi-Fi | Pixel connects locally to the web UI | open |
| 8 | Marina Wi-Fi + server | measurements visible from home | open |
| 9 | Enclosure and installation | box mounted safely, cables labelled | open |
| 10 | Bilge level (optional) | 4-20 mA calibrated | open |
| 11 | SeaTalk RX | read only, log raw data | open |
| 12 | Decode SeaTalk | depth/speed/heading/GPS identified | open |
| 13 | Track logger | trip stored offline and uploaded | open |
| 14 | SeaTalk TX | bench test, then local autopilot control | open |
| 15 | NMEA2000 | isolated CAN interface added | open |

---

## Open decisions

| Topic | State |
|-------|-------|
| Shore power at the berth? | **resolved 2026-09-13 - permanently connected.** Continuous operation; the low-voltage cutoff stays as a backstop, and shore-power-loss becomes the headline alarm (B1a) |
| How to tell "marina" from "underway" | **resolved 2026-09-13** - no mode switch. The alarm is gated on ≥6 h of prior charging, which only shore power sustains. From stage 2, `seatalk_online == false` confirms it (001) |
| House bank and chemistry | **resolved 2026-09-13** - 2 x 100 Ah AGM, ~100 Ah usable, no solar or wind. AGM rests ~0.2 V higher than flooded, so thresholds are 12.3 V warning / 12.0 V critical (001) |
| Current shunt for real Ah counting | **rejected for now** - bus monitors are NMEA2000 and die when the instruments are off; a DIY shunt needs a heavy shunt and a proper SoC algorithm. Voltage-only accepted, spare ADS1115 channels keep the door open (001) |
| SHT31 mounting point | open - outside box and cabinet is decided, exact point is not. Up to ~3 m needs nothing extra; 5-6 m needs a 3.3 kΩ pull-up pair (I1a) |
| Minimum supply voltage of the 4-20 mA probe | open - now a **purchase criterion**, pick one specified from 9-10 V up. Default to the 50 Ω shunt either way (I2) |
| DevKit variant: WROOM-1 or WROOM-1**U** | **resolved 2026-09-13** - neither. It is a third-party module (sparkleIoT XH-S3E) with both a PCB antenna and a U.FL socket, chosen by a solder jumper set to the PCB antenna by default. The bundled SMA antenna does nothing until that jumper is moved (M9a, 002) |
| Onboard vs. external antenna | open - measure RSSI at the real mounting point first; only rework the jumper if it falls short (002) |
| Bilge probe sheath bonded to GND internally? | open - **bench check** with a multimeter. More urgent now: permanent shore power ties the boat's negative to shore earth (I8a) |
| Maximum output voltage of the charger | open - the TVS starts conducting at 17.1 V standoff (001) |
| Second ESP as a dedicated network gateway | spare only; revisit if router/NAT should be separated from boat functions (coupled over UART). The forced AP+STA channel sharing (I4) is the concrete argument for it |
| SeaTalk RX/TX circuit | deliberately not fixed, own revision before connecting |
| NAPT/NAT in the firmware | optional; AP+STA alone is not a router |
| Lossless MOSFET reverse-polarity protection | only with a custom PCB; the prototype measures behind the Schottky diode |
| Bilge probe stainless grade | check for corrosion regularly in salt/brackish water, mount so it can be replaced |
