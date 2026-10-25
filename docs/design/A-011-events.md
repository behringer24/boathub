# A-011 - Events

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | no |

## 1. Goal

Tell the server **when something happens**, at the moment it happens, rather than in the next
five-minute aggregate. A knock against the pier at 03:14, a sensor that stops answering, a frost
alarm: each is one message on `boathub/<boat-id>/events`, stored on the server and drawn on the
dashboard.

This document is the path: what an event is, how it leaves the board, how it survives having
nowhere to go, and how the server keeps it. The first events are the ones that need no threshold:

- `boot` - the board started, why, and with which sensors
- `sensor_lost` and `sensor_back` - a fitted sensor stopped answering, or came back
- `knock` - the IMU's impact detector fired ([A-009](A-009-imu-heel-and-motion.md) section 7)

and the alarms of [A-012](A-012-alarm-and-threshold-logic.md), which use the same path.

**Out of scope:**

- What raises an alarm. That is [A-012](A-012-alarm-and-threshold-logic.md).
- Notification - a message on somebody's phone. The events are stored and shown first; which of
  them should wake anybody is decided once there is a record of what they look like.
- Anything flowing the other way. Events are telemetry: the board publishes them and subscribes to
  nothing ([A-005](A-005-server-uplink.md) section 2).

## 2. Starting point

The topic has been part of the interface since [A-005](A-005-server-uplink.md): telemetry every
window, a retained `status` with its last will, and `events` "only when something happens". The
server's schema carries an `events` table, and several design documents already name events they
expect - "event once" in the failure tables of the probes, the SHT31, the IMU and the BME680, the
knock in A-009, the boot event in A-005.

Two properties of the existing telemetry path carry over unchanged, because they were the hard
part:

- **Identity.** Every record carries `boot_id` and a sequence number stamped when it is created, so
  a QoS 1 redelivery is recognised rather than stored twice ([A-006](A-006-telemetry-storage.md)
  section 3).
- **Store and forward.** A record is written to flash first and deleted only when the broker has
  acknowledged it ([A-007](A-007-store-and-forward.md)).

## 3. What an event is

One JSON object, with the same envelope as a telemetry record and a `kind` that says what the rest
means:

```json
{
  "ts": "2026-10-16T03:14:07Z",
  "time_valid": true,
  "time_source": "ntp",
  "boot_id": 23,
  "seq": 4,
  "uptime_s": 51807,
  "kind": "knock",
  "axes": ["athwartships"],
  "g": 0.84,
  "held": 3
}
```

| Field | Meaning |
|-------|---------|
| `ts`, `time_valid`, `time_source` | when it happened, by the board's clock - exactly as in telemetry ([A-005](A-005-server-uplink.md)) |
| `boot_id`, `seq` | the identity. `seq` counts events within one boot, separately from telemetry's own `seq` |
| `uptime_s` | seconds since boot at the moment of the event, which orders events when the clock is unknown |
| `kind` | what happened; the remaining fields depend on it |
| `dropped` | only present when events were lost to the queue's limit since boot - section 5 |

### The kinds

| `kind` | Fields | Raised when |
|--------|--------|-------------|
| `boot` | `reset_reason`, `build`, `sensors` | once, as the board starts |
| `sensor_lost` | `sensor` | a sensor that was answering has had no valid reading for a minute |
| `sensor_back` | `sensor` | a lost sensor gives a valid reading again |
| `knock` | `axes`, `g`, `held` | the IMU reports an impact, outside its hold-off |
| `alarm` | `alarm`, `state`, `severity`, `sensor`, `value`, `limit` | an alarm is raised or cleared - [A-012](A-012-alarm-and-threshold-logic.md) |
| `ack` | | somebody acknowledged the alarms with the BOOT button |

**`boot`.** `reset_reason` is the same value telemetry carries. `build` names the firmware: the
commit it was built from - with a `+` when the working tree had changes - and the time of the
build in UTC, written into the build on every compile. A boot event still waiting from before a
firmware update leaves `build` out rather than name the wrong one. `sensors` lists what answered at
start, by the names below - so a probe that was there yesterday and is missing after a restart
shows as a difference between two boot events, not as silence.

**`sensor_lost` and `sensor_back`.** Sensors are named by where they measure, the same prefixes the
telemetry uses:

| Name | Sensor |
|------|--------|
| `cabin` | SHT31 |
| `bay` | BME680 |
| `imu` | LSM6DSOX |
| `engine`, `bilge`, `fridge` | the three DS18B20 probes |
| `battery` | ADS1115 A0, the battery divider ([B-001](B-001-power-supply.md)) |
| `bilge_level` | ADS1115 A1, the 4-20 mA sender - fitted only once a sender has delivered a reading ([B-003](B-003-bilge-level.md)) |

A sensor counts as lost when it **had** answered this boot and has then had no valid reading for
**60 s**. That minute is counted from the moment its driver stopped offering a reading - each
driver already holds its last one for three and a half sample intervals - so a sensor is reported
about a minute and a half after its last measurement. That rules out a single failed read and a
sensor that was never fitted. The SHT31's heater cycle ([A-008](A-008-sht31-cabin-climate.md)
section 4) deliberately produces no readings for two minutes; it is not a loss and raises nothing.

The watch sits outside the drivers: it asks each one for its latest reading once a second, so not
one driver has to know that events exist.

**`knock`.** `axes` names the boat's axes that crossed the threshold - `fore-and-aft`,
`athwartships`, `vertical` - through the IMU's axis setting. `g` is the motion measured at the
knock. `held` is how many further knocks the previous hold-off swallowed, the count A-009 section 7
promises the next report will carry. The knock threshold is still being set from recorded counts,
so these are stored and shown, and notify nobody.

**`alarm`** and **`ack`** are described with the alarms in
[A-012](A-012-alarm-and-threshold-logic.md) section 5.

### A message is an array

As with telemetry, a message on `events` is a **JSON array** of events, even when it holds one. A
live event and a batch drained after an outage then travel the same way.

## 4. Leaving the board

```
   watch / imu / alarms ──► events ──► queue on flash ──► uplink ──► boathub/<id>/events
                                           ▲                              │
                                           └──────── commit on PUBACK ◄───┘
```

`watch` is the sensor watch, `imu` raises the knock, `alarms` the alarms of A-012; `events` stamps
each one with its identity and the time and writes it to the queue.

**Every event goes to flash first**, and the uplink sends whatever is waiting. "Live" is the case
where the queue was empty and the event goes out within the same pass through `loop()` - there is
one code path, and being offline is not a mode, exactly as in [A-007](A-007-store-and-forward.md).

- QoS 1, not retained. A retained event would be replayed to every client that subscribes later, as
  if it had just happened.
- **Up to ten events per message**, one message in flight at a time, released from the queue only
  by the broker's PUBACK.
- **Events go before the telemetry backlog.** On connecting, the board sends `online`, a spot
  reading, then the events, and only then starts on stored windows. A frost alarm that waited
  behind three days of cabin temperatures would arrive when it no longer mattered.
- An event dated before the clock was known is dated as it is encoded, from the offset recovered
  for that boot, and labelled `restored` - the rule telemetry already follows.

### Why events are stored on flash

At sea there is no marina Wi-Fi at all, and the events that matter most are the ones around a fault:
a charger that fails, a battery that sags, a brown-out that restarts the board. Held only in RAM,
everything before that restart would be gone exactly when it explains the restart. On flash it is
sent when the boat is next within reach of a network, late but complete, and its `ts` says when it
happened.

## 5. The queue

A single file, `/evt/queue.bin`, of **32-byte records**, appended to. Its read position lives in NVS
beside the telemetry buffer's cursor and is written once per acknowledged message - a few writes a
day.

| | |
|---|---|
| Record | 32 bytes, fixed |
| Limit | 512 events waiting - 16 kB |
| Full | the oldest waiting event is dropped and counted |
| Drained | the file is removed and starts again |
| Grown to twice the limit | the waiting tail is copied into a fresh file |

The layout, little-endian:

```
 0  seq        4     14  flags     1     18  count     2
 4  boot_id    2     15  kind      1     20  value     4  float
 6  t_mono_s   4     16  subject   1     24  limit     4  float
10  t_wall     4     17  detail    1     28  reserved  3,  31  version
```

`subject` is the sensor, the alarm or the reset reason, depending on `kind`; `detail` is a knock's
axes or an alarm's sensor. `count` is a knock's held-back knocks or, for a boot, one bit per sensor
that answered. `flags` carries the time source in its low two bits and, for an alarm, whether it
was raised and its severity.

The read position is the number of records released from the start of the file. Losing power at
any point leaves either the old file with a position that sends events again - which the server
turns away by their identity - or the new one; never a position that skips an event.

**The limit drops the oldest, not the newest.** Of a week of events nobody could receive, the last
ones are the ones still worth acting on. A drop is never silent: the count since boot travels as
`dropped` in every following message, the rule [A-007](A-007-store-and-forward.md) set for
telemetry.

A week at sea with the boat slamming into waves is the case that fills it: one knock a minute
through the hold-off is 1 440 a day. Those are noise - A-009 already says the knock is about the
berth - and the telemetry's knock count keeps the record of them. What the limit protects is the
rest of the board.

## 6. On the server

Ingest subscribes to `boathub/+/events` with the same guarantees as for telemetry: a persistent
session, QoS 1, and an acknowledgement only after the rows are committed
([A-006](A-006-telemetry-storage.md) section 4).

| Column | |
|--------|--|
| `received_at`, `boat_id` | as for telemetry |
| `ts`, `time_valid`, `time_source` | the board's time of the event |
| `boot_id`, `seq` | the identity, unique per boat |
| `kind` | `boot`, `sensor_lost`, ... |
| `subject` | the sensor or the alarm the event is about, where it has one |
| `state` | `raised` or `cleared` for an alarm |
| `severity` | `alarm` or `warning` for an alarm |
| `payload` | the whole event as the board sent it |

`events` is an ordinary table, not a hypertable - a few rows a day do not need partitioning - so the
identity is a **unique index** on `(boat_id, boot_id, seq)` and a redelivery is turned away by
`ON CONFLICT DO NOTHING`. The telemetry needs a separate claim table only because a hypertable
cannot carry that index.

The columns beside `payload` are the ones a dashboard filters on. Everything else stays in
`payload`, so a new field from newer firmware is stored the day it is sent, without a migration.

### On the dashboard

- **Events**, a dashboard of its own: the alarms active now, and every event in the selected time
  range as a table, newest first.
- **Markers in the sensor panels.** Alarms, knocks, lost sensors and boots are drawn as annotations
  on the time axis of `BoatHub - sensors`, so a dip in a curve and the event that explains it are
  seen together.

An alarm counts as **active** when its most recent event is `raised` **and comes from the board's
latest boot**. A board that restarts forgets its alarms
([A-012](A-012-alarm-and-threshold-logic.md) section 6), and one whose condition still holds is
raised again once its hold has passed - so a raise from before the last boot is history, not an
alarm standing now. Events are ordered by `boot_id` and `seq` for this, not by time: they are the
board's own sequence, and a clock restored after a restart is only a lower bound.

## 7. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| No connection | uplink not connected | events wait on flash and drain when it returns, before the telemetry backlog |
| Broker never acknowledges | no PUBACK | the batch is sent again on the next connection; the server's unique index turns away the copy |
| Queue full | 512 waiting | oldest dropped, counted, `dropped` in every following message |
| Filesystem unavailable | mount failed at start | events are logged on the serial port and not sent; telemetry and measuring carry on |
| Torn write at the tail | file size not a multiple of 32 | the partial record is ignored and overwritten |
| Server does not know a field | not in the columns | kept in `payload` |
| Payload malformed | JSON parse fails | logged and acknowledged, as for telemetry - a payload that cannot be parsed now will not parse on redelivery |

Nothing in this path can stop a measurement: an event that cannot be stored is logged and dropped,
and the sensors never wait for it.

## 8. Safety

- **One way only.** The board publishes events; nothing it subscribes to can make it act. An
  acknowledgement exists only at the box, by the BOOT button - never from the server.
- An event is information. Nothing on the server acts on one by itself.

## 9. Verification

**On the bench**

- [ ] A restart publishes one `boot` event with the reset reason, the build and the sensors that
      answered
- [ ] Disconnecting the SHT31 publishes `sensor_lost` with `cabin` after about a minute, and
      reconnecting it publishes `sensor_back`
- [ ] A forced SHT31 heater cycle publishes neither
- [ ] A tap on the enclosure publishes `knock` with the boat's axis named; a second tap within the
      hold-off publishes nothing, and the next knock after it carries `held: 1`
- [ ] With the broker stopped, events are queued; started again, they arrive before the telemetry
      backlog, with their original `ts`
- [ ] Restarting the board while events wait: they survive the restart and are sent after it
- [ ] Publishing the same batch twice stores it once
- [ ] The queue's limit, shortened for the test, drops the oldest event and the next message carries
      `dropped`

**On the server**

- [ ] Each event appears in `events` within a second, with `subject`, `state` and `severity` filled
      where they apply
- [ ] The Events dashboard lists them, and a raised alarm shows as active until it is cleared
- [ ] The sensor panels show the markers at the right time

## 10. References

- [A-005-server-uplink.md](A-005-server-uplink.md) - topics, envelope and time
- [A-006-telemetry-storage.md](A-006-telemetry-storage.md) - identity and lossless ingest
- [A-007-store-and-forward.md](A-007-store-and-forward.md) - the same discipline for telemetry
- [A-009-imu-heel-and-motion.md](A-009-imu-heel-and-motion.md) - the knock and its hold-off
- [A-012-alarm-and-threshold-logic.md](A-012-alarm-and-threshold-logic.md) - the alarms on this path
