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

## 3. The circuit

Reference designators start at 20 so that this stage can be merged onto the main board later
without renumbering anything - [B-002](B-002-main-board.md) uses J1-J12, R1-R9, C1-C5, D1-D2 and
U1-U2.

```
  SeaTalk cable                     J20
  ─────────────                    ─────
   red    +12 V ──────────────────► 1     landed, not used
   yellow DATA  ──────────────────► 2 ──┬─────────── ST_DATA
   screen GND   ──────────────────► 3   │
                                    │   │
                                  ST_GND│
                                        │
                              [ D20 ]  SMBJ15A, ST_DATA to ST_GND
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

### If this stage ends up on the main board

[B-002](B-002-main-board.md) uses a ground plane, and a plane fills everything it is not forbidden
to fill. `ST_GND` would therefore have to be an **island**, excluded from the pour, with a
deliberate gap in the copper running beneath OK20 - across both layers.

A separation that exists in the schematic and not in the copper is worse than none, because it
reads as isolation on every drawing and is not. That alone is an argument for giving this stage its
own small board.

## 5. Parts

| Ref | Value | Purpose |
|-----|-------|---------|
| J20 | 3-pole screw terminal, 5.08 mm | SeaTalk +12 V / DATA / GND |
| D20 | SMBJ15A | clamps transients on the data line; 15 V standoff sits above the 12 V idle |
| OK20 | PC817 | level shift and isolation. ~4 µs edges against a 208 µs bit |
| R20 | 4.7 kΩ | LED series resistor, on the instrument side |
| R21 | 10 kΩ | pull-up on the ESP side |

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
- [B-002-main-board.md](B-002-main-board.md) - `IO15` is brought out on the reserved header J11
- [A-001-devkit-and-carrier.md](A-001-devkit-and-carrier.md) - the pin plan reserving IO15 and IO16
- Thomas Knauf's public SeaTalk reference, the standard description of the bus and its datagrams
