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
boathub/<boat-id>/telemetry     one aggregate every pub_secs seconds
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

### The message is an aggregate

The board **measures every 10 s and publishes every 5 min**. A message therefore covers a window
rather than an instant:

| Field | Meaning |
|-------|---------|
| `<sensor>` | the **mean** over the window |
| `<sensor>_min`, `<sensor>_max` | the extremes within it |
| `window_s` | how long the window actually covered |
| `n` | how many measurements went into it - 30 for a full window |

**A mean alone would hide what matters.** The fridge compressor cycles, the bilge pump starts, the
battery sags under both: those are excursions inside a window, invisible in an average and exactly
what you want to see. Keeping min and max costs two numbers and preserves them.

**A spot reading is the same shape with `n: 1` and no extremes** - the bare field carries the
reading. That is what the BOOT button produces and what the first message after a restart looks
like. There is deliberately no second message format.

Three rates, deliberately independent:

| | Rate | Why |
|---|---|---|
| Measuring | 10 s | alarms have to react, and [B-001](B-001-power-supply.md)'s battery state machine decides on a median over minutes |
| Publishing | 5 min | nothing here changes faster, and these have to be buffered at sea |
| Alarms | immediate, on `events` | a rising bilge level waits for no interval |

That separation is what lets the bilge be sampled often without storing it often.

### First payload

```json
{
  "ts": "2026-09-14T13:05:00Z",
  "time_valid": true,
  "window_s": 300,
  "n": 30,
  "uptime_s": 1234,
  "heap_free": 370244,
  "rssi_dbm": -58,
  "reset_reason": "POWERON"
}
```

Diagnostics are **instantaneous at the moment the window closed**, not aggregated. An averaged
uptime would be meaningless.

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

Measurement interval `sample_secs`, publish interval `pub_secs`, both in NVS. Defaults **10 s** and
**300 s**. A window shorter than a sample would close before anything went into it, so the portal
clamps it.

**The BOOT button sends a spot reading and leaves the running window alone.** You press it to prove
the chain works while standing at the box, not to cut a measurement short. The first message after
connecting is the same thing, so a working link shows itself at once instead of after five minutes.

**Nothing in the publish path blocks.** Connection attempts are polled with the same backoff as the
station connection, and a broker that is down slows nothing else.

### An unsent message is buffered, not dropped

Telemetry is a **record**, not only a heartbeat. At the berth the distinction does not show: if a
message cannot go out now, the next one is along in five minutes. At sea it is the whole point -
there is no marina Wi-Fi at all, and Starlink only in phases on longer trips, so a dropped message
is part of a passage that cannot be measured again.

- every aggregate is written to the buffer in LittleFS, always
- the uplink drains the buffer whenever a connection exists
- "live" is simply the case where the buffer is empty and the aggregate goes straight out

One code path, and being offline is not a mode. The device side - buffer format, ring buffer
behaviour, batching, resumable drain - is **[A-007](A-007-store-and-forward.md)**. The server side
is ready for it: see the delivery guarantees in [A-006](A-006-telemetry-storage.md).

A **stale reading still has to be recognisable as stale**. Every record carries its own `ts` and
the server stores that alongside its own `received_at`, so a backfilled window never masquerades as
current.

**Check the payload against its buffer before serialising into it.** `measureJson` gives the
length the document wants; `serializeJson` into something shorter truncates silently and publishes
invalid JSON, which looks like a healthy system until somebody reads the table. The firmware
refuses to publish in that case and says so on the serial port.

### A failed connect stalls the loop for three seconds

The MQTT client hands the TCP connect to a `WiFiClient`, whose default timeout
is three seconds, and there is no supported way to change it: the client
library's own `setTimeout` is the interval for retransmitting unacknowledged
packets and never reaches the transport.

So while the broker is unreachable, every retry stops `loop()` for three
seconds - no sampling, no configuration portal, and a status LED that holds
whatever it was showing. Measured against a successful connect on a local
network, which takes **34 to 52 ms**, the limit is almost entirely dead time.

**It is left alone deliberately.** With the buffer in [A-007](A-007-store-and-forward.md)
a window closing during that stall is stored rather than lost, so the only
measurable cost is one sample fewer in the window that contains a retry - and
that only while the link is already down. Shortening it would mean reaching
into the library's transport, and the setter takes whole seconds and also
changes the socket's receive timeout: an untested change to the path every
measurement travels, bought for two seconds in a state where nothing is being
lost.

Worth knowing rather than fixing, and worth remembering if the LED is ever
taken as evidence that the firmware has hung.

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
