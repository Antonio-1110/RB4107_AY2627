# 05 – ESP32-C6 sensor node (ESP-NOW sender)

TODO sections 1 and 5. Target: **FireBeetle 2 ESP32-C6** with the C4002 and
MLX90640. This is the **complete sensor-node firmware**.

```text
C4002 ──┐
        ├─→ C6 → ESP-NOW → S3
MLX90640┘
```

## Tasks

| Task | Priority | Job |
|---|---|---|
| `c4002_rx` | 6 | parses C4002 UART reports (component `c4002`) |
| `espnow_link` | 6 | every 50 ms: builds fault flags, sends SENSOR_FAULT on change, SENSOR_DATA every `RB_NODE_DATA_PERIOD_MS`, HEARTBEAT every `RB_NODE_HEARTBEAT_PERIOD_MS` |
| `thermal` | 5 | reads MLX90640 frames and extracts features. If the sensor is missing it keeps retrying without blocking the link |

The node never runs the safety state machine. The sender numbers every packet
with an incrementing sequence number. Send success/failure is tracked from
the ESP-NOW MAC-layer ACK and logged once per `RB_NODE_HEALTH_LOG_PERIOD_MS`.

## Board checks (section 1)

At boot the node prints its chip revision, Wi-Fi MAC (the ESP-NOW source
address) and reset reason. A `brownout` or `watchdog` reset points to wiring
or power problems. Every `RB_NODE_HEALTH_LOG_PERIOD_MS` it logs uptime and
free heap, and the status LED (`RB_NODE_STATUS_LED_GPIO`, UNCONFIRMED) blinks
while it runs. Logs go over the native USB-Serial/JTAG.

## Setup

1. Flash project 06 (or 29) on the S3 and copy the MAC address it prints.
2. `idf.py menuconfig` → *RB4107 configuration*:
   - *Sensor node* → `RB_NODE_CONTROLLER_MAC` = that MAC,
   - *ESP-NOW link* → `RB_ESPNOW_CHANNEL` = same value as on the S3.
3. `idf.py build flash monitor`

If you see `controller not acknowledging` in the log, the MAC address or the
channel is wrong, or the S3 isn't running.
