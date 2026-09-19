# A-009 - IMU: heel, pitch and motion

| | |
|---|---|
| **Phase** | A |
| **Software version** | v3 |
| **Touches hardware** | yes |

## 1. Goal

Two questions from one sensor:

- **Under sail:** how far is she laying over, and how far did she lay over in the gust?
- **At the berth:** how hard is she working in her lines, and **did something hit her?**

Reported as `heel_deg`, `pitch_deg` and `motion_g` in the aggregate, plus an impact event.

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
wave motion; the accelerometer carries the long term, where the gyroscope has drifted. A
**complementary filter** is the whole of it:

```
angle = a * (angle + rate * dt) + (1 - a) * angle_from_gravity
```

`a` sets how long the accelerometer takes to pull the gyroscope back:
`tau = a * dt / (1 - a)`.

**For a boat, tau wants to be seconds, not tenths.** Wave-induced acceleration lasts whole seconds,
so a filter tuned for a handheld device would follow the waves rather than the heel. At 25 Hz,
`a = 0.987` gives about 3 s. That constant belongs in NVS: it is the one number that will need
tuning against real motion, and it is the one a boat needs different from everybody else.

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

**Recommendation: LSM6DSOX.** The complementary filter is perhaps thirty lines and it is not the
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

One thing to expect rather than chase: the module carries its own STM32 for the Arduino API, and
that answers on the bus at **0x7E**. Nothing here talks to it, but a bus scan finds it, and an
unexplained address is what somebody spends an evening on.

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
- The action needs somewhere to live: the configuration portal, alongside the probe re-learn from
  [A-003](A-003-ds18b20-temperature-sensors.md).

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
write it in the commissioning notes. If the mounting position makes the right orientation awkward,
the axis mapping belongs in NVS beside the zero reference rather than being fought mechanically -
but it has to be a recorded decision either way, because nothing downstream can infer it.

## 5. Two rates, and why the bus decides

Heel and an impact want very different sampling.

| | needs | because |
|---|---|---|
| Heel, pitch | 10-25 Hz | a sailing boat's roll period is 3-6 s |
| Impact | 100 Hz and up | the peak of a knock lasts tens of milliseconds |

Sampling everything at 100 Hz is not free. Fourteen bytes per sample at 100 kHz is roughly
1.4 ms of bus time, so 100 Hz would occupy the I2C bus about **14 % of the time** - shared with the
SHT31 and three ADS1115. Batching through the FIFO saves the per-transaction overhead and not much
else; the traffic is the traffic.

**So the chip does the watching.** The LSM6DSOX runs its own high-g detector at full internal rate
and pulls an interrupt line when a threshold is crossed. The ESP polls attitude at a leisurely
25 Hz - about 3 % of the bus - and hears about impacts the instant they happen, at a bandwidth it
could never afford to poll for.

That costs one GPIO. **IO2** is free and has no other claim.

## 6. What reaches the aggregate

| Channel | Meaning | What min/max are worth |
|---------|---------|------------------------|
| `heel_deg` | positive to starboard | **min and max are the roll range** in that window; the mean is the settled heel |
| `pitch_deg` | positive bow-up | pitching in a seaway, and trim at rest |
| `motion_g` | magnitude of acceleration less gravity | **max is the hardest single jolt** - how much she is being thrown about |

The existing channel structure already does exactly this. A five minute window that reports
`heel_deg` mean 18, min 12, max 31 says more about a passage than any single number could.

### One extension is needed

Every other sensor produces a reading slower than the window and hands it over one at a time. This
one produces 25 a second, and it is the **peak between handovers** that matters - a maximum taken
from downsampled values is not the maximum.

So the IMU keeps its own running min, mean and max at full rate and hands over a **sub-aggregate**,
once a second. `Channel` gains a `merge(min, mean, max, count)` beside its `add(value)`. The count
that arrives is real samples, so `n` keeps meaning what it says.

`Channel::n` becomes 32-bit while this is done: at 25 Hz a ten minute window is 15 000 samples, and
a 16-bit count leaves less headroom than anyone should have to think about.

## 7. Impact detection

A threshold crossing on the chip's interrupt raises an event on `boathub/<boat-id>/events` -
immediately, not at the next window. An unattended boat that was hit at 03:14 is worth knowing
about at 03:14.

The event carries the peak magnitude and the time. Settings in NVS:

| | Starting point | |
|---|---|---|
| Threshold | 0.5 g | a guess until there is real data. A boat snatching at her lines runs perhaps 0.1-0.3 g; a knock against the pier is well above it |
| Hold-off | 60 s | one impact, not forty reports of the same one |

**Expect to tune this.** The threshold that separates "a wave" from "something hit us" is a
property of this boat at this berth, and nobody can pick it from a datasheet. Until it is tuned,
`motion_g` max per window is the record to tune it against - which is a good reason to have the
channel whether or not the alarm is ever armed.

## 8. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Sensor does not answer | I2C NACK | the three channels are absent, event once; the other sensors on the bus carry on |
| Never zeroed | no reference in NVS | report `motion_g` only. A heel figure measured against an unknown mounting angle is worse than none |
| Gyroscope drifting badly | attitude disagrees with gravity while at rest | the filter corrects it continuously; a persistent disagreement is a failing part |
| Enclosure has moved | heel at rest is no longer near zero | cannot be told apart from the boat's trim changing - which is why re-zeroing belongs in the commissioning notes, not in the firmware's judgement |
| Interrupt line stuck | impacts reported continuously | hold-off limits the rate; a stuck line shows as a steady stream and is its own diagnosis |

## 9. Verification

- [ ] Sensor answers at its address, and the bus scan still finds every other device
- [ ] At rest and zeroed, heel and pitch read within a fraction of a degree of zero
- [ ] **Tilting the enclosure bow-up moves `pitch_deg` and not `heel_deg`**, and tilting it to
      starboard moves `heel_deg` and not `pitch_deg`. This is the check for a quarter turn in
      the horizontal plane, which zeroing cannot detect
- [ ] Tilting the enclosure by a known angle reads that angle back
- [ ] Shaking it does **not** move the heel reading much - that is the filter's time constant doing
      its job, and the test that distinguishes a working fusion from a bare accelerometer
- [ ] Rotating it quickly and returning it leaves the reading back where it started, with no
      accumulated offset
- [ ] A tap on the enclosure raises an impact event; a gentle rock does not
- [ ] The hold-off suppresses a second report from the same knock
- [ ] `motion_g` sits near zero at rest and rises with deliberate rocking
- [ ] A window shows a roll range in min/max rather than a flat mean
- [ ] Removing the sensor leaves the other channels and the uplink untouched

## 10. References

- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the I2C bus this shares
- [A-003-ds18b20-temperature-sensors.md](A-003-ds18b20-temperature-sensors.md) - the re-learn action
  this borrows a home from
- [A-005-server-uplink.md](A-005-server-uplink.md) - the aggregate these channels join
- [ST LSM6DSOX datasheet](https://www.st.com/en/mems-and-sensors/lsm6dsox.html)
