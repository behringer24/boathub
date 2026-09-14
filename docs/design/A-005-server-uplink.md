# A-005 - Server uplink

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | no |

## 1. Goal

Get a measurement out of the boat and into a database, end to end, before there is anything
interesting to measure. The first payload carries only what the board knows about itself, and every
sensor added later extends the same message on the same topic.

Proving the whole path early is the point. A working uplink turns every later sensor into a small
addition instead of a new class of problem.

**Out of scope:** getting onto the network at all, which is
[A-004](A-004-wifi-and-configuration-portal.md). Long-term storage, dashboards and the logbook.

## 2. Starting point

The server runs in Docker on the local network. At this stage board and broker sit on the **same
LAN**, so the connection is plain MQTT on port 1883.

**The finished system will not look like this.** Once the server is reachable from the marina, the
uplink moves to **TLS on 8883** and the board carries the CA certificate. The code is therefore
written so that the change is a swap of the client class plus a certificate - the topics, the
payload and the publish logic do not change.

**Telemetry is one-way.** The board publishes; it subscribes to nothing that can make it act. No
control path exists from the server, and none is added later - autopilot control lives on the local
on-board Wi-Fi only.

## 3. The broker

An `eclipse-mosquitto` container in [`server/`](../../server), published on port 1883.

| Item | Value |
|------|-------|
| Port | 1883 plain, 8883 reserved for the TLS step |
| Anonymous access | **denied** - Mosquitto 2.x refuses it by default and that default is kept |
| Authentication | username and password from a `passwd` file |
| Persistence | on, so retained messages survive a restart of the container |

The board's broker credentials live in NVS, entered on the configuration page, never in source.

Port 1883 is IANA-registered for MQTT and has nothing to do with 80 or 443. NTP additionally needs
outbound **UDP 123**.

## 4. Topics and payload

```
boathub/<boat-id>/telemetry     measurements, every pub_secs seconds
boathub/<boat-id>/status        "online" / "offline", retained, last will
boathub/<boat-id>/events        alarms and state changes, only when something happens
```

`status` is **retained** and carries `offline` as the last will, so a client connecting later
immediately learns whether the boat is reachable, without waiting for a heartbeat to time out.

**A restart produces a spurious `offline` / `online` pair, milliseconds wide.** The board reconnects
under the same client id, the broker evicts the session it has not yet noticed is dead - logging
`session taken over` - and publishing that session's will on the way out. The new connection then
publishes `online`.

This matters more than it looks. **An alarm that fires on `offline` would fire on every reboot**,
and an alarm that cries wolf gets muted within a week. Whatever raises the shore-power-loss alarm
has to require the boat to stay unreachable for a sustained period, not merely to have gone quiet
once. The genuine case is distinguishable in the broker log: a real outage disconnects with
`exceeded timeout` after the keepalive lapses, not with `session taken over`.

### First payload

```json
{
  "ts": "2026-09-14T13:05:00Z",
  "time_valid": true,
  "uptime_s": 1234,
  "heap_free": 370244,
  "rssi_dbm": -58,
  "reset_reason": "POWERON"
}
```

These are not filler. They are the four numbers that explain everything a growing system does
wrong: `uptime_s` exposes silent restarts, `heap_free` exposes a leak long before it crashes
anything, `rssi_dbm` separates a radio problem from a firmware problem, and `reset_reason`
distinguishes a watchdog bite from a brownout. They stay in the payload when the sensors arrive.

**Read `POWERON` as "EN went low or the supply was interrupted", not as "the supply failed".** The
RST button pulls the enable pin to ground, and so does opening a serial port on most hosts, because
the port asserts DTR/RTS as it opens. The chip cannot tell any of those from a real power loss. The
values that are actually diagnostic are the other ones - `BROWNOUT`, `TASK_WDT`, `PANIC` - and
those can be believed.

`reset_reason` is a property of the boot rather than of the sample, so it is redundant in every
message. It is here because a payload that stands on its own survives the loss of the first message
after a restart, which is exactly when a message is most likely to be lost. It belongs in a boot
event on the `events` topic once anything publishes there, and can leave the telemetry then.

Sensor fields are added to the same object as they come up, matching the schema in
[../ROADMAP.md](../ROADMAP.md). A field that has no sensor yet is **left out**, never sent as zero -
a missing key and a real measurement of zero must not look the same.

### Time

NTP over UDP 123, UTC, no local time anywhere in the payload. The timestamp is ISO 8601 with a `Z`.

**The first publish does not wait for NTP.** If the clock has not synced, the message goes out with
`time_valid: false` and no `ts`, and the server timestamps its arrival. Blocking the uplink on a
time server would mean a failure at the NTP server stops telemetry entirely, which is the wrong
trade for data that is already timestamped on receipt.

Once TLS is in use this becomes stricter: certificate validation fails on a wrong clock, so NTP
must succeed before the first connect. NTP runs over plain UDP and needs no TLS itself, so this is
an ordering requirement, not a circular one.

## 5. Publishing

Interval from `pub_secs` in NVS, default **10 s**. A heartbeat rather than a measurement rate -
nothing in the system changes fast enough to need more.

**Nothing in the publish path blocks.** Connection attempts are polled with the same backoff as the
station connection, and a broker that is down slows nothing else. If the connection is not up the
message is dropped rather than queued: telemetry is a heartbeat, and a stale reading delivered
minutes later is worse than none.

**PubSubClient's default buffer is 256 bytes** and a full payload will exceed that once the sensors
are in. Call `setBufferSize()` explicitly rather than discovering the limit as silent message loss.

## 6. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Broker unreachable | connect fails | retry with backoff, drop telemetry meanwhile, everything else keeps running |
| Broker rejects credentials | connect returns not-authorised | same backoff; the reason goes to the serial log and to `/status` on the portal |
| Station down | no address | nothing to do here, [A-004](A-004-wifi-and-configuration-portal.md) handles it |
| NTP never answers | no sync | keep publishing with `time_valid: false` |
| Payload outgrows the buffer | publish returns false | buffer sized up front; the failure is logged, not silent |
| Board loses power | keepalive expires | broker publishes the retained last will, server sees `offline` |

## 7. Verification

- [ ] `docker compose up -d` in `server/` brings the broker up and it stays up after a restart
- [ ] Anonymous connections are refused
- [ ] `mosquitto_sub` with valid credentials shows a message every 10 s
- [ ] `status` reads `online` while the board runs
- [ ] Powering the board down makes `status` go to `offline` once the keepalive expires
- [ ] A client connecting after that immediately sees the retained `offline`
- [ ] `ts` is plausible UTC, and `time_valid` is true once NTP has synced
- [ ] Stopping the broker does not restart the board and does not stop the local portal
- [ ] Restarting the broker reconnects the board without intervention

## 8. References

- [A-004-wifi-and-configuration-portal.md](A-004-wifi-and-configuration-portal.md) - the connection this uses
- [../ROADMAP.md](../ROADMAP.md) - full telemetry schema and later versions
- [Eclipse Mosquitto documentation](https://mosquitto.org/documentation/)
