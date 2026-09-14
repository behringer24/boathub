# <PHASE>-NNN - Feature title

| | |
|---|---|
| **Phase** | A / B / C - or "project-wide" |
| **Software version** | v1 / v2 / v2.5 / v2B / v3 / v4 - see ../ROADMAP.md |
| **Touches hardware** | yes / no |

## 1. Goal

What the feature is supposed to do, in two or three sentences. What is explicitly **out of scope**.

## 2. Starting point

What already exists, what is assumed, which other documents are related.

## 3. Hardware

Only fill in when something gets soldered or wired.

**Parts**

| Part | Qty | Purpose | Note |
|------|-----|---------|------|
| | | | |

New parts also go into [../MATERIAL.md](../MATERIAL.md).

**Pin assignment**

| GPIO | Signal | Direction | Level | Note |
|------|--------|-----------|-------|------|
| | | | | |

**Circuit**

```
ASCII sketch or pointer to the schematic
```

**Electrical constraints** - voltage ranges, currents, pull-ups, protection, cable lengths,
grounding.

## 4. Software

Structure, modules, state machine, timing and intervals, required libraries.

**Data format / interface**

```json
```

**Configurable values** - what belongs in NVS/Preferences, what are constants, what are
calibration values.

## 5. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Sensor does not answer | | |
| Value implausible | | |
| Connection lost | | |

A fault in this feature must not drag the remaining functions down with it.

## 6. Safety

What can go wrong and what protects against it. In particular:

- Behaviour during boot, after a reset and after a lost connection
- Effects on existing boat systems (autopilot, SeaTalk bus, house supply)
- Fusing and galvanic isolation
- Access control: what is restricted to the local on-board Wi-Fi, what may go through the server

For anything that writes to SeaTalk or influences the course, this section is mandatory.

## 7. Verification

**On the bench**

- [ ] ...

**In the boat**

- [ ] ...

**Calibration** - reference instrument, procedure, where the resulting value is stored.

Leave the boxes unticked. This is a checklist for whoever builds the thing, not a progress report;
our own state belongs in `PROGRESS.md`.

## 8. References

- Datasheets, projects, discussions
