# A-006 - Telemetry storage and dashboard

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | no |

## 1. Goal

Keep what the boat sends, and make it visible. Until now the broker forwards messages and they are
gone; a monitor that cannot show last week's temperatures is not a monitor.

**Out of scope:** the uplink itself ([A-005](A-005-server-uplink.md)), alarm notification, and the
track logbook - which this schema has to leave room for, but which is a later version.

## 2. Starting point

### How much data this actually is

A row is an aggregate over a 5 min window, not a single reading - see
[A-005](A-005-server-uplink.md). That is **288 rows a day, about 105 000 a year.** With min/max
alongside each mean that is roughly thirty columns, or about 200 bytes a row:

| | |
|---|---|
| Raw, per year | ~21 MB |
| With column-store compression | a few MB |

Against any modern disk that is nothing. **So full resolution is kept indefinitely.** The usual
question - how long to retain, when to downsample - simply does not arise at this scale, and not
having to answer it removes a whole class of decisions and of bugs.

**Aggregation is not for this table's benefit.** The server could store every 10 s reading without
noticing: that would be 390 MB a year raw, still nothing. The constraint is on the board, which
buffers these while it is at sea and has **6 MB of flash**. Thinning is the device's problem, and
this table is simply the shape that arrives. It follows that there is no continuous aggregate and
no retention policy here, and that adding one would be solving a problem nobody has.

### Why one database rather than two

A time-series database would handle the telemetry beautifully and the logbook not at all: trips,
positions and distances are relational and geospatial. Running two stores for one boat is the wrong
trade, so this is **PostgreSQL** with two extensions:

- **TimescaleDB** turns the telemetry table into a hypertable: automatic partitioning by time, and
  the column-store compression the figures above assume.
- **PostGIS** is not needed yet. It is in the image from the start so that the track logger does
  not require a migration of the whole database later.

## 3. Schema

### Which timestamp partitions the data

Every row carries two times, and the distinction matters:

| Column | Meaning |
|--------|---------|
| `received_at` | when the server took delivery. Always present, always set by the server |
| `ts` | what the board believed the time was. **Null until NTP has synced** |

The hypertable partitions on **`received_at`**, because a partitioning column cannot be null and
because the server's clock is the one that can be trusted. `ts` is kept alongside as the board's
own claim; once NTP is up the two differ by the network delay.

The track logger will not work this way. Its points carry GPS time and arrive in bulk hours later,
so it gets its own table partitioned on the point's own timestamp. That is exactly why this table
is called `telemetry` and not `measurements`.

### Mean, min and max in one row

Each sensor has three columns: the bare name carries the **mean** over the window, `_min` and
`_max` the extremes. A spot reading - `n = 1` - fills the bare column and leaves the extremes null,
so there is one row shape rather than two.

`n` and `window_s` describe the window itself. They are worth having: `n` consistently below 30
means the board is missing measurements, which is a fault that would otherwise be invisible behind
a perfectly plausible average.

`seatalk_online` is a boolean and is taken at window close - there is nothing to average.

### Missing is not zero

Every sensor column is nullable, and a field the board did not send stays **null**. A missing
reading and a real measurement of zero must never look alike - the difference between "the bilge
sensor is not fitted" and "the bilge is dry" is the whole point of the system.

### Tables

| Table | Holds |
|-------|-------|
| `telemetry` | one row per received message, hypertable on `received_at` |
| `boat_status` | online/offline transitions from the retained `status` topic |
| `events` | alarms and state changes, for the version that raises them |

`boat_status` is not decoration. The shore-power-loss alarm is about the boat going quiet, so a
history of when it was reachable is evidence, and it costs a handful of rows a week.

## 4. Ingest

A small service of our own, not a metrics collector. It subscribes to `boathub/+/telemetry` and
`boathub/+/status` and writes rows.

A configuration-driven collector would cover today's payload with no code at all. It would not
cover what comes next: the track logger needs acknowledged, resumable uploads with duplicate
detection, and that is application logic. Starting with a collector means replacing it, so the
service that grows is written now while it is still small.

Rules it follows:

- **Unknown fields are ignored, not rejected.** Firmware will send fields this server has not been
  taught yet, and telemetry must not stop because of it.
- **A malformed message is logged and dropped**, never fatal. One bad payload cannot stop ingest.
  It *is* acknowledged: a payload that will not parse now will not parse on redelivery either, and
  leaving it unacknowledged would block everything behind it.
- **Reconnects are its own business.** Broker restarts, network blips and a database that is still
  starting up are all normal; the service retries with backoff and never exits because of them.
- It is a subscriber, nothing more. It publishes nothing and can make no boat do anything.

### Delivery has to be lossless, and that is new

Once the board buffers measurements at sea it **deletes them when they are acknowledged**. The
acknowledgement it sees comes from the *broker*, not from this service. So a message the broker
accepts and this service never stores is a measurement gone for good, with nothing anywhere
reporting a problem.

Three settings close that gap:

| Setting | Without it |
|---------|-----------|
| **Clean session off**, stable client id | the broker discards everything published while ingest restarts |
| **QoS 1** subscriptions | delivery is fire-and-forget; the broker may drop it and nobody is told |
| **Manual acknowledgement**, after the row is committed | a database hiccup loses the row, because the message was acknowledged on arrival |

The earlier version of this document said the opposite - *"the broker holds nothing, so those
messages are lost by design"*. That was an acceptable trade while telemetry was a heartbeat on the
same LAN and the next one came in ten seconds. It stops being acceptable the moment a message
represents five minutes that cannot be measured again.

**A failed insert is deliberately left unacknowledged.** The broker redelivers it on the next
reconnect, and its inflight limit becomes backpressure - a database outage slows ingest down
instead of quietly draining measurements into the void. The broker's queue is sized for this:
`max_queued_messages` is raised well above the default so a backlog uploaded after a trip survives
ingest being down at that moment.

## 5. Dashboard

Grafana, reading the database directly - Postgres is a first-class data source, so nothing sits in
between.

Two dashboards, provisioned from files:

**`BoatHub - heartbeat`** shows what exists: uptime, free heap, signal strength, messages per hour,
and **samples per window**. That last one is the panel the aggregation makes necessary - `n` below
30 means the board is missing measurements, and without it that fault hides behind an average that
looks entirely reasonable.

**`BoatHub - sensors`** shows the measurements with the **mean as a line and the min/max range
shaded behind it**. That is the whole reason for keeping the extremes: on the fridge panel the band
*is* the compressor cycle, and on the battery panel it is the sag under load. Those panels stay
empty until the probes are wired, which is the point at which they start earning their place.

`allowUiUpdates` is on, so panels can be tried out in the browser. Note what that means: edits live
in the Grafana volume and are **not** written back to the JSON files, so a provisioning reload
discards them. Anything worth keeping has to be exported back into
`grafana/provisioning/dashboards/json/`.

## 6. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Database not up yet | connection refused | ingest retries with backoff and acknowledges nothing, so the broker holds the messages and redelivers them |
| Insert fails | `pool.Exec` returns an error | retried briefly, then left unacknowledged for redelivery. Never acknowledged-and-dropped |
| Ingest restarting | - | the broker queues for its persistent session; nothing is lost across a redeploy |
| Broker restarts | subscription drops | reconnect and resubscribe; `status` is retained, so the state is re-learned immediately |
| Malformed payload | JSON parse fails | log the raw message, drop it, carry on |
| Field the server does not know | absent from the schema | ignored; add the column when the sensor is real |
| Disk fills | writes fail | with tens of MB a year this is not a realistic failure, but ingest must log and keep retrying rather than exit |
| **Volume lost** | - | **everything is gone.** See below |

### A Docker volume is not a backup

While this is heartbeats, losing it costs nothing. Once the logbook is in here it is irreplaceable
- a trip is not re-measurable. Before that point the database needs a `pg_dump` onto a different
medium, and a restore that has actually been tried once.

## 7. Verification

- [ ] `docker compose up -d` brings broker, database, ingest and Grafana up, and they survive a restart
- [ ] A message published by the board appears as a row within a second
- [ ] **Stop ingest, publish several messages, start it again: every one of them is in the table.**
      This is the persistent session doing its job and is the check that matters most
- [ ] Stop the database, publish, restart it: the rows arrive once ingest reconnects, none are lost
- [ ] An aggregate fills `n`, `window_s` and the `_min`/`_max` columns; a BOOT-button message
      arrives with `n = 1` and null extremes
- [ ] `ts` is null on the first message after a cold start and filled once NTP has synced
- [ ] A sensor field that is not sent stays null rather than becoming 0
- [ ] Stopping the database does not kill ingest; it recovers on its own when the database returns
- [ ] A deliberately malformed payload is logged and does not stop ingest
- [ ] Powering the board down writes an `offline` row to `boat_status`
- [ ] Grafana shows the heartbeat without any manual data source setup
- [ ] `pg_dump` produces a file that restores into an empty database

## 8. References

- [A-005-server-uplink.md](A-005-server-uplink.md) - where the messages come from
- [../ROADMAP.md](../ROADMAP.md) - the telemetry schema as it grows
- [TimescaleDB documentation](https://docs.timescale.com/)
