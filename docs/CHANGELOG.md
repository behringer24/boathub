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

### Changed

- MQTT client is **espMqttClient** rather than PubSubClient, and telemetry publishes at **QoS 1**.
  The broker now answers every message with a PUBACK, which is what the store-and-forward buffer in
  A-007 will delete a stored record on - at QoS 0 there was no answer at all, so a buffer would have
  had to delete on a guess. The client runs from `loop()` rather than its own task
  (`UseInternalTask::NO`), so an acknowledgement arrives in the task that will own the filesystem
  and no part of the buffer needs a mutex. Connecting is asynchronous now: `connect()` starts the
  attempt and the outcome arrives through callbacks, so the backoff sits in `onDisconnect` rather
  than around a blocking call. Outstanding packet ids are tracked and their round-trip time logged,
  which is the hook A-007 replaces with advancing the read cursor.

- The serial status line names the probes as well as the cabin sensor, and distinguishes a
  probe that was never found from one that enumerated and has gone quiet - `-` against `?`.
  The line predates every sensor and only the SHT31 had been folded into it, so standing at
  the box told you the cabin climate and nothing about the three probes beside it.

- Every telemetry record carries an identity: `boot_id`, incremented in NVS on each boot, and
  `seq`, counting records within that boot. It is stamped when the record is created rather
  than when it is sent, so a retry carries the same pair - otherwise it would not be a retry.
  `seq` deliberately stays out of NVS: persisting it per message would be hundreds of flash
  writes a day, and `boot_id` already separates one run from the next.
- Ingest stores the identity and refuses a second copy of it. QoS 1 is at-least-once, so a
  publish whose acknowledgement is lost is redelivered, and without this the same five
  minutes would appear twice in every average. The claim and the row are written in one
  transaction, so a crash between them leaves neither. A redelivered message is acknowledged
  rather than left outstanding - it is stored, just not by that delivery. Messages without an
  identity are stored unconditionally, which is what firmware older than this sends.
- The identity lives in a claim table rather than a unique index on `telemetry`: that is a
  hypertable, and TimescaleDB requires every unique index to contain the partitioning column
  `received_at` - which a redelivered copy does not share, since it is stamped on arrival.
- `server/db/migrations/`, applied by hand to a database that predates a schema change.
  `db/init` only runs on a fresh data directory, so until now a change meant discarding every
  measurement. Migrations are written to survive being run twice.

- Ingest supervises its own broker connection instead of leaving it to the client library's
  automatic reconnect. Under Docker a stopped container leaves the embedded DNS, so the
  broker's name stops resolving rather than refusing a connection - and the library treats
  that as final, stops trying and says nothing. Observed once for twenty minutes while
  measurements queued for a session that never came back, and the broker's log recorded no
  further attempt from the service at all.

- A telemetry message is a **JSON array of records**. Backfill from the board's buffer and a
  live reading then have one shape, and ingest needs one code path rather than two. A single
  object is still accepted - a board sends what its firmware knows how to send.
- The whole batch is one transaction: a redelivery finds either every record already claimed
  or none of them, never half.
- `time_source` says which clock produced a timestamp - `ntp`, `gps`, `restored` or `none`.
  `time_valid` stays the field to filter on; this one turns "that timestamp looks odd" into a
  diagnosis.

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
  for both. The IMU board's own microcontroller is listed among the expected addresses beside
  the sensor itself - an address the project never talks to is still one somebody would
  otherwise spend an evening explaining.
- `adsread` build environment: a bench meter that prints every channel of every ADS1115 it finds, in
  volts, with a bar so a potentiometer swept across the 3.3 V rail shows the whole scale and any
  dead patch in it. A bus scan proves an address answers; it cannot prove the converter converts.
  Wanted again in phase B, to watch the battery divider against a multimeter before trusting a
  calibration factor.
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
- Three DS18B20 probes on GPIO4/5/6, reported as `engine_temp_c`, `bilge_temp_c` and
  `fridge_temp_c`. All three buses are started back to back and waited on once, so the set costs one
  375 ms conversion rather than three - and the wait happens across loop passes rather than in a
  delay, which would otherwise stall Wi-Fi and every other sensor for a third of a second each
  cycle. 11-bit resolution: 0.125 C is finer than anything here needs.
- Each probe's ROM address is recorded the first time it is seen and compared on every start. The
  realistic failure is not a broken sensor but three identical cables unplugged for service and two
  put back the wrong way round, after which engine bay temperature silently reports bilge water. A
  mismatch is reported and does not stop the reading, since a legitimately replaced probe must not
  take the channel down with it.
- A reading is rejected unless it passes in order: the library's own scratchpad CRC, not the
  disconnected sentinel, **not exactly 85.0 C** - the scratchpad's power-on value, and a legal
  temperature, which is what makes it dangerous - a plausibility range per location, and a jump
  limit against the previous reading.
- SHT31 cabin climate on the shared I2C bus at 0x44, measured every 10 s and reported as
  `cabin_temp_c` and `cabin_rh`. Single shot rather than free-running, because continuous
  measurement warms the sensor and a warm humidity sensor reads low; and the non-stretching command,
  because clock stretching would hold SCL low for every ADS1115 sharing the bus as well. Both
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
- Sampling and aggregation split out of the uplink into `telemetry`: measurements are taken every
  `sample_secs` and summarised over `pub_secs`, defaults 10 s and 300 s. The module sits between the
  sensors and the uplink deliberately - the buffer in A-007 will sit in the same place, and an
  aggregate is what it stores. `n` is the smallest count behind any reported channel, so a sensor
  that missed half the window cannot hide behind one that did not.
- Sensors hand each measurement over exactly once rather than being polled. `telemetry` has no
  clock of its own: a second timer would drift against the sensors' timers, so a reading would
  sometimes be counted twice and sometimes skipped, and the sample count in the message would
  quietly stop meaning the number of measurements. The measurement rate is now the sensor's own,
  configured by `sample_secs`.
- The extremes are written only when more than one sample is behind them, so a spot reading carries
  the bare value alone rather than claiming a range it never measured. A window that closes while
  the uplink is not ready is counted and logged - until the buffer exists, that count is the honest
  measure of what was lost.
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
  drain that sends the current state first and the backlog second. Not implemented yet; the QoS 1
  publisher it depends on is.
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
  leak, a radio degrading and an outage that happened while nobody was watching. Its heap axis
  starts at zero: left to autoscale, the few dozen bytes of ordinary variation fill the panel and
  read as a collapse, which is indistinguishable at a glance from one that matters. **Sensors** gives
  every measured quantity its own panel, plotting the mean as a line with the min/max range shaded
  behind it, so the band on the fridge panel *is* the compressor cycle. The three probe
  temperatures additionally share one overview panel, and that one carries the means alone - three
  shaded bands on a single axis read as mush. Panels stay empty until their sensor is wired.
- `bringup` build environment: the board verification firmware moved out of the way now that
  `boathub` carries the real application.
