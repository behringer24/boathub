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
- `play` build environment with an LED sandbox for working out patterns on the onboard WS2812 and
  BOOT button, without touching the firmware.
- Wi-Fi: access point `BOOT-NETZ` and station run at the same time, with the station retried under
  exponential backoff so a network outage never takes the local side down.
- Configuration portal on the access point. Network, broker and boat identity are entered there and
  stored in NVS, so no credential is ever compiled in. The access point password defaults to one
  derived from the MAC address rather than a shared constant.
- MQTT uplink: a heartbeat on `boathub/<boat-id>/telemetry` every 10 s carrying timestamp, uptime,
  free heap, RSSI and reset reason, plus a retained `status` topic with `offline` as the last will.
  Timestamps come from NTP in UTC; a missing sync is reported as `time_valid: false` rather than
  holding up telemetry.
- `server/`: Mosquitto broker as a Docker Compose service on port 1883, authenticated, with
  persistence so retained messages survive a restart.
- Telemetry storage: PostgreSQL with TimescaleDB and PostGIS. The `telemetry` hypertable is
  partitioned on the server's receive time rather than the board's clock, which is null until NTP
  has synced, and compressed after seven days. At roughly 40 MB a year compressed there is no
  retention policy - full resolution is kept. PostGIS is unused so far and present so the track
  logger needs no migration of the whole database.
- `server/ingest`: a Go service that subscribes to the broker and writes rows. It ignores fields it
  does not know so newer firmware cannot stop it, drops malformed payloads with a log line rather
  than exiting, and retries broker and database independently.
- Grafana with a provisioned data source and a heartbeat dashboard - uptime, free heap, signal
  strength and messages per minute. Those four show a board restarting at night, a leak, a radio
  degrading and an outage that happened while nobody was watching.
- `bringup` build environment: the board verification firmware moved out of the way now that
  `boathub` carries the real application.
