# Software roadmap

Planned functionality for the two pieces of software in this repository: the board firmware in
[`board/`](../board) and the telemetry server in `server/`.

The hardware build order is not here - it lives in [design/README.md](design/README.md). This file
is about what the software does, in the order it is meant to grow.

---

## v1 - Base monitoring

Measure temperatures, humidity and battery voltage, serve them on the on-board Wi-Fi, and push them
to the self-hosted server.

### Board firmware

| Feature | Notes |
|---------|-------|
| Three DS18B20 on dedicated GPIOs | pin the ROM addresses explicitly rather than letting the library auto-detect; verify the CRC on every read and reject bad frames |
| SHT31-D on I2C | bus at 100 kHz, address 0x44 |
| ADS1115 converters on I2C | fix the PGA once and never change it, or an existing calibration silently becomes wrong |
| SoftAP `BOOT-NETZ` plus a local configuration web UI | |
| Station mode, credentials in NVS/Preferences | **no Wi-Fi or server passwords in source** |
| Battery state machine with debouncing | thresholds and windows configurable in NVS, see [design/B-001-power-supply.md](design/B-001-power-supply.md) |
| Shore-power-loss alarm | gated on at least 6 h of prior charging so it stays quiet underway |
| Alarm and threshold logic | frost, fridge too warm, humidity |
| Fault handling and watchdog | a sensor failure must not take the network path down |

Two constraints that bite late if ignored:

- **The telemetry payload grows with every sensor.** Measure it against its buffer before
  serialising, or a message that outgrew the buffer is published truncated and invalid rather than
  refused.
- **NTP must sync before the first MQTT connect.** TLS certificate validation fails on a wrong
  clock. NTP runs over UDP and needs no TLS, so this is an ordering problem, not a circular one.

### Server

| Feature | Notes |
|---------|-------|
| MQTT broker over TLS | the certificate has to be in place before the board's first uplink |
| Telemetry ingest and storage | |
| Heartbeat and last-will handling | board reported offline once the keepalive expires |
| Alarm notification | |

### Interface

```
boathub/<boat-id>/telemetry
boathub/<boat-id>/status      last will: "offline"
boathub/<boat-id>/events
```

```json
{
  "ts": "2026-09-13T08:15:00Z",
  "window_s": 300,
  "n": 30,
  "battery_v": 12.73, "battery_v_min": 12.41, "battery_v_max": 12.79,
  "cabin_temp_c": 8.4, "cabin_temp_c_min": 8.1, "cabin_temp_c_max": 8.6,
  "cabin_rh": 72.1,
  "engine_temp_c": 7.9,
  "bilge_temp_c": 6.1,
  "fridge_temp_c": 5.2, "fridge_temp_c_min": 3.8, "fridge_temp_c_max": 6.9,
  "bilge_level_cm": 1.3,
  "seatalk_online": false
}
```

**A message is an aggregate, not a reading.** The board measures every **10 s** and publishes every
**5 min**: the bare field is the mean over the window, `_min`/`_max` are the extremes. A mean alone
would hide exactly what matters - the fridge compressor cycling, the battery sagging under it.

A spot reading is the same shape with `n: 1` and no extremes. That is what the **BOOT button**
produces, and what the first message after a restart looks like.

Three rates that have nothing to do with each other:

| | Rate | Why |
|---|---|---|
| Measuring | 10 s | alarms have to react, and the battery state machine needs samples for its median |
| Storing and publishing | 5 min | nothing here changes faster, and the board has to buffer these at sea |
| Alarms | immediate | a rising bilge level waits for no interval |

### Done when

- Each DS18B20 is identified individually and reports plausible values
- SHT31 reports temperature and relative humidity
- Every ADS1115 present answers at its address - 0x48 and 0x49 on the main board, all three of
  the pack on the breadboard
- Battery voltage matches the multimeter after calibration
- `BOOT-NETZ` appears and a phone reaches the local web UI
- Marina Wi-Fi connects, and reconnects after an outage
- The server shows heartbeat and measurements from outside the marina
- Pulling shore power after a long float period raises the alarm; restoring it clears it
- An engine run followed by shutdown enters `ON_BATTERY` and raises **no** alarm
- A two-second dip from the bilge pump or the fridge produces **no** false alarm
- `BOOT-NETZ` clients survive a marina channel change
- Watchdog active, sensor and network failures decoupled

---

## v2 - Read SeaTalk1

Listen in on the existing SeaTalk1 network through the free port on the Raymarine S1. Requires v1
running reliably in the boat.

| Feature | Notes |
|---------|-------|
| 9th-bit recovery | **the ESP32 UART has no 9-bit mode.** It offers 5-8 data bits plus parity, so the 9th bit is recovered from the parity result: a parity error means the 9th bit is the opposite of what the configured parity implies. The alternative is a bit-banged receiver. Prove this on the bench with a logic analyser before the interface hardware is finalised - it is the largest unknown in v2 |
| Raw byte logging on GPIO15 | |
| Datagram decoding: depth, log speed, compass heading | |
| GPS position, SOG/COG and time, if present on the bus | |
| Autopilot status and target heading | |
| Unknown datagrams collected raw for analysis | |
| SeaTalk values in telemetry and on the on-board Wi-Fi | the `seatalk_online` field |

### Done when

- The raw data stream is stable over hours with no effect on the bus
- Depth, speed, heading and - if available - GPS are unambiguously identified
- `seatalk_online` in telemetry is correct

---

## v2.5 - Track logging and digital logbook

Record trips offline and sync them once back in the marina. Requires GPS data from v2.

| Feature | Notes |
|---------|-------|
| Track point record | time, lat/lon, SOG/COG, heading, depth, autopilot status, battery |
| Ring buffer in LittleFS | **LittleFS, not SPIFFS** - it is power-fail safe. At ~50 bytes per point every 6 s the 6 MB partition holds well over a hundred hours, and the write volume is nowhere near the flash endurance limit. Write in blocks out of a RAM buffer |
| Trip detection | start on a GPS fix plus movement above a threshold, end after a quiet period |
| Time base | GPS time, otherwise NTP over marina Wi-Fi |
| Upload of pending trips | acknowledged and resumable |
| Server: map view, logbook entries, GPX/CSV export | |

### Done when

- A point every **25 m**, or on a course change, or after 60 s at the latest - not a fixed interval.
  At 5 kn a 6 s interval would put a point every 15 m, which is below chart resolution and actively
  harms the logged distance: GPS jitter accumulates over every segment, so a boat lying at anchor
  logs phantom miles
- Flash write cycles visibly reduced by the RAM buffer
- A complete trip is recorded without internet and afterwards uploaded in full
- Transferred tracks are discarded first when space runs low, current ones never
- The server shows start, destination, duration, distance, average and maximum speed

---

## v2B - Autopilot control (SeaTalk1 TX)

Operate the Raymarine S1 from the on-board Wi-Fi. **Blocked until v2 runs reliably and the output
stage has been tested on the bench.**

| Feature | Notes |
|---------|-------|
| Commands +1 / -1 / +10 / -10 degrees | |
| AUTO / STANDBY | |
| Arming logic | local on-board Wi-Fi only, locked again after a restart |
| TRACK / route | only after a navigation test, and with explicit user confirmation |

### Safety rules

- The bus is never driven actively to 12 V.
- STANDBY is always directly reachable.
- AUTO only through an explicit user action, never automatically after a restart.
- The existing Raymarine control head stays in place and functional.
- **No autopilot commands from the internet or the server.**
- After a reset or a lost connection the transmit path is passive. What guarantees that is the
  gate pull-down in the TX stage, not firmware.

---

## v3 - NMEA2000

TWAI/CAN as a gateway to modern marine electronics. Requires v1 and SeaTalk stable.

| Feature | Notes |
|---------|-------|
| CAN interface on GPIO17/18 | **3.3 V transceiver, not the MCP2551**, whose 5 V RX output would sit on a GPIO rated 3.6 V absolute maximum. The BoatHub is a drop off the backbone and must **not** carry a 120 Ω terminator |
| NMEA2000 to Wi-Fi for tablet and server | |
| SeaTalk1 to NMEA2000 for legacy Raymarine data | |
| Own sensor values to NMEA2000 where sensible PGNs exist | |
| Optional: AIS and modern sensors | |

---

## v4 - OpenCPN gateway

The on-board Wi-Fi distributes navigation data to OpenCPN on a tablet, with a phone as backup. The
ESP stays a gateway, not a chart plotter.

---

## Deferred

| Item | Why |
|------|-----|
| NAPT/NAT so clients reach the internet through the ESP | AP+STA alone is not a router; only worth building if the need turns out to be real |
| Ah counting from a current shunt | needs a heavy shunt and a proper state-of-charge algorithm. Voltage-only is accepted, and spare ADS1115 channels keep the door open |
| Water tank temperature on GPIO7 | the pin is reserved, the feature is not planned yet |
| Second ESP as a dedicated network gateway | held in reserve. The forced AP+STA channel sharing is the concrete argument for it |
