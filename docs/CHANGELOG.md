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

### Changed

- Documentation language switched to English; only the source PDF stays German until it is
  rewritten.

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
