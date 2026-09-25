# 06 – ESP32-S3 central controller base project

TODO section 6. Target: **Waveshare ESP32-S3-ETH-8DI-8RO** (ESP-IDF + FreeRTOS).

Board bring-up only:

- prints chip information and the **Wi-Fi STA MAC** (copy it into the sensor
  node's `RB_NODE_CONTROLLER_MAC`),
- scans the board I2C bus (SDA 42 / SCL 41) and names the expected devices:
  `0x20` TCA9554 relay expander, `0x51` PCF85063 RTC,
- logs uptime and heap every `RB_S3_HEALTH_LOG_PERIOD_MS`.

## Code structure

The TODO suggests one `main/` tree with sub-folders. ESP-IDF components
separate things more cleanly: each has its own dependencies, is unit-testable,
and is shared by all the section projects. The mapping is:

| Suggested folder | Component(s) in `firmware/components` |
|---|---|
| `safety/` | `safety` (state machine, pure C) |
| `communication/` | `rb_protocol`, `rb_espnow`, `rb_mqtt`, `rb_telemetry` |
| `sensors/` | `sensor_node` (per-node state and health) |
| `hardware/` | `rb_board_s3`, `buzzer`, `shutdown_output`, `rb_time` |
| `system/` | `fault_manager`, `rb_controller` (tasks and queues) |
| `config/` | `rb_config` (menuconfig: pins and all tunables) |

Each later section adds its components; project 29 wires them all together.
