# A-012 - Alarm and threshold logic

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | no |

## 1. Goal

Turn measurements into the three conditions an unattended boat must not sit in unnoticed, decided on
the board, shown at the box and sent as events:

| Alarm | Severity | Raised when | Cleared when |
|-------|----------|-------------|--------------|
| `frost` | alarm | any of cabin, engine, bilge or battery bay below **3 °C** for **10 min** | all of them at or above **4 °C** for 10 min |
| `fridge_warm` | alarm | the fridge, once armed, above **10 °C** for **30 min** | the fridge back below 6 °C |
| `humidity` | warning | the cabin at or above **85 %RH** for **6 h** | the cabin below **80 %RH** for 30 min |

Every number is a setting.

**Out of scope:**

- Notification. An alarm is an event on the server and a red LED at the box; which of them reaches
  a phone, and how, is decided once their record exists.
- The alarms that need the server's history rather than the board's present: the pressure tendency
  ([A-010](A-010-bme680-barometer-and-battery-bay.md) section 7) and a list or trim that keeps
  drifting ([A-009](A-009-imu-heel-and-motion.md) section 6). They belong with notification.
- Shore power and the battery thresholds, which need the battery state machine of
  [B-001](B-001-power-supply.md).
- The knock as a notification, which waits for its threshold to be set from recorded counts
  (A-009 section 7).

## 2. Starting point

The measurements exist: the SHT31 in the cabin ([A-008](A-008-sht31-cabin-climate.md)), the three
DS18B20 probes ([A-003](A-003-ds18b20-temperature-sensors.md)), the BME680 in the battery bay
([A-010](A-010-bme680-barometer-and-battery-bay.md)). Each driver offers its latest valid reading
and reports nothing once that reading is stale.

So does the indication. The LED shows red, twice a second, for an alarm, and a short press on the
BOOT button acknowledges it ([A-004](A-004-wifi-and-configuration-portal.md) section 5). The events
path of [A-011](A-011-events.md) carries what happens to the server.

### Why on the board

**The board is where the measurement is, and the server is not always reachable.** At the berth the
marina Wi-Fi drops out; at sea there is none. An alarm decided on the server would be decided late
or not at all, while the LED at the box works with every network down. The event goes to the
server whenever it can, with its own timestamp.

What the server decides is what needs its history - three hours of pressure, days of heel - and
that is section 1's out-of-scope list.

## 3. How a condition is judged

The alarms are evaluated **once per measurement interval**, every 10 s, on the latest reading of
each sensor.

**Nothing raises on a single reading.** Each condition has to **hold** - stay true on every reading
- for its time before it counts, and the reverse condition has to hold before it clears. A cold
draught past the cabin sensor, a fridge door left open while it is loaded, a shower's steam: each
crosses a threshold for minutes and is not an alarm.

**Clearing has its own threshold.** Frost clears at 4 °C, not at 3, and humidity at 80 %, not
85. A temperature that wanders around one figure would otherwise raise and clear every few minutes,
and an alarm that does that is muted within a week.

**A missing reading decides nothing.** A sensor that is lost, or the SHT31 during its heater cycle,
neither starts nor breaks a run - a gap does not prove the cabin warmed up. A gap longer than the
hold itself does end a run, so a condition cannot be completed by readings hours apart. A lost
sensor is reported in its own right ([A-011](A-011-events.md) section 3).

## 4. The three alarms

### Frost

**Sensors:** cabin (SHT31), engine and bilge (DS18B20), battery bay (BME680). The **coldest** of those
with a valid reading is what is judged. The fridge is left out, for obvious reasons.

**3 °C, not 0 °C.** Water freezes at zero; the point of the alarm is to leave time to act before it
does - a heater switched on, the engine's raw-water circuit drained. The engine probe is the one
that matters most: raw water in the cooling circuit is what a frost night breaks. 3 °C at the
coldest sensor, held for ten minutes, is a night that is going to get colder.

The event names the sensor that was coldest when the alarm was raised, and its reading.

### The fridge, armed by being cold

A fridge alarm in a boat has one awkward property: **switching the fridge off is normal.** Nobody
leaves it running for an empty boat, and a plain "above 10 °C" alarm would go off every time the
crew leaves.

So the alarm **arms itself**:

```
              fridge below 6 °C for 60 min
  disarmed  ────────────────────────────►  armed
     ▲                                       │
     │ below 6 °C:                           │ above 10 °C for 30 min
     │ cleared, and arming starts again      ▼
     └──────────────────────────────────  raised
```

- **Armed** only once the fridge has been below 6 °C for an hour. A fridge that was never switched
  on can never raise it.
- **Raised** when an armed fridge stays above 10 °C for half an hour - a compressor that stopped, a
  fridge that lost its supply, a door left open. On the way it **disarms**: one alarm, not one per
  interval.
- **Switching it off deliberately raises it once.** That is the price of not needing a switch, and
  one press of the BOOT button acknowledges it. It does not come back until the fridge has been
  cold again and armed itself afresh.
- **Cleared** when the fridge is back below 6 °C, which also starts the hour that arms it again.

Thirty minutes because a compressor cycles and a door gets opened: a fridge loaded with warm
shopping can sit above 10 °C for a quarter of an hour without anything being wrong.

### Humidity

**A warning, not an alarm.** Mould and condensation are a matter of hours and days, never minutes,
and nobody needs to come to the boat in the night for them. A warning is an event and an entry on
the dashboard; it does not light the LED.

The cabin's relative humidity from the SHT31, at or above 85 % for six hours. Below 80 % for half an
hour clears it.

**The SHT31's heater is not this alarm.** The heater acts at 95 % to dry a sensor that condensation
has stuck at 100 % ([A-008](A-008-sht31-cabin-climate.md) section 4); this warns about the cabin's
air long before that. The heater cycle's missing readings do not interrupt the six hours - section
3.

## 5. At the box and on the wire

### The LED and the button

A raised **alarm** - not a warning - turns the LED red, twice a second, over every other state. It
stays red while any raised alarm is **unacknowledged**.

A short press on the BOOT button acknowledges every alarm raised at that moment: the LED returns to
its normal state and an `ack` event goes out. The alarms themselves stay raised until their
condition clears - an acknowledgement says that somebody has seen it, not that it is over. An alarm
raised after the press lights the LED again.

The acknowledgement exists **only at the box**. Nothing from the server can silence or clear an
alarm; that would be a control path from the internet, and there is none.

### The events

Each raise and each clear is one `alarm` event on the path of [A-011](A-011-events.md):

```json
{
  "kind": "alarm",
  "alarm": "frost",
  "state": "raised",
  "severity": "alarm",
  "sensor": "engine",
  "value": 2.8,
  "limit": 3.0
}
```

with the envelope every event carries. `value` is the reading that decided it, `limit` the threshold
it was judged against: 3.0 when frost is raised, 4.0 when it clears. `sensor` is the sensor the
reading came from - for `fridge_warm` always `fridge`, for `humidity` always `cabin`.

The portal shows the alarms raised now in its status line, and `/status` lists them.

## 6. Settings

In the `boathub` namespace and on the configuration portal under **Alarms**. Each alarm can be
switched off - a boat with no fridge probe, or one laid up dry where frost is expected, should not
have to live with an alarm it cannot use.

| Key | Meaning | Default |
|-----|---------|---------|
| `al_frost` | frost alarm on | on |
| `frost_c10` | frost below, in tenths of a degree | 30 |
| `frost_min` | for minutes; also the time to clear | 10 |
| `al_fridge` | fridge alarm on | on |
| `fr_arm_c10` | arms below, in tenths of a degree; also clears below | 60 |
| `fr_arm_min` | for minutes | 60 |
| `fr_warm_c10` | raised above, in tenths of a degree | 100 |
| `fr_warm_min` | for minutes | 30 |
| `al_rh` | humidity warning on | on |
| `rh_warn` | at or above, %RH | 85 |
| `rh_clear` | clears below, %RH | 80 |
| `rh_hours` | for hours | 6 |

Constants rather than settings: frost clears 1 K above its threshold, and humidity clears after 30
minutes below its lower threshold. The portal refuses settings that contradict each other - a
fridge armed above the temperature that raises it, humidity that clears above where it warns.

**A restart forgets the alarms.** They are not kept in NVS: a board coming back up re-reads every
sensor, and a condition that still holds raises its alarm again once its hold has passed - minutes
for frost, an hour plus half an hour for an armed fridge, six hours for humidity. Every hold starts
over, which means a restart can delay an alarm but never invent one. Saving a setting restarts the
board like every other setting.

## 7. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| A sensor is lost | no valid reading | it drops out of the judgement; a run ends only if the gap outlasts the hold. The loss itself is a `sensor_lost` event |
| Every frost sensor lost | no valid reading at all | frost cannot be judged, and is neither raised nor cleared. The `sensor_lost` events say why |
| The fridge probe lost while armed | no valid reading | stays armed; a warm fridge found on its return raises after the hold |
| Fridge switched off on purpose | it warms up | raised once, acknowledged at the box, then disarmed until it has been cold again |
| A reading wanders around a threshold | - | the clear threshold and the hold keep it from flapping |
| Board restarts with an alarm raised | boot | forgotten; raised again after its hold if the condition still holds |
| No connection | uplink down | the LED shows it at the box; the events wait on flash ([A-011](A-011-events.md) section 5) |

## 8. Safety

- **Nothing here acts on the boat.** An alarm lights an LED and sends an event. No relay, no
  heater, no pump is switched.
- **No path from the server.** An alarm is acknowledged only by the BOOT button at the box.
- An alarm that cannot be judged says so through the sensor events rather than staying quiet as if
  everything were fine.

## 9. Verification

**On the bench** - the holds shortened to a minute in the portal, so a test takes minutes rather
than hours:

- [ ] A probe in iced water raises `frost` after the hold, naming that probe, with the LED red
- [ ] Taking it out clears `frost` only once it is above 4 °C for the hold
- [ ] A probe that hovers at 3 °C raises once and does not flap
- [ ] A short press acknowledges: the LED returns to normal, an `ack` event goes out, the alarm
      stays raised
- [ ] A second alarm raised after the press lights the LED again
- [ ] The fridge probe at room temperature raises nothing however long it waits
- [ ] The fridge probe cooled below 6 °C for the arming time, then warmed above 10 °C, raises
      `fridge_warm` once and not again while it stays warm
- [ ] Cooled again, it clears, and raises again only after it has re-armed
- [ ] Humidity held above the threshold - a damp cloth over the SHT31 - raises the warning, which
      does not light the LED; dry air clears it after the clear hold
- [ ] Disconnecting the only cold sensor while frost is raised leaves frost raised, and its
      `sensor_lost` event appears
- [ ] An alarm switched off in the portal is never raised
- [ ] Restarting with frost raised: the alarm is gone after the boot and comes back after the hold

**In the boat**

- [ ] The portal's status line and the Events dashboard agree on what is raised
- [ ] The first autumn nights: `frost` raised on the nights the engine bay gets near freezing, and
      on none other

## 10. References

- [A-004-wifi-and-configuration-portal.md](A-004-wifi-and-configuration-portal.md) - the LED and the
  BOOT button
- [A-008-sht31-cabin-climate.md](A-008-sht31-cabin-climate.md) - cabin humidity and the heater cycle
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the probes
- [A-010-bme680-barometer-and-battery-bay.md](A-010-bme680-barometer-and-battery-bay.md) - the bay
  temperature
- [A-011-events.md](A-011-events.md) - the path the alarms take to the server
- [B-001-power-supply.md](B-001-power-supply.md) - the battery and shore-power alarms
