# Thermal node: ESP32-C6 + one MLX90640 (system firmware)

> **System firmware: flash this on the thermal C6 board** (node ID 3).
> Together with [`presence_node`](../presence_node) on the two
> presence boards and [`controller`](../controller) on the S3, this is
> the running system.

Target: a **DFRobot DFR1117 ESP32-C6 mini** wired to one
**MLX90640** (32×24) thermal camera looking at the hob.

```text
MLX90640 → C6 → feature extraction → ESP-NOW (THERMAL_DATA) → S3 controller
```

## What it does

| Task | Priority | Job |
|---|---|---|
| `thermal` | 5 | reads MLX90640 frames and extracts features (max/min/mean, hot-region temperature, rate of change, pixels above threshold). If the sensor is missing it keeps retrying without blocking the link |
| `espnow_link` | 6 | every 50 ms: builds the fault flags, sends SENSOR_FAULT on change, THERMAL_DATA every `RB_NODE_DATA_PERIOD_MS`, HEARTBEAT every `RB_NODE_HEARTBEAT_PERIOD_MS` (component `rb_node_app`) |

The safety logic only gets the features. Separately, for the dashboard heat
map, the link task sends the whole picture at 1 byte per pixel every
`RB_THERMAL_HEATMAP_PERIOD_MS` (default 3 s) as 4 THERMAL_FRAME pieces, one
per tick ([protocol.md](../../docs/protocol.md#thermal_frame-type-10-224-bytes-total)).
Turn it off with `RB_THERMAL_HEATMAP`. The node does not decide anything: the
controller runs the safety state machine. Every packet carries the node ID
and the role "thermal".

Board checks are the same as on the presence node: boot report,
periodic uptime/heap/ESP-NOW line, blinking status LED.

Pins (menuconfig → *RB4107 configuration → Sensor node → MLX90640 thermal
sensor*) default to SDA IO6 / SCL IO7, confirmed on the bench (see
[project `testing/mlx90640`](../testing/mlx90640/README.md)).

## Setup

1. Flash project `controller` on the S3 (or `testing/s3_board` during bring-up) and copy the MAC
   address it prints.
2. `idf.py menuconfig` → *RB4107 configuration*:
   - *Sensor node* → `RB_NODE_CONTROLLER_MAC` = that MAC (the node ID is
     already 3 from `sdkconfig.defaults`),
   - *ESP-NOW link* → `RB_ESPNOW_CHANNEL` = same value as on the S3 (the
     node starts there and follows the S3 if it moves to a Wi-Fi router's
     channel).
3. `idf.py -p <PORT> flash monitor`

To test the camera on its own first (I2C pins, frames, readings at different
distances and heat sources), use [`testing/mlx90640`](../testing/mlx90640).
