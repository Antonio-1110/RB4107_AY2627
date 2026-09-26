# 03 – MLX90640 thermal sensor integration

> **Test-only project, not part of the running system.** It checks one part on its own and replaces the system firmware on that board. To run the
> system, flash [`05a_c6_presence_node`](../05a_c6_presence_node) on the two
> presence C6 boards, [`05b_c6_thermal_node`](../05b_c6_thermal_node) on the
> thermal C6 board and [`29_end_to_end`](../29_end_to_end) on the S3.

TODO section 3 and 3.1. Target: **FireBeetle 2 ESP32-C6** + **MLX90640 (32×24)**.

## Wiring (menuconfig defaults, UNCONFIRMED)

| MLX90640 | ESP32-C6 |
|---|---|
| SDA | `RB_MLX_SDA_GPIO` = GPIO19 |
| SCL | `RB_MLX_SCL_GPIO` = GPIO20 |
| VIN / GND | 3.3 V / GND |

Change them in `idf.py menuconfig` → *RB4107 configuration* → *Sensor node* →
*MLX90640 thermal sensor*. The legacy Arduino sketch used SDA 21 / SCL 22, but
that was a different board.

## Software

- `components/mlx90640`: the official Melexis driver (vendored, unmodified)
  with an ESP-IDF I2C shim. It reads the calibration EEPROM, combines both
  chess sub-pages into one full frame and corrects defective pixels.
- `components/thermal_features`: pure C, unit-tested in project 28. Computes
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
