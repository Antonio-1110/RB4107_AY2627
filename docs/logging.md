# Logging (TODO section 25)

## Format

Firmware uses ESP-IDF logging, one tag per subsystem:

```text
I (12835) SAFETY: MONITORING -> UNATTENDED (person absent)
W (70935) SAFETY: UNATTENDED -> WARNING (unattended timeout)
W (100935) OUTPUT: shutdown relay 1 ACTIVATED
W (142435) SENSOR: node_01 STALE
W (150435) FAULT: SAFETY fault RAISED: sensor_node_offline (detail 0)
W (8742) MQTT: broker disconnected; retrying every 5000 ms
I (21282) MQTT: broker connected (mqtt://192.168.1.127:1883)
E (2034) THERMAL: frame read failed (ESP_ERR_TIMEOUT), 1 so far
```

The Django subscriber uses the same shape: `[MQTT][WARNING] broker disconnected ...`.

## Tags

| Tag | Subsystem | Typical lines |
|---|---|---|
| `SAFETY` | state machine / safety task | every transition with its reason; config changes |
| `OUTPUT` | buzzer, shutdown relay | pattern changes, relay activated/released, read-back faults |
| `SENSOR` | controller view of the node, sensor-node glue | ONLINE/STALE/OFFLINE, presence/thermal lost/restored |
| `FAULT` | fault manager | every fault raised/cleared, with class |
| `ESPNOW` | ESP-NOW link (node and controller) | tx/rx statistics summaries, sequence anomalies |
| `C4002`, `PRESENCE` | C4002 driver / project 02 | settings applied, command failures, readings |
| `THERMAL` | MLX90640 driver and features | init, frame errors (rate-limited), feature summaries |
| `NET`, `WIFI` | network | link up/down, IP address, reconnect back-off |
| `MQTT` | MQTT client, telemetry | connection state changes, oversized payloads |
| `RTC` | wall clock | restore from RTC, SNTP sync, RTC failure |
| `NODE`, `CONTROLLER`, `BOARD` | boot and health lines | chip/MAC info, periodic heap/uptime |
| `SIM` | simulated sensor node | scenario steps (diagnostics only) |
| `DIAG` | diagnostic console | command output |

## Rules

- **State changes and summaries, not loops.** Drivers log changes (e.g.
  `NONE -> STATIONARY`) and periodic summaries (every
  `RB_NODE_HEALTH_LOG_PERIOD_MS` / `RB_S3_HEALTH_LOG_PERIOD_MS`). Nothing
  logs on every 100 ms safety tick, every ESP-NOW packet or every thermal
  frame.
- **Rate-limit anything that can repeat.** Use
  `RB_LOG_EVERY_MS(ms, ESP_LOGW, TAG, ...)` from `components/rb_log`.
- **Never log from ISR or Wi-Fi callbacks.** The ESP-NOW receive callback
  only counts; the counters show up in the summaries.
- **Levels:** `E` = something is broken, `W` = safety-relevant or degraded
  (WARNING/SHUTDOWN/FAULT transitions, faults, disconnects), `I` = normal
  milestones, `D` = detail for debugging.

## Verbosity

| Where | How |
|---|---|
| Global default | `idf.py menuconfig` → Component config → Log → *Default log verbosity* |
| DEBUG available at runtime | set *Maximum log verbosity* to Debug (`CONFIG_LOG_MAXIMUM_LEVEL_DEBUG`), which projects 27/29/30 already do |
| Per tag at boot | menuconfig → *RB4107 configuration* → *Logging*: `RB_LOG_DEBUG_TAGS="ESPNOW,SAFETY"`, `RB_LOG_QUIET_TAGS="TELEMETRY"` |
| Per tag at runtime | diagnostic console (project 27): `log SAFETY debug` |
| Django | `RB4107_LOG_LEVEL=DEBUG python manage.py mqtt_subscriber` |
