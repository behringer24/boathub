# D-003 - SeaTalk1 TX output stage

| | |
|---|---|
| **Phase** | D |
| **Software version** | v2B |
| **Touches hardware** | yes |

## 1. Goal

Pull the SeaTalk1 bus low under firmware control, and **never** by accident.

**Out of scope:** what to transmit and when it may be armed, which is the autopilot document; and
receiving, which is [D-001](D-001-seatalk-rx-stage.md) and has to work before any of this is built.

## 2. The rule this stage is judged by

SeaTalk1 is wired-OR. Any device holding the line low stops **every instrument on the boat's
network** - depth, log, wind and the autopilot alike. There is no arbitration to protect them; the
bus trusts every device on it.

So the output stage is not judged by how well it transmits. It is judged by what it does when
nothing is working:

| State | The GPIO is | The bus must be |
|-------|-------------|-----------------|
| Board unpowered | floating | free |
| ESP booting, before `setup()` | high impedance | free |
| Firmware crashed | high impedance | free |
| After a watchdog reset | high impedance | free |

In every one of those the pin cannot be relied on, so **the answer has to be in the hardware**. That
is what the base pull-down in section 3 is for, and it is the one component in this design that has
no function in normal operation at all.

## 3. The circuit

Designators continue [D-001](D-001-seatalk-rx-stage.md)'s 20 series.

```
   IO16 ──[ R22  1 kΩ ]──┬──── B
                         │        Q20   BC337 / 2N3904 (NPN, TO-92)
                    [ R23  10 kΩ ]
                         │
                        GND ──── E

                                  C ──[ R24  100 Ω ]──► ST_DATA
```

| Ref | Value | Purpose |
|-----|-------|---------|
| Q20 | BC337-25 or 2N3904, TO-92 | pulls `ST_DATA` low |
| R22 | 1 kΩ | base current from a 3.3 V pin |
| R23 | 10 kΩ | **base to emitter. Not optional - see section 2** |
| R24 | 100 Ω | series into the bus |

### Why a bipolar rather than a MOSFET

The obvious part is a small logic-level MOSFET, and it is the wrong one here for two reasons.

**The common ones are surface mount.** 2N7002 and BSS138 are SOT-23, on a board that is otherwise
entirely through-hole and soldered by hand ([B-002](B-002-main-board.md)).

**The through-hole ones are not logic level enough.** 2N7000 and BS170 specify a gate threshold of
up to 3 V. Driven from a 3.3 V pin, a worst-case part is barely above its threshold and conducts
poorly - and it will be the one unit in the bag that does this, in the boat, in a year.

A small NPN has no threshold to worry about. 3.3 V through 1 kΩ gives about **2.6 mA of base
current**, and any current gain above ten covers a bus that needs perhaps 10 mA.

The price is a saturation voltage of roughly 0.25 V instead of a few ohms of channel resistance.
Against a bus pull-up around 1 kΩ and the 100 Ω series resistor, that moves the low level from
about 1.1 V to about 1.3 V - both far below what any receiver on that bus treats as high.

### R24 stays at the low end

It sits in series with the transistor and forms a divider with the bus pull-up. At 470 Ω against a
1 kΩ pull-up the low level would be some 3.8 V, which no receiver reads as low. At 100 Ω it is
about 1.3 V, which every receiver does.

**Measure the bus pull-up before settling this value.** It is a property of the instruments on that
particular boat, and no datasheet has it.

## 4. This stage needs D-001's ground link closed

Q20's emitter sits on the board's ground and pulls `ST_DATA` down towards it. That only means
anything if `ST_GND` and `GND` are at the same potential - so **JP20 is soldered closed when this
stage is fitted**, and the galvanic separation that receive-only enjoyed ends there.

[D-001](D-001-seatalk-rx-stage.md) section 4 explains the trade and why the link exists as a
deliberate act rather than a plane drawn carelessly.

The alternative, kept open by that link: a second optocoupler driving a transistor powered from the
instrument side, which preserves the separation at the cost of two more parts and a supply taken
from the bus. It is worth revisiting if the ground loop through the SeaTalk screen ever causes
trouble.

## 5. Collisions, and why receiving comes first

SeaTalk1 has no arbitration. Every device may start talking whenever the line looks idle, and two
that start together corrupt each other's datagrams.

The protocol's answer is that a transmitter **listens to itself**: it waits for an idle line, sends,
and reads the bus back bit by bit. Reading a low where it sent a high means somebody else is
driving - so it stops immediately and retries after a random delay.

**The receive stage is what provides that read-back.** Transmitting without it is transmitting
blind into a network the boat navigates by, which is the second reason
[D-001](D-001-seatalk-rx-stage.md) is built and proven alone first.

## 6. Failure modes

| Case | Consequence | What prevents it |
|------|-------------|------------------|
| ESP unpowered or booting | would hold the bus low - **the whole instrument network down** | R23 |
| Firmware crash, GPIO left high | as above | R23 only helps once the pin goes high-impedance; the watchdog has to reset the board, and after reset R23 holds |
| Firmware bug transmitting at the wrong moment | corrupted datagrams on a live bus | the arming state machine, its own document |
| Two devices transmitting together | both datagrams lost | read-back and retry, section 5 |
| R24 too large | nobody hears the low level | measured, section 3 |
| Bus shorted to 12 V externally | Q20 sees 12 V through R24 | 100 Ω limits it to ~120 mA, inside a BC337's rating |

The first row is the reason this document exists. Every other fault on this board costs a
measurement; that one costs the boat its instruments.

## 7. Verification

**None of this happens on the boat's bus first.** Build a bench bus: 12 V through a 1 kΩ pull-up,
and the receive stage watching it.

- [ ] With the board unpowered, the bench bus stays high
- [ ] Through the whole power-on sequence, the bus stays high - watched on a scope, not inferred
- [ ] With `IO16` deliberately left unconfigured, the bus stays high
- [ ] A reset mid-transmission releases the bus immediately
- [ ] Driving `IO16` high pulls the bus to **below 2 V**, measured
- [ ] The receive stage reads back what the transmit stage sent, bit for bit
- [ ] A simulated collision - a second device pulling low mid-datagram - is detected and the
      transmission aborted
- [ ] **Only then**, on the boat: the interface connected with transmitting disabled in firmware,
      and every instrument watched for an hour before anything is armed

## 8. Open points

| Point | Decide by |
|-------|-----------|
| The bus pull-up value on this boat, which sets R24 | measured at the Raymarine S1 before the board is ordered |
| Whether to keep the isolation with a second optocoupler instead of closing JP20 | if a ground loop through the SeaTalk screen shows up in the measurements |
| What may be transmitted at all, and what arms it | the autopilot document. **Nothing here is reachable from the server** - control stays on the local on-board Wi-Fi |

## 9. References

- [D-001-seatalk-rx-stage.md](D-001-seatalk-rx-stage.md) - the receive stage, the shared terminal
  and TVS, and the ground link this stage closes
- [D-002-seatalk-decoding.md](D-002-seatalk-decoding.md) - the frame format this has to produce
- [B-002-main-board.md](B-002-main-board.md) - `IO16` is brought out for this
