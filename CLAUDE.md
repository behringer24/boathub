# ESP32 BoatHub - working conventions

## Git

- Main branch is `main`.
- **Never push.** Andreas pushes himself. Commit and merge locally only.
- Work happens on feature branches: `feature/<short-name>`, merged back into `main` with
  `--no-ff`. Never commit straight onto `main`.
- **Commit messages never contain a `Co-Authored-By:` trailer** and no reference to an AI tool.
  The same applies to pull request descriptions.
- Commit messages in English, short and meaningful, subject line in the imperative.

## Documentation

- All documentation is written in English. The only exception is the source PDF under
  `docs/reference/`, which stays German until it is rewritten.
- **Keep the documentation in sync with every change.** A change is not finished until the
  affected documents are updated in the same commit:

  | Change | Also update |
  |--------|-------------|
  | anything notable | `docs/CHANGELOG.md` (hardware marked **[HW]**) |
  | work package status | `docs/ROADMAP.md` |
  | parts, prices, sources, order status | `docs/MATERIAL.md` |
  | a design document's own parts table | reconcile it into `docs/MATERIAL.md` in the same commit - the design doc is the rationale, MATERIAL.md is the single orderable list |
  | new or changed feature | design document in `docs/design/` plus its index |
  | pin assignment, architecture, safety rules | `README.md` |

- New features get a design document in `docs/design/` before they are implemented
  (template: `docs/design/TEMPLATE.md`), listed in the index in `docs/design/README.md`.

## Technical guardrails

These rules come from the project guide and are not up for negotiation:

- No Wi-Fi or server passwords in source code. Configuration lives in NVS/Preferences.
- Server telemetry is one-way: **no autopilot or control commands from the internet.** Control is
  restricted to the local on-board Wi-Fi.
- SeaTalk TX stays disabled until RX runs reliably and the output stage has been tested. After a
  reset or a lost connection the transmit path is passive.
- Sensor and network faults are decoupled; a broken sensor must not take the system down. Use the
  watchdog.
- Do not reassign the reserved GPIOs (15/16 SeaTalk, 17/18 TWAI, 43/44 debug UART). Avoid the
  strapping pins GPIO0/3/45/46, the USB pins GPIO19/20 and GPIO33-37 on the N16R8.
