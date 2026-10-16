# A-009 - IMU: heel, pitch and motion

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | yes |

## 1. Goal

Two questions from one sensor:

- **Under sail:** how far is she laying over, and how far did she lay over in the gust?
- **At the berth:** is it rough, how much is she rolling, is the way she lies changing - and
  **did something hit her?**

Reported in the aggregate as heel and pitch with their spread, `motion_g`, and a count of knocks;
plus an impact event.

**Out of scope:** heading, which comes from the SeaTalk compass in stage 2. Nothing here acts on the
boat - this is a sensor, and the autopilot is not connected to it.

## 2. A gyroscope is not what measures heel

Heel is the angle to **gravity**, so the instrument that measures it is an accelerometer, not a
gyroscope. A gyroscope measures rate of turn and has no idea which way is down; integrate it and it
drifts away within minutes.

But an accelerometer alone is not enough **on a boat**. It measures gravity *plus* whatever the
hull is doing. At a pontoon in still air those are the same thing. Under sail in a seaway they are
not, and the error is worst exactly at the peaks - which is the number worth having.

So: both, combined. The gyroscope carries the short term, where the accelerometer is polluted by
wave motion; the accelerometer carries the long term, where the gyroscope has drifted. That is a
**complementary filter**, and in its textbook form it runs once per angle:

```
angle = a * (angle + rate * dt) + (1 - a) * angle_from_gravity
```

`a` sets how long the accelerometer takes to pull the gyroscope back:
`tau = a * dt / (1 - a)`.

**For a boat, tau wants to be seconds, not tenths.** Wave-induced acceleration lasts whole seconds,
so a filter tuned for a handheld device would follow the waves rather than the heel. Three seconds
is the starting point. The firmware keeps tau itself in NVS rather than `a`, so that the setting
means the same at any sampling rate: it is the one number that will need tuning against real
motion, and it is the one a boat needs different from everybody else.

### One direction, not two angles

The per-angle form breaks down exactly when it matters. Heeled at 25 degrees and bearing away, the
boat turns about her mast, and the mast is no longer vertical: part of that turn lands on the axis
the filter calls pitch. Turning at 3 degrees a second, a per-angle filter settles nearly four
degrees bow-down with the bow perfectly level.

So the firmware does not filter angles. It tracks the **direction of "up" as the boat sees it**:
the gyroscope turns that direction by however far the boat turned, the accelerometer pulls it back
towards gravity over tau, and heel and pitch are read off the result at the end. The same filter
and the same tau, with no geometry left out - some forty lines, and no library.

### The gyroscope's own offset

A MEMS gyroscope reads a small rate when nothing is turning - up to about a degree per second on
this part. A complementary filter does not drift away on it, but it does settle off by it: the
offset times tau, **3 degrees at 1 dps and tau = 3 s**. That is the whole error budget several
times over.

So the filter learns the offset from the same disagreement that corrects the angle: where the
gyroscope keeps carrying the estimate away from gravity, the disagreement points along the offset.
Learning starts fast - over 4 x tau, the fastest that does not overshoot, for the first 10 x tau
after a start - and then slows to a minute, so that a seaway averages out of it rather than being
learned as offset. After a restart the readings settle within about half a minute, and nothing is
calibrated by hand.

It learns **no faster than an offset could explain**. An offset of 2 dps - twice what the
datasheet gives - holds the estimate at most 6 degrees from gravity at tau = 3 s. Anything larger
is motion: a knock, or the module turned over in the hand on the bench. Learning from all of it
winds the offset up, and the reading then sits several degrees off for a minute while it unwinds.
So a larger disagreement counts as 6 degrees and no more. It is capped rather than discarded,
because a seaway pushes the disagreement far out to both sides: dropping the far samples would
keep a lopsided remainder and learn an offset that is not there.

The part of the offset about the "up" axis itself cannot be seen this way, and does not matter:
turning about "up" does not move "up".

### No magnetometer

Nine-axis parts include one and it is not wanted here. Heading arrives over SeaTalk in stage 2, and
heel and pitch do not need it. On a boat a magnetometer also needs hard- and soft-iron calibration
while sitting near an engine, a battery bank and an anchor chain - a great deal of trouble for a
number already available elsewhere.

## 3. Choosing the part

| | For | Against |
|---|---|---|
| **LSM6DSOX** (ST), ~4 EUR | current production, 6-axis, 0x6A - no clash with anything on this bus, programmable wake/high-g interrupt, FIFO | the filter is yours to write |
| **MPU6050**, ~2 EUR | everywhere, endless examples | **0x68 clashes with the DS3231** if that is ever fitted (resolvable on AD0), ageing part, many fakes |
| **BNO055** (Bosch), ~25 EUR | fusion **on the chip** - read degrees, write no filter | its fusion is tuned for consumer motion and **cannot be retuned**, which is the one thing a boat needs to change |

**Recommendation: LSM6DSOX.** The complementary filter is some forty lines and it is not the
hard part of this feature - the zero reference and the mounting are. And a filter of one's own can
be given the seconds-long time constant that a boat needs, which the BNO055 cannot be told to do at
any price.

### Which board carries it

The chip costs a few euro; what matters is which breakout brings out **INT1**. Section 5's whole
argument - poll attitude slowly, let the chip watch for impacts - collapses without that pin, and
plenty of boards route only the four bus lines.

**The Arduino Modulino Movement** (ABX00101, ~13 EUR) brings it out. Beside the four-pin Qwiic bus
it carries a second **1x10 header with the sensor's own signals** - INT1, INT2, SDO/SA0, CS and the
SPI lines. Five conductors reach the main board: 3.3 V, ground, SDA and SCL from the bus header,
and INT1 from the other.

What it settles that a bare adapter does not:

- The sensor answers at **0x6A** as shipped, 0x6B on a solder jumper. CS and the address pin are
  already tied for I2C, so there is nothing at chip level left to get wrong.
- Decoupling is fitted and **the I2C pull-ups are not** - the pads are there, empty. That is the
  right way round: the SHT31 and the two ADS1115 already put some 3.3 kΩ on this bus, and a fourth
  set would drag it lower still.
- It runs at 3.3 V natively.

ST's own **STEVAL-MKI217V1** is the alternative at around 21 EUR: a DIL-24 adapter breaking out the
raw chip pins, with a LIS2MDL magnetometer that would stay unconnected. It works, but it costs
more, sells as clearance stock, and leaves CS and the address pin for you to tie.

## 4. Mounting - the opposite of the SHT31

The SHT31 had to leave the enclosure to measure cabin air. **This one wants to be inside it**,
bolted down. An IMU on a flying lead measures the lead.

Requirements:

- rigid to the hull structure, not to a panel that flexes
- roughly aligned with the boat's axes - within a few degrees, so that heel and pitch stay separate
  quantities rather than mixtures of each other
- once fixed, it must not move again; every reading afterwards is relative to where it was zeroed

The module has four 3.2 mm holes for M3 screws, 32 mm apart along its length and 16 mm across.

### The cable

The five signals run on a length of **Cat 5e or Cat 6 patch cable**: eight stranded cores in four
twisted pairs, thin enough for the module's pads, and an offcut is in most drawers. Two things to
check before cutting it:

- **Stranded, not solid.** Patch cable is stranded. Installation cable for walls has solid cores,
  and those break at the solder joint under vibration. A cable that hangs limp is stranded; one
  that keeps the shape it is bent into is not.
- **Copper, not CCA.** Cheap cables use copper-clad aluminium, printed `CCA` on the jacket, and it
  takes solder badly.

| Pair | Core | Signal | Module pad | J4 pole |
|------|------|--------|------------|---------|
| brown | brown | 3.3 V | 4-pin side | 1 |
| | white | GND | 4-pin side | 2 |
| orange | orange | SCL | 4-pin side | 4 |
| | white | GND | 4-pin side | 2 |
| blue | blue | SDA | 4-pin side | 3 |
| | white | GND | 4-pin side | 2 |
| green | green | INT1 | 10-pin side | 5 |
| | white | **cut off at both ends** | | |

The first three pairs are wired exactly like the sensor runs in [A-002](A-002-bench-setup-usb.md):
brown is the supply, and every white is ground. **In these cables the white partner of each pair is
plain white** - there is no white-blue or white-orange to tell one from another - so no white core
may carry anything but ground: a supply on one of them is indistinguishable from the grounds beside
it at the far end. INT1 takes the green, and its white partner is cut off flush with the jacket at
both ends; find it by following the twist before anything is stripped.

These are not the red, black, blue and yellow of the bench wiring, so write the table inside the
enclosure lid.

- **Solder the cores straight into the pads**, without a pin header. The cable is the permanent
  connection, and a plug-in contact is exactly what [B-002](B-002-main-board.md) keeps off this
  bus. Tie the cable to a mounting hole so the joints do not carry the movement.
- **Ferrules at J4: 0.25 mm² on brown, orange, blue and green, and one 0.5 mm² over the three
  whites twisted together.** A screw on bare thin strands flattens them and the contact loosens
  over time. Tinning the strands instead does not help: solder creeps under the screw. At the
  module the three whites go twisted together into the ground pad.
- **Of the ten sensor pads, only INT1 is connected.** CS and SDO/SA0 stay open - CS pulled low
  switches the chip to SPI and it drops off the bus, and SDO/SA0 sets the address. The VDDIO and
  address solder jumpers stay as shipped.

### The zero reference

Neither the enclosure nor the boat is level. The box sits at whatever angle the bracket gives it,
and a boat at rest floats at whatever her tanks, crew and stores make her.

So the firmware does not assume a mounting angle. A **"level now"** action records the gravity
vector at that moment into NVS, and every later heel and pitch is measured against it. Rough
mechanical alignment plus a recorded zero beats trying to shim a box true.

Consequences worth being explicit about:

- Zeroing has to be done **with the boat at rest and reasonably loaded**, and repeated if the
  enclosure is ever remounted.
- It records the boat's own trim as zero, so this measures **change in attitude**, not absolute
  angle from the waterline. That is the useful quantity anyway.
- It is refused for the first half minute after a start. The gyroscope's offset is still being
  learned then (section 2), and a zero taken while the reading is off by it keeps that error for
  good.
- Until it has been done once, heel and pitch are left out entirely (section 9). The action lives
  on the configuration portal - section 8.

### The one misalignment zeroing cannot catch

Recording the gravity vector corrects any **tilt** of the mounting - a crooked bracket, the boat's
own trim, a bulkhead that is not plumb. It cannot correct a **rotation about the vertical axis**.

Mount the sensor a quarter turn out in the horizontal plane and gravity still points exactly down.
The zero reference sees nothing wrong. But the axis the firmware calls heel is now measuring pitch,
and the one it calls pitch is measuring heel.

Nothing in the data gives it away. At a berth both channels are small and plausible; the error only
shows on the first real passage, when the boat appears to pitch twenty degrees and hardly heel at
all - and by then it has been recorded that way for weeks.

So **decide which of the sensor's axes runs fore and aft before it is bolted down**, mark it, and
write it in the commissioning notes. The Modulino prints its X and Y axes and the sense of rotation
about Z on the board; the bench reader in section 10 confirms them against gravity before anything
is fixed.

The firmware takes the mapping from a setting in NVS beside the zero reference - which of the
module's axes points forward and which points up - so a mounting position that makes the natural
orientation awkward is a configuration change rather than a mechanical fight. It is still a
recorded decision, because nothing downstream can infer it.

## 5. Two rates, and why the bus decides

Heel and an impact want very different sampling.

| | needs | because |
|---|---|---|
| Heel, pitch | 10-25 Hz | a sailing boat's roll period is 3-6 s |
| Impact | 100 Hz and up | the peak of a knock lasts tens of milliseconds |

Sampling everything at 100 Hz is not free. Fourteen bytes per sample at 100 kHz is roughly
1.4 ms of bus time, so 100 Hz would occupy the I2C bus about **14 % of the time** - shared with the
SHT31 and the ADS1115 converters. Batching through the FIFO saves the per-transaction overhead and not much
else; the traffic is the traffic.

**So the chip does the watching.** The LSM6DSOX samples its accelerometer at 104 Hz, runs its own
knock detector on every sample (section 7) and pulls an interrupt line when it fires. The ESP polls
attitude at a leisurely 25 Hz - about 3 % of the bus - and hears about impacts the instant they
happen, at a bandwidth it could never afford to poll for. The gyroscope runs at 26 Hz, matched to
the polling, and at ±1000 dps. A boat never turns anywhere near that fast, but a module turned over
in the hand easily passes 250 dps, and a gyroscope driven past its range loses the angle it was
turned through.

That costs one GPIO. **IO2** is free and has no other claim.

The line is **latched**: it stays high until the ESP reads the chip's source register. So `loop()`
only has to look at the pin each time it comes round - no interrupt routine, and no knock lost to
a pass that took longer than usual. GPIO2 is pulled down, so with no module fitted the line reads
quiet rather than floating.

The chip drives the pin only once something is routed to it; until then its own pull-down holds
it low whatever the configured polarity. Any check of the wire therefore comes after the knock
detector is armed - before it, a connected line and a missing one read the same.

A line that is high is read at most every 10 ms. One held up from outside then costs a little bus
time rather than a register read on every pass, and is reported once as stuck.

**A slow pass is not integrated.** `loop()` can stall for far longer than 40 ms - a failed MQTT
connect holds it for three seconds ([A-005](A-005-server-uplink.md)). Integrating the last rate of
turn over such a gap would swing the estimate by that rate times the gap, so a step longer than
0.1 s counts as one ordinary step, and the accelerometer closes whatever the boat did meanwhile.

## 6. What reaches the aggregate

The window is the same five minutes as every other sensor's. What it has to answer: how is she
lying, how is she moving, and is either changing.

| Field | Meaning | What it answers |
|-------|---------|-----------------|
| `heel_deg` | mean heel, positive to starboard | the settled list. Over days, a slow change is water collecting, stores shifting, or a boat hanging in her lines on a falling tide |
| `heel_deg_min`, `heel_deg_max` | the extremes | the single largest roll - one passing wake, one gust |
| `heel_deg_sd` | standard deviation within the window | **how much she rolls**, typically. A steady roll of ±5° reads about 3.5 |
| `pitch_deg`, with `_min`, `_max`, `_sd` | the same for pitch, positive bow-up | trim at rest, and pitching in a seaway |
| `motion_g`, with `_min`, `_max` | acceleration less gravity | **how hard she is thrown about**, including the up and down that no angle shows. The max is the hardest jolt the 25 Hz polling caught |
| `knocks` | knocks the chip detected, counted straight through the hold-off | the record the knock threshold is set against - section 7 |

Under sail a window reading `heel_deg` 18, min 12, max 31, sd 4 says more about the passage than
any single number could. At the berth the same four numbers say whether it is blowing.

**The spread beside the extremes.** Min and max are set by a single event. Five quiet minutes with
one ferry wake read exactly like five rough ones. The standard deviation describes the whole
window, and the two together tell "restless" from "once, hard".

**Not the rate of turn.** It is the obvious candidate and it does not earn a channel. Its minimum
is always near zero, because a rolling boat stops at the end of every roll. Its mean is the roll
amplitude divided by the roll period, and the period is a property of the boat that hardly
changes - so it says what the spread already says, in a less intuitive unit.

**What the IMU cannot see is how deep she lies.** A boat that settles evenly does not tilt. Water
coming in usually shows anyway - in the trim, as it runs aft or forward, and in the bilge level -
but not in any number from here directly. Trends below about 0.3° over months are not worth
reading either: the accelerometer's offset drifts that much with temperature.

Heel and pitch are left out until the boat has been levelled (section 4). `motion_g` and `knocks`
are there from the start.

### Summarised at full rate, handed over per measurement

Every other sensor produces one reading per measurement interval, 10 s. This one produces 250 in
that time, and it is what happens between handovers that matters: the maximum of one reading in
250 is not the maximum, and a spread computed from thirty readings is a guess.

So the IMU keeps its own running statistics at 25 Hz - count, sum, sum of squares, min and max -
and hands them over as **one sub-aggregate per measurement interval**. `Channel` gains a sum of
squares and a `merge()` beside its `add()`, and counts samples separately from measurements:

- the window's mean, spread, min and max are exact over every sample
- `n` keeps counting measurements, as for every other sensor - 30 for a full window

The sums are kept in double precision. A spread of a tenth of a degree beside a heel of twenty is
a difference in the fifth digit of the sum of squares, and seven and a half thousand additions in a
float's seven digits lose it.

**The first half minute after a start is left out.** While the filter is still learning the
gyroscope's offset (section 2) the reading drifts by up to a couple of degrees, and kept, those
readings would set the extremes and the spread of the first window after every restart. Left out,
they make `n` dip in that window instead - 27 rather than 30 - the same as the SHT31's heater does
in [A-008](A-008-sht31-cabin-climate.md). Knocks are counted regardless.

A spot reading carries the present heel, pitch and motion and no spread, the same as every other
spot reading carries no range.

### The alarm these channels carry

**A list or trim that keeps changing while the boat is unattended** is the alarm worth having from
this sensor, more than the knock: water coming in, or lines too short on a falling tide, show as a
mean that drifts window after window. A wake or a gust moves the extremes and the spread, but not
the mean for long. A sustained drift over several windows therefore rarely raises a false alarm.

The rule belongs on the server, which has the history, as part of the alarm logic. The board only
has to deliver the means.

## 7. Impact detection

A knock raises an event on `boathub/<boat-id>/events` - immediately, not at the next window. An
unattended boat that was hit at 03:14 is worth knowing about at 03:14.

### What the chip compares

Not the acceleration itself but **how fast it changes**. The chip's slope filter takes the
difference between consecutive samples, 9.6 ms apart at 104 Hz, and compares it against the
threshold on every axis. That is what tells a knock from a seaway: a wave can throw the boat about
by a few tenths of a g, but over seconds, so between two samples it barely moves. A knock against
the pier delivers its jolt within a sample or two.

The threshold is therefore the size of that **jump**. At the ±4 g range the chip resolves it in
steps of 0.125 g, up to 7.9 g.

The event carries the time, the axes that crossed - named as the boat's, fore-and-aft,
athwartships or vertical, through the axis setting - and the acceleration read the moment the
line rises. Its fields, and how it reaches the server, are in [A-011](A-011-events.md).

| | Starting point | |
|---|---|---|
| Threshold | a jump of 0.5 g | a guess until there is real data. A boat snatching at her lines changes her acceleration over tenths of a second; a knock against the pier does it within milliseconds |
| Hold-off | 60 s | one impact, not forty reports of the same one |

The hold-off runs from a report and is **not restarted** by the knocks it suppresses. Those are
counted, and the count travels with the next report. A boat banging against the pier for an hour
is then reported once a minute, each report saying how many knocks it stands for - rather than
once, followed by an hour of silence that looks like calm.

**Expect to tune this - before anything is notified.** The threshold that separates "life aboard"
from "something hit us" is a property of this boat at this berth, and nobody can pick it from a
datasheet. A boat being rammed by one that is berthing badly jolts far harder than a crew moving
about a 10 m yacht. How much harder is what has to be measured, and an alarm that cries wolf while
that is being found out gets ignored for good.

So it is set in two steps:

1. **Record without alarming.** `knocks` per window shows what the threshold catches. Leave it a
   week or two, with days aboard and windy days among them.
2. **Set the threshold above all of that, with margin**, until those windows count none. Only then
   is a knock event turned into a notification.

## 8. Settings and the portal

Everything a boat needs different from the next one is a setting, in the `boathub` namespace
beside the rest of the configuration, and on the configuration portal under **Motion sensor**.
Saving restarts the board, like every other setting.

| Key | Meaning | Default |
|-----|---------|---------|
| `imu_fwd` | module axis pointing forward: 0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z | 0, +X |
| `imu_up` | module axis pointing up, same codes | 4, +Z - component side up |
| `imu_tau` | filter time constant, in tenths of a second | 30 |
| `imu_knock` | knock threshold, the jump in mg between samples | 500 |
| `imu_hold` | hold-off after a reported knock, in seconds | 60 |

The portal refuses forward and up along the same axis, which would leave the third undefined. It
clamps tau to between half a second, which follows the waves, and a minute, which stops following
the boat, and the threshold to what the chip can be set to.

**Level now** is a button of its own under the settings, posting to `/level`. It acts on the
running board rather than changing a setting, so it restarts nothing, and it answers with a page
that says why when it is refused. The reference it records goes into its own namespace, `imu`,
**in the module's own axes**. It is a fact about how the module sits, so correcting the axis
setting later does not throw it away.

## 9. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Sensor does not answer | I2C NACK | the three channels are absent, event once; the other sensors on the bus carry on |
| Never zeroed | no reference in NVS | report `motion_g` only. A heel figure measured against an unknown mounting angle is worse than none |
| Gyroscope drifting badly | attitude disagrees with gravity while at rest | the filter corrects it continuously; a persistent disagreement is a failing part |
| Enclosure has moved | heel at rest is no longer near zero | cannot be told apart from the boat's trim changing - which is why re-zeroing belongs in the commissioning notes, not in the firmware's judgement |
| Interrupt line stuck | impacts reported continuously | hold-off limits the rate; a stuck line shows as a steady stream and is its own diagnosis |
| INT1 not connected | at start the chip inverts the line's idle level, and GPIO2 does not follow | attitude carries on; knocks go undetected, which is said once in the log and in the status rather than discovered after the first one |

## 10. Verification

Most of this is done on the desk first, with the bench reader:

```
pio run -d board -e imuread -t upload -t monitor
```

It runs the firmware's own driver and filter, prints heel and pitch beside what gravity alone says,
and takes `z` to level, `c` to clear the level and `r` to restart the filter. It assumes X toward
the bow and the component side up.

- [ ] Sensor answers at its address, and the bus scan still finds every other device
- [ ] The bench reader reports the INT1 wire connected
- [ ] Standing the module on each edge in turn, the bench reader names the printed arrow that
      points at the ceiling - `+X`, `+Y`, and `+Z` lying flat - and the opposite face as minus
- [ ] The gyro bias the bench reader shows settles within half a minute of a start, and stays put
- [ ] In the firmware, the status line reads `settling` after a start and `not level` within half
      a minute, and **Level now** is refused until it does
- [ ] At rest and zeroed, heel and pitch read within a fraction of a degree of zero, and the zero
      survives a restart and a change of the axis setting
- [ ] **Tilting the enclosure bow-up moves `pitch_deg` and not `heel_deg`**, and tilting it to
      starboard moves `heel_deg` and not `pitch_deg`. This is the check for a quarter turn in
      the horizontal plane, which zeroing cannot detect
- [ ] Tilting the enclosure by a known angle reads that angle back
- [ ] Shaking it does **not** move the heel reading much - that is the filter's time constant doing
      its job, and the test that distinguishes a working fusion from a bare accelerometer
- [ ] Rotating it quickly and returning it leaves the reading back where it started within about
      ten seconds, with no lasting offset
- [ ] A tap on the enclosure raises an impact event; a gentle rock does not
- [ ] A knock during the hold-off is not reported on its own, and is counted in the next report
- [ ] `motion_g` sits near zero at rest and rises with deliberate rocking
- [ ] A window shows a roll range in min/max rather than a flat mean, and `heel_deg_sd` near zero
      at rest that rises with a steady rocking - about 0.7 of the rocking's amplitude
- [ ] Heel and pitch are absent from the telemetry until the boat is levelled; `motion_g` and
      `knocks` are present from the start, and `knocks` reads 0 rather than absent in a quiet window
- [ ] Knocks during a hold-off still count in `knocks`
- [ ] A spot reading carries heel, pitch and motion without `_sd`, `_min` or `_max`
- [ ] Removing the sensor leaves the other channels and the uplink untouched

## 11. References

- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the I2C bus this shares
- [A-004-wifi-and-configuration-portal.md](A-004-wifi-and-configuration-portal.md) - the portal
  that holds the settings and Level now
- [A-005-server-uplink.md](A-005-server-uplink.md) - the aggregate these channels join
- [ST LSM6DSOX datasheet](https://www.st.com/en/mems-and-sensors/lsm6dsox.html)
