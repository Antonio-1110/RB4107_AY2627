# Logging

## Format

Firmware uses ESP-IDF logging, one tag per subsystem:

```text
I (12835) SAFETY: MONITORING -> UNATTENDED (person absent)
W (70935) SAFETY: UNATTENDED -> WARNING (unattended timeout)
W (100935) OUTPUT: shutdown relay 1 ACTIVATED
W (142435) SENSOR: presence_b node_02 STALE
W (150435) FAULT: SAFETY fault RAISED: presence_b_node_offline (detail 2)
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
| `SENSOR` | controller view of the three nodes, sensor-node glue | `<slot> node_NN` ONLINE/STALE/OFFLINE, sensor reading lost/restored, packets from unknown nodes |
| `FAULT` | fault manager | every fault raised/cleared, with class |
| `ESPNOW` | ESP-NOW link (node and controller) | tx/rx statistics summaries, sequence anomalies |
| `C4002`, `PRESENCE` | C4002 driver / project `testing/c4002` | settings applied, command failures, readings |
| `THERMAL` | MLX90640 driver and features | init, frame errors (rate-limited), feature summaries |
| `NET`, `WIFI` | network | link up/down, IP address, reconnect back-off |
| `MQTT` | MQTT client, telemetry | connection state changes, oversized payloads |
| `RTC` | wall clock | restore from RTC, SNTP sync, RTC failure |
| `NODE`, `CONTROLLER`, `BOARD` | boot and health lines | chip/MAC info, periodic heap/uptime |
| `SIM` | simulated sensor nodes | start-up line naming the simulated nodes (diagnostics only) |
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
| DEBUG available at runtime | set *Maximum log verbosity* to Debug (`CONFIG_LOG_MAXIMUM_LEVEL_DEBUG`), which firmware `controller` already does |
| Per tag at boot | menuconfig → *RB4107 configuration* → *Logging*: `RB_LOG_DEBUG_TAGS="ESPNOW,SAFETY"`, `RB_LOG_QUIET_TAGS="TELEMETRY"` |
| Per tag at runtime, controller | dashboard → Tuning → **Logs** → *Log levels on the controller*, or the diagnostic console: `log SAFETY debug` |
| Django | `RB4107_LOG_LEVEL=DEBUG python manage.py mqtt_subscriber` |

A runtime change lasts until the controller reboots; it then starts again from
the menuconfig levels. `*` is the default level for every tag without its own.

## Logs tab

The dashboard's **Tuning → Logs** tab shows the controller's log lines without
a serial cable: the same lines the serial monitor shows, at the same per-tag
levels. The sensor nodes have no MQTT connection, so their logs stay on their
own serial ports.

```text
ESP_LOGx anywhere on the S3 ─► serial port (unchanged)
          └─► rb_log_capture: copy into a ring buffer (RB_MQTT_LOG_BUFFER_LINES, default 100)
                 └─► telemetry task: batches of up to 10 lines on <prefix>/controller/log (QoS 0)
                        └─► Django: LogLine rows (kept RB4107_RETENTION_LOG_DAYS, default 7)
                               └─► GET /api/devices/<id>/logs/ ─► Logs tab (polls every second)
```

- **Never in the way of safety.** The copy happens in the task that logged,
  without waiting: if another task is being copied at that moment, the line
  is dropped and counted. Only the telemetry task publishes. Lines logged while
  the broker is down wait in the buffer (the oldest are overwritten when it is
  full) and go out after reconnecting, so the lines around a disconnection
  are usually there.
- **What a message carries** (`controller_log` in
  [the schema](schema/rb4107_mqtt.schema.json)): the lines (`uptime_ms`,
  `level`, `tag`, up to 119 characters of message; non-ASCII becomes `?`),
  `dropped` (lines lost since boot) and `levels`, the levels in force. A
  message without lines goes out when the levels change, after reconnecting
  and every 30 s, so the tab always shows the levels in force.
- **Filters** (Show, Tag, Search) are applied by Django; *Follow new lines*
  keeps the newest line in view unless you scroll up. A dashed line marks a
  controller restart (new `boot_id`).
- **Changing a level** (a level menu in the table, or *Set level* for a tag
  not listed yet; *Back to menuconfig levels* undoes them all) sends
  `POST /api/devices/<id>/log-level/` with `{"tag": "SAFETY", "level": "debug"}`
  or `{"defaults": true}`. Django publishes a `controller_command` with action
  `log_level` (`log_tag`, `log_level`) or `log_level_defaults` on
  `<prefix>/controller/command`; the table updates when the controller's next
  log message confirms it. It changes only what is logged, never what the
  controller does. Turn it off with `RB_CTRL_REMOTE_LOG`.
- **ESP-MQTT's own tags** (`RB_MQTT_LOG_SKIP_TAGS`: `mqtt_client`, `outbox`,
  `transport_base`, `transport`, `esp-tls`) are never sent: at DEBUG, sending
  a line would log a line. They still show on the serial port.
- **Off:** `RB_MQTT_PUBLISH_LOG=n` (menuconfig → RB4107 configuration → MQTT broker (MacBook)).
  The tab then says no log message has arrived.
