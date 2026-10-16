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
| BME680 on I2C, address 0x76 | station pressure for the weather trend, plus the battery bay's air temperature and humidity. Register-level driver in forced mode, no Bosch BSEC; the gas heater stays off unless switched on in NVS, and the pressure offset against a nearby station lives there too, see [design/A-010-bme680-barometer-and-battery-bay.md](design/A-010-bme680-barometer-and-battery-bay.md) |
| ADS1115 converters on I2C | fix the PGA once and never change it, or an existing calibration silently becomes wrong |
| LSM6DSOX IMU on I2C, interrupt on GPIO2 | heel, pitch and motion in the aggregate, an event when something hits the boat. The filter's time constant, the axis mapping, the zero reference and the knock threshold live in NVS, see [design/A-009-imu-heel-and-motion.md](design/A-009-imu-heel-and-motion.md) |
| SoftAP `BOOT-NETZ` plus a local configuration web UI | |
| Station mode, credentials in NVS/Preferences | **no Wi-Fi or server passwords in source** |
| Battery state machine with debouncing | thresholds and windows configurable in NVS, see [design/B-001-power-supply.md](design/B-001-power-supply.md) |
| Shore-power-loss alarm | gated on at least 6 h of prior charging so it stays quiet underway |
| Events on `boathub/<boat-id>/events` | the moment something happens: the boot with its reset reason, build and sensors, a sensor lost and back, a knock. Written to a queue on flash first and sent ahead of the telemetry backlog, so an event raised offline arrives late rather than not at all, see [design/A-011-events.md](design/A-011-events.md) |
| Alarm and threshold logic | judged on the board: frost at the coldest of cabin, engine, bilge and battery bay; a fridge too warm, armed only once it has been cold; cabin humidity as a warning. Each holds for its time before it raises and clears at its own threshold; the LED shows an alarm until the BOOT button acknowledges it. Thresholds and holds live in NVS, see [design/A-012-alarm-and-threshold-logic.md](design/A-012-alarm-and-threshold-logic.md) |
| Alarms from the server's history | a barometer falling 3 hPa or more in three hours; a list or trim that keeps changing while the boat lies unattended, and a knock once its threshold has been set from recorded counts - both from [design/A-009-imu-heel-and-motion.md](design/A-009-imu-heel-and-motion.md). They come with notification |
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
| Pressure tendency and sea-level pressure | the three-hour tendency from the stored station pressure, and sea-level pressure for display from a per-boat height setting. The board sends station pressure only |
| Heartbeat and last-will handling | board reported offline once the keepalive expires |
| Event ingest and display | events stored with their identity, so a redelivery is turned away; an Events dashboard with the alarms active now, and markers for alarms, knocks and boots in the sensor panels, see [design/A-011-events.md](design/A-011-events.md) |
| Alarm notification | which events reach a phone, and how - decided once the record of them exists |

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
  "pressure_hpa": 1013.4, "pressure_hpa_min": 1013.1, "pressure_hpa_max": 1013.6,
  "bay_temp_c": 11.2, "bay_temp_c_min": 10.8, "bay_temp_c_max": 12.9,
  "bay_rh": 81.0,
  "engine_temp_c": 7.9,
  "bilge_temp_c": 6.1,
  "fridge_temp_c": 5.2, "fridge_temp_c_min": 3.8, "fridge_temp_c_max": 6.9,
  "bilge_level_cm": 1.3,
  "heel_deg": 0.4, "heel_deg_min": -2.9, "heel_deg_max": 3.6, "heel_deg_sd": 1.2,
  "pitch_deg": -0.2, "pitch_deg_min": -0.9, "pitch_deg_max": 0.5, "pitch_deg_sd": 0.3,
  "motion_g": 0.04, "motion_g_min": 0.01, "motion_g_max": 0.21,
  "knocks": 0,
  "seatalk_online": false
}
```

Heel and pitch carry a fourth figure, the standard deviation within the window: the extremes are
set by a single wake, the spread by the whole five minutes. `knocks` is a count, so 0 is a quiet
window and an absent key is a board without an IMU. See
[design/A-009-imu-heel-and-motion.md](design/A-009-imu-heel-and-motion.md) section 6.

`pressure_hpa` is **station pressure**, at the height of the sensor. Sea level is a matter of
display on the server, and the three-hour tendency needs neither height nor offset. `bay_` is the
battery bay, where the BME680 sits; the cabin's climate stays `cabin_`. See
[design/A-010-bme680-barometer-and-battery-bay.md](design/A-010-bme680-barometer-and-battery-bay.md).

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

**An event is not an aggregate.** It goes out on `events` the moment it happens, with the same
envelope and a `kind` that says what the rest means - here a frost alarm, raised because the
engine bay read 2.8 °C against a threshold of 3 °C:

```json
[
  {
    "ts": "2026-10-16T04:00:00Z", "time_valid": true, "time_source": "ntp",
    "boot_id": 23, "seq": 3, "uptime_s": 3603,
    "kind": "alarm", "alarm": "frost", "state": "raised", "severity": "alarm",
    "sensor": "engine", "value": 2.8, "limit": 3.0
  }
]
```

The kinds are `boot`, `sensor_lost`, `sensor_back`, `knock`, `alarm` and `ack`; their fields are in
[design/A-011-events.md](design/A-011-events.md) section 3.

### Done when

- Each DS18B20 is identified individually and reports plausible values
- SHT31 reports temperature and relative humidity
- BME680 reports station pressure and the battery bay's temperature and humidity, with its gas
  heater off; the server shows the three-hour tendency
- Every ADS1115 present answers at its address - 0x48 and 0x49 on the main board, all three of
  the pack on the breadboard
- Battery voltage matches the multimeter after calibration
- `BOOT-NETZ` appears and a phone reaches the local web UI
- Marina Wi-Fi connects, and reconnects after an outage
- The server shows heartbeat and measurements from outside the marina
- A boot, a sensor pulled and plugged back in, and a knock each appear on the Events dashboard
  within a second, and events raised without a connection arrive once it returns
- A probe in iced water raises the frost alarm after its hold, the LED shows it, BOOT
  acknowledges it, and it clears only above its clear threshold
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
