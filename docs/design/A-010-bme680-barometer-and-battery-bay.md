# A-010 - BME680 barometer and battery bay

| | |
|---|---|
| **Phase** | A |
| **Software version** | v1 |
| **Touches hardware** | yes |

## 1. Goal

Two things from one sensor in the battery bay:

- **First, the barometer.** Air pressure for the weather trend: the three-hour tendency, and a fall
  of about 3 hPa or more in three hours as the classic warning sign.
- **Second, the air in the battery bay.** Its temperature, which the chargers drive, and its
  humidity, which the bilge below drives. This watches the battery installation, not the crew's
  comfort.

Reported in the aggregate as `pressure_hpa`, `bay_temp_c` and `bay_rh`.

**Out of scope:**

- Cabin climate. That stays with the SHT31 ([A-008](A-008-sht31-cabin-climate.md)), which is the
  more accurate sensor and sits where the cabin air is.
- The temperature of the batteries themselves. This sensor measures the air around them.
  Temperature-compensated AGM charging thresholds want a DS18B20 on the battery case - the optional
  fourth probe in [MATERIAL.md](../MATERIAL.md).
- Air quality and hydrogen. The gas heater stays off (section 4), and a hydrogen alarm, if one is
  wanted, is a dedicated H2 detector.
- The alarm thresholds, which belong to the alarm logic.

## 2. Starting point

The sensor joins the I2C bus on GPIO8/9 at 100 kHz, beside the SHT31 at 0x44, the ADS1115
converters at 0x48 and 0x49 and the IMU at 0x6A. It answers at **0x76**, which clashes with none
of them.

The battery bay is closed but not airtight, sits roughly amidships, and is open to the bilge below.
It also holds the battery chargers, the battery splitter and the shore-power inlet. The BoatHub
enclosure is mounted in the same bay, with the IMU inside it near the boat's pitch axis
([A-009](A-009-imu-heel-and-motion.md)). The sensor therefore sits on a short cable beside the box.

Because the bay is not airtight, the pressure inside it follows the pressure outside. A barometer
in the bay reads the weather.

The driver follows the same discipline as the SHT31 and IMU drivers: register level, no vendor
library, and nothing in `loop()` waits.

## 3. What it measures, and what it does not

### Pressure: the trend, not the number

From the Bosch datasheet:

| | |
|---|---|
| Absolute accuracy | ±0.6 hPa, 300-1100 hPa, 0-65 °C |
| Relative accuracy | ±0.12 hPa, at constant temperature and humidity |
| Long-term stability | ±1.0 hPa per year |
| Temperature coefficient of the offset | ±1.3 Pa/K |

The absolute figure is the weak one. A reading can be half a hectopascal off on the day it is
fitted and drift by a whole one over a year. The **change** over a few hours is good to about a
tenth, and that is what the weather is read from. A fall of 3 hPa in three hours is twenty-five
times the relative accuracy.

The temperature coefficient is worth knowing in a bay that warms up. Chargers that heat the bay by
ten degrees move the reading by about 0.13 hPa - small against a 3 hPa tendency, but not nothing,
and it is one reason the tendency is read over three hours rather than one.

### Station pressure, not sea level

The board reports **station pressure**: what the sensor measures at the height where it sits,
corrected only by the offset in section 8. Reducing it to sea level is a matter of display and
happens on the server:

```
p_sea = p_station / (1 - 0.0065 * h / 288.15) ^ 5.255
```

`h` is the height of the sensor above sea level in metres. Near sea level that is about 0.12 hPa
per metre. On the coast `h` is a metre or two and the reduction is a quarter of a hectopascal,
below the sensor's own absolute accuracy. On an inland lake it is not: at 400 m it is some 47 hPa.

Storing the station pressure keeps the record true whatever the height setting turns out to be.
The height is a property of where the boat lies, not of the sensor; the server keeps it per boat,
and correcting it later corrects every displayed value without touching a stored one.

**The tendency needs neither the height nor the offset.** Both cancel in a difference, which is
the other reason the tendency is the reliable quantity.

### The air in the bay

| | Temperature | Humidity |
|---|---|---|
| Accuracy | ±0.5 °C at 25 °C, ±1.0 °C over 0-65 °C | ±3 %RH, plus ±1.5 %RH hysteresis |
| Response | | about 8 s to 63 % of a step |

Good enough to see the chargers working and the bilge breathing, and coarser than the SHT31 on
both counts. The datasheet adds that the chip's temperature reading depends on the board it sits on
and its own self-heating, and is typically above ambient.

**What the bay temperature says about the batteries is limited.** It is the air around them. A
battery that is charging hard warms from the inside and lags the air on the way up and on the way
down. For charging thresholds that is the wrong measurement - section 1.

**Condensation is the humidity element's enemy.** The datasheet specifies the humidity range for a
non-condensing environment and allows condensation only with no power applied. A bay open to the
bilge can reach saturation, and the BME680 has no heater for its humidity element to dry it off
again (the gas heater is a different structure, section 4). Placement is the protection - section
5. A reading pinned at 100 %RH for hours means the sensor is wet: a reason to go and look, and not
a value to trust until it comes down again.

## 4. The gas heater stays off

The BME680 carries a fourth sensor, a metal-oxide layer on a hotplate heated to 200-400 °C, whose
resistance changes with volatile compounds in the air. **In this installation the heater stays
off**, and it is off by default in the firmware, behind a switch in NVS.

The reasons:

- **Nothing here needs it.** Pressure, temperature and humidity are measured without it.
- **It skews the other readings.** A hotplate at several hundred degrees warms the die it sits on,
  so the chip's own temperature reads high and the relative humidity reads low.
- **It is a hot surface in a battery bay.** AGM batteries recombine almost all of their gas and
  normally release very little. A faulty or wrongly configured charger - too high a voltage, a
  flooded-cell profile - makes the valves vent hydrogen. The risk is low: the hotplate stays below
  hydrogen's autoignition temperature of about 560 °C, and the chargers themselves are worse
  ignition sources. But a hot surface that serves no purpose has no place there.
- **It would not be a hydrogen alarm anyway.** The gas resistance responds to a broad mix of
  compounds, humidity among them, and says nothing specific. A hydrogen alarm is a dedicated H2
  detector, a feature of its own.

**With the heater off, the chip needs no Bosch BSEC.** BSEC is Bosch's closed library, and what it
does is turn the gas resistance into an air-quality index. The compensation for temperature,
pressure and humidity is ordinary integer arithmetic on calibration words stored in the chip. So
this is a register-level driver like the SHT31's and the LSM6DSOX's, with no binary blob in the
build.

Off means two register bits, both written with every measurement (section 6): `run_gas` cleared in
`ctrl_gas_1` (0x71), so no gas conversion runs - the heater only operates during one - and
`heat_off` set in `ctrl_gas_0` (0x70).

**With the switch on**, each measurement adds a heater phase - Bosch's starting values are 320 °C
for 150 ms - and the gas resistance joins the aggregate as `bay_gas_kohm`, mean, min and max like
the others. Bay temperature and humidity then read warm and dry by an amount that depends on the
interval and the mounting. The switch is there for a sensor mounted somewhere else; in the battery
bay it stays off.

## 5. Hardware

**Parts**

| Part | Qty | Purpose | Note |
|------|-----|---------|------|
| BME680 breakout, CJMCU-680 | 1 | barometer, bay temperature and humidity | sold in pairs; the second is the spare |
| Cat 5e or Cat 6 patch cable, round | ~0.3 m | the run to J2 | stranded, copper |
| Ferrules 0.25 mm² and 0.5 mm² | 3 + 1 | at J2 | from the ferrule kit |
| Cable gland and lock nut, M12 x 1.5 | 1 each | where the cable leaves the box | the same part as every other entry |

All of them are in [MATERIAL.md](../MATERIAL.md).

**Pin assignment** - no GPIO of its own. It is on the I2C bus, GPIO8 SDA and GPIO9 SCL, through
J2.

### The breakout

The CJMCU-680 brings out six pins: `VCC`, `GND`, `SCL`, `SDA`, `SDO` and `CS`. Three things to
check before anything is soldered:

- **That it is a BME680.** Boards are sold under the wrong name, and a BMP280 on a board printed
  BME280 or BME680 has no humidity sensor at all. `i2cscan` reads the chip ID and names it: a
  BME680 reports 0x61.
- **Its pull-ups.** Measure `SDA` to `VCC` and `SCL` to `VCC` on the unpowered module. Most
  breakouts carry 10 kΩ or 4.7 kΩ. Leave them fitted: with the SHT31 and both converters beside
  them the bus stays well above the 1.1 kΩ at which the 3 mA every device here can sink runs out.
  Write the value down; it enters the bus sum in [B-002](B-002-main-board.md) section 6.
- **Power it from 3.3 V, never 5 V.** The same rule as the SHT31 in
  [A-002](A-002-bench-setup-usb.md): a breakout's pull-ups go to its supply pin, so a 5 V feed puts
  5 V on ESP32 pins rated 3.6 V absolute maximum. A board without a regulator would also put it on
  a chip rated 4.25 V.

### Tie CS and SDO at the module

**`CS` hard to `VCC`.** The chip picks its interface from this pin. In the datasheet's words, once
CS has been pulled low, even once, the I2C interface is disabled until the next power-on reset.
A floating `CS` that catches one low edge leaves a sensor that has stopped answering, and nothing
the firmware sends can bring it back - only taking its power away does. A wire link from `CS` to
`VCC` on the module makes that impossible.

**`SDO` to `GND`**, which gives the address **0x76**. `SDO` must not float: the datasheet says the
address is then undefined, which in practice is a sensor that answers sometimes. A hard link to
ground also wins against any resistor the breakout puts on the pin.

| Address | Device |
|---------|--------|
| 0x44 | SHT31 |
| 0x48, 0x49 | ADS1115 |
| 0x6A | LSM6DSOX |
| **0x76** | **BME680** |

### Placement in the battery bay

- **About 15 cm of cable from the box**, fixed to a bulkhead, **clear of the 230 V shore-power
  terminals**. If the module ever comes loose, nothing on it may be able to reach a mains terminal.
- **In bay air, not in a charger's exhaust.** A charger's fan blowing over the sensor measures the
  charger. A spot where the bay's own air moves past is what is wanted.
- **The sensor opening sideways or down.** The vent hole is in the top of the chip's metal lid.
  Mounted face up under a deck head that drips, water collects on it. Turned to the side or
  downwards, drips and condensate run off.
- **Nothing over the package opening** - no lacquer, conformal coating, hot glue or heat-shrink. A
  coated opening is a pressure sensor that measures its own seal. The cable is secured with a tie to
  the board's mounting hole, not with glue.
- **Not in a box of its own.** A closed housing measures the housing's air. A drip shield above it
  is fine.

**Why not inside the BoatHub enclosure?** For pressure alone it would work: the vent membrane
equalises the box with the bay, and the trend would come through. For the bay climate it would not.
The box runs 10-12 °C above ambient ([A-002](A-002-bench-setup-usb.md)), so its temperature and
humidity are the box's, not the bay's - the same reason the SHT31 lives outside.

### The cable

Its own run of **Cat 5e or Cat 6 patch cable** into **J2**, beside the SHT31's run. Both cables
are wired the same way:

| Pair | Core | Signal | J2 pole |
|------|------|--------|---------|
| brown | brown | 3.3 V | 1 |
| | white | GND | 2 |
| orange | orange | SCL | 3 |
| | white | GND | 2 |
| blue | blue | SDA | 4 |
| | white | GND | 2 |
| green | both | **cut off at both ends** | |

- **All three white cores are ground.** In these cables the white partner of each pair is plain
  white, so the three are indistinguishable anyway - and putting all of them on ground gives the
  supply and each signal a ground of its own in its twisted pair, which is the twisting rule from
  [A-002](A-002-bench-setup-usb.md).
- **The green pair is cut off at both ends**, flush with the jacket, so neither end can touch
  anything.
- **One ferrule per core per cable at J2:** 0.25 mm² on brown, orange and blue, and one 0.5 mm²
  over the three whites twisted together. Each pole of J2 therefore holds two ferrules, one from
  each cable. **Pull-test every ferrule** after tightening, one at a time. A shared pole can hold
  one ferrule firmly and the other barely.
- **At the module, solder the cores straight into the pads.** The breakout ships with a loose pin
  header; leave it off. A plug-in contact is exactly what [B-002](B-002-main-board.md) keeps off
  this bus. The `CS` and `SDO` links go on at the same time.
- **Stranded and copper**, as for the IMU cable ([A-009](A-009-imu-heel-and-motion.md)): solid
  installation cable breaks at a solder joint under vibration, and `CCA` takes solder badly.
- **Round, with an outer diameter within the gland's 3.5-7 mm.** Most Cat 5e and Cat 6 patch cable
  is 5-6.5 mm; flat patch cable will not seal in a gland, and thick shielded Cat 6A may not fit it.

The cable leaves the box through **its own M12 gland**, next to the SHT31's. Both end in J2, but a
gland seals around one round cable, not two.

**On the bus, 15 cm is almost nothing** - some 15 pF. No extra pull-ups are needed for it, and the
module's own decoupling is enough at that distance; the 100 nF at the far end in
[A-002](A-002-bench-setup-usb.md) is for the SHT31's long run. What it does add is its share of
the cable-length rule in A-002, which counts **every cable on the bus together**.

## 6. Reading it

### Forced mode, one measurement per interval

The BME680 has no free-running mode worth using here. **Forced mode** takes one measurement of
temperature, pressure and humidity and puts the chip back to sleep, which is exactly a 10 s
measurement interval. Once every 10 s at roughly 43 ms a measurement, self-heating is not
measurable.

| Setting | Value | Why |
|---------|-------|-----|
| Humidity oversampling | ×1 | the humidity element is slower than any oversampling helps with |
| Temperature oversampling | ×2 | temperature feeds the compensation of the other two |
| Pressure oversampling | ×16 | pressure is what this sensor is for |
| IIR filter | off | at 10 s between samples it would average across tens of seconds and smear the window's min and max; the window mean already filters |
| Gas | off | section 4 |

That is also the datasheet's own quick-start configuration. A measurement takes about 43 ms.

### Without waiting

Each interval:

1. Write `ctrl_hum` (0x72), then `ctrl_meas` (0x74) with the oversampling and forced mode, and
   `ctrl_gas_0` and `ctrl_gas_1` with the heater off. Return to the loop.
2. On a pass at least 50 ms later, read ten bytes from 0x1D: `meas_status_0`, then pressure,
   temperature and humidity.
3. If `new_data_0` (bit 7 of 0x1D) is set, compensate - temperature first, because its
   intermediate value feeds the pressure and humidity formulas. If not, look again on the next pass.

**The configuration is written with every measurement**, not once at start. The BME680 has no
status bit that reports a reset, unlike the SHT31. After a brown-out it comes back in sleep mode
with every measurement switched to "skipped". Writing the full configuration each time means a
reset costs at most one sample, for a handful of bytes on the bus. The order matters: the
datasheet asks for `ctrl_hum` before `ctrl_meas`.

**The chip does not stretch the clock**, so the bus is free for the converters and the IMU while
it measures - the same reason the SHT31 is read without clock stretching in
[A-008](A-008-sht31-cabin-climate.md).

At start the driver reads the chip ID (0xD0, 0x61), issues a soft reset (0xB6 to 0xE0) and reads
the calibration words once. The compensation formulas and the addresses of those words come from
Bosch's BME68x sensor API on GitHub, which the datasheet refers to; they are a few dozen lines of
integer arithmetic and copy across without the rest of the API.

### Validating a reading

There is no CRC: unlike the SHT31, nothing on the wire says a byte arrived wrong. Fifteen
centimetres of cable make that unlikely, and the checks below catch the gross cases.

| Check | Reject if |
|-------|-----------|
| Skipped measurement | raw pressure or temperature reads 0x80000, or raw humidity 0x8000 - what the chip returns after a reset |
| Range, pressure | outside 300 to 1100 hPa, the sensor's own range |
| Range, temperature | outside -20 to +60 °C |
| Range, humidity | outside 0 to 100 %RH |
| Rate of change | more than 2 hPa, 10 °C or 30 %RH between consecutive 10 s samples |

A rejected sample is not counted into the window, exactly as for the SHT31. `n` drops, and the
aggregate stays correct for the samples that remain.

## 7. What reaches the aggregate

| Field | Meaning |
|-------|---------|
| `pressure_hpa`, with `_min`, `_max` | station pressure in hPa, offset applied |
| `bay_temp_c`, with `_min`, `_max` | air temperature in the battery bay |
| `bay_rh`, with `_min`, `_max` | relative humidity in the battery bay |
| `bay_gas_kohm`, with `_min`, `_max` | gas resistance - only with the heater switched on, section 4 |

The names follow the rest of the payload: a location and a unit. `bay` is the battery bay - the
engine bay's probe is `engine_temp_c` - and the prefix keeps these apart from the SHT31's
`cabin_temp_c` and `cabin_rh`, which measure somewhere else.

If every sample in a window was rejected, or the sensor is not fitted, the fields are **absent**,
never zero.

**The stored record grows.** Three channels of mean, min and max in 16 bits each are 18 bytes,
more than the 8 the record keeps in reserve, so they arrive with a new record format; section 4 of
[A-007](A-007-store-and-forward.md) covers how a backlog in the old one drains. Temperature and
humidity scale as the SHT31's, pressure in 0.1 hPa. The gas channel, if it is ever switched on,
needs three words of its own.

### On the server

Each field gets its column triple, like every other sensor ([A-006](A-006-telemetry-storage.md)).
Two values are derived there rather than on the board, because the server has the history and the
height:

- **The three-hour tendency:** the latest window's `pressure_hpa` minus the one three hours
  earlier. A fall of 3 hPa or more is the warning; the rule belongs to the alarm logic.
- **Sea-level pressure for display**, from the stored station pressure and the boat's height
  setting - section 3.

Underway the records wait in the board's buffer and the tendency is computed when they arrive.
Showing it on the on-board Wi-Fi at sea would need the board to keep three hours of window means
itself - 36 values - and belongs to the local display.

## 8. Settings

In the `boathub` namespace beside the rest of the configuration, and on the configuration portal
under **Barometer**. Saving restarts the board, like every other setting.

| Key | Meaning | Default |
|-----|---------|---------|
| `bme_gas` | gas heater and gas measurement | off |
| `bme_p_off` | offset added to every pressure reading, in Pa, signed | 0 |

The oversampling, the filter and the address are constants. The height of the berth is not a board
setting: it belongs to the server, which is where sea-level pressure is computed.

## 9. Failure modes

| Case | Detection | Reaction |
|------|-----------|----------|
| Sensor does not answer | I2C NACK at 0x76 | fields absent, event once, retried each interval; the other devices on the bus carry on |
| A different chip at 0x76 | chip ID is not 0x61 | not read, logged once. `i2cscan` names what it is |
| Sensor reset itself | raw values read as skipped | sample discarded; the next measurement writes the configuration again |
| Value implausible | range and rate checks | sample discarded, counted |
| Answered once, then never again after a disturbance | NACK, and a power cycle brings it back | `CS` caught a low edge. The link from `CS` to `VCC` is what prevents it |
| Reads a hard 100 %RH for hours | plausibility over time | the sensor is wet. There is no heater to dry it; pressure and temperature stay valid. Look at the placement - section 5 |
| Supply core loose at J2, signals still connected | every device on the bus NACKs or misbehaves | the datasheet notes that SDA and SCL carry protection diodes to the chip's supply, so an unpowered chip is fed from the bus pull-ups and can hold the bus. The pull test at J2 prevents it; the bus scan in the portal finds it |
| Absolute pressure off against a station | calibration check | the offset drifts with the sensor's ±1.0 hPa per year. Recheck it once a year; the tendency is unaffected |

A fault in this sensor costs its own three fields and nothing else.

## 10. Safety

- **The gas heater is off by default** - section 4. It is the only hot surface this sensor could add
  to a bay where a failing charger can vent hydrogen.
- **The module is mounted clear of the 230 V shore-power terminals**, and its cable routed apart
  from the mains wiring.
- **This is not a hydrogen alarm and not a fire alarm.** Nothing in its readings should be taken
  as one.
- The sensor only reads. It has no path to any of the boat's systems, and it is 3.3 V from the
  DevKit's own regulator.

## 11. Verification

**On the bench**

- [ ] `CS` measures 0 Ω to `VCC` and `SDO` 0 Ω to `GND` on the module, before power is applied
- [ ] The module's pull-ups measured on the unpowered module and written down
- [ ] `i2cscan` names a BME680 at 0x76 (`SDO` to GND), and still finds 0x44, 0x48 and 0x6A
- [ ] Pressure within a few hPa of a nearby station's reading before any offset is applied
- [ ] At rest, consecutive pressure readings differ by a few hundredths of a hPa
- [ ] **Lifting the module by one metre lowers the pressure by about 0.12 hPa**, and putting it
      back restores it - the relative accuracy, seen directly
- [ ] Breathing on the module raises the humidity within about ten seconds, and it recovers
- [ ] Side by side with the SHT31 at room temperature, temperature agrees within about 1 °C and
      humidity within about 5 %RH
- [ ] The measurement does not block: the ADS1115 and the IMU stay readable while it converts
- [ ] With `bme_gas` off, `ctrl_gas_1` (0x71) reads back with `run_gas` clear
- [ ] Unplugging the sensor removes its three fields from the message and leaves the rest intact;
      plugging it back in brings them back without a restart
- [ ] Pulling the module's supply and restoring it costs at most one sample, and no value of the
      skipped kind reaches the aggregate
- [ ] A spot reading carries the three fields without `_min` or `_max`

**In the boat**

- [ ] The sensor opening faces sideways or down, out of any charger's exhaust, clear of the 230 V
      terminals, and nothing covers the package
- [ ] Every ferrule at J2 pull-tested, both cables each through its own gland
- [ ] The bay temperature rises while the chargers work hard and falls back afterwards
- [ ] Over a day, `pressure_hpa` follows a nearby station's trend within a few tenths of a hPa

**Calibration** - against a nearby official weather station, within a few tens of kilometres, on a
settled day with little wind, so that the pressure field between the two is flat. Reduce the
board's reading to sea level with the same height the server uses, compare it with the station's
sea-level pressure over several hours, and average the differences. The result, station minus
board, goes into `bme_p_off` in Pa. Repeat once a year.

## 12. References

- [A-002-bench-setup-usb.md](A-002-bench-setup-usb.md) - the bus, its pull-ups and the
  cable-length rule
- [A-005-server-uplink.md](A-005-server-uplink.md) - the aggregate these fields join
- [A-006-telemetry-storage.md](A-006-telemetry-storage.md) - the columns, and where the tendency is
  computed
- [A-007-store-and-forward.md](A-007-store-and-forward.md) - the record format change
- [A-008-sht31-cabin-climate.md](A-008-sht31-cabin-climate.md) - the cabin sensor, and the same
  non-blocking discipline
- [B-002-main-board.md](B-002-main-board.md) - J2 and the bus sum
- [Bosch BME680 datasheet](https://www.bosch-sensortec.com/products/environmental-sensors/gas-sensors/bme680/)
- [Bosch BME68x sensor API](https://github.com/boschsensortec/BME68x_SensorAPI) - the compensation
  formulas
