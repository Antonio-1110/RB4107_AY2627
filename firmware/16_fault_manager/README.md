# 16 – Fault manager

TODO section 16. Target: **ESP32-S3**, optionally paired with project 05 on the C6.

`components/fault_manager` is the one list of faults. `components/rb_app`
(`rb_app_faults.c`) raises and clears them from the subsystems. Every change
is logged and posted as an `RB_EVT_FAULT` event, which is published over
MQTT from section 22 on.

| Fault | Class | Raised by |
|---|---|---|
| `c4002_unavailable` | **SAFETY** | node reports presence invalid/missing |
| `mlx90640_unavailable` | **SAFETY** | node reports thermal invalid/missing |
| `sensor_node_offline` | **SAFETY** | no packet for `RB_CTRL_NODE_OFFLINE_MS` |
| `shutdown_output_failure` | **SAFETY** | relay expander write/read-back failure |
| `self_test_failed` | **SAFETY** | outputs not usable at boot |
| `espnow_invalid_packet` | telemetry | bad length/magic/version/type/CRC counters rising |
| `espnow_link_degraded` | telemetry | node STALE |
| `rtc_failure` | telemetry | PCF85063 not responding |
| `network_disconnected` | telemetry | Ethernet/Wi-Fi down (section 17) |
| `mqtt_disconnected` | telemetry | broker unreachable (section 19) |
| `rx_queue_overflow` | telemetry | ESP-NOW packets dropped before the safety task saw them* |
| `event_queue_overflow` | telemetry | telemetry events dropped |

\* A dropped sensor packet on its own is not a safety problem. If packets stop
arriving, node health (STALE/OFFLINE) catches it, and that is safety-class.

**Only SAFETY faults feed the state machine** (`safety_inputs_t.safety_fault`),
which puts it in FAULT. A telemetry fault such as *MQTT disconnected* is
logged and reported but never changes the safety state. The demo shows this
every 15 s.
