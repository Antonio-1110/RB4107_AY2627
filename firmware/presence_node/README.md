# Presence node: ESP32-C6 + one C4002 (system firmware)

> **System firmware: flash this on BOTH presence C6 boards** (node ID 1 and
> node ID 2). Together with [`thermal_node`](../thermal_node)
> and [`controller`](../controller) on the S3, this is the running
> system.

Target: a **DFRobot DFR1117 ESP32-C6 mini** wired to one
**DFRobot C4002** radar. The system has two of these boards; they run the same
firmware and differ only in their node ID.

```text
C4002 #1 → C6 (node 1) ─┐
C4002 #2 → C6 (node 2) ─┼─ ESP-NOW → S3 controller
MLX90640 → C6 (node 3) ─┘   (thermal node: project `thermal_node`)
```

## What it does

| Task | Priority | Job |
|---|---|---|
| `c4002_rx` | 6 | parses C4002 UART reports (component `c4002`) |
| `espnow_link` | 6 | every 50 ms: builds the fault flags, sends SENSOR_FAULT on change, PRESENCE_DATA every `RB_NODE_DATA_PERIOD_MS`, HEARTBEAT every `RB_NODE_HEARTBEAT_PERIOD_MS` (component `rb_node_app`) |
| `c4002_tuning` | 4 | radar settings and calibration sent from the dashboard; saves them in NVS (component `rb_c4002_remote`, [docs/c4002_tuning.md](../../docs/c4002_tuning.md)) |

At boot the radar gets the settings saved from the dashboard if there are
any, otherwise the menuconfig ones. The node only reads the radar and
transmits. It does not decide anything: the
controller combines both radars (a person seen by either one counts as
present) and runs the safety state machine. Every packet carries the node ID
and the role "presence", so the controller rejects a board flashed with the
wrong firmware or ID.

Board checks: at boot the node prints its node ID, chip revision,
Wi-Fi MAC (the ESP-NOW source address) and reset reason. A `brownout` or
`watchdog` reset points to wiring or power problems. Every
`RB_NODE_HEALTH_LOG_PERIOD_MS` it logs uptime, free heap, ESP-NOW delivery
counts and the current reading. The status LED (`RB_NODE_STATUS_LED_GPIO`)
blinks while it runs.

Pins (menuconfig → *RB4107 configuration → Sensor node → C4002 presence
sensor*) default to the DFR1117 board: C4002 TX → IO4, C4002 RX → IO5 (header
P3, confirmed on the bench), status LED IO15. See
[project `testing/c4002`](../testing/c4002/README.md#interface).

## Setup

1. Flash project `controller` on the S3 (or `testing/s3_board` during bring-up) and copy the MAC
   address it prints.
2. **Presence node A (node ID 1):**
   ```bash
   idf.py menuconfig   # Sensor node → RB_NODE_CONTROLLER_MAC = that MAC; ESP-NOW link → same channel as the S3
   idf.py -p <PORT> flash monitor
   ```
3. **Presence node B (node ID 2):** same firmware, built with
   `sdkconfig.node_b` on top and its own build folder, so A and B never share
   a configuration:
   ```bash
   idf.py -B build_b -D SDKCONFIG=build_b/sdkconfig \
          -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node_b" menuconfig   # same MAC and channel as node A
   idf.py -B build_b -D SDKCONFIG=build_b/sdkconfig \
          -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node_b" -p <PORT> flash monitor
   ```
   The boot log must say `presence node (C4002), node ID 2`.

If you see `controller not acknowledging` in the log, the node searches the
other channels for the S3 (`controller found on channel N` when it does). If it
keeps failing, the MAC address is wrong or the S3 isn't running. If the controller logs
`dropped packet from node_NN`, that node ID isn't configured on the controller
(menuconfig → *Sensor node link* in project `controller`).

To test the radar on its own first (UART pins, readings, false detections),
use [`testing/c4002`](../testing/c4002).
