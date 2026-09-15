# A-007 - Store and forward

| | |
|---|---|
| **Phase** | A |
| **Software version** | v2 |
| **Touches hardware** | no |

## 1. Goal

Write every measurement to flash and drain the buffer whenever a connection exists, so that a
passage without marina Wi-Fi is **recorded** rather than lost.

**Out of scope:** the track logger's own record type, which is stage 2.5 and needs GPS - but the
buffer built here is the one it will use, so the layout has to leave room for it. TLS on 8883, which
is independent of this.

## 2. Starting point

[A-005](A-005-server-uplink.md) originally dropped a message when the connection was down, on the
grounds that telemetry is a heartbeat and a stale reading is worth less than none. **That was right
for a boat at a pontoon and wrong for a boat that sails.** Under way there is no marina Wi-Fi at
all, and Starlink only in phases on longer trips. Dropping discards precisely the part of the
record that cannot be measured again.

So the buffer stops being a special case:

> Every aggregate is written to flash. The uplink drains the buffer when it can. **"Live" is simply
> the case where the buffer is empty** and the record goes straight back out again.

One code path, and being offline stops being a mode.

### What there is to work with

| | |
|---|---|
| LittleFS partition | **6 MB** (`0x5E0000`), already in `partitions.csv` and `platformio.ini` |
| PSRAM | 8 MB, so RAM buffering before a flash write costs nothing |
| Aggregate | 5 min window, measured every 10 s - [A-005](A-005-server-uplink.md) |

Nothing currently mounts LittleFS. This is the feature that first uses it.

## 3. The blocker: PubSubClient cannot do this

**PubSubClient publishes at QoS 0 only.** There is no QoS argument in its publish API at all.

That is fatal here, and not in an obvious way. QoS 0 is fire-and-forget: the board gets **no
acknowledgement** that the broker accepted the message. A buffer whose whole purpose is to delete a
record once it is safely delivered has nothing to trigger the deletion. Either it deletes on a
guess - and loses data whenever a publish is dropped in flight - or it never deletes and fills up.

It also quietly undermines the work already done on the server: [A-006](A-006-telemetry-storage.md)
makes delivery lossless from broker to database on the assumption that the board publishes at
QoS 1. Without that, the chain is sound from the broker onwards and open at the very first hop.

**So the MQTT client has to change before any of this is built.**

| Candidate | For | Against |
|-----------|-----|---------|
| **espMqttClient** (bertmelis) | QoS 0/1/2, TLS, maintained, Arduino-style API close to what `uplink.cpp` already does | another third-party dependency |
| **esp-mqtt** (ESP-IDF native) | no third-party dependency - Arduino sits on ESP-IDF anyway; QoS 1/2; TLS shares the certificate handling 8883 needs | event-driven API, a larger rewrite of `uplink.cpp` |
| arduino-mqtt (256dpi) | simple, QoS 1 | less active |

**Recommendation: espMqttClient**, because it keeps `uplink.cpp` recognisable and the change stays
proportionate. `esp-mqtt` is the better long-term answer if the TLS work turns out to want ESP-IDF
certificate handling regardless - in which case doing both at once is cheaper than doing them
separately. **Decide before implementing, not during.**

## 4. Layout on flash

### Segments, not one file

Records go into **append-only segment files** of 64 kB, `seg-00000123.dat`, numbered monotonically.

The alternative - one large ring file - means deleting from the front, which on a filesystem
requires rewriting everything behind it. **Deleting a whole segment is one `remove()`**, and
LittleFS reclaims the blocks. This is the ordinary log-structured answer and it is why the segment
size matters more than elegance: 64 kB is 1024 telemetry records, about three and a half days, so a
fully drained segment is released promptly without the file count ever getting silly.

### Fixed-size records

64 bytes, fixed, little-endian. Fixed size means record *n* is at offset `n * 64` - no scanning, no
index, and a torn write at the tail is detectable and discardable rather than corrupting the file.

| Field | Bytes | Note |
|-------|-------|------|
| `seq` | 4 | monotonic per boat, never reused |
| `boot_id` | 2 | incremented in NVS on every boot |
| `t_mono_s` | 4 | seconds since boot - always known |
| `t_wall` | 4 | unix seconds, **0 when the clock was never set** |
| `window_s` | 2 | |
| `n` | 1 | measurements in the window; 1 marks a spot reading |
| `flags` | 1 | time valid, spot reading, sensor-fault bits |
| `rssi` | 1 | dBm, signed |
| `heap_kb` | 2 | |
| 7 channels x mean/min/max | 42 | scaled integers, see below |
| reserved | 1 | |

Scaling keeps everything in 16 bits: temperatures in 0.01 °C (`int16`, ±327 °C), battery in mV
(`uint16`), humidity in 0.1 % (`uint16`), bilge level in mm (`uint16`). A channel with no sensor
stores a sentinel that the encoder turns back into an **absent JSON key**, never a zero - the same
rule the database follows.

**JSON is not stored.** The same record as JSON is about 250 bytes; binary is 64. Storing what goes
on the wire would cost a factor of four of the one resource that is actually scarce.

### What fits

| | per record | per day | 6 MB alone |
|---|---|---|---|
| Telemetry, 5 min aggregates | 64 B | 18 kB | ~325 days |
| Track points at 25 m (stage 2.5) | 32 B | ~160 kB | ~37 days |
| **Both, under way** | | ~178 kB | **~34 days** |

### Telemetry gets a reserved share

Tracks and telemetry share the partition, and tracks outweigh telemetry nine to one. Without a
quota, **one hard day of tacking evicts weeks of temperature history** - the record that is cheap
to keep gets thrown away to make room for the one that is expensive.

So telemetry gets a guaranteed **1 MB** (some 55 days) that track segments may not take. Tracks get
the rest and evict among themselves.

## 5. Time

A buffered record is worth much less if it cannot be dated, and under way there is no NTP and -
before stage 2 - no GPS either.

- `t_mono_s` is **always** written. Ordering within one boot is therefore always recoverable.
- `t_wall` is written when the clock is known, and is 0 otherwise.
- When the clock becomes known mid-boot, the offset `t_wall - t_mono_s` is recorded once and
  applied to earlier records of the same boot **as they are encoded for upload**. Records on flash
  are never rewritten.
- A boot that never learns the time uploads with `time_valid: false`. The server keeps its own
  `received_at`, which is what that column is for.
- `boot_id` lets the server group and order records from such a boot even with no absolute time.

From stage 2, GPS is the time source at sea, which is what the project guide already assumed.

## 6. Draining

### Current state first, backlog second

On connect, **publish a fresh spot reading before starting the backlog.**

This looks like a detail and is not. A Starlink window may be minutes. Spent oldest-first, it goes
entirely on three-day-old cabin temperatures and the one question worth answering - *what is the
boat doing right now* - is still unanswered when the window closes.

After that, oldest first, so the record stays contiguous.

### Batched, acknowledged, resumable

- **Batch** about 50 records into one message as a JSON array. Individually, a day of backlog is 288
  round trips, each waiting for its PUBACK over a satellite link - the batching is what decides
  whether a window is enough.
- **QoS 1**, and the read cursor advances **only on PUBACK**.
- The cursor (`segment`, `index`) is persisted in NVS after each batch, not each record - roughly a
  hundred writes a day, which NVS wear levelling does not notice.
- A segment whose every record is acknowledged is deleted.
- The window closing mid-drain costs nothing: the cursor is where it was, and the next connection
  continues there.

### Always an array

A telemetry message is **always a JSON array**, even with one element. Live and backfill then have
one shape, and the ingest needs one code path rather than two.

## 7. When the buffer is full

Within a quota, the oldest segment is deleted - acknowledged ones first, and only then
unacknowledged ones.

Thinning old records instead is tempting and deliberately not done in v2. At 34 days of combined
recording the case barely arises, and decimation is real code with real edge cases.

What is **not** optional is admitting it: a counter of dropped records goes into the payload and to
the dashboard. Silent loss is the failure this whole document exists to prevent, and a buffer that
overflows quietly would reintroduce it at the other end.

## 8. What this needs elsewhere

Not yet done. All three belong to implementing this document:

| Where | Change |
|-------|--------|
| `server/ingest` | accept a **JSON array** of records, not only a single object |
| `server/db` | `boot_id` and `seq` columns, plus a unique index on `(boat_id, boot_id, seq)`. QoS 1 is at-least-once, so a redelivered batch **will** arrive twice; insert with `ON CONFLICT DO NOTHING` |
| `board` | replace PubSubClient - section 3 |

The duplicate case is not theoretical. Without the unique index a reconnect mid-batch double-counts
rows, and the first place it shows is the dashboard.

## 9. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Power lost mid-write | short or torn tail record | discard the partial record; LittleFS is power-fail safe by design |
| Cursor points past the end | segment shorter than the index | clamp to the end, log, carry on |
| Segment file missing | open fails | skip to the next, count it as dropped |
| Filesystem will not mount | mount fails | **keep publishing live** and log loudly. Losing the buffer must not cost the live path as well |
| Broker acknowledges, ingest never stores | - | out of scope here; that is what [A-006](A-006-telemetry-storage.md)'s persistent session and manual acknowledgement are for |
| Clock jumps when NTP arrives | offset recorded once | earlier records of the boot are dated at encode time, not rewritten |

## 10. Verification

- [ ] Pull the network: records keep accumulating, the file count grows, nothing resets
- [ ] Reconnect: a current reading arrives **first**, then the backlog oldest-first
- [ ] Kill the connection mid-drain, reconnect: no gap and no duplicate in the database
- [ ] Power-cycle mid-drain: the cursor survives and the drain resumes where it stopped
- [ ] Power-cycle mid-write: the board comes back up and the partial record is discarded
- [ ] Fill the buffer past its quota: oldest go first, the dropped counter rises and reaches the
      dashboard
- [ ] Boot with no NTP, buffer, then let NTP arrive: earlier records upload with plausible times
- [ ] Boot that never sees NTP: records arrive with `time_valid: false` and are still ordered
- [ ] BOOT button with no connection: a spot reading is buffered rather than refused
- [ ] Corrupt a segment file by hand: it is skipped and counted, ingest is unaffected
- [ ] 24 h unattended with the network flapping: no gap, no duplicate, no reset

## 11. References

- [A-005-server-uplink.md](A-005-server-uplink.md) - the payload, and the decision this reverses
- [A-006-telemetry-storage.md](A-006-telemetry-storage.md) - the delivery guarantees this depends on
- [A-004-wifi-and-configuration-portal.md](A-004-wifi-and-configuration-portal.md) - connection state
- [../ROADMAP.md](../ROADMAP.md) - stage 2.5, the track logger that shares this buffer
