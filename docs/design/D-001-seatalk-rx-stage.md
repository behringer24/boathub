# D-001 - SeaTalk1 RX stage

| | |
|---|---|
| **Phase** | D |
| **Software version** | v2 |
| **Touches hardware** | yes |

## 1. Goal

Listen to the boat's existing SeaTalk1 bus through the spare port on the Raymarine S1, without
putting the ESP at risk from a 12 V bus and without putting the instruments at risk from the ESP.

**Out of scope:** turning the bit stream into datagrams, which is
[D-002](D-002-seatalk-decoding.md), and transmitting, which is D-003 and must not be built until
receiving works.

## 2. What SeaTalk1 is, electrically

One wire carries everything. Every device on the bus can pull it low; nobody drives it high.

| | |
|---|---|
| Cable | three cores: **+12 V (red), DATA (yellow), GND (screen)** |
| Idle level | +12 V, held up by pull-ups inside the instruments |
| Drive | open collector, wired-OR - any device pulls low, all devices see it |
| Rate | 4800 baud |
| Frame | 1 start bit, 8 data bits, **1 command bit**, 1 stop bit - eleven bits, 2.29 ms |

The command bit is what makes SeaTalk awkward and it is
[D-002](D-002-seatalk-decoding.md)'s problem, not this document's. Electrically it is simply an
eleventh bit time.

**The +12 V core is landed on the terminal and used for nothing.** The board has its own supply;
taking power from the instrument bus would put the ESP's consumption onto a network whose current
budget belongs to Raymarine.

Ground takes pin 1 and the supply core pin 3, so the two conductors that carry something sit
together at one end and the unused core ends up beside the spare pole, where neither can be taken
for a signal.

## 3. The circuit

Designators are the ones in the KiCad project, as [B-002](B-002-main-board.md) section 4 lists
them.

```
  SeaTalk cable                     J17
  ─────────────                    ─────
   screen GND   ──────────────────► 1 ──── ST_GND
   yellow DATA  ──────────────────► 2 ──── ST_DATA
   red    +12 V ──────────────────► 3      landed, used for nothing
                                    4      spare pole, mark it n.c.

              ST_DATA ──[ D3 ]── ST_GND    1.5KE20A, cathode to ST_DATA
```

```
   ST_DATA ──[ R10  4.7 kΩ ]──┐
                              │
                        ┌─────┴──────────────┐
                        │  1 ──►|── 4        │          3.3V
                        │       U1           │            │
                        │       PC817        │      [ R11  10 kΩ ]
                        │  2 ────────── 3    │            │
                        └─────┬────────┬─────┘            │
                              │        │                  │
   ST_GND ────────────────────┘        └──── 4 ───────────┴────► IO15
                                                          │
                                       3 ─────────────────┴──── GND
```

PC817 pins: 1 anode, 2 cathode, 3 emitter, 4 collector.

### The nets

| Net | Nodes |
|-----|-------|
| `ST_GND` | **J17.1**, D3.2, U1.2, JP2.1 |
| `ST_DATA` | **J17.2**, D3.1, R10.1 |
| - | **J17.3** is where the cable's 12 V core is parked, connected to nothing. Give it a no-connect flag, or ERC reports a pin unconnected on purpose |
| - | **J17.4** is the spare pole of a four-way block. Mark it `n.c.` on the silkscreen, or somebody hunts for a fourth core |
| `Net-(R10-Pad1)` | R10.2, U1.1 |
| `Net-(J7-GPIO15)` | U1.4, R11.2, **J7.8** - the socket pin carrying IO15 |
| `3.3V` | R11.1 |
| `GND` | U1.3, JP2.2 |

IO16 reaches the transmit stage on J7.9.

### Why the TVS is the same part as the supply input's

A 15 V standoff device is the obvious choice for a 12 V bus and it is the wrong one here. The
SeaTalk supply core hangs off the same bank as everything else: absorption takes it to **14.7 V**,
and a charger set to a flooded profile would push 15.5 V, which
[B-001](B-001-power-supply.md) warns about explicitly. A 15 V part would sit at its threshold for
hours at a time.

**Use the 1.5KE20A already on the parts list** - 17.1 V standoff, clear of both cases, through-hole
like the rest of the board, and one fewer line to order. Its couple of nanofarads give a time
constant of about 2 µs against a 208 µs bit, which nothing on this bus will notice.

### The LED current decides how hard this loads the bus

(12 V − 1.2 V) / 4.7 kΩ ≈ **2.3 mA**. At a CTR around 50 % that gives roughly 1 mA of collector
current, comfortably enough to pull a 10 kΩ pull-up down against 3.3 V.

The temptation is a smaller resistor for a healthier optocoupler. Resist it: **the bus is held high
by pull-ups inside the instruments, and every milliamp drawn comes out of that budget.** A 1 kΩ
resistor draws 11 mA continuously from a line that was never meant to feed anything.

**Measure the idle level with the interface connected and disconnected.** If it sags, go up to
10 kΩ; the PC817 still manages, and the bus matters more than the margin.

A 6N137 would need the larger current - several milliamps - in exchange for far faster edges. That
trade only becomes interesting if [D-002](D-002-seatalk-decoding.md) ends up needing sharp
transitions; see section 7.

### The signal arrives inverted

Bus idle high → LED conducts → phototransistor conducts → collector low. A UART expects its idle
state high, so this is upside down.

Do not fix it with another transistor. The ESP32 inverts in hardware inside the UART block, and a
bit-banged receiver inverts by reading the pin the other way round. It costs nothing at either end.

## 4. Grounding: what the optocoupler separates, and what it does not

This is the part worth being precise about, because the obvious word for it is the wrong one.

The LED loop runs entirely on the instrument side - `ST_DATA` → R10 → LED → `ST_GND`. The board's
own ground is not involved, and between the two halves of the PC817 sits a barrier rated in
kilovolts.

**That is not two floating systems held apart.** This board is powered from the boat's battery
through J9, so `GND` is battery negative; the instruments' `ST_GND` is battery negative at their
end. The two are bonded through the boat's own wiring whether or not JP2 is closed. What is
isolated is the **signal path** - there were never two ground systems to separate.

Which is still worth the part, and worth being exact about:

| What it buys | |
|---|---|
| No bus current through this board's ground | the LED loop closes on the instrument side, so nothing the bus does appears across the ground plane the battery divider is measured against |
| No conductive path from the bus to a GPIO | a fault on `ST_DATA` meets D3, then the LED, then the barrier. IO15 is on the far side of it |
| No second conductor in parallel with the boat's negative | and this is the only one of the three that JP2 changes |

The third is a real effect rather than a formality. Closing the link puts the supply cable's
negative and the SeaTalk screen in parallel between the same two points. That loop carries
circulating current whenever the boat's negative bus has a potential gradient along it, which with
an engine running and an alternator charging it does.

### What the link does not do

The transmit stage's return does not depend on it. Q1's emitter sits on `GND`, which reaches the
instruments' ground through the supply cable regardless - and twelve milliamps through a few tens
of milliohms of boat wiring is well under a millivolt, against bus levels with volts of margin.
**Transmitting works with JP2 open.**

So the link is not an interlock, and nothing should be built on the belief that an open link keeps
this board off the boat's bus. What keeps it off the bus is R13 holding Q1's base at ground - see
[D-003](D-003-seatalk-tx-stage.md) section 2. **JP2 decides where the transmit current returns, not
whether it flows.**

[D-003](D-003-seatalk-tx-stage.md) accepts the board-side return rather than driving a second
optocoupler from the instrument side, which would remove it altogether at the cost of two more
parts and a supply taken from the bus. That remains the fallback if a ground loop through the
SeaTalk screen ever shows up in the measurements.

### What the two directions share

Almost nothing, which is what makes a compact layout possible.

| Shared | |
|--------|---|
| **J17**, the four-pole terminal | one cable serves both directions |
| **D3**, the TVS on `ST_DATA` | protects receiving and transmitting alike |

| Receiving only | Transmitting only |
|----------------|-------------------|
| R10 and the optocoupler | Q1, its base pull-down and its collector resistor |
| R11, on the ESP side of the isolation | - |

The two branches meet at one net, `ST_DATA`, and nowhere else.

### The ground island on the main board

[B-002](B-002-main-board.md) uses a ground plane, and a plane fills everything it is not forbidden
to fill. `ST_GND` therefore has to be an **island**, excluded from the pour, and it is a small one -
four pads:

```
J17.1    the cable's ground
U1.2     the LED's cathode
D3.2     the TVS anode
JP2.1    the pad the wire link lands on
```

The gap in the copper runs **lengthwise beneath the optocoupler**, between its two pin rows. A
DIP-4 puts them 7.62 mm apart, which is room enough for a clean break on both layers. R11 belongs
on the far side of that gap, with the board's own ground.

Put J17, D3, R10 and U1 together as one block at the board edge and the island stays small,
which is what an island should be.

### Track width here is set by the TVS, not by the signal

In operation this stage carries nothing: 2.3 mA through the optocoupler's LED, some 10 mA when the
transmit stage pulls the bus down. The default 0.2 mm handles 750 mA, so everything is sixty times
over-provisioned.

One path is different. When a transient arrives down the SeaTalk cable, D3 clamps it, and the
clamping current runs through **J17.2 → D3 → J17.1** and nowhere else - up to 54 A for about a
millisecond. Those two short segments get **1 mm**, because they are a few millimetres long and the
copper costs nothing.

**Placement matters more than width there.** A clamp is only as good as the loop it clamps across:
the inductance between the terminal and the TVS produces a voltage the TVS cannot remove, because
it appears behind it. Put D3 close to J17, within a centimetre or so. On the 8/20 µs waveform
the 1.5KE is rated for, ten millimetres of track adds well under a tenth of a volt - so the rule
has room in it, and it only tightens if the threat is a nanosecond edge rather than a surge down a
long cable.

The surge returns through the `ST_GND` island rather than the ground plane, since the island is
separate by design. Keep it solid - it is small enough that this happens by itself, as long as no
track cuts through it.

A separation that exists in the schematic and not in the copper is worse than none, because it
reads as isolation on every drawing and is not.

### Keep the decision open with a wire link

Whether this board puts a second conductor in parallel with the boat's negative is decided by the
link - and by a ground plane, if one is drawn without care. A plane that swallows `ST_GND` makes
the bond permanent and makes it invisible, which is the worst of both.

**Two pads at 2.54 mm between `ST_GND` and `GND`, left open.** A short piece of wire soldered
through closes the link, and side cutters reopen it - so the bond is something you can see, undo
and measure, rather than a property of a pour.

A pluggable shunt would do the same job and is the obvious thing to reach for. It is the wrong
choice here for two reasons that only apply on a boat: the enclosure vents to outside air because
moisture gets in, which is hard on a plug contact left closed for years; and a board in an engine
space lives with vibration a desk does not. A shunt that works loose does not announce itself - the
transmit path simply stops, which is the failure mode this design spends its effort avoiding
everywhere else.

Give the two pads a **two-pad test point footprint** - plain through-holes at 2.54 mm. They take
a wire link directly, and nothing about the layout forces the choice before assembly.

That costs two pads and keeps the return path a decision rather than an accident. It also keeps
the second-optocoupler variant available: isolating the transmit side properly requires the link to
be open, and a plane that has already bonded the two leaves nothing to open.

## 5. Parts

| Ref | Value | Purpose |
|-----|-------|---------|
| J17 | screw terminal, 5.08 mm, 4-pole | GND, DATA, the bus 12 V parked, fourth pole unused - the same part as the probe terminals |
| D3 | 1.5KE20A | clamps transients on the data line - **the same part as the supply input's TVS**, see below |
| U1 | PC817 | level shift and isolation. ~4 µs edges against a 208 µs bit |
| R10 | 4.7 kΩ | LED series resistor, on the instrument side |
| R11 | 10 kΩ | pull-up on the ESP side |
| JP2 | two pads at 2.54 mm, left open | a wire link bridges `ST_GND` to `GND` when the transmit stage is fitted - see section 4 |

## 6. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Cable not connected | no edges at all; the pin sits at one level | `seatalk_online` false, everything else carries on |
| Bus loaded too hard by R10 | instruments misbehave, idle level sags | measured in section 8 before the bus is trusted |
| Optocoupler too slow for the chosen receiver | bits misread at the edges, CRC-less garbage | visible on a logic analyser as narrow or shifted bits; the answer is a 6N137, not a smaller resistor |
| A bus device jams the line low | permanent low, no framing | reported as offline; **nothing on this board can cause it, because this stage cannot drive the bus at all** |
| Transient on the data line | - | clamped by D3 before it reaches the LED |

The last row holds for this stage on its own: it has no driver, so it cannot pull the bus down at
all. The transmit stage is fitted beside it, and what keeps the bus free while the ESP is
unpowered, booting or crashed is R13 - see [D-003](D-003-seatalk-tx-stage.md) section 2.

## 7. Verification

Before anything is connected to the Raymarine S1:

- [ ] Bench test against a signal generator or a second microcontroller sending 4800 baud, with the
      LED fed from a 12 V bench supply rather than the boat
- [ ] Pin level at IO15 follows the simulated bus, inverted, with clean edges on a logic analyser
- [ ] `ST_GND` measures open against `GND` on the bare board - nothing bridges the island by
      accident, through a shared terminal or a pour that filled where it should not have
- [ ] **`ST_DATA` measures open against `GND` with IO16 held low**, which is what proves R13 is
      doing its job. It is the transmit stage's only barrier, and the one measurement that
      distinguishes a fitted R13 from an empty pad

On the boat:

- [ ] **Bus idle voltage measured with the interface disconnected, then connected.** A drop of more
      than a few tenths of a volt means R10 is too small
- [ ] Every instrument still behaves normally with the interface attached - depth, log, wind and
      autopilot all watched for several minutes
- [ ] Edges at IO15 on a logic analyser: bit time 208 µs ±5 µs, frames of eleven bits
- [ ] Interface disconnected and reconnected while the instruments run: nothing on the bus notices

## 8. Open points

| Point | Decide by |
|-------|-----------|
| R10's final value, which depends on what the bus tolerates | during the idle-level measurement in section 7 |
| Whether `seatalk_online` is derived from datagram traffic or from edge activity | in [D-002](D-002-seatalk-decoding.md) |

## 9. References

- [D-002-seatalk-decoding.md](D-002-seatalk-decoding.md) - the bit stream this stage delivers
- [B-002-main-board.md](B-002-main-board.md) - where this stage sits on the board, and what its
  parts are called there
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - the pin plan reserving IO15 and IO16
- Thomas Knauf's public SeaTalk reference, the standard description of the bus and its datagrams
