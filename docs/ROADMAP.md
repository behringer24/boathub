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

| # | Package | Status | Design doc |
|---|---------|--------|------------|
| 1.0 | **Decide the power concept: shore power at the berth, or duty cycling plus low-voltage cutoff (B1)** | **open - blocks 1.5 and 1.11** | [000](design/000-design-review.md) |
| 1.1 | Toolchain set up, blink and serial test | open | - |
| 1.2 | DS18B20 engine bay / bilge / fridge (GPIO4/5/6), pull-up value chosen on the bench (I3) | open | planned |
| 1.3 | SHT31-D cabin climate (I2C 0x44), mounted outside the enclosure, max 1 m of bus (I1) | open | planned |
| 1.4 | ADS1115 x3 (0x48/0x49/0x4A), PGA fixed before calibration (M2) | open | planned |
| 1.5 | 12 V supply: fuse, **TVS ahead of the Schottky (B2)**, reverse-polarity protection | open | planned |
| 1.6 | Battery measurement 82k/10k, **tapped upstream of the Schottky (B3)**, calibration | open | planned |
| 1.7 | SoftAP BOOT-NETZ plus local configuration web UI | open | planned |
| 1.8 | Station mode for marina Wi-Fi, configuration in NVS | open | planned |
| 1.9 | Server uplink: MQTT over TLS, telemetry, heartbeat, last will | open | planned |
| 1.10 | Alarms (low battery, frost, humidity, bilge) | open | planned |
| 1.11 | Enclosure, installation, cable labelling, vent membrane against condensation (I7) | open | - |
| 1.12 | Optional: bilge level 4-20 mA, calibrated, shunt value per compliance budget (I2) | open | planned |
| 1.13 | Optional: NAPT/NAT so clients reach the internet through the ESP | deferred | planned |
| 1.14 | Optional: water tank temperature (GPIO7) | deferred | - |

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
- [ ] Average current draw measured against the estimate in B1, low-voltage cutoff verified
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
| **Shore power at the berth?** | **open - decides whether the system may run continuously or needs duty cycling plus a low-voltage cutoff (B1)** |
| SHT31 distance from the enclosure | open - over roughly 1 m, I2C is the wrong transport and the humidity reading is lost (I1) |
| Minimum supply voltage of the 4-20 mA probe | open - decides the 100 Ω / 50 Ω shunt value (I2) |
| DevKit variant: WROOM-1 or WROOM-1**U** | open - only the -1U has the U.FL antenna connector (M9) |
| Bilge probe sheath bonded to GND internally? | open - a grounded submerged stainless probe joins the galvanic circuit (I8) |
| Second ESP as a dedicated network gateway | spare only; revisit if router/NAT should be separated from boat functions (coupled over UART). The forced AP+STA channel sharing (I4) is the concrete argument for it |
| SeaTalk RX/TX circuit | deliberately not fixed, own revision before connecting |
| NAPT/NAT in the firmware | optional; AP+STA alone is not a router |
| Lossless MOSFET reverse-polarity protection | only with a custom PCB; the prototype measures behind the Schottky diode |
| Bilge probe stainless grade | check for corrosion regularly in salt/brackish water, mount so it can be replaced |
