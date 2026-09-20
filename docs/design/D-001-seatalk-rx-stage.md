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

The designators below name the parts of this stage on their own. **What they are called on the
board is in [B-002](B-002-main-board.md) section 4**, which is read from the KiCad project - the
terminal is J17 there, the clamp D3, the optocoupler U1.

```
  SeaTalk cable                     J20
  ─────────────                    ─────
   screen GND   ──────────────────► 1 ──── ST_GND
   yellow DATA  ──────────────────► 2 ──── ST_DATA
   red    +12 V ──────────────────► 3      landed, used for nothing
                                    4      spare pole, mark it n.c.

              ST_DATA ──[ D20 ]── ST_GND    1.5KE20A, cathode to ST_DATA
```

```
   ST_DATA ──[ R20  4.7 kΩ ]──┐
                              │
                        ┌─────┴──────────────┐
                        │  1 ──►|── 4        │          +3V3
                        │       OK20         │            │
                        │       PC817        │      [ R21  10 kΩ ]
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
| `ST_GND` | **J20.1**, D20.2, OK20.2, JP20.1 |
| `ST_DATA` | **J20.2**, D20.1, R20.1 |
| `ST_12V` | **J20.3** alone - give it a no-connect flag, or ERC reports a pin unconnected on purpose |
| - | **J20.4** is the spare pole of a four-way block. Mark it `n.c.` on the silkscreen, or somebody hunts for a fourth core |
| `ST_LED` | R20.2, OK20.1 |
| `SEATALK_RX` | OK20.4, R21.2, and the socket pin carrying **IO15** |
| `+3V3` | R21.1 |
| `GND` | OK20.3, JP20.2 |

IO16 stays free for the transmit stage.

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

## 4. Grounding: receive-only is genuinely isolated

This is the part worth being precise about, because it stops being true the moment transmitting is
added.

The LED loop runs entirely on the instrument side - `ST_DATA` → R20 → LED → `ST_GND`. The board's
own ground is not involved. **So `ST_GND` is not connected to `GND`, and must not be.**

| | |
|---|---|
| **Receive only** | true galvanic separation. A fault on the bus cannot reach a GPIO, and no second ground path runs through the SeaTalk screen |
| **Once transmitting exists** | an open-drain MOSFET referenced to the board's ground can only pull `ST_DATA` low if both grounds sit at the same potential, so they have to be bonded - and the separation is gone |

On a boat both are battery negative anyway, so bonding them costs little in practice. It does add a
second ground path through the SeaTalk cable, which is a loop that was not there before.

D-003 decides between accepting that and driving a second optocoupler on the instrument side. It is
a real decision, and building receive-only first keeps it open.

### What the two directions share

Almost nothing, which is what makes a compact layout possible.

| Shared | |
|--------|---|
| **J20**, the three-pole terminal | one cable serves both directions |
| **D20**, the TVS on `ST_DATA` | protects receiving and transmitting alike |

| Receiving only | Transmitting only |
|----------------|-------------------|
| R20 and the optocoupler | the MOSFET, its gate pull-down and its drain resistor |
| R21, on the ESP side of the isolation | - |

The two branches meet at one net, `ST_DATA`, and nowhere else.

### If this stage ends up on the main board

[B-002](B-002-main-board.md) uses a ground plane, and a plane fills everything it is not forbidden
to fill. `ST_GND` therefore has to be an **island**, excluded from the pour, and it is a small one -
three pads:

```
J20.3    the cable's ground
OK20.2   the LED's cathode
D20      the TVS anode
```

The gap in the copper runs **lengthwise beneath the optocoupler**, between its two pin rows. A
DIP-4 puts them 7.62 mm apart, which is room enough for a clean break on both layers. R21 belongs
on the far side of that gap, with the board's own ground.

Put J20, D20, R20 and OK20 together as one block at the board edge and the island stays small,
which is what an island should be.

### Track width here is set by the TVS, not by the signal

In operation this stage carries nothing: 2.3 mA through the optocoupler's LED, some 10 mA when the
transmit stage pulls the bus down. The default 0.2 mm handles 750 mA, so everything is sixty times
over-provisioned.

One path is different. When a transient arrives down the SeaTalk cable, D20 clamps it, and the
clamping current runs through **J20.2 → D20 → J20.3** and nowhere else - up to 54 A for about a
millisecond. Those two short segments get **1 mm**, because they are a few millimetres long and the
copper costs nothing.

**Placement matters more than width there.** A clamp is only as good as the loop it clamps across:
the inductance between the terminal and the TVS produces a voltage the TVS cannot remove, because
it appears behind it. Put D20 hard against J20.

The surge returns through the `ST_GND` island rather than the ground plane, since the island is
separate by design. Keep it solid - it is small enough that this happens by itself, as long as no
track cuts through it.

A separation that exists in the schematic and not in the copper is worse than none, because it
reads as isolation on every drawing and is not.

### Keep the decision open with a wire link

Receiving alone is isolated; adding the transmit stage bonds the two grounds. Building receive-only
first is therefore a decision not yet taken - and a ground plane drawn without care takes it for
you, silently.

**Two pads at 2.54 mm between `ST_GND` and `GND`, left open.** While the board only receives, the
separation is real. When the transmit stage arrives, a short piece of wire soldered through closes
it, and side cutters reopen it.

A pluggable shunt would do the same job and is the obvious thing to reach for. It is the wrong
choice here for two reasons that only apply on a boat: the enclosure vents to outside air because
moisture gets in, which is hard on a plug contact left closed for years; and a board in an engine
space lives with vibration a desk does not. A shunt that works loose does not announce itself - the
transmit path simply stops, which is the failure mode this design spends its effort avoiding
everywhere else.

Give the two pads a **pin header footprint** all the same. The holes take a wire link just as well,
and nothing about the layout forces the choice before assembly.

That costs two pads and keeps a decision open that this document is deliberately not making yet.
Reserve roughly 15 x 10 mm beside the terminal for the MOSFET and its two resistors, and a track to
IO16, or the transmit stage becomes a new board rather than an addition to this one.

## 5. Parts

| Ref | Value | Purpose |
|-----|-------|---------|
| J20 | screw terminal, 5.08 mm, 4-pole | SeaTalk +12 V / DATA / GND, fourth pole unused - the same part as the probe terminals |
| D20 | 1.5KE20A | clamps transients on the data line - **the same part as the supply input's TVS**, see below |
| OK20 | PC817 | level shift and isolation. ~4 µs edges against a 208 µs bit |
| R20 | 4.7 kΩ | LED series resistor, on the instrument side |
| R21 | 10 kΩ | pull-up on the ESP side |
| JP20 | two pads at 2.54 mm, left open | a wire link bridges `ST_GND` to `GND` when the transmit stage is fitted - see section 4 |

## 6. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Cable not connected | no edges at all; the pin sits at one level | `seatalk_online` false, everything else carries on |
| Bus loaded too hard by R20 | instruments misbehave, idle level sags | measured in section 8 before the bus is trusted |
| Optocoupler too slow for the chosen receiver | bits misread at the edges, CRC-less garbage | visible on a logic analyser as narrow or shifted bits; the answer is a 6N137, not a smaller resistor |
| A bus device jams the line low | permanent low, no framing | reported as offline; **nothing on this board can cause it, because this stage cannot drive the bus at all** |
| Transient on the data line | - | clamped by D20 before it reaches the LED |

The last row is the reason receive-only is built first: until D-003 exists, there is no failure of
this board that can take the instrument network down.

## 7. Verification

Before anything is connected to the Raymarine S1:

- [ ] Bench test against a signal generator or a second microcontroller sending 4800 baud, with the
      LED fed from a 12 V bench supply rather than the boat
- [ ] Pin level at IO15 follows the simulated bus, inverted, with clean edges on a logic analyser
- [ ] `ST_GND` measures open against `GND` - the isolation is real and not accidentally bridged by
      a shared terminal

On the boat:

- [ ] **Bus idle voltage measured with the interface disconnected, then connected.** A drop of more
      than a few tenths of a volt means R20 is too small
- [ ] Every instrument still behaves normally with the interface attached - depth, log, wind and
      autopilot all watched for several minutes
- [ ] Edges at IO15 on a logic analyser: bit time 208 µs ±5 µs, frames of eleven bits
- [ ] Interface disconnected and reconnected while the instruments run: nothing on the bus notices

## 8. Open points

| Point | Decide by |
|-------|-----------|
| Whether this stage sits on the main board or on its own. Its own board keeps the isolation obvious and the 12 V bus away from the sensor rail; on the main board it saves a connector | when [D-002](D-002-seatalk-decoding.md) has settled the receiver, since that decides whether a PC817 is fast enough |
| R20's final value, which depends on what the bus tolerates | during the idle-level measurement in section 7 |
| Whether `seatalk_online` is derived from datagram traffic or from edge activity | in [D-002](D-002-seatalk-decoding.md) |

## 9. References

- [D-002-seatalk-decoding.md](D-002-seatalk-decoding.md) - the bit stream this stage delivers
- [B-002-main-board.md](B-002-main-board.md) - where this stage sits on the board, and what its
  parts are called there
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - the pin plan reserving IO15 and IO16
- Thomas Knauf's public SeaTalk reference, the standard description of the bus and its datagrams
