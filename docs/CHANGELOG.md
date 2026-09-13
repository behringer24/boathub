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
- `docs/design/001-power-supply.md`: 12 V input protection, battery measurement, and the battery
  state machine including shore-power-loss detection.
- Shore-power-loss alarm as a work package (1.6a). With a charger running, the battery voltage no
  longer reports state of charge - what it reports instead is whether the charger is still there.
- `docs/design/002-devkit-and-carrier.md`: identifies the actually-delivered board (a third-party
  sparkleIoT XH-S3E module on an MRD076A screw-terminal carrier), maps the project pin plan onto
  the carrier's terminals, and lists the terminals that must not be used.

### Changed

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
