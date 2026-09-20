# D-003 - SeaTalk1 TX output stage

| | |
|---|---|
| **Phase** | D |
| **Software version** | v2B |
| **Touches hardware** | yes |

## 1. Goal

Pull the SeaTalk1 bus low under firmware control, and **never** by accident.

**Out of scope:** what to transmit and when it may be armed, which is the autopilot document; and
receiving, which is [D-001](D-001-seatalk-rx-stage.md) and has to work before any of this is
armed.

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

Designators are the ones in the KiCad project, as [B-002](B-002-main-board.md) section 4 lists
them.

```
   IO16 ──[ R12  1 kΩ ]──┬──── B
                         │        Q1   BC337-25 (NPN, TO-92)
                    [ R13  10 kΩ ]
                         │
                        GND ──── E

                                  C ──[ R14  100 Ω ]──► ST_DATA
```

| Ref | Value | Purpose |
|-----|-------|---------|
| Q1 | BC337-25, TO-92 | pulls `ST_DATA` low. A 2N3904 substitutes electrically, but **check its pin order** - it is not the same as a BC337's, and the board's footprint is not either |
| R12 | 1 kΩ | base current from a 3.3 V pin |
| R13 | 10 kΩ | **base to emitter. Not optional - see section 2** |
| R14 | 100 Ω | series into the bus |

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

### R14 stays at the low end

It sits in series with the transistor and forms a divider with the bus pull-up. At 470 Ω against a
1 kΩ pull-up the low level would be some 3.8 V, which no receiver reads as low. At 100 Ω it is
about 1.3 V, which every receiver does.

**Measure the bus pull-up before settling this value.** It is a property of the instruments on that
particular boat, and no datasheet has it.

## 4. Where the transmit current returns

Q1's emitter sits on the board's ground and pulls `ST_DATA` down towards it. The return then has to
reach the instruments' ground, and there are two routes.

**Through the supply cable**, which exists whether anyone wants it or not: `GND` is battery
negative at J9, and `ST_GND` is battery negative at the instrument end. Twelve milliamps through a
few tens of milliohms of boat wiring is under a millivolt, so this route works.

**Through JP2**, a few millimetres of wire on the board.

The second is better, and it is what the link is for: a short, defined return instead of one that
runs the length of the boat and shares a conductor with the DevKit's transmit bursts. **Close JP2
when this stage is commissioned.**

What the link is *not* is a safety measure. Leaving it open does not keep this board off the bus,
because the first route is still there - [D-001](D-001-seatalk-rx-stage.md) section 4 sets that
out. What keeps the bus free is R13, which is section 2 and the reason this document exists.

The alternative the open link keeps available: a second optocoupler driving a transistor powered
from the instrument side, which removes the board-side return altogether at the cost of two more
parts and a supply taken from the bus. Worth revisiting if a ground loop through the SeaTalk screen
ever causes trouble.

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
| ESP unpowered or booting | would hold the bus low - **the whole instrument network down** | R13 |
| Firmware crash, GPIO left high | as above | R13 only helps once the pin goes high-impedance; the watchdog has to reset the board, and after reset R13 holds |
| Firmware bug transmitting at the wrong moment | corrupted datagrams on a live bus | the arming state machine, its own document |
| Two devices transmitting together | both datagrams lost | read-back and retry, section 5 |
| R14 too large | nobody hears the low level | measured, section 3 |
| Bus shorted to 12 V externally | Q1 sees 12 V through R14 | 100 Ω limits it to ~120 mA, inside a BC337's rating |
| **Q1 fails collector-emitter short** | the bus is held low permanently - the whole instrument network down, and no firmware can release it | nothing on the board. Opening JP2 does not help, because the return still runs through the supply cable. **The cure is to unplug the SeaTalk cable** |

The first row is the reason this document exists. Every other fault on this board costs a
measurement; that one costs the boat its instruments.

The last row has no electrical remedy, and does not need one to be acceptable: a wired-OR bus gives
every device on it the same power to jam the line, and the boat already carries several. What this
board owes the bus is that it **notices**. The receive stage reads the line the transmit stage
drives, so a bus that stays low while nothing is being sent is detectable - and that belongs in the
alarm path rather than in a puzzled hour at the chart table.

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
| The bus pull-up value on this boat, which sets R14 | measured at the Raymarine S1 before the board is ordered |
| Whether to remove the board-side return with a second optocoupler instead of closing JP2 | if a ground loop through the SeaTalk screen shows up in the measurements |
| What may be transmitted at all, and what arms it | the autopilot document. **Nothing here is reachable from the server** - control stays on the local on-board Wi-Fi |

## 9. References

- [D-001-seatalk-rx-stage.md](D-001-seatalk-rx-stage.md) - the receive stage, the shared terminal
  and TVS, and the ground link this stage closes
- [D-002-seatalk-decoding.md](D-002-seatalk-decoding.md) - the frame format this has to produce
- [B-002-main-board.md](B-002-main-board.md) - `IO16` reaches this stage from the DevKit socket,
  and the board's own designators for it
