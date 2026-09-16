# Changelog

Changes to the software in this repository: the board firmware in [`board/`](../board) and the
telemetry server in `server/`.

Hardware and build documentation is deliberately not tracked here. The design documents in
[design/](design/) describe the system as it is to be built, and are corrected in place when a
decision changes.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows
[Semantic Versioning](https://semver.org/). Planned functionality is in [ROADMAP.md](ROADMAP.md).

Categories: `Added` · `Changed` · `Deprecated` · `Removed` · `Fixed` · `Security`

---

## [Unreleased]

### Added

- PlatformIO project for the ESP32-S3 N16R8 in `board/`. PlatformIO ships no board definition for
  this module, so the project builds on `esp32-s3-devkitc-1` - the N8 variant without PSRAM - with
  explicit overrides for 16 MB flash and octal PSRAM.
- 16 MB partition layout in `board/partitions.csv`: two 5 MB app slots, 6 MB LittleFS for the later
  track logger, 64 kB core dump.
- Bring-up firmware: verifies flash size, PSRAM and the partition table over the serial port, so a
  wrong board configuration cannot pass as a successful build.
- `i2cscan` build environment: a bench scanner that first checks the bus electrically - whether the
  pull-ups are there at all, and whether either line is stuck low - and only then reports which
  addresses answer, naming the ones this project expects. It rescans every two seconds so a module
  can be plugged in and watched appearing. Most "the sensor does not work" sessions are a wire
  rather than a sensor, and a scanner that only lists addresses reports the same "nothing found"
  for both.
- `play` build environment with an LED sandbox for working out patterns on the onboard WS2812 and
  BOOT button, without touching the firmware.
- Wi-Fi: access point `BOOT-NETZ` and station run at the same time, with the station retried under
  exponential backoff so a network outage never takes the local side down.
- Configuration portal on the access point. Network, broker and boat identity are entered there and
  stored in NVS, so no credential is ever compiled in. The access point password defaults to one
  derived from the MAC address rather than a shared constant.
- MQTT uplink on `boathub/<boat-id>/telemetry` carrying timestamp, uptime, free heap, RSSI and
  reset reason, plus a retained `status` topic with `offline` as the last will. Timestamps come
  from NTP in UTC; a missing sync is reported as `time_valid: false` rather than holding up
  telemetry.
- A message is an **aggregate over a 5 min window**, not a reading: the board measures every 10 s,
  the bare field carries the mean and `_min`/`_max` the extremes. A mean alone would hide the
  fridge compressor cycling and the battery sagging under it, which is the part worth seeing.
  `window_s` and `n` describe the window - `n` below 30 means measurements were missed, a fault
  that otherwise hides behind a plausible average. A BOOT-button press sends the same shape with
  `n: 1` and no extremes, so there is no second message format.
- SHT31 cabin climate on the shared I2C bus at 0x44, measured every 10 s and reported as
  `cabin_temp_c` and `cabin_rh`. Single shot rather than free-running, because continuous
  measurement warms the sensor and a warm humidity sensor reads low; and the non-stretching command,
  because clock stretching would hold SCL low for the three ADS1115 sharing the bus as well. Both
  CRCs are checked, and a reading outside a plausible range or jumping implausibly is discarded
  rather than averaged in.
- SHT31 heater, on by default: condensation on the sensor leaves it stuck at 100 %RH long after the
  air has dried, which reads like a measurement rather than a fault. Above 95 %RH for 30 minutes it
  heats for 10 s and then waits 120 s to cool, taking no samples in either window - so `n` dips in a
  window containing a cycle, by design. All five numbers are in NVS, including the off switch.
- `i2cbus`: the bus is owned centrally rather than by whichever sensor starts first, so the order of
  `begin()` calls cannot matter once the ADS1115s join it.
- Telemetry payload is checked against the buffer with `measureJson` before publishing. Serialising
  into a buffer that is too small truncates silently and publishes invalid JSON, which looks like a
  healthy system until somebody checks the server.
- `server/`: Mosquitto broker as a Docker Compose service on port 1883, authenticated, with
  persistence so retained messages survive a restart.
- Telemetry storage: PostgreSQL with TimescaleDB and PostGIS. The `telemetry` hypertable is
  partitioned on the server's receive time rather than the board's clock, which is null until NTP
  has synced, and compressed after seven days. At 288 aggregates a day that is some 21 MB a year
  raw and a few compressed, so there is no retention policy and no continuous aggregate - full
  resolution is kept. Thinning is the board's problem, with its 6 MB of flash, not this table's.
  PostGIS is unused so far and present so the track logger needs no migration of the whole
  database.
- Design **A-007**: the board buffers every aggregate in LittleFS and drains it when a connection
  exists, so a passage without marina Wi-Fi is recorded rather than lost. Segment files with
  fixed 64-byte records, a reserved share so track points cannot evict temperature history, and a
  drain that sends the current state first and the backlog second. Not implemented yet, and it
  surfaced a blocker: **PubSubClient publishes at QoS 0 only**, so the board gets no acknowledgement
  and cannot know when a buffered record is safe to delete. The MQTT client has to be replaced
  before any of it is built.
- `server/ingest`: a Go service that subscribes to the broker and writes rows. It ignores fields it
  does not know so newer firmware cannot stop it, drops malformed payloads with a log line rather
  than exiting, and retries broker and database independently.
- Ingest checks the telemetry table's columns at startup. `db/init` only runs when the data
  directory is created, so a database that predates a schema change keeps the old columns and every
  insert fails with the same error indefinitely. Said once at startup it is a diagnosis; found
  through the insert log it is an afternoon. It does not exit on a mismatch - a service that dies on
  a bad environment is one somebody has to watch.
- Delivery from broker to database is **lossless**: a persistent session so the broker queues while
  ingest restarts, QoS 1 subscriptions, and acknowledgement only after the row is committed. A
  failed insert is left unacknowledged for redelivery, turning the broker's inflight limit into
  backpressure instead of quietly dropping measurements. This matters because the board will delete
  buffered measurements on the strength of an acknowledgement it gets from the broker, not from
  ingest. `max_queued_messages` is raised to 100000 so a backlog uploaded after a trip survives
  ingest being down at that moment.
- Status indication on the onboard RGB LED: blue when nothing is configured, yellow while
  connecting or while the broker is silent, green once telemetry flows, red for an alarm. It blinks
  rather than sitting still so that a frozen pattern gives a hanging firmware away - a steady LED
  would only prove the supply is on. `status::setAlarm()` is in place for the alarm logic to call.
- The BOOT button does something: a short press acknowledges an alarm if one shows and otherwise
  publishes immediately, which is how you prove the chain works while standing at the box. Held for
  eight seconds it restores the access point password to its MAC-derived default - the only lockout
  this design allows, since the access point is never switched off. The LED signals the long press
  building from one second in.
- Grafana with a provisioned data source and two dashboards. **Heartbeat** - uptime, free heap,
  signal strength, messages per hour and samples per window - shows a board restarting at night, a
  leak, a radio degrading and an outage that happened while nobody was watching. **Sensors** plots
  the mean as a line with the min/max range shaded behind it, so the band on the fridge panel *is*
  the compressor cycle; it stays empty until the probes are wired.
- `bringup` build environment: the board verification firmware moved out of the way now that
  `boathub` carries the real application.
