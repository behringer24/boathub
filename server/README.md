# Server

The server side of the BoatHub: a broker the boat publishes to, a database that keeps what it sent,
and a dashboard to look at it.

| Service | Does | Reachable on |
|---------|------|--------------|
| `mosquitto` | MQTT broker | 1883 |
| `db` | PostgreSQL with TimescaleDB and PostGIS | compose network only |
| `ingest` | subscribes and writes rows | - |
| `grafana` | dashboard | 3000 |

The database is deliberately **not** published to the host. Only ingest and Grafana need it and both
sit on the compose network.

Design: [../docs/design/A-005-server-uplink.md](../docs/design/A-005-server-uplink.md) and
[../docs/design/A-006-telemetry-storage.md](../docs/design/A-006-telemetry-storage.md).

## Setup

**Every command below runs in this directory**, not in the repository root - the compose file lives
here. Starting from the root gives "no configuration file provided".

```
cd server
```

### 1. Create the broker user

The broker denies anonymous access, so the password file has to exist **before the first start**.
Run this once and pick your own password when prompted:

```
docker run --rm -it --user 1883:1883 -v "${PWD}/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 mosquitto_passwd -c /mosquitto/config/passwd boathub
```

`boathub` is the username. The file holds only a hash, but it is still excluded from git.

**`--user 1883:1883` is not optional.** `mosquitto_passwd` creates the file with mode 0600 owned by
whoever ran it. The broker itself drops to the unprivileged `mosquitto` user, uid 1883, and cannot
read a file owned by root - it starts, fails with `Unable to open pwfile`, and restarts in a loop
that reads like a broken image rather than a permissions problem. Creating the file as 1883 in the
first place avoids it.

If a file created the wrong way already exists, hand it over instead of retyping the password:

```
docker run --rm -v "${PWD}/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 chown 1883:1883 /mosquitto/config/passwd
```

### 2. Fill in the passwords

```
cp .env.example .env
```

Four values, none of which belong in git:

| Variable | What it is |
|----------|------------|
| `POSTGRES_PASSWORD` | pick anything; only the containers ever use it |
| `MQTT_USER` | the username from step 1, `boathub` unless you changed it |
| `MQTT_PASSWORD` | **the same password you typed in step 1.** The broker stores only a hash, so it cannot be recovered - if you have forgotten it, redo step 1 |
| `GRAFANA_PASSWORD` | the `admin` password for the dashboard |

### 3. Start everything

```
docker compose up -d
```

```
docker compose logs -f
```

A healthy start ends with mosquitto `running`, the database `ready to accept connections`, and
ingest `subscribed to boathub/+/telemetry`. If the log repeats instead, something is restarting -
read the last error before the repetition begins.

### 4. Point the board at this machine

The board connects over the network, so it needs the **LAN address of this machine** - not
`localhost`, which on the board means the board itself.

```
ipconfig
```

Take the IPv4 address of the adapter on your home network and enter it on the board's configuration
page, with port 1883 and the credentials from step 1.

Two things worth doing once: give this machine a fixed DHCP lease in the router, or the board will
be pointing at nothing after the next router restart. And if nothing arrives, check the Windows
firewall allows inbound TCP 1883 for Docker.

## Looking at the data

Dashboard at **http://localhost:3000**, user `admin` with the password from `.env`. The data source
and the heartbeat dashboard are provisioned - there is nothing to set up by hand.

Straight into the database:

```
docker exec -it boathub-db psql -U boathub -d boathub
```

```sql
-- Most recent aggregates. n is how many measurements went into each window.
SELECT received_at, boat_id, n, window_s, uptime_s, heap_free, rssi_dbm
  FROM telemetry ORDER BY received_at DESC LIMIT 10;

-- Sensors: the bare column is the mean, _min/_max the extremes in that window.
SELECT received_at, battery_v_min, battery_v, battery_v_max
  FROM telemetry WHERE battery_v IS NOT NULL ORDER BY received_at DESC LIMIT 10;

-- Windows that were not full: the board missed measurements.
SELECT received_at, n, window_s FROM telemetry
  WHERE n IS NOT NULL AND n < 30 ORDER BY received_at DESC LIMIT 20;

SELECT * FROM boat_status ORDER BY received_at DESC LIMIT 10;
```

Watching the raw MQTT traffic:

```
docker exec -it boathub-mqtt mosquitto_sub -h localhost -p 1883 -u boathub -P '<password>' -t 'boathub/#' -v
```

## Topics

```
boathub/<boat-id>/telemetry     one aggregate every 5 min by default
boathub/<boat-id>/status        "online" / "offline", retained
boathub/<boat-id>/events        alarms and state changes
```

The board is the only publisher, and ingest only ever subscribes. No control path from the server
exists or is planned.

A telemetry message covers a **window**, not an instant: the board measures every 10 s and
publishes every 5 min. The bare field is the mean, `_min`/`_max` the extremes, `n` the number of
measurements behind it. A press of the BOOT button sends a spot reading - same shape, `n: 1`, no
extremes.

Delivery is lossless on purpose, because the board will delete buffered measurements once they are
acknowledged: ingest uses a **persistent session**, subscribes at **QoS 1**, and acknowledges only
**after the row is committed**. Stopping ingest for a redeploy costs no data - the broker holds its
messages until it comes back.

## Backups

**The Docker volume is not a backup.** While this is heartbeats, losing it costs nothing. Once the
logbook is in here it is irreplaceable, because a trip cannot be measured again:

```
docker exec boathub-db pg_dump -U boathub boathub | gzip > boathub-$(date +%F).sql.gz
```

Put it somewhere that is not this machine, and restore it into an empty database once so you know
the file is good.

## Changing the schema

`db/init/01-schema.sql` runs **only** when the data directory is created. Editing it does nothing
to a database that already exists. While the contents are still test data, the simplest way to pick
up a schema change is to throw the volume away:

```
docker compose down
docker volume rm server_db-data
docker compose up -d
```

That deletes every measurement. Once there is anything worth keeping - and certainly once the
logbook exists - this stops being acceptable and the change needs a migration instead.

## Not yet, but planned

TLS on 8883 once the server is reachable from outside the home network. Until then this belongs on
a trusted LAN only: on port 1883 the credentials cross the network in the clear.

Device-side buffering (**A-007**): the board stores every aggregate in flash and drains the buffer
when it finds a connection, so a passage without marina Wi-Fi is recorded rather than lost. The
server side is already prepared for it - that is what the delivery guarantees above are for.
