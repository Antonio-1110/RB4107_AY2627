# 02 – C4002 presence sensor integration

> **Test-only project, not part of the running system.** It checks one part on its own and replaces the system firmware on that board. To run the
> system, flash [`05a_c6_presence_node`](../05a_c6_presence_node) on the two
> presence C6 boards, [`05b_c6_thermal_node`](../05b_c6_thermal_node) on the
> thermal C6 board and [`29_end_to_end`](../29_end_to_end) on the S3.

TODO section 2. Target: **FireBeetle 2 ESP32-C6** + **DFRobot C4002 (SEN0691)**.

## Interface

The C4002 talks over **UART at 115200 8N1** using DFRobot's framed protocol.
The frame layout comes from the official
[DFRobot_C4002](https://github.com/DFRobot/DFRobot_C4002) library and is
documented in `components/c4002/include/c4002_proto.h`. An optional **OUT**
pin also gives a digital "target present" level.

| C4002 | ESP32-C6 (menuconfig default, UNCONFIRMED) |
|---|---|
| TX | `RB_C4002_RX_GPIO` = GPIO17 |
| RX | `RB_C4002_TX_GPIO` = GPIO16 |
| OUT (optional) | `RB_C4002_OUT_GPIO` = not wired |
| VCC / GND | 5 V / GND |

The pins are placeholders. Set them in `idf.py menuconfig` → *RB4107 configuration*
→ *Sensor node* → *C4002 presence sensor*.

## What the driver provides

- `presence_reading_t`: `valid`, `presence_detected`, `moving_target`,
  `stationary_target`, `distance_m`, `timestamp_ms`. The C4002 reports all of them.
- Validity: checksum and length errors are counted and dropped, unknown
  target states or impossible distances make a reading invalid, and a
  reading goes **invalid** after `RB_C4002_STALE_TIMEOUT_MS` without a report.
  Invalid never turns into "no presence".
- Configurable detection parameters: report period, range, gate resolution,
  motion/static sensitivity group, target-disappear delay, and environment
  calibration at boot.

## Running

```bash
idf.py build flash monitor
```

The log shows a line on every state change (`NONE -> STATIONARY at 1.20 m`) and,
every 5 s, the raw report plus parser statistics.

## Investigating false static-presence detections

Things to try, in order. Record what you observe for each.

0. **Make sure the readings are new.** In DFRobot's Arduino driver,
   `getNoteInfo()` declares its return value without initialising it
   (`sRetResult_t ret;`). When no frame has arrived, `noteType` is whatever
   was left on the stack, often `eResult` from the previous call. So the
   `getAllResults` example keeps printing the *last* result again and again,
   as if the sensor kept reporting the same thing. This driver only takes a
   reading from a complete frame that passed its checksum. Every reading
   has an age, and a reading older than `RB_C4002_STALE_TIMEOUT_MS` is
   invalid. In this project's log, `age=` must stay below the report period
   and `frames=` must keep going up. If `frames=` stops, the sensor has
   stopped sending (check wiring, baud rate, report period).
1. **Parse the report correctly.** DFRobot's Arduino driver `memcpy()`s the
   18-byte result into a naturally aligned struct, which garbles every field
   after `target_state` on 32-bit MCUs (the Python driver parses it packed).
   This driver parses it packed. If earlier tests used the Arduino driver,
   their distance and energy values were wrong.
2. **Hold time.** `RB_C4002_DISAPPEAR_DELAY_S` and the `hold=` countdown in the
   summary: a long hold looks like someone "still being there".
3. **Range.** Reduce `RB_C4002_RANGE_MAX_CM` to the cooking area, so reflections
   from far walls or other rooms drop out.
4. **Sensitivity.** Lower `RB_C4002_PRESENCE_SENSITIVITY`.
5. **Environment calibration.** Enable `RB_C4002_ENV_CALIBRATION_AT_BOOT` and
   leave the room empty for the calibration period.
6. **Which gates fire.** `gates=` in the summary is a bitmask of the distance
   gates holding a static target. Fixed gates firing with the room empty point
   to a static reflector (fridge, extractor fan, metal pans).
