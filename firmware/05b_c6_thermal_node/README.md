# 05b – ESP32-C6 thermal node (one MLX90640)

> **System firmware: flash this on the thermal C6 board** (node ID 3).
> Together with [`05a_c6_presence_node`](../05a_c6_presence_node) on the two
> presence boards and [`29_end_to_end`](../29_end_to_end) on the S3, this is
> the running system.

TODO sections 1, 3.1 and 5. Target: an **ESP32-C6 mini board** wired to one
**MLX90640** (32×24) thermal camera looking at the hob.

```text
MLX90640 → C6 → feature extraction → ESP-NOW (THERMAL_DATA) → S3 controller (29)
```

## What it does

| Task | Priority | Job |
|---|---|---|
| `thermal` | 5 | reads MLX90640 frames and extracts features (max/min/mean, hot-region temperature, rate of change, pixels above threshold). If the sensor is missing it keeps retrying without blocking the link |
| `espnow_link` | 6 | every 50 ms: builds the fault flags, sends SENSOR_FAULT on change, THERMAL_DATA every `RB_NODE_DATA_PERIOD_MS`, HEARTBEAT every `RB_NODE_HEARTBEAT_PERIOD_MS` (component `rb_node_app`) |

Raw frames never leave the node; only the features are sent. The node does
not decide anything: the controller runs the safety state machine. Every
packet carries the node ID and the role "thermal".

Board checks (section 1) are the same as on the presence node: boot report,
periodic uptime/heap/ESP-NOW line, blinking status LED.

Pins (menuconfig → *RB4107 configuration → Sensor node → MLX90640 thermal
sensor*) are placeholders, **UNCONFIRMED** for the C6 mini board.

## Setup

1. Flash project 29 on the S3 (or 06 during bring-up) and copy the MAC
   address it prints.
2. `idf.py menuconfig` → *RB4107 configuration*:
   - *Sensor node* → `RB_NODE_CONTROLLER_MAC` = that MAC (the node ID is
     already 3 from `sdkconfig.defaults`),
   - *ESP-NOW link* → `RB_ESPNOW_CHANNEL` = same value as on the S3.
3. `idf.py -p <PORT> flash monitor`

To test the camera on its own first (I2C pins, frames, readings at different
distances and heat sources), use project [03](../03_mlx90640_integration).
