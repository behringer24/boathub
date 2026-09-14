# Server

The server side of the BoatHub. At this stage it is one container: an MQTT broker the board
publishes telemetry to. Storage, dashboards and the logbook come later.

Design: [../docs/design/A-005-server-uplink.md](../docs/design/A-005-server-uplink.md).

## Setup

### 1. Create the broker user

The broker denies anonymous access, so the password file has to exist before the first start. Run
this once, from this directory, and pick your own password when prompted:

```
docker run --rm -it -v "${PWD}/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 mosquitto_passwd -c /mosquitto/config/passwd boathub
```

`boathub` is the username. The file it writes holds only a hash, but it is still excluded from git.

### 2. Start the broker

```
docker compose up -d
```

```
docker compose logs -f
```

### 3. Find the address the board has to use

The board connects over the network, so it needs the **LAN address of this machine** - not
`localhost`, which on the board means the board itself.

```
ipconfig
```

Take the IPv4 address of the adapter that carries your Wi-Fi, and enter it on the board's
configuration page along with port 1883 and the credentials from step 1.

If nothing arrives, the Windows firewall is the first suspect: inbound TCP 1883 has to be allowed
for Docker.

## Watching what arrives

```
docker exec -it boathub-mqtt mosquitto_sub -h localhost -p 1883 -u boathub -P '<password>' -t 'boathub/#' -v
```

`-v` prints the topic alongside the payload, which is what you want when more than one topic is
live. Expect a `telemetry` message every ten seconds and a retained `status` of `online`.

Powering the board down makes `status` turn to `offline` once the keepalive expires - that is the
last will, published by the broker rather than by the board.

## Topics

```
boathub/<boat-id>/telemetry     measurements, every 10 s by default
boathub/<boat-id>/status        "online" / "offline", retained
boathub/<boat-id>/events        alarms and state changes
```

The board is the only publisher. It subscribes to nothing that can make it act, and no control path
from the server exists or is planned.

## Not yet, but planned

TLS on 8883 once the server is reachable from outside the home network. Until then this setup
belongs on a trusted LAN only: on port 1883 the credentials cross the network in the clear.
