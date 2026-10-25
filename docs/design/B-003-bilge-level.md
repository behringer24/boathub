# B-003 - Bilge level, 4-20 mA

| | |
|---|---|
| **Phase** | B |
| **Software version** | v1 |
| **Touches hardware** | yes - a sender on J12, nothing on the board |

## 1. Goal

Report the water level in the bilge, continuously, from a submersible pressure sender on the main
board's loop terminal J12. The level goes out with every telemetry window as mean, minimum and
maximum: a rising mean is the slow leak, and a rising maximum over a flat mean is the pump cycling.

The firmware is complete whether or not a sender is fitted. **With no sender connected the field
is absent** - not zero - so a boat without the probe never shows a dry bilge it has not measured.

**Out of scope:** a bilge level alarm, which belongs with the other thresholds in
[A-012](A-012-alarm-and-threshold-logic.md) once a real bilge has been watched for a while, and
switching the loop off in `BATTERY_CRITICAL` ([B-001](B-001-power-supply.md) section 8).

## 2. Starting point

Everything on the board side exists and is described in [B-002](B-002-main-board.md):

- J12, a 2-pole screw terminal: the loop's +12 V on one pole, its return on the other
- R22, the 100 Ω burden from the return to ground, where the current becomes a voltage
- R15, 1 kΩ in series into ADS1115 no. 1 input A1, with C6 at the input

The converter is the one that measures the battery on A0 ([B-001](B-001-power-supply.md)). Both
channels are read by the same firmware module, one after the other, at the same full-scale range.

The server already stores `bilge_level_cm`, and the sensor dashboard has its panel.

## 3. Hardware

**Parts**

| Part | Qty | Purpose | Note |
|------|-----|---------|------|
| Submersible pressure sender, 0-1 m, 4-20 mA, two-wire, IP68 | 1 | the bilge level | listed in [MATERIAL.md](../MATERIAL.md). Buy one specified from 9-10 V up |

**Circuit** - [B-002](B-002-main-board.md) section 6 has the drawing and the reasoning:

```
   J12.2 ── +12 V ──────────► sender ─┐
   J12.1 ◄─────── loop return ────────┘   4-20 mA
     ├──[ R22 100 Ω ]── GND               0.4-2.0 V
     └──[ R15 1 kΩ ]──┬──► ADS1115 A1
                      └──[ C6 100 nF ]── GND
```

**Electrical constraints**

- **The sender gets what the burden leaves.** At 20 mA R22 takes 2.0 V, so on a battery at 12.0 V
  the sender has 10 V. That is why the purchase is a sender specified from 9-10 V.
- **The sender only runs while the 12 V side is on.** On USB alone the loop has no supply, reads
  0 mA, and the field stays absent.
- **A hydrostatic sender measures pressure against the air.** Its cable carries a vent tube to the
  far end. That end stays dry and open - inside the enclosure, never in the bilge - or the reading
  drifts with the weather and then with the water in the tube.
- **Its zero is the sensing face, not the bilge floor.** A sender that hangs a few centimetres
  above the lowest point reads zero until the water reaches it; the offset in section 4 moves the
  zero to the floor.

## 4. Software

The module `analog` in `board/src/` reads the converter, the driver `ads1115` talks to it.

**One converter, two channels, every sample interval.** Each interval starts a single-shot
conversion of A0, collects it on a later pass, then does the same for A1. At 128 samples per
second a conversion takes 8 ms; nothing waits for it. Both channels use **±2.048 V**, fixed in the
driver: the battery's calibration factor includes the converter's gain error at that setting, and
the burden is sized to fill it.

**From voltage to level:**

```
current  I = V(A1) / 100 Ω                     4 mA = empty, 20 mA = full scale
level      = (I - 4 mA) / 16 mA x full scale + offset
```

| Loop current | Reported |
|--------------|----------|
| below **3.6 mA** | **nothing.** No sender, a cut cable, or no loop supply - the field is absent |
| 3.6-4.0 mA | 0 cm plus the offset. A sender reading a little low in air |
| 4-20 mA | the level |
| above 20 mA | full scale. The converter tops out at 20.48 mA, so a sender signalling a fault high reads as a full bilge - the safe direction to be wrong in |

3.6 mA is the lower fault limit of NAMUR NE 43, which most 4-20 mA senders follow. It is a property
of the standard, not of the boat, and is compiled in.

A level below zero is reported as zero: a negative offset that corrects a sender's zero must not
produce a negative bilge.

**Present, lost and back.** The sender counts as fitted once it has delivered a reading this boot,
and its name in the events is `bilge_level`. One that then stays below 3.6 mA for a minute raises
`sensor_lost` - the cut cable - and `sensor_back` when it returns. A sender that was never there
raises nothing; the boot event's sensor list simply leaves it out
([A-011](A-011-events.md)).

**Data format** - three fields of the telemetry record, absent without a sender:

```json
"bilge_level_cm": 1.3, "bilge_level_cm_min": 1.1, "bilge_level_cm_max": 6.8
```

**Configurable values** - in NVS, set on the configuration page:

| Key | Default | Meaning |
|-----|---------|---------|
| `bilge_fs` | 100 | the sender's range in cm: the level at 20 mA. 100 for a 0-1 m sender |
| `bilge_off` | 0 | mm added to every reading: the sensing face's height above the bilge floor, or a correction of the sender's zero |

The page shows the loop current beside the level, so a sender can be checked without a meter.

## 5. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| No sender fitted | below 3.6 mA from the start | field absent, no event |
| Cable cut, sender failed open | below 3.6 mA after readings | field absent, `sensor_lost` after a minute |
| Board on USB, 12 V side off | no loop supply, 0 mA | field absent |
| Sender signals a fault high, or the bilge is above its range | at the converter's ceiling | full scale - read as a full bilge |
| 12 V onto the loop return (chafed cable) | R22 opens, the loop reads 0 mA | field absent; R15 holds the converter's input clamp to about 8 mA ([B-002](B-002-main-board.md)) |
| ADS1115 does not answer | I2C NACK | battery and bilge both absent, `sensor_lost` for each that had answered; nothing else stops |

A fault here costs the bilge level and nothing else. The battery reading on the same converter
survives everything in this table except the converter itself failing.

## 6. Safety

- **Read only.** Nothing on this channel drives anything, on the board or from the server.
- **The loop draws up to 20 mA from the house battery** for as long as the board is powered - a
  third of the board's own draw. Whether `BATTERY_CRITICAL` should cut it is an open point in
  [B-001](B-001-power-supply.md).
- **The sender cable is a 12 V conductor into the bilge.** Its +12 V pole comes off the fused
  supply; route and strain-relieve it like any other 12 V cable, and keep it clear of the pump.

## 7. Verification

**On the bench**

- [ ] No sender on J12: the status line shows `bilge --`, the configuration page says no sender,
      and no `bilge_level_cm` reaches the server
- [ ] A current source on J12: 4 mA reads 0 cm, 12 mA reads half the range, 20 mA the full range;
      the page shows the same currents
- [ ] 3.0 mA reads nothing; 3.8 mA reads 0 cm
- [ ] Disconnect the source after a reading: `sensor_lost` with `bilge_level` about a minute later -
      the reading drops out at once, without the hold other sensors get - and `sensor_back` when
      it is reconnected

**In the boat**

- [ ] Sender at the lowest point of the bilge, vent end of the cable dry inside the enclosure
- [ ] A measured depth of water in a bucket - 10 cm, say - reads within a centimetre before the
      sender goes into the bilge
- [ ] The level follows the pump: a rising level, a drop when it runs

**Calibration** - set `bilge_off` so that a dry bilge reads zero, or so that a known depth reads
right. A hydrostatic sender is calibrated for fresh water; in sea water it reads about 2.5 % high,
which at bilge depths is a few millimetres and not worth correcting.

## 8. References

- [B-002](B-002-main-board.md) section 6 - the loop, the burden and the input protection
- [B-001](B-001-power-supply.md) - the converter's other channel, the battery
- NAMUR NE 43 - signal levels for failure information of 4-20 mA transmitters
- Texas Instruments, ADS1115 datasheet - single-shot mode, the config register
