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

The page is served over plain HTTP on the local access point. That is acceptable because the
access point itself is WPA2-protected and the page is not reachable from the station side.

### What the page must not do

No scan-and-pick list of surrounding networks in this version. A scan in AP+STA mode interrupts the
access point, which is exactly the behaviour the rest of the design works to avoid. Type the SSID.

## 5. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Station credentials wrong | association fails repeatedly | stay in `CONNECTING`, keep retrying, portal stays reachable to correct them |
| Marina access point gone | association lost | back to `CONNECTING` with backoff, no restart |
| Marina changes channel | clients on `BOOT-NETZ` drop | expected, clients reconnect on their own |
| NVS empty or corrupt | no SSID readable | `PORTAL` - the board is always configurable |
| Somebody sets an unusable AP password | - | shorter than 8 characters is rejected by the form; WPA2 requires 8 |

## 6. Verification

- [ ] With NVS cleared, `BOOT-NETZ` appears and the serial port prints the generated password
- [ ] `http://192.168.4.1` serves the form on a phone
- [ ] Saving credentials stores them and the board comes up associated after the restart
- [ ] Pulling the station network drops the board to `CONNECTING`, and `BOOT-NETZ` stays up
- [ ] Restoring the network reconnects without a power cycle
- [ ] A reflash keeps the configuration - NVS survives, because the partition layout is fixed
- [ ] Wrong password: the board keeps retrying and the portal is still reachable
- [ ] `GET /status` reports station address and RSSI

## 7. References

- [A-005-server-uplink.md](A-005-server-uplink.md) - what uses this connection
- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the bench environment
- [ESP-IDF Wi-Fi API - AP+STA channel behaviour](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html)
