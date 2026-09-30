# Test: ESP32-S3 board bring-up

> **Test only, not part of the running system.** It checks the S3 board on its own. Flashing it
> replaces the system firmware on that board. To run the system, flash
> [`presence_node`](../../presence_node) on the two presence C6 boards,
> [`thermal_node`](../../thermal_node) on the thermal C6 board and
> [`controller`](../../controller) on the S3.

Target: **Waveshare ESP32-S3-POE-ETH-8DI-8DO** (ESP-IDF + FreeRTOS).

Board bring-up only:

- prints chip information and the **Wi-Fi STA MAC** (copy it into the sensor
  node's `RB_NODE_CONTROLLER_MAC`),
- scans the board I2C bus (SDA 42 / SCL 41) and names the expected devices:
  `0x20` TCA9554 relay expander, `0x51` PCF85063 RTC,
- logs uptime and heap every `RB_S3_HEALTH_LOG_PERIOD_MS`.
