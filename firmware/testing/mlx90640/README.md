# Test: MLX90640 thermal sensor

> **Test only, not part of the running system.** It checks the MLX90640 camera on its own. Flashing it
> replaces the system firmware on that board. To run the system, flash
> [`presence_node`](../../presence_node) on the two presence C6 boards,
> [`thermal_node`](../../thermal_node) on the thermal C6 board and
> [`controller`](../../controller) on the S3.

TODO section 3 and 3.1. Target: **DFRobot DFR1117 (ESP32-C6 mini)** + **MLX90640 (32×24)**.

## Wiring (menuconfig defaults, UNCONFIRMED)

| MLX90640 | ESP32-C6 |
|---|---|
| SDA | `RB_MLX_SDA_GPIO` = IO19 (header P4 pin 6) |
| SCL | `RB_MLX_SCL_GPIO` = IO20 (header P4 pin 7) |
| VIN / GND | 3.3 V / GND |

Change them in `idf.py menuconfig` → *RB4107 configuration* → *Sensor node* →
*MLX90640 thermal sensor*. The DFR1117 schematic doesn't label any pin SDA/SCL,
so check your wiring. The legacy Arduino sketch used SDA 21 / SCL 22; IO21 and
IO22 are also on the DFR1117 (header P3 pins 7 and 6), so if the camera is
wired that way, set 21 / 22.

## Software

- `components/mlx90640`: the official Melexis driver (vendored, unmodified)
  with an ESP-IDF I2C shim. It reads the calibration EEPROM, combines both
  chess sub-pages into one full frame and corrects defective pixels.
- `components/thermal_features`: pure C, unit-tested in project `testing/unit_tests`. Computes
  max, min and mean temperature, the hot-region temperature (square around the
  hottest pixel), the count of pixels above the threshold, the rate of change
  over a configurable window, and validity (NaN or out-of-range pixels).

All thresholds are in menuconfig → *Thermal feature extraction*. The 50 °C
hot-pixel threshold is a placeholder.

## Diagnostics

Set `RB_THERMAL_DIAG_DUMP_EVERY` to dump raw frames, then:

```bash
idf.py -p <PORT> monitor | python3 ../../tools/diagnostics/thermal_frame_view.py
```

## Bench tests still to do

Log the summary line for each case:

| Test | Setup | max | hot-region | px > threshold | rate |
|---|---|---|---|---|---|
| Empty room baseline | | | | | |
| Pan on hob, 30 / 60 / 100 cm | | | | | |
| Boiling water | | | | | |
| Frying pan (hot oil) | | | | | |
| Person only, no heat | | | | | |
