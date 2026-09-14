# Changelog

All notable changes to the firmware, hardware and server side of the ESP32 BoatHub.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows
[Semantic Versioning](https://semver.org/).

Categories: `Added` · `Changed` · `Deprecated` · `Removed` · `Fixed` · `Security`

Hardware changes (circuit, pin assignment, parts) are prefixed with **[HW]** so they stay easy to
find when rebuilding the system or revising the board.

---

## [Unreleased]

### Added

- Project repository created: `docs/` with roadmap, changelog, design directory and source
  documents.
- README with architecture overview, pin assignment, hardware summary and safety rules.
- Roadmap covering stages 1, 2, 2.5, 2B, 3 and 4 including acceptance criteria and the order of
  the build evenings.
- Template and index for per-feature design documents under `docs/design/`.
- `docs/MATERIAL.md`: bill of materials for stage 1 with quantities, purpose, prices, order status
  and sources, plus the tool list and the preliminary parts for stages 2 and 3.
- Project guide v0.1 (2026-09-13) as a source document under `docs/reference/`.

- `docs/design/000-design-review.md`: validation of the whole design against the component
  datasheets - 3 blockers, 8 important findings, 11 minor ones, plus the missing parts per stage.
- `docs/design/B-001-power-supply.md`: 12 V input protection, battery measurement, and the battery
  state machine including shore-power-loss detection.
- Shore-power-loss alarm as a work package (1.6a). With a charger running, the battery voltage no
  longer reports state of charge - what it reports instead is whether the charger is still there.
- `docs/design/A-001-devkit-and-carrier.md`: identifies the actually-delivered board (a third-party
  sparkleIoT XH-S3E module on an MRD076A screw-terminal carrier), maps the project pin plan onto
  the carrier's terminals, and lists the terminals that must not be used.

- `board/`: PlatformIO firmware project for the ESP32-S3 N16R8, in its own subdirectory so the
  telemetry server can later live beside it in `server/`. PlatformIO ships no board definition for
  this module, so `esp32-s3-devkitc-1` - the N8 variant without PSRAM - is used with explicit
  overrides: 16 MB flash and `board_build.arduino.memory_type = qio_opi` for the octal PSRAM.
- `board/partitions.csv`: 16 MB layout - two 5 MB app slots, 6 MB LittleFS for the later track
  logger, 64 kB core dump. Pinned down before the first flash on purpose, because changing the
  layout afterwards erases NVS, which is where the Wi-Fi and server credentials are to live.
- `board/src/main.cpp`: bring-up firmware for A.1. It verifies flash size, PSRAM and the partition
  table over the CH343P port rather than only blinking an LED, so a wrong board configuration
  cannot pass as success.
- `boathub.code-workspace`: multi-root workspace, so the PlatformIO extension picks up the project
  in `board/` while the documentation stays open at the repository root.

### Changed

- Toolchain question resolved: **PlatformIO, not the Arduino IDE** (A-002 section 9,
  `docs/MATERIAL.md`). The N16R8 overrides have to be checked in and reviewable, which IDE menu
  settings cannot provide.
- **[HW]** WS2812 RGB LED confirmed on **GPIO48** of the delivered board, added to the pin table in
  the README as reserved.
- **[HW]** Octal PSRAM confirmed working on the delivered board (8 386 279 bytes usable, 1 MB
  write/read test passed). This is the empirical proof that GPIO33-37 are occupied and must stay
  off the project pin plan - until now that was only derived from the datasheet.
- Documentation language switched to English; only the source PDF stays German until it is
  rewritten.
- **[HW]** Input protection reordered: the TVS now sits **ahead of** the 1N5822 instead of behind
  it, so a surge no longer has to pass through the 3 A Schottky before being clamped (B2).
- **[HW]** Battery divider tap moved **upstream of** the 1N5822. Measuring behind it leaves roughly
  ±80 mV of load- and temperature-dependent error that calibration cannot remove - about the width
  of a 25 % state-of-charge step (B3).
- **[HW]** SeaTalk TX driver changed from the 74LS07 reference to a 2N7002/BSS138 N-MOSFET **with a
  mandatory 10 kΩ gate pull-down**, so the bus cannot be held low while the ESP boots or after a
  crash (I5).
- **[HW]** NMEA2000 transceiver pinned to a 3.3 V part (SN65HVD230 class); the 5 V MCP2551 would
  drive an ESP32 GPIO beyond its absolute maximum (I6).
- **[HW]** 4-20 mA shunt: second 100 Ω resistor added so 50 Ω is available, halving the burden
  voltage at identical resolution if the probe needs the compliance headroom (I2).
- 1-Wire pull-ups: 2.2 kΩ and 3.3 kΩ added as alternatives to 4.7 kΩ, which is marginal on the 5 m
  probes (I3).
- Power concept settled: the boat is permanently on shore power, so the system runs continuously
  and no low-quiescent-current DC/DC is needed. That part is dropped from the parts list (B1).
- SHT31 I2C run revised from a conservative 1 m to a calculated **3 m**, extendable to 5-6 m with
  one extra pull-up pair. The sensor sits outside both the enclosure and the S1 cabinet (I1a).
- 4-20 mA probe minimum supply voltage turned from an open question into a purchase criterion,
  with the 50 Ω shunt as the default so the answer matters less (I2).
- **[HW]** Controller identified as the actually-purchased Heemol set (ASIN B0GJZS3P1J): a
  third-party sparkleIoT XH-S3E module on an MRD076A screw-terminal carrier, not a genuine
  Espressif WROOM-1/-1U. Superseded finding M9 with M9a (002).
- Battery thresholds retuned for the actual bank, 2 x 100 Ah AGM. AGM rests about 0.2 V higher
  than flooded lead-acid, so the generic values were far too low: warning moves from 12.0 V to
  **12.3 V** (~50 % SoC) and critical from 11.8 V to **12.0 V** (~30 %).
- Shore-power-loss alarm gated on **at least 6 hours of prior charging** instead of a bare voltage
  threshold. Only a shore charger floats that long, so the alarm stays quiet underway - where
  running on the battery is the normal state - without needing a mode switch, a user action or
  SeaTalk. From stage 2, `seatalk_online == false` can confirm it.
- `BATTERY_CRITICAL` behaviour split on the same condition: deep sleep to preserve capacity for the
  bilge pump when nobody is aboard, stay awake and keep the local UI live when somebody is.
- Recorded that a current shunt for Ah counting was considered and rejected: bus-based monitors are
  NMEA2000 devices ("SeaTalkNG" is not SeaTalk1) and are unpowered exactly when the marina alarm
  matters.
- `docs/design/A-002-bench-setup-usb.md`: the USB-powered bench build - power path, current budget,
  how to prove the ADS1115 without a 12 V supply, deliberate failure-mode tests, and the USB-to-12 V
  changeover procedure.
- Stage 1 restructured into three phases: **A** bench on USB power, **B** power supply, **C**
  installation. The 12 V work moves to the end of the bench phase, matching the sequence the
  project guide already specified. Package numbers now carry the phase (A.1, B.2, ...) so the
  table reads as build order.
- Design documents renamed to `<PHASE>-NNN-*` with numbering restarting per phase, so an insertion
  into one phase never renumbers another. Done now, while nothing outside the repository cites
  them: `001-power-supply` → `B-001-power-supply`, `002-devkit-and-carrier` → `A-001-…`,
  `003-bench-setup-usb` → `A-002-…`. The design review keeps the bare `000` as project-wide
  reference material with no phase.
- `docs/design/A-003-ds18b20-temperature-sensors.md`: the three 5 m probes - circuit, mounting,
  non-blocking conversion, ROM identity check against swapped cables, and the reading-validation
  ladder including the 85 °C power-on-default trap.
- **[HW]** 1-Wire pull-up settled at **2.2 kΩ** rather than the guide's 4.7 kΩ. At 5 m the textbook
  value leaves ~2.8 µs of rise against a 15 µs read slot; 2.2 kΩ more than halves that at a sink
  current of 1.5 mA, well inside the DS18B20's 4 mA rating (I3).
- **[HW]** 100 Ω in series in each DS18B20 DATA line at the board end, for surge and ringing
  protection. Deliberately no clamping diodes - their capacitance would cost more in edge quality
  than they buy at 3.3 V (I3).

- Bill of materials audited against every design document and restructured: a **Phase** column says
  when each part is first needed, review additions folded into the functional sections, and an
  "order now" summary at the top. Gaps closed that the design documents had introduced without
  reaching the list - most importantly the **1 kΩ series resistor into ADS1115 A0**, which is the
  safety part that makes reverse polarity survivable (B3).
- Inventory update 2026-09-14: 2 x DevKit sets, 3 x ADS1115, 3 x SHT3x, 3 x DS18B20 and a soldering
  iron in stock. The only remaining phase A purchase is a resistor assortment - without a 1-Wire
  pull-up the DS18B20 cannot work at all.

### Security

- Recorded that SeaTalk TX has no fail-safe in the original design: an ESP32 GPIO is
  high-impedance for roughly 300 ms after power-on and floating again after a crash, which without
  a gate pull-down can hold the SeaTalk bus low and take down the instruments and autopilot (I5).

---

## Prehistory

State before this repository's history began, taken from the change log of the project guide.

### 0.1 - 2026-09-13

- First consolidated project guide.
- Stage 1 detailed; SeaTalk1 and NMEA2000 scoped as later build stages.
- **[HW]** Battery voltage divider fixed at 82 kΩ / 10 kΩ (divider factor 9.2) for more headroom
  against house-supply spikes.
- **[HW]** Pin assignment fixed: DS18B20 on GPIO4/5/6, I2C on GPIO8/9, SeaTalk reserved on
  GPIO15/16, TWAI reserved on GPIO17/18.
