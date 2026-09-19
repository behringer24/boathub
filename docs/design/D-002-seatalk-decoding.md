# D-002 - SeaTalk1 decoding

| | |
|---|---|
| **Phase** | D |
| **Software version** | v2 |
| **Touches hardware** | no |

## 1. Goal

Turn the bit stream arriving on IO15 into datagrams, and the datagrams into telemetry channels -
depth, speed through water, wind, heading and autopilot state.

**Out of scope:** the interface hardware, which is [D-001](D-001-seatalk-rx-stage.md), and
transmitting anything, which is D-003. Nothing here sends. Reading an autopilot keystroke off the
bus is not the same as pressing one, and the distinction is the whole safety argument.

## 2. The frame, and the bit that causes all the trouble

| | |
|---|---|
| Rate | 4800 baud, so **208.33 µs per bit** |
| Frame | 1 start bit, 8 data bits, **1 command bit**, 1 stop bit |
| Total | eleven bit times, 2.29 ms per byte |

The ninth bit marks the **first byte of a datagram**. Set means "this is a command byte"; clear
means "this is data belonging to the command before it". Without it, there is no way to tell where
a datagram starts, because SeaTalk has no preamble, no checksum and no fixed length.

So the ninth bit is not an optional extra. It is the framing.

### The ESP32 UART cannot do nine data bits

It offers eight plus parity, and that is where the trouble starts.

| Approach | Verdict |
|----------|---------|
| **UART with the parity trick** - let the command bit land in the parity position and recover it from the parity error flag | Arithmetically sound: with even parity, `command_bit = even_parity(data) XOR parity_error`. In practice the ESP-IDF driver reports parity errors as queue events without telling you **which byte** they belong to, and a misaligned flag corrupts framing rather than one value. Avoid |
| **RMT capture** - record edge timings and reconstruct the frames | Works, and gives all eleven bits plus the inter-frame gaps. Costs an RMT channel, block chaining for long datagrams, and a good deal of code |
| **Bit-banged receiver** - GPIO interrupt on the start edge, then a timer sampling mid-bit | **The recommendation.** See below |

### Why bit-banging wins here

At 4800 baud each bit lasts 208 µs. Sampling in the middle leaves **±104 µs of tolerance** - an
eternity on a 240 MHz processor. Wi-Fi interrupt latency is measured in microseconds, three orders
of magnitude inside the budget.

So the usual objection to bit-banging, that other interrupts will ruin the timing, does not apply
at this rate. What you get in exchange is all nine bits, explicitly, with no fight against a driver
that was built for eight.

Requirements on it:

- the interrupt handler and its timer run **pinned to the core that is not serving Wi-Fi**, so the
  radio and the bus cannot interfere with each other at all
- the handler does nothing but capture: it pushes raw bytes plus their command bit into a queue,
  and a task assembles datagrams. A decoder in an interrupt handler is how a bad datagram becomes
  a watchdog reset
- a frame with a bad stop bit is discarded and counted, not guessed at

If the bit-banger turns out to jitter after all, RMT is the fallback - and that is the point at
which [D-001](D-001-seatalk-rx-stage.md)'s optocoupler choice needs revisiting, because RMT
measures edges and a PC817's 4 µs transitions start to matter.

## 3. Datagram structure

```
  byte 0 : command,  ninth bit SET
  byte 1 : attribute - low nibble = number of further data bytes
                       high nibble = payload, meaning depends on the command
  byte 2 : data
  ...
```

Total length is `3 + (byte1 & 0x0F)`. So the second byte tells you how much more to read, and a
datagram is between three and eighteen bytes.

**There is no checksum.** The ninth bit and the length field are the whole of the integrity story,
which has consequences in section 5.

### Datagrams this project wants

The identifiers below are the widely documented ones. **Confirm each against the boat's own
instruments before believing it** - what a bus actually carries depends on which devices are on it,
and a Raymarine S1 with a few instruments is not the full catalogue.

| ID | Carries | Wanted for |
|----|---------|------------|
| `0x00` | depth below transducer | telemetry, and a shallow alarm later |
| `0x10`, `0x11` | apparent wind angle, apparent wind speed | telemetry; wind at the berth is also the context for [A-009](A-009-imu-heel-and-motion.md)'s motion figures |
| `0x20` | speed through water | telemetry, and the log |
| `0x23` | water temperature | telemetry, alongside the DS18B20 channels |
| `0x50`, `0x51` | latitude, longitude | the track logger, if the instruments carry GPS |
| `0x84` | autopilot state and heading | telemetry, **read only** |

Everything else is decoded far enough to skip: read the command, read the length, discard. An
unknown datagram must never break framing, because an unrecognised device on the bus is normal.

## 4. What reaches telemetry

SeaTalk values join the existing aggregate as further channels, the same as any sensor. They arrive
far faster than the window, so they average like the others.

Two that do not fit the pattern:

- **Position** belongs to the track logger, not the aggregate. A mean latitude is meaningless.
- **Autopilot state** is a state, not a measurement. It belongs in the message as the value at the
  end of the window, plus a flag if it changed during it.

`seatalk_online` in telemetry is derived from **datagram traffic**, not from edge activity on the
pin. A bus with a fault can produce plenty of edges and no valid datagrams, and the field exists to
say whether the data is trustworthy.

## 5. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| No traffic at all | nothing received for several seconds | `seatalk_online` false; sensors and uplink unaffected |
| Framing lost mid-stream | stop bit wrong, or a data byte where a command byte was expected | discard to the next byte with the ninth bit set. **That is what resynchronisation means here**, and it is why the ninth bit is not optional |
| Datagram shorter than its length field claims | the next command bit arrives early | discard the partial datagram, count it |
| Unknown command byte | not in the table | skip `3 + (byte1 & 0x0F)` bytes and carry on. Never an error |
| A corrupted byte that still parses | **cannot be detected - there is no checksum** | plausibility limits per channel, exactly as the DS18B20 readings get in [A-003](A-003-ds18b20-temperature-sensors.md). A depth of 300 m under a boat in a marina is rejected, not averaged |
| Bus flooded by a faulty device | queue fills | drop oldest, count, keep the task alive |

The absence of a checksum is the thing to design around. Every SeaTalk channel needs the same
rejection ladder the temperature probes have: a plausible range and a jump limit. Without it, one
corrupted byte becomes a permanent outlier in the stored minimum or maximum.

## 6. Verification

- [ ] Against a recorded bit stream first, not against the boat - a replayed capture is repeatable
      and a live bus is not
- [ ] Bit timing confirmed on a logic analyser: 208 µs, eleven bits per frame
- [ ] Command bit recovered correctly on every frame of a known capture
- [ ] Datagram lengths honoured: a synthetic datagram claiming fifteen further bytes is read as
      eighteen
- [ ] An unknown command byte is skipped without losing the datagram after it
- [ ] Deliberately corrupted framing resynchronises on the next command bit, within one datagram
- [ ] Values match the instrument displays: depth, log and wind read the same on the ESP and on the
      Raymarine head
- [ ] `seatalk_online` goes false within seconds of unplugging the bus, and true again on reconnect
- [ ] **Loop timing unaffected**: the MQTT keepalive and the sensor sample counts do not change
      when the bus is busy
- [ ] The board runs with the bus disconnected, with the bus jammed low, and with a flood of
      traffic, without a reset in any of the three

## 7. Open points

| Point | Decide by |
|-------|-----------|
| Whether the instruments on this boat actually carry GPS over SeaTalk, which decides whether the track logger has a position source before a GPS module is bought | after the first capture from the real bus |
| Which datagrams beyond the table above are present, and whether any are worth storing | after a day of logging everything |
| Plausibility ranges per channel | once real data exists; guessing them first produces limits that reject valid readings |
| Whether a capture facility belongs in the firmware - logging raw datagrams to LittleFS for later analysis | when the buffer from [A-007](A-007-store-and-forward.md) exists, since it is the same machinery |

## 8. References

- [D-001-seatalk-rx-stage.md](D-001-seatalk-rx-stage.md) - the interface delivering this bit stream
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the rejection
  ladder this borrows, for the same reason: a plausible-looking wrong value is the dangerous one
- [A-005-server-uplink.md](A-005-server-uplink.md) - the aggregate these channels join
- [A-009-imu-heel-and-motion.md](A-009-imu-heel-and-motion.md) - heading comes from here, which is
  why the IMU carries no magnetometer
- Thomas Knauf's public SeaTalk reference, the standard description of the datagrams
