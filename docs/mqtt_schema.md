# MQTT JSON schema (schema_version 2)

Produced by [`firmware/components/rb_json`](../firmware/components/rb_json).
Machine-readable: [`schema/rb4107_mqtt.schema.json`](schema/rb4107_mqtt.schema.json).
Check any capture with `tools/diagnostics/validate_json.py`.

## Rules

- Every message has `schema_version`, `type`, `controller_id`, `boot_id`,
  `timestamp`, `uptime_ms` and `sequence`, except `controller_status`, which
  has only `schema_version`, `type`, `controller_id`, `boot_id` and `online`.
- `boot_id` is 8 random hex characters chosen at every controller boot. With
  `sequence` it orders messages without a wall clock: same `boot_id` and a
  lower `sequence` means an older message; a new `boot_id` means a reboot.
  It is optional in the JSON Schema only so captures from earlier firmware
  still validate.
- `timestamp` is ISO 8601 with offset (`2026-09-26T00:00:00+08:00`). It is
  `null` while the controller's wall clock is unset (no RTC time and no SNTP
  yet). `uptime_ms` is always there.
- `sequence` counts every message a controller publishes, starting again at
  each boot. A gap means lost QoS 0 messages.
- **Unknown is `null`.** An invalid or missing presence reading has
  `"valid": false` and `"detected": null`, never `false`. Missing sensor data
  is not "nobody there". The combined `presence_state` is `UNKNOWN` in that
  case, never `ABSENT`.
- Readers must ignore fields they don't know. New sensors or fields can be
  added without bumping `schema_version`. Removing or changing the meaning of
  a field requires a bump.
- `protocol_version` in telemetry is the ESP-NOW protocol version the sensor
  data arrived with (see [protocol.md](protocol.md)).
- Version 2 (three sensor nodes) replaced the single node's `sensor_node`,
  `node_link` and `presence` in `telemetry` with `presence_state` and
  `nodes`, and `presence_valid` / `thermal_valid` in `node_status` with
  `role` and `valid`.

## `telemetry` (topic `controller/state`)

```json
{
  "schema_version": 2,
  "type": "telemetry",
  "controller_id": "controller_01",
  "boot_id": "3f9a01c2",
  "timestamp": "2026-09-26T00:00:00+08:00",
  "uptime_ms": 18400,
  "sequence": 4127,
  "protocol_version": 2,
  "espnow_channel": 1,
  "presence_state": "ABSENT",
  "nodes": [
    {"sensor_node": "node_01", "role": "presence", "link": "ONLINE", "valid": true, "detected": false},
    {"sensor_node": "node_02", "role": "presence", "link": "ONLINE", "valid": true, "detected": false},
    {"sensor_node": "node_03", "role": "thermal", "link": "ONLINE", "valid": true, "detected": null}
  ],
  "thermal": {"valid": true, "max_c": 84.2, "min_c": 21.0, "mean_c": 42.8, "hot_region_c": 80.1,
              "rate_c_per_min": 1.7, "pixels_above_threshold": 37},
  "safety": {"state": "UNATTENDED", "state_ms": 16400, "unattended_ms": 18400,
             "buzzer": "OFF", "shutdown": false, "reset_required": false,
             "warning_after_ms": 60000, "shutdown_after_ms": 90000, "shutdown_counts_from": "UNATTENDED",
             "test_timers": false},
  "faults": ["mqtt_disconnected"]
}
```

- `presence_state` is the two radars combined, exactly as the safety state
  machine used it: `PRESENT` if either radar sees a person, `ABSENT` only if
  both give a valid "absent", `UNKNOWN` otherwise.
- `nodes` lists every configured sensor node. `detected` is only set for a
  presence node with a valid reading; it is `null` for the thermal node and
  for any node that is invalid, STALE or OFFLINE.
- `thermal` is the thermal node's reading.
- `espnow_channel` is the radio channel ESP-NOW uses right now: always
  `RB_ESPNOW_CHANNEL` on Ethernet, the router's channel on Wi-Fi (the nodes
  follow it). `null` if unknown.
- `safety.reset_required` is true while the supply is cut and latched: only an
  operator reset restores it. `warning_after_ms` and `shutdown_after_ms` are the
  timers in use (the test timers when `test_timers` is true);
  `shutdown_counts_from` says whether the shutdown time counts from the start
  of UNATTENDED or from entering WARNING.

## `event` (topics `events/warning`, `events/shutdown`, `events/fault`)

`event` is one of:

| `event` | Topic | When |
|---|---|---|
| `warning` | `events/warning` | the controller entered WARNING |
| `shutdown` | `events/shutdown` | the controller entered SHUTDOWN (relay activated) |
| `state_change` | `events/fault` | the controller entered the FAULT state |
| `fault_raised` / `fault_cleared` | `events/fault` | an individual fault changed (`fault` names it) |

Every state transition also publishes a fresh `telemetry` message on
`controller/state` straight away, at QoS 1.

Events that happen before the first broker connection (the boot-time "no
data yet" faults) are not replayed. The retained `controller/faults` message
always holds the current set.

```json
{
  "schema_version": 2, "type": "event", "controller_id": "controller_01",
  "timestamp": "2026-09-26T00:01:00+08:00", "uptime_ms": 78400, "sequence": 4128,
  "event": "warning",
  "safety": {"state": "WARNING", "from_state": "UNATTENDED", "reason": "unattended timeout", "unattended_ms": 60000},
  "fault": null
}
```

For fault events, `fault` is `{"name": "mqtt_disconnected", "class": "TELEMETRY"}`.

## Other types

| `type` | Topic | Extra fields |
|---|---|---|
| `heartbeat` | `controller/heartbeat` | `safety_state`, `safety_loop_count` (goes up while the safety task is alive) |
| `faults` | `controller/faults` | `faults: [{name, class}]` (active faults) |
| `presence` | `sensors/<node>/presence` (each presence node) | `sensor_node`, `presence{valid, detected, moving, stationary, distance_m}` |
| `thermal` | `sensors/<node>/thermal` (the thermal node) | `sensor_node`, `thermal{...}` |
| `thermal_frame` | `sensors/<node>/thermal_frame` (the thermal node) | `sensor_node`, `frame{...}` (below) |
| `node_status` | `sensors/<node>/status` | `sensor_node`, `role` (`presence` / `thermal`), `link`, `valid`, `node_fault_flags`, `missed_packets`, `restarts` |
| `controller_status` | `controller/status` | `boot_id`, `online` (retained; the Last Will publishes `false`) |
| `c4002_live` | `sensors/<node>/c4002_live` (presence nodes) | `sensor_node`, `node_uptime_ms`, `results`, `age_ms`, `target`, `gate_size_cm`, `presence_gates[]`, `presence{distance_cm, energy, countdown_s}`, `motion{distance_cm, speed_cm_s, energy, direction}`, `light_lux`, `calibration_remaining_s` |
| `c4002_config` | `sensors/<node>/c4002_config` (presence nodes) | `sensor_node`, `request_id`, `action`, `result`, `error`, `calibration_remaining_s`, `saved`, `settings{...}` or null ([c4002_tuning.md](c4002_tuning.md)) |

The one message the controller receives, `c4002_command` on
`sensors/<node>/c4002_set`, is also in the schema file; see
[c4002_tuning.md](c4002_tuning.md).

## `thermal_frame` (topic `sensors/<node>/thermal_frame`)

One MLX90640 picture for the dashboard heat map, about every 3 s. Display
only: the safety decision uses the `thermal` values, never this. About 1.3 KB.

```json
{
  "schema_version": 2, "type": "thermal_frame", "controller_id": "controller_01",
  "boot_id": "3f9a01c2", "timestamp": null, "uptime_ms": 51310, "sequence": 312,
  "sensor_node": "node_03",
  "frame": {"number": 17, "width": 32, "height": 24, "base_c": 24.00, "step_c": 0.56,
            "invalid": 255, "hot_threshold_c": 50.0, "hot_region_radius": 1,
            "encoding": "u8_base64", "pixels": "/wAAAAAA...(1024 characters)"}
}
```

- `pixels` is base64 of `width × height` bytes, row by row. Pixel value `v` is
  `base_c + v × step_c` °C; the value `invalid` (255) means no reading.
- `hot_threshold_c` and `hot_region_radius` are the node's own settings
  (`RB_THERMAL_HOT_PIXEL_THRESHOLD_DC`, `RB_THERMAL_HOT_REGION_RADIUS`), so the
  dashboard can outline the pixels and region its `thermal` values come from.
- `number` is the thermal node's picture counter; it restarts when the node
  reboots. The header's `uptime_ms` and `timestamp` are when the controller
  received the picture.
- Not retained: a late subscriber waits a few seconds for the next picture.
