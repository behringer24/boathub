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

**A boat at a pontoon and a boat that sails need opposite things from the uplink.** At the berth a
measurement is a heartbeat: if it cannot be delivered now, the next one is along in five minutes
and the old one is worth little. At sea there is no marina Wi-Fi at all, and Starlink only in
phases on longer trips - and a measurement dropped there is part of a passage that cannot be
measured again.

The passage wins, because it is the irreplaceable one. So the buffer is not a special case:

> Every aggregate is written to flash. The uplink drains the buffer when it can. **"Live" is simply
> the case where the buffer is empty** and the record goes straight back out again.

One code path, and being offline stops being a mode.

### What there is to work with

| | |
|---|---|
| LittleFS partition | **6 MB** (`0x5E0000`), already in `partitions.csv` and `platformio.ini`. It is **named** `littlefs`; `spiffs` is its subtype, which is what the partition table calls this kind of storage and is not a name. `LittleFS.begin()` looks up by name and defaults to `"spiffs"`, so the label has to be passed or the mount finds nothing |
| PSRAM | 8 MB, so RAM buffering before a flash write costs nothing |
| Aggregate | 5 min window, measured every 10 s - [A-005](A-005-server-uplink.md) |

Nothing currently mounts LittleFS. This is the feature that first uses it.

## 3. Why the board publishes at QoS 1

**A buffer needs something to release a record on.** QoS 0 is fire-and-forget: the board gets no
acknowledgement that the broker accepted the message, so a buffer whose whole purpose is to delete
a record once it is safely delivered would have to delete on a guess - and lose data whenever a
publish is dropped in flight - or never delete and fill up.

It is also what makes the rest of the chain worth having.
[A-006](A-006-telemetry-storage.md) makes delivery lossless from broker to database - persistent
session, QoS 1, acknowledgement after the row is committed - and all of that rests on the board
publishing at QoS 1. At QoS 0 the chain is sound from the broker onwards and **open at the very
first hop**.

The client is **espMqttClient**, driven from `loop()` rather than from a task of its own
(`UseInternalTask::NO`). `publish()` returns the packet id, and `onPublish()` reports that same id
when the PUBACK arrives - which is the trigger the read cursor in section 6 advances on.

**Running it synchronously is not a detail.** An acknowledgement then arrives in the task that owns
the filesystem, so the cursor, the segment files and the outstanding-packet table are touched from
one thread and none of this needs a mutex. A client with its own task would put a callback from
elsewhere in the middle of a LittleFS write.

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
| `t_wall` | 4 | unix seconds, **0 when the clock was never set**; see section 5 |
| `window_s` | 2 | |
| `n` | 1 | measurements in the window; 1 marks a spot reading |
| `flags` | 1 | time source (2 bits, section 5), spot reading, sensor-fault bits |
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

A buffered record is worth much less if it cannot be dated, and at sea there is no NTP - and,
before stage 2, no GPS either.

### The board has no battery-backed clock

What it does have is a system time that runs off the module's 40 MHz crystal while the chip is
awake, and an RTC domain that survives deep sleep. **Neither survives losing power**: after an
interruption the clock restarts at 1970. There is no coin cell anywhere on this board.

Drift, though, is a non-problem. At the 10-20 ppm typical of the module crystal:

| Drift | per day | over a two-week trip |
|-------|---------|----------------------|
| 10 ppm | 0.9 s | 12 s |
| 20 ppm | 1.7 s | 24 s |
| 50 ppm, a poor crystal | 4.3 s | 60 s |

Against a 5 min aggregate window even the worst of those is a fifth of one record. Sync at the dock,
sail for a fortnight, and the timestamps are still good. The much less accurate internal RC
oscillator only takes over in **deep sleep**, which here is the exceptional `BATTERY_CRITICAL`
state rather than normal running.

**So the thing to design for is a power interruption, not drift.**

### What a record carries

- `t_mono_s` is **always** written. Ordering within one boot is therefore always recoverable, no
  matter what the wall clock did or did not know.
- `t_wall` is written when the clock is known, and is 0 otherwise.
- When the clock becomes known mid-boot, the offset `t_wall - t_mono_s` is recorded once and
  applied to earlier records of the same boot **as they are encoded for upload**. Records on flash
  are never rewritten.
- `boot_id` lets the server group and order records from one boot even with no absolute time at all.

### Surviving a restart: a persisted floor

The drain already writes its cursor to NVS after each batch. **The last known wall time goes
alongside it**, which costs nothing extra.

On boot that value is restored as a *lower bound*: the clock is at least that late. Combined with
`t_mono_s` the records of the new boot are then dated to within the length of the outage - seconds
for a watchdog reset, and only genuinely wrong after a long lay-up. That turns the common case from
"undated" into "approximately dated", which is the difference between a record that can be read and
one that cannot.

### Three states, not two

| `time_source` | `time_valid` | Meaning |
|---------------|--------------|---------|
| `ntp` / `gps` | true | synced, trust it |
| `restored` | false | the NVS floor plus elapsed time - ordering is right, absolute time is a lower bound |
| `none` | false | no clock this boot; only `boot_id` and `t_mono_s` order these |

`time_valid` stays the field to filter on; `time_source` says why, which is what turns "this
timestamp looks odd" into a diagnosis.

### If exactness matters

A **DS3231** on the existing I2C bus is about 3 EUR and needs no extra pin: address 0x68 clashes
with neither the SHT31 at 0x44 nor the ADS1115s at 0x48/0x49. Temperature-compensated to
±2 ppm - about a minute a year - with a coin cell that lasts years.

Note that the cheap ZS-042 modules carry a **charging circuit for rechargeable LIR2032 cells**.
Fitted with an ordinary CR2032 they will try to charge it; the usual remedy is to remove the
charging resistor or its diode.

An external 32.768 kHz crystal on the ESP itself is **not** the answer. `XTAL_32K_P`/`XTAL_32K_N`
are GPIO15 and GPIO16 - exactly the SeaTalk reservation - and it would only improve accuracy, not
survive the power loss that is the actual problem.

From stage 2, GPS is the time source at sea, which is what the project guide already assumed. That
leaves one narrow gap: stage 1, at sea, after an interruption - and the persisted floor covers it
well enough that hardware is optional.

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
  hundred writes a day, which NVS wear levelling does not notice. **The last known wall time is
  written with it** (section 5), so a restart starts from a floor rather than from 1970.
- A segment whose every record is acknowledged is deleted.
- The window closing mid-drain costs nothing: the cursor is where it was, and the next connection
  continues there.

### Always an array

A telemetry message is **always a JSON array**, even with one element. Live and backfill then have
one shape, and the ingest needs one code path rather than two.

## 7. When the buffer is full

Within a quota, the oldest segment is deleted - acknowledged ones first, and only then
unacknowledged ones.

Thinning old records instead is tempting, and deliberately left out of v2. At 34 days of combined
recording the case barely arises, and decimation is real code with real edge cases.

What is **not** optional is admitting it: a counter of dropped records goes into the payload and to
the dashboard. Silent loss is the failure this whole document exists to prevent, and a buffer that
overflows quietly would reintroduce it at the other end.

## 8. What this needs elsewhere

Implementing this document means three changes outside the board firmware:

| Where | Change |
|-------|--------|
| `server/ingest` | accept a **JSON array** of records, not only a single object |
| `server/db` | `boot_id` and `seq` columns, and a **claim table** keyed on `(boat_id, boot_id, seq)`, written in the same transaction as the row. QoS 1 is at-least-once, so a redelivered batch **will** arrive twice |
| `server/db`, `server/ingest` | a `time_source` column and field - `ntp`, `gps`, `restored` or `none` (section 5). `time_valid` stays the boolean to filter on |
| `board` | the buffer itself: segments, cursor, drain, on top of the QoS 1 publisher in section 3 |

**Why a claim table and not a unique index on `telemetry`.** That table is a hypertable, and
TimescaleDB refuses any unique index that does not contain the partitioning column:

```
ERROR: cannot create a unique index without the column "received_at"
```

Adding `received_at` would satisfy the rule and destroy the purpose - it is stamped on arrival, so
a redelivered copy carries a different one and collides with nothing. An ordinary table holding
just the identity has no such constraint, and writing it in the same transaction as the row keeps
the two from ever disagreeing: either both exist or neither does.

The duplicate case is not theoretical. Without the claim a reconnect mid-batch double-counts rows,
and the first place it shows is the dashboard.

## 9. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Power lost mid-write | short or torn tail record | discard the partial record; LittleFS is power-fail safe by design |
| Cursor points past the end | segment shorter than the index | clamp to the end, log, carry on |
| Segment file missing | open fails | skip to the next, count it as dropped |
| Filesystem will not mount | mount fails | **keep publishing live** and log loudly. Losing the buffer must not cost the live path as well |
| Broker acknowledges, ingest never stores | - | out of scope here; that is what [A-006](A-006-telemetry-storage.md)'s persistent session and manual acknowledgement are for |
| Clock jumps when NTP arrives | offset recorded once | earlier records of the boot are dated at encode time, not rewritten |
| Power lost, clock back to 1970 | restored floor is older than `t_mono_s` implies | records go out as `time_source: restored`, ordered correctly and dated to within the outage |

## 10. Verification

- [ ] Pull the network: records keep accumulating, the file count grows, nothing resets
- [ ] Reconnect: a current reading arrives **first**, then the backlog oldest-first
- [ ] Kill the connection mid-drain, reconnect: no gap and no duplicate in the database
- [ ] Power-cycle mid-drain: the cursor survives and the drain resumes where it stopped
- [ ] Power-cycle mid-write: the board comes back up and the partial record is discarded
- [ ] Fill the buffer past its quota: oldest go first, the dropped counter rises and reaches the
      dashboard
- [ ] Boot with no NTP, buffer, then let NTP arrive: earlier records upload with plausible times
- [ ] Power-cycle with no network at all, buffer, reconnect: records carry `time_source: restored`
      and times within the length of the outage, not 1970
- [ ] Erase NVS and boot with no network: records arrive with `time_source: none`, `time_valid`
      false, and are still correctly ordered within the boot
- [ ] BOOT button with no connection: a spot reading is buffered rather than refused
- [ ] Corrupt a segment file by hand: it is skipped and counted, ingest is unaffected
- [ ] 24 h unattended with the network flapping: no gap, no duplicate, no reset

## 11. References

- [A-005-server-uplink.md](A-005-server-uplink.md) - the payload and the publish path
- [A-006-telemetry-storage.md](A-006-telemetry-storage.md) - the delivery guarantees this depends on
- [A-004-wifi-and-configuration-portal.md](A-004-wifi-and-configuration-portal.md) - connection state
- [../ROADMAP.md](../ROADMAP.md) - stage 2.5, the track logger that shares this buffer
