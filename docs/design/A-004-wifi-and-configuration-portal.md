# A-004 - Wi-Fi operation and configuration portal

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | no |

## 1. Goal

Bring the BoatHub onto a Wi-Fi network and give it everything it needs to know - network, broker,
boat identity - without ever putting a credential into source code.

The board opens its own access point `BOOT-NETZ` and serves a configuration page on it. What you
enter there goes into NVS and survives both a restart and a reflash.

**Out of scope:** the telemetry itself and the broker, which are
[A-005](A-005-server-uplink.md). NAPT/NAT so clients reach the internet through the ESP - the
BoatHub is not a router.

## 2. Starting point

Two requirements pull in opposite directions and shape everything below.

- **No Wi-Fi or server passwords in source code.** Configuration lives in NVS/Preferences.
- **`BOOT-NETZ` is permanently available**, because the local UI and later the autopilot control
  hang off it. It cannot be a setup-only mode that disappears once the station connects.

So the board runs **AP and station at the same time**, and the first boot has to be usable with
nothing configured at all.

### One radio, one channel

In AP+STA mode the ESP32 has a single radio, so **the SoftAP is forced onto whatever channel the
station connects to**. When the marina access point changes channel - many do so automatically -
every client on `BOOT-NETZ` is disconnected.

This is normal behaviour and has to be designed for rather than fixed: clients must tolerate
reconnects, and no local UI may treat a dropped socket as anything but routine.

## 3. Behaviour

### States

| State | When | What runs |
|-------|------|-----------|
| `PORTAL` | no station credentials in NVS | SoftAP and configuration page only |
| `CONNECTING` | credentials present, station not up | SoftAP stays up, station retries with backoff |
| `ONLINE` | station associated and has an address | SoftAP and station both up |

There is no state in which the SoftAP is off. A failing station connection degrades the system to
`CONNECTING` and never takes the local network with it.

### Reconnect

The station retries with **exponential backoff from 1 s to 60 s**, and keeps retrying forever. A
marina outage lasting a night must not need a power cycle.

**Never call a blocking connect in the main loop.** The Wi-Fi state is polled, and every other part
of the firmware keeps running while the station is down. That is the same rule the sensors and the
watchdog follow.

### The access point password

`BOOT-NETZ` is **not open**. Its password defaults to `boathub-` plus the last three bytes of the
MAC address, which makes every board different without putting a secret into source. The default is
printed on the serial port at first boot, and can be changed on the configuration page.

## 4. Software

### Configuration in NVS

Preferences namespace `boathub`. Keys stay within the 15-character limit.

| Key | Meaning | Default |
|-----|---------|---------|
| `wifi_ssid` | station network | empty - this is what decides `PORTAL` |
| `wifi_pass` | station password | empty |
| `ap_pass` | `BOOT-NETZ` password | derived from the MAC |
| `boat_id` | identity in the MQTT topic | derived from the MAC |
| `mqtt_host` | broker address | empty |
| `mqtt_port` | broker port | 1883 |
| `mqtt_user` | broker user | empty |
| `mqtt_pass` | broker password | empty |
| `pub_secs` | publish interval in seconds | 10 |

### The configuration page

A plain synchronous `WebServer` on port 80 of the access point, plus a `DNSServer` answering every
query with the AP's own address so that a phone offers the page by itself. Browsing to
**`http://192.168.4.1`** always works, captive-portal detection or not.

| Route | Purpose |
|-------|---------|
| `GET /` | the form, pre-filled with everything except passwords |
| `POST /save` | validate, store in NVS, restart |
| `GET /status` | JSON: state, station address, RSSI, broker connection, uptime |

**Passwords are never sent back to the browser.** The form shows an empty field with a placeholder;
an empty field on save means "keep what is stored", so the page can be used to change the SSID
without retyping the password.

### Access point only

The portal asks for no credentials of its own, and it can change where telemetry goes and what the
`BOOT-NETZ` password is. **Every route therefore refuses requests that do not come from the access
point subnet**, comparing the peer address against `softAPIP()` masked with `softAPSubnetMask()`,
and answering 403 otherwise.

Without that the page would be reachable from the station side as well, because the web server
binds to every interface. In a marina that means anyone on the same network could point the boat at
their own broker or lock the owner out of the local network.

What remains is plain HTTP over a WPA2-protected access point, with physical proximity as the
access control. That is proportionate for a configuration page on a boat.

### What the page must not do

No scan-and-pick list of surrounding networks in this version. A scan in AP+STA mode interrupts the
access point, which is exactly the behaviour the rest of the design works to avoid. Type the SSID.

## 5. Local indication

The onboard WS2812 on GPIO48 shows the state at the box. On a boat that is worth more than the
dashboard: you walk past, look, and know - without a phone, without Wi-Fi, and without the server
being reachable, which is exactly the situation where you most want to know.

| Colour | Pattern | State |
|--------|---------|-------|
| blue | slow single blink | nothing configured - the portal is waiting for you |
| yellow | single blink, once a second | credentials known, not associated |
| yellow | double blink | Wi-Fi up, broker not answering |
| green | breathing, 3 s cycle | everything works, telemetry is flowing |
| red | twice a second | alarm |

Two "not finished yet" states share yellow because at a glance the distinction that matters is
blue / yellow / green. The pattern separates them once you look properly.

Green breathes rather than pulses: a second dark, a second fading up, a second fading down. Calm
reads as "running", where a pulse reads as "reporting". The ramp is squared, because perceived
brightness is roughly the square root of emitted light and a linear fade appears to rush the bright
end.

**Nothing blinks to save current.** A WS2812 draws about 1 mA just being powered, and the pattern
adds roughly 0.2 mA against 55-90 mA for the system - about two per cent, which is not a reason to
do anything. It blinks because **a steady LED only proves the supply is on, while a moving pattern
proves `loop()` is still running.** If the firmware hangs, the pattern freezes, and that is visible
from across the cabin. A static indicator could not tell "healthy" from "crashed with the light
left on".

Write the table on the inside of the enclosure lid, next to the wire colours.

### The BOOT button

GPIO0 also selects the boot mode while the chip is resetting, so "hold it during power-up" already
means something else. What follows applies only after boot.

| Press | Effect |
|-------|--------|
| short, under 1 s, while an alarm shows | acknowledge it |
| short, otherwise | publish a telemetry message immediately |
| held for 8 s | put the access point password back to its MAC-derived default and restart |

The short press is one idea rather than two: *I am here, and I am responding to what you are
showing me.* Standing at the box during installation, pressing and watching the message arrive at
the server is the fastest proof that the whole chain works.

Eight seconds is deliberately uncomfortable. Anything shorter eventually happens by accident while
feeling for the box in the dark. **The LED blinks magenta from one second in**, so the press is
visibly building rather than a guess, and goes solid white the moment it fires.

Resetting the access point password is the only lockout recovery this design needs: everything else
stays reachable, because the access point is never switched off. Wi-Fi and broker credentials are
deliberately left alone - there is no factory reset on a button that somebody might lean on inside
a locker.

## 6. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Station credentials wrong | association fails repeatedly | stay in `CONNECTING`, keep retrying, portal stays reachable to correct them |
| Marina access point gone | association lost | back to `CONNECTING` with backoff, no restart |
| Marina changes channel | clients on `BOOT-NETZ` drop | expected, clients reconnect on their own |
| NVS empty or corrupt | no SSID readable | `PORTAL` - the board is always configurable |
| Somebody sets an unusable AP password | - | shorter than 8 characters is rejected by the form; WPA2 requires 8 |

## 7. Verification

- [ ] With NVS cleared, `BOOT-NETZ` appears and the serial port prints the generated password
- [ ] `http://192.168.4.1` serves the form on a phone
- [ ] Saving credentials stores them and the board comes up associated after the restart
- [ ] Pulling the station network drops the board to `CONNECTING`, and `BOOT-NETZ` stays up
- [ ] Restoring the network reconnects without a power cycle
- [ ] A reflash keeps the configuration - NVS survives, because the partition layout is fixed
- [ ] Wrong password: the board keeps retrying and the portal is still reachable
- [ ] `GET /status` reports station address and RSSI
- [ ] The LED is blue with nothing configured, yellow while connecting and green once telemetry flows
- [ ] The same page requested from the station side is refused with 403, and the refusal is logged

## 8. References

- [A-005-server-uplink.md](A-005-server-uplink.md) - what uses this connection
- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the bench environment
- [ESP-IDF Wi-Fi API - AP+STA channel behaviour](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html)
