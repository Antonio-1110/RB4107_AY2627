# MQTT topics and JSON schema (schema_version 2)

Produced by [`firmware/components/rb_json`](../firmware/components/rb_json).
Machine-readable: [`schema/rb4107_mqtt.schema.json`](schema/rb4107_mqtt.schema.json).
Check any capture with `tools/diagnostics/validate_json.py`.

## Topics

Topics are defined in [`firmware/components/rb_topics`](../firmware/components/rb_topics).
The prefix is configurable (`RB_MQTT_TOPIC_PREFIX`). The controller uses
`rb4107/controller_01`, one tree per controller, so read `rb4107/` below as
`rb4107/<controller_id>/`. Node names are
`node_<id>` with the ID zero-padded to two digits. With the default IDs:
`node_01` and `node_02` are the presence nodes, `node_03` the thermal node.

```text
rb4107/
├── controller/
│   ├── status      retained online/offline (Last Will)
│   ├── heartbeat   liveness
│   ├── state       full telemetry snapshot (see below)
│   ├── faults      active fault list
│   ├── log         the controller's log lines (dashboard Logs tab)
│   └── command     dashboard → controller commands (subscribed)
├── sensors/
│   └── <node>/
│       ├── presence
│       ├── thermal
│       ├── status        node link state
│       ├── c4002_config  presence node radar settings / tuning result
│       ├── c4002_live    raw radar results for the live view
│       ├── c4002_set     dashboard → controller tuning command (subscribed)
│       └── thermal_frame heat-map picture (thermal node, display only)
└── events/
    ├── warning
    ├── shutdown
    └── fault
```

| Topic | QoS | Retained | When |
|---|---|---|---|
| `controller/status` | 1 | yes | on connect (`online: true`); Last Will (`online: false`) |
| `controller/heartbeat` | 0 | no | every telemetry period |
| `controller/state` | 0 | yes | every telemetry period, and right away on every state transition |
| `controller/faults` | 1 | yes | on every fault raised/cleared |
| `controller/log` | 0 | no | batches of the controller's log lines while the broker is connected, plus the log levels every 30 s and on change; off with `RB_MQTT_PUBLISH_LOG` ([logging.md](logging.md)) |
| `controller/command` | 1 | no | published by Django when the dashboard's reset button is pressed; the controller subscribes ([remote_reset.md](remote_reset.md)) |
| `sensors/<node>/presence` | 0 | no | every telemetry period, for each presence node (`node_01`, `node_02`) |
| `sensors/<node>/thermal` | 0 | no | every telemetry period, for the thermal node (`node_03`) |
| `sensors/<node>/thermal_frame` | 0 | no | each complete heat-map picture from the thermal node, about every 3 s (`RB_THERMAL_HEATMAP_PERIOD_MS`); off with `RB_MQTT_PUBLISH_HEATMAP` |
| `sensors/<node>/status` | 1 | yes | on node ONLINE/STALE/OFFLINE and sensor validity changes |
| `sensors/<node>/c4002_config` | 1 | yes | when a presence node answers a tuning command, or a command fails ([c4002_tuning.md](c4002_tuning.md)) |
| `sensors/<node>/c4002_live` | 0 | no | each new raw C4002 result, a few per second, for the dashboard's live radar view |
| `sensors/<node>/c4002_set` | 1 | no | published by Django; the controller subscribes to `sensors/+/c4002_set` |
| `events/warning` | 1 | no | entering WARNING |
| `events/shutdown` | 1 | no | entering SHUTDOWN |
| `events/fault` | 1 | no | fault raised or cleared (incl. node offline/restored); entering the FAULT state |

Retained topics let a subscriber that starts later (e.g. Django) see the
current status, state and faults straight away.

Subscribe to everything: `mosquitto_sub -h <broker> -t 'rb4107/#' -v`
(or `tools/mqtt/watch.sh`).

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
  "protocol_version": 3,
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
             "warning_after_ms": 150000, "shutdown_after_ms": 300000, "shutdown_counts_from": "UNATTENDED",
             "test_timers": false, "hold": "none", "tier": "tier1",
             "effective_warning_ms": 150000, "effective_shutdown_ms": 300000,
             "tiers": {"levels": [{"tier": 2, "temp_c": 100.0, "warning_ms": 120000, "shutdown_ms": 240000},
                                  {"tier": 3, "temp_c": 150.0, "warning_ms": 90000, "shutdown_ms": 180000},
                                  {"tier": 4, "temp_c": 200.0, "warning_ms": 60000, "shutdown_ms": 120000},
                                  {"tier": 5, "temp_c": 250.0, "warning_ms": 30000, "shutdown_ms": 60000}],
                       "rate": true, "source": "menuconfig"},
             "presence_unknown": false, "thermal_lost": false, "absent_ms": 0, "returning_ms": 0,
             "presence_filter": {"absence_ms": 2000, "return_ms": 5000, "return_gap_ms": 1000,
                                 "fusion": "all", "pause_budget_cfg_ms": 30000, "pause_budget_pct": 25,
                                 "attendance_source": "menuconfig",
                                 "source": "menuconfig"},
             "cooking": {"active": true, "phase": "holding", "rate_c_per_min": 0.1, "settle_c": 98.7,
                         "room_c": 31.5, "on_c": 50.0, "off_c": 40.0, "source": "menuconfig"},
             "cookware": {"profile": "mixed", "tau_s": 600, "source": "menuconfig"},
             "params": {"shutdown_timing": 0, "cooling_policy": 1, "fault_shutdown_ms": 90000, "...": 0},
             "params_source": "menuconfig"},
  "faults": ["mqtt_disconnected"]
}
```

- `presence_unknown` and `thermal_lost` are the sensor flags (a lost sensor
  does not put the controller in `FAULT`; see
  [safety_state_machine.md](safety_state_machine.md#lost-sensors-flags-not-fault)).
  `cooking.active` is the heat detector's state. `absent_ms` / `returning_ms`
  are the progress of the absence and return debounce (0 = not running).
  `tier` is the temperature tier in force (`tier1` = the base times ... `tier5`);
  `effective_warning_ms` / `effective_shutdown_ms` are the buzzer and shutdown
  times in force now, never longer than the base `warning_after_ms` /
  `shutdown_after_ms`, and `tiers` holds the settings of tiers 2 to 5 and whether
  a rising pan gets the tier it is heading for (`rate`; `null` if tiers are off).
  `hold` is what holds a due step back: `none`, `return`, `cooling`, `falling` or `checking` (in MONITORING: the countdown waits to start while the pan may have been switched off). See [safety_state_machine.md](safety_state_machine.md#temperature-tiers-the-hotter-the-pan-the-shorter-the-time).
  `cooking.phase` is the judgment from the temperature trend (`off`, `checking`,
  `heating`, `holding`, `cooling`), with the fitted `rate_c_per_min`, its standard
  error `rate_err_c_per_min`, the fall `cool_rate_c_per_min` that counts as only
  cooling, and where the temperature is heading (`settle_c`, noisy, used only to
  check the profile fits), against the room temperature in use
  (`room_c`: the thermal camera's 10th-percentile pixel, kept between 30 and 45 °C,
  38 °C without a reading); `cookware` is the profile that sets the
  cooling time constant; `hold` says why a due step is held back
  (`none`, `return`, `cooling`, `falling`, `checking`). See
  [safety_state_machine.md](safety_state_machine.md#holds-a-due-buzzer-or-shutdown-that-waits).
  The controller publishes `controller/state` immediately whenever presence,
  cooking or one of these flags changes (at most every 200 ms), in addition to
  the periodic message; all four fields are absent from older firmware.
- `params` is every other safety setting in force, keyed and in the units of
  [`rb_params.def`](../firmware/components/rb_params/include/rb_params.def) (the
  dashboard's Other settings; see [remote_reset.md](remote_reset.md#other-safety-settings)),
  with `params_source` `dashboard` or `menuconfig`. Absent from older firmware.
- `nearby` is somebody near either radar. `presence_state` is *at the stove*: with
  two radars and `presence_filter.fusion` `all`, PRESENT only when every working
  radar sees someone. `safety.paused`, `pause_used_ms` and `pause_budget_ms` say
  whether the unattended clock is stopped for someone at the stove, and how much
  of the pause budget is used (see
  [safety_state_machine.md](safety_state_machine.md#who-is-at-the-stove-and-the-pause)).
- `presence_state` is the two radars combined, exactly as the safety state
  machine used it (see [safety_rules.md](safety_rules.md), A2-A5).
- `nodes` lists every configured sensor node. `detected` is only set for a
  presence node with a valid reading; it is `null` for the thermal node and
  for any node that is invalid, STALE or OFFLINE.
- `thermal` is the thermal node's reading.
- `espnow_channel` is the radio channel ESP-NOW uses right now: the
  router's channel on Wi-Fi (the nodes follow it), `RB_ESPNOW_CHANNEL` only
  with the unused Ethernet options. `null` if unknown.
- `safety.reset_required` is true while the supply is cut and latched: only an
  operator reset restores it. `warning_after_ms` and `shutdown_after_ms` are the
  timers in use (the test timers when `test_timers` is true);
  `shutdown_counts_from` says whether the shutdown time counts from the start
  of UNATTENDED or from entering WARNING.
- `safety.presence_filter` is the presence filter in use: absence needed before
  UNATTENDED, presence needed to cancel UNATTENDED/WARNING, and the "absent"
  gaps that don't restart that count. `source` is `dashboard` after a
  `presence_filter` command, otherwise `menuconfig`; that command also sets
  `warning_after_ms` and `shutdown_after_ms`
  ([remote_reset.md](remote_reset.md#presence-filter-and-timers)). Older firmware leaves
  it out.
- `safety.cooking` is the cooking temperature in use: IDLE → MONITORING when
  the hottest pixel (`thermal.max_c`) reaches `on_c`, back to IDLE below
  `off_c`. `source` is `dashboard` after a `cooking_temp` command (saved on the
  controller), otherwise `menuconfig`
  ([remote_reset.md](remote_reset.md#cooking-temperature)). Older firmware
  leaves it out.

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
  "safety": {"state": "WARNING", "from_state": "UNATTENDED", "reason": "unattended timeout", "unattended_ms": 180000},
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
| `controller_log` | `controller/log` | `dropped` (lines lost since boot), `levels{"*": default, <tag>: level}`, `lines: [{uptime_ms, level (E W I D V), tag, msg}]` ([logging.md](logging.md)) |
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
  (`RB_THERMAL_HOT_PIXEL_THRESHOLD_DC`, or the cooking temperature the
  controller forwarded, and `RB_THERMAL_HOT_REGION_RADIUS`). The dashboard
  outlines pixels against the controller's `safety.cooking.on_c` when the
  controller reports it, and against `hot_threshold_c` otherwise.
- `number` is the thermal node's picture counter; it restarts when the node
  reboots. The header's `uptime_ms` and `timestamp` are when the controller
  received the picture.
- Not retained: a late subscriber waits a few seconds for the next picture.
