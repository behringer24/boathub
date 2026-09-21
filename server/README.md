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
docker run --rm -it -v "${PWD}/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 mosquitto_passwd -c /mosquitto/config/passwd boathub
chown 1883:1883 mosquitto/config/passwd
```

`boathub` is the username. The file holds only a hash, but it is still excluded from git.

**The second command is not optional.** `mosquitto_passwd` creates the file owned by whoever ran it,
which here is root. The broker drops to the unprivileged `mosquitto` user, uid 1883, and cannot read
a root-owned file: it starts, fails with `Unable to open pwfile`, and restarts in a loop that reads
like a broken image rather than a permissions problem.

Running the container as `--user 1883:1883` in the first place looks tidier and only works where the
directory itself is writable by that uid. On a host where the repository was checked out as root it
is not, and the attempt fails before it can ask for a password:

```
Error: Unable to open file /mosquitto/config/passwd for writing. Permission denied.
```

So: create it as whoever can, then hand it over. What matters is who owns the file at the end, not
who made it.

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
to a database that already exists, so every schema change is written twice: into `01-schema.sql`
for databases yet to be created, and as a numbered file in `db/migrations/` for the one that is
already running.

```
docker exec -i boathub-db psql -U boathub -d boathub < db/migrations/001-record-identity.sql
```

Migrations are written to be safe to run twice - `ADD COLUMN IF NOT EXISTS`, `CREATE TABLE IF NOT
EXISTS` - because nothing records which of them a given database has had. At this number of
changes that is cheaper than a migration table, and it means running the whole directory in order
is always a valid thing to do.

Throwing the volume away is the other option while the contents are still test data:

```
docker compose down
docker volume rm server_db-data
docker compose up -d
```

That deletes every measurement, and stops being acceptable the moment there is anything worth
keeping.

## TLS on 8883

The boat reaches the server over the open internet, across marina Wi-Fi and phone hotspots that
nobody controls. On 1883 the username and password cross that in the clear, so anything published
beyond a trusted LAN listens on **8883** instead.

### The certificate comes from the host

Whatever already manages Let's Encrypt on this machine keeps issuing and renewing; the broker only
borrows the result.

```
sudo server/deploy/install-certs.sh boathub.example.com
```

That copies `fullchain.pem` and `privkey.pem` into `server/mosquitto/certs/`, gives them to the
user the broker runs as, and reloads the broker.

**Why a copy and not a mount.** `privkey.pem` belongs to root and is readable by nobody else, while
the broker runs unprivileged inside its container - mounting `/etc/letsencrypt` read-only does not
change that, it only makes the refusal read-only too. The alternatives are running the broker as
root or loosening the permissions on every key the host holds. A copy owned by the broker's own
user is the smaller concession.

Renewal needs the same command, which certbot will run itself:

```
# /etc/letsencrypt/renewal-hooks/deploy/boathub.sh
#!/bin/sh
exec /srv/boathub/server/deploy/install-certs.sh boathub.example.com
```

Mosquitto re-reads its certificate on `SIGHUP`, which the script sends, so a renewal costs no
downtime and drops no session.

### Grafana in front of it

The certificate has to be asked for by something, and with `nginx-proxy` plus
`acme-companion` that something is a container carrying `VIRTUAL_HOST` and
`LETSENCRYPT_HOST`. The broker is not an HTTP service and cannot be one, so
Grafana takes the name - which it should anyway: on port 3000 its login
password crosses the network in the clear.

```
docker compose -f docker-compose.yml -f docker-compose.proxy.yml up -d
```

An override rather than part of the base file, because the base stack has to
come up on a machine that has no reverse proxy at all, and because the network
and domain belong to your own infrastructure rather than in this repository.
Fill `BOATHUB_DOMAIN`, `LETSENCRYPT_EMAIL` and `PROXY_NETWORK` into `.env`
first; the file refuses to render without them rather than guessing.

Grafana then answers on 443 through the proxy, port 3000 only from the machine
itself, and `install-certs.sh` finds the certificate in the proxy's own volume.

### Then switch the listener on

The TLS block in `mosquitto/config/mosquitto.conf` is commented out on purpose: mosquitto refuses
to start when a certificate file named in its configuration is missing, and a broker that will not
start is a worse first experience than one without TLS. Uncomment it once the files are in place,
and restart.

### Close 1883 from outside

On a server, put this in `.env`:

```
MQTT_BIND=127.0.0.1
```

The plain listener then answers only on the machine itself. Everything inside the stack - ingest
above all - reaches the broker over Docker's own network, where the traffic never leaves the host,
so nothing inside needs TLS and nothing outside gets the plain port.

### What the board needs

The board validates the server's certificate against a root it carries, and that check needs a
clock: a wrong date makes a valid certificate look expired or not yet valid. It therefore does not
attempt a TLS connection before NTP has answered - see [A-005](../docs/design/A-005-server-uplink.md).

8883 is also the port most likely to be blocked by exactly the networks a boat uses. If it turns
out to be, MQTT over WebSockets on 443 is the fallback, and it is a change on the board rather than
here.

## Not yet, but planned


Device-side buffering (**A-007**): the board stores every aggregate in flash and drains the buffer
when it finds a connection, so a passage without marina Wi-Fi is recorded rather than lost. The
server side is already prepared for it - that is what the delivery guarantees above are for.
