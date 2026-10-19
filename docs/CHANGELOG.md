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

- The **Temperatures** panel on the sensor dashboard shows every temperature the boat measures:
  the cabin from the SHT31 and the battery bay from the BME680 beside the three probes. They
  share one axis, so a warm battery bay can be read against the cabin and the engine bay at a
  glance.

- `i2cscan` tells a BME688 from a BME680 by the variant register at 0xF0. Both report chip ID
  0x61, and a board sold as a CJMCU-680 can carry either; named by the chip ID alone, a BME688
  passed as a BME680. Pressure, temperature and humidity are the same on both; only the gas
  readout differs, and the firmware keeps a BME688's heater off.

- **Telemetry records are 128 bytes, format 3**, to carry the BME680: four more channels, for
  pressure in 0.1 hPa, the battery bay's temperature and humidity, and gas resistance in 0.1 kΩ.
  Segments of the 64- and 96-byte formats still drain at their own size - `.seg`, `.sg2`, and now
  `.sg3` - because the firmware notes the highest segment of each old format at start and appends
  only in the new one. Sixteen segments now hold 2 MB, some 57 days. `bufcheck` covers the new
  channels and keeps its read-back array off the stack.

- `i2cscan` reads the chip ID of a device at 0x76 or 0x77 and names it: BME680, BME280, BMP280 or
  BMP180. The whole Bosch pressure family shares those two addresses, so the address alone could
  only guess - and a board printed "BME280" often carries a BMP280, which has no humidity sensor.

- **Telemetry records are 96 bytes, format 2**, to carry the IMU. Segments of the 64-byte format
  stay readable: the extension tells them apart (`.seg`, `.sg2`), the firmware notes the highest old
  one at start, drains those at their own size and appends only in the new format. A backlog that
  was waiting when the board was updated is sent, not stranded. Sixteen segments now hold 1.5 MB,
  still some 55 days.
- A channel counts its readings apart from its measurements, and keeps a sum of squares in double
  precision. The IMU summarises each measurement interval's 250 readings into one channel and the
  window merges it: mean, spread and extremes are exact over every reading, and `n` still counts
  measurements - 30 for a full window. A stored value saturates instead of wrapping round.

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

- `buffer`: the store-and-forward storage layer. Fixed 64-byte records in segment files of
  1024, so record n sits at offset n x 64 - no index, no scanning, and a torn tail is dropped
  by integer division rather than corrupting what is behind it. Releasing storage is one
  `remove()` of a drained segment instead of rewriting a file from the front. Channels are
  scaled into 16 bits with a sentinel for absent, never a zero, so the database's rule that
  "no sensor" and "measured zero" differ survives the round trip. A quota of 1 MB - some 55
  days - evicts the oldest segment and counts what that cost.
- The cursor lives in NVS and is written per acknowledged batch, not per record. The last
  known wall time goes with it, so the next boot starts from a floor rather than from 1970.
- `bufcheck` build environment: exercises the buffer on the real filesystem - append, read
  back field by field, confirm peek does not consume, commit, and check the cursor survives a
  restart. The buffer is the one module whose failures are invisible from outside, so it is
  proven on hardware before anything depends on it. It refuses to run while records are
  waiting, because committing them there would discard measurements that were never sent.

- **Store and forward is live.** A closed window goes to flash, not to a single slot that the
  next one overwrites, and the drain sends it whenever there is somewhere to send it. An
  evening with the server switched off now costs nothing; so does a passage without marina
  Wi-Fi.
- On connecting, a **spot reading goes out before any backlog**. A window of connectivity may
  be minutes, and spent oldest-first it is spent entirely on three-day-old cabin temperatures
  while the one question worth answering - what is the boat doing right now - is still
  unanswered when the window closes.
- The backlog drains in batches of fifty as one array, and **the cursor moves only on the
  PUBACK**. A batch that is never acknowledged was never read: the next connection sends the
  same records again. One batch is in flight at a time, because the cursor is one position in
  one stream and a second batch could only repeat the first.
- One encoder for both paths. A live reading is a batch of one, so a measurement sent
  immediately and the same measurement drained from flash a week later are the same shape on
  the wire.
- The clock starts from the floor the last run stored beside its cursor rather than from
  1970, and records say `restored` so the server can tell a timestamp from a lower bound. A
  record written before the clock was known is dated as it is encoded, from the offset
  recovered for that boot - records on flash are never rewritten.

- Status LED brightness halved. It is a comfort setting rather than a power one: averaged over
  the breathing cycle the light costs some 0.3 mA against a system drawing 52-70 mA, and the
  WS2812's own controller draws three times that in the dark, where no software setting
  reaches it.

- **TLS to the broker**, switched on by a checkbox in the configuration portal so that one
  firmware talks both to a broker on the bench and to one across the internet. The board carries
  ISRG Root X1 rather than the server's own certificate: pinning that would be tighter for ninety
  days and then the board would fall silent when it was reissued.
- A TLS connection is not attempted before NTP has answered. A certificate is valid only between
  two dates, and the board's clock starts at 1970 - or, after a lay-up, at the floor stored beside
  the buffer's cursor, which can be months behind.
- Only one MQTT client is built. Plain and TLS are different types in the library and each carries
  its receive buffer as a member, so the unused one is not constructed at all.

- A clock that survived a soft reset is reported as `restored` rather than `none`. The RTC domain
  keeps running across a watchdog or a restart from the configuration portal, so the time is real
  and merely unverified - but records carried a perfectly good timestamp labelled as having no
  clock at all, which the server has no way to resolve.

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
- `adsread` build environment: a bench meter that prints every channel of every ADS1115 it finds, in
  volts, with a bar so a potentiometer swept across the 3.3 V rail shows the whole scale and any
  dead patch in it. A bus scan proves an address answers; it cannot prove the converter converts.
  Wanted again in phase B, to watch the battery divider against a multimeter before trusting a
  calibration factor.
- The BME680 in the battery bay: station pressure as `pressure_hpa`, and the bay's air as
  `bay_temp_c` and `bay_rh`, each with min and max. A register-level driver in forced mode -
  humidity x1, temperature x2, pressure x16, IIR off - with Bosch's integer compensation from the
  BME68x API and no BSEC. The whole configuration is written in one transaction with every
  measurement, `ctrl_meas` last because writing forced mode is what starts it, so a sensor that
  reset itself costs at most one sample. Nothing waits: the measurement is started and collected on
  a later pass 50 ms on. A reading is rejected for a skipped value, for being out of range, or for a
  jump of more than 2 hPa, 10 °C or 30 %RH - measured against the previous reading rather than the
  last one kept, so a real step cannot lock the sensor out for good. A sensor that stops answering
  is looked for every interval, and on its return it is reset and its calibration read afresh: it
  may be the spare.
- The gas heater is off unless `bme_gas` in NVS switches it on. Then each measurement heats the
  plate to 320 °C for 150 ms and the gas resistance reaches the telemetry as `bay_gas_kohm`; on a
  BME688, whose gas readout differs, the heater stays off regardless. `bme_p_off` adds a signed
  offset in Pa to every pressure reading. Both are on the portal under **Barometer**, which refuses
  an offset beyond 20 hPa - that is a sea-level figure compared with station pressure, not a sensor
  error.
- `bmeread` build environment: pressure, temperature and humidity once a second from the
  firmware's own driver, with the raw `meas_status_0` and both gas control registers read back from
  the chip, so the heater can be seen to be off. A key takes the current pressure as a reference,
  which makes the one-metre lift test readable directly.
- Ingest stores the BME680's fields, migration `004-barometer.sql` adds their columns, and the
  sensor dashboard gains station pressure, the three-hour pressure tendency with the 3 hPa warning
  line, and the battery bay's temperature and humidity. The tendency is computed in the panel's
  query by the board's own timestamps, so a backlog uploaded after a passage shows the trend as it
  happened rather than all at once on arrival.
- Sea-level pressure on the sensor dashboard, between station pressure and the tendency. Each
  boat's barometer height lives in a `boats` table on the server, set by hand with the statement in
  `server/README.md`, and the panel reduces the stored station pressure with it in its query: a
  corrected height redraws the whole history and changes nothing stored. A boat without a height
  shows none rather than an unreduced figure. Migration `005-boat-height.sql` adds the table.
- **Events** on `boathub/<boat-id>/events`, the moment something happens: `boot` with the reset
  reason, the build and the sensors that answered; `sensor_lost` when a sensor that was answering
  has had no valid reading for a minute, and `sensor_back`; `knock` with the boat's axes, its size
  and the knocks the previous hold-off held back; `alarm` and `ack`. Every event goes to a queue of
  32-byte records on flash first and leaves it only on the broker's PUBACK, so one raised without a
  connection, or before a restart, is sent late rather than lost. Events go out ahead of the
  telemetry backlog. Up to 512 wait; beyond that the oldest is dropped and counted in `dropped`.
  The SHT31's heater cycle is not reported as a lost sensor.
- **Alarms judged on the board**: `frost` when the coldest of cabin, engine, bilge and battery bay
  stays below 3 °C for 10 min, cleared at 4 °C; `fridge_warm` above 10 °C for 30 min, armed only
  after the fridge has been below 6 °C for an hour, so a fridge switched off for an empty boat
  raises it once at most; and `humidity`, a warning, at 85 %RH for 6 h, cleared below 80 %. A
  missing reading neither starts nor breaks a hold. An alarm turns the LED red until the BOOT
  button acknowledges it; a warning does not. Thresholds, holds and a switch per alarm are on the
  portal under **Alarms**, which refuses a fridge armed above where it alarms and humidity that
  clears above where it warns. The status line, the portal and `/status` show what is raised.
- The boot event names the firmware: `tools/build_stamp.py` writes the commit and the build time
  into a header in the build directory on every build, and only the one file that includes it is
  compiled again.
- Ingest stores events with their identity - migration `006-events.sql` adds the columns and a
  unique index, so a redelivery is turned away - and keeps each one whole in `payload`. A new
  **BoatHub - events** dashboard shows the alarms active now and every event in the range, and the
  sensor dashboard draws alarms, knocks and the board's own events as markers on every panel.
- The green breathing of the status LED can be switched off, for a cabin where it is too bright at
  night: **Breathe green while everything works** on the configuration portal, NVS key
  `led_breathe`, on by default. Off, the healthy state is dark and every other state shows as
  before. A short press on BOOT then shows two breaths of green, so a healthy board can still be
  told from a hung one.
- Heel, pitch and motion in the telemetry: `heel_deg` and `pitch_deg` with min, max and their
  standard deviation within the window, `motion_g` with min and max, and `knocks`, counted straight
  through the hold-off. The spread is there because the extremes are set by a single wake and say
  nothing about the other five minutes. Heel and pitch stay absent until the boat is levelled; a
  spot reading carries them without spread. The half minute after a start, while the gyroscope's
  offset is still being learned, stays out of the window: kept, it set the first window's extremes
  a degree away from a boat lying perfectly still. Batches drained from the buffer shrink from 50
  records to 20, because a record with every sensor is now about a kilobyte of JSON. Ingest stores the new fields, migration
  `003-motion.sql` adds their columns, and the sensor dashboard gains heel, trim, rolling and
  pitching, and motion with the knock count.
- The IMU runs in the firmware: attitude at 25 Hz, knocks watched by the chip and read off the
  latched INT1 line, both on the serial log and the status line. Heel and pitch stay out until the
  boat has been levelled once - **Level now** on the configuration portal, refused for the first
  half minute while the gyroscope's offset is still being learned. The axis mapping, the filter's
  time constant, the knock threshold and the hold-off are settings on the portal. The zero
  reference is stored in the module's own axes, so correcting the axis setting keeps it. A knock
  during the hold-off is counted into the next report rather than dropped, and a loop pass stalled
  by a failed MQTT connect is not integrated as three seconds of turning.
- LSM6DSOX driver at register level and the attitude filter for A-009, with an `imuread` bench
  environment that runs both. The filter tracks the direction of "up" rather than two separate
  angles, because a heeled boat turning about her mast would otherwise read the turn as pitch, and
  it learns the gyroscope's zero offset as it runs: uncorrected, a 1 dps offset holds the reading
  3 degrees off at a 3 s time constant. It learns no faster than an offset could explain, so a
  knock or a module turned over in the hand does not wind the offset up. The gyroscope runs at
  ±1000 dps for the same reason. The bench reader checks the INT1 wire at start by having
  the chip invert the line, names the axis gravity pulls along so the print on the module can be
  confirmed, and shows heel and pitch beside what gravity alone says. The knock detector compares
  the change between consecutive samples rather than the acceleration itself, which is what tells
  a knock from a seaway.
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

### Fixed

- The SHT31 can no longer lock itself out with its rate-of-change check. A reading was compared
  with the last *accepted* one, so after one real step beyond 10 °C or 30 %RH - a wet sensor dried
  by the heater in a cabin below about 65 %RH, or the sensor unplugged while the cabin warmed up -
  every later reading was rejected as a jump until the board restarted. It is now compared with the
  previous reading that passed the CRC and range checks, kept or not, and only when that one is no
  more than two intervals old; the first reading after a heater cycle is not compared at all. A
  spike costs two samples, a real step one. The BME680 already works this way.
