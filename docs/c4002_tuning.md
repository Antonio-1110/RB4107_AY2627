# Remote C4002 tuning

You can tune each presence node's C4002 radar from the dashboard's **Radar tuning** card:
range, gate size, sensitivity, hold and lock time, report period, turning
individual distance gates on or off, per-gate thresholds, and environment
calibration. Above the settings, a live view shows what the radar reports,
result by result, so the effect of a change shows at once. It's built for
demos and bench tuning (issue #20, false presence),
not for production: anyone who can reach the API or publish to the broker can
change a safety sensor.

```text
dashboard ── POST /api/devices/<controller>/nodes/<node>/c4002/ ──► Django
Django ── MQTT rb4107/sensors/<node>/c4002_set (QoS 1, not retained) ──► S3
S3 ── ESP-NOW C4002_CONFIG ──► presence node ── UART ──► C4002
presence node ── ESP-NOW C4002_CONFIG_ACK ──► S3
S3 ── MQTT rb4107/sensors/<node>/c4002_config (QoS 1, retained) ──► Django ──► dashboard

presence node ── ESP-NOW C4002_LIVE (each new result, ≤ 4/s) ──► S3
S3 ── MQTT rb4107/sensors/<node>/c4002_live (QoS 0) ──► Django ──► live view (polls every 0.5 s)
```

The safety state machine is never involved. A node keeps sending presence data
while it is reconfigured.

## What each part does

| Part | Code | Job |
|---|---|---|
| Dashboard | `frontend/js/tuning.js` | form, per-gate table, status of the last command |
| Django | `django/ingest/commands.py`, `views.c4002_command` | checks the command against the schema, publishes it |
| S3 | `rb_c4002_cmd` (parser), `rb_c4002_relay` | parses the JSON, sends it to the node's MAC, times out after 8 s, publishes the reply |
| Node | `rb_c4002_remote`, `rb_node_sensors` | merges, checks, applies, **saves in NVS**, answers |
| Shared | `rb_types/rb_c4002_params.h`, `rb_protocol` | one parameter struct and its limits, used everywhere |

The S3 learns each node's MAC from the packets it sends, so a node can only be
tuned after the controller has heard from it since it booted. When the
controller first hears a node, it asks for the node's settings, so the card
fills in without anyone pressing a button.

## Live view

For each result the C4002 reports its verdict (none, stationary, moving),
which gates hold a stationary target, the stationary target's distance and
energy, the moving target's distance, speed, energy and direction, and the
hold countdown. It does **not** report a per-gate energy, so the view shows
which gates fired, not how close each one was to its threshold.

The chart covers the last minute. Rows are distance (one per gate), orange
cells are gates holding a stationary target, and blue dots are the moving
target (bigger = more energy). Grey rows are gates turned off, dashed lines
are the range limits, and a purple line marks when new settings were
applied. The column on the right is the share of results in which each gate
fired.

To check for false positives, leave the room empty and watch which gates
fire. Then turn those gates off or raise their thresholds, and watch them go
quiet. Live results never reach the safety logic, which still uses the
node's filtered PRESENCE_DATA.

Django stores each result like any other message (about 4 rows a second per
node). `GET /api/devices/<controller>/nodes/<node>/c4002/live/?after=<id>`
returns up to the last minute, oldest first. Set `RB_C4002_LIVE_PERIOD_MS`
to 0 on the node to turn the stream off.

## Settings

| JSON key | Meaning | Range |
|---|---|---|
| `range_min_cm`, `range_max_cm` | detection range | 0–1100, min ≤ max |
| `resolution_cm` | gate size: 80 (15 gates, ~11 m) or 20 (25 gates, ~4.9 m) | 20, 80 |
| `motion_sensitivity`, `presence_sensitivity` | threshold group | `low` `mid` `high` `custom` |
| `disappear_delay_s` | how long a target is held after it leaves | 0–65535 |
| `lock_time_ds` | detection paused after occupied → empty, 0.1 s units | 2–100 |
| `report_period_ds` | result report period, 0.1 s units | 1–255 |
| `motion_gates`, `presence_gates` | 25 × 0/1, gate *i* on or off | |
| `motion_thresholds`, `presence_thresholds` | 25 × 0–99 per gate (higher = less sensitive); sending them sets that sensitivity to `custom` | |

Arrays always have 25 entries. With 80 cm gates only the first 15 are used.
`custom` is only accepted once the node knows thresholds for that type, from a
calibration, from **Read from sensor**, or from sending them.

## Actions

| `action` | What the node does |
|---|---|
| `apply` | merges the keys sent into its current settings, checks them, pushes everything to the C4002, saves in NVS. Nothing changes if a value is out of range. If the sensor refuses a command, the node re-pushes the previous settings. |
| `calibrate` | starts the C4002's environment calibration (`calibration_delay_s` 0–600, default 10; `calibration_duration_s` 1–600, default 30). When it ends, the node reads the learned thresholds back, switches both sensitivities to `custom`, saves, and reports again. |
| `read` | reads the thresholds the sensor uses now and reports the settings (not saved) |
| `reset` | erases the saved settings and goes back to the menuconfig values |

Saved settings win over menuconfig at every boot, until `reset`.

## The reply (`c4002_config`)

`result` is `ok`, `invalid` (a value was refused, see the node log),
`sensor_error` (the C4002 didn't accept a command), or one of the controller's:
`rejected` (bad command JSON, `error` says why), `unknown_node` (not heard
from since boot), `send_failed`, `no_reply` (8 s without an answer).
`settings` is the full current set, or `null` when the command never reached
the node. The dashboard keeps showing the last known settings in that case.
Full schema: `c4002_config` and `c4002_command` in
[schema/rb4107_mqtt.schema.json](schema/rb4107_mqtt.schema.json).

## Size limits

| Item | Size | Limit |
|---|---|---|
| ESP-NOW `C4002_CONFIG` | 102 B | 250 B (`_Static_assert` in `rb_protocol.h`) |
| ESP-NOW `C4002_CONFIG_ACK` | 96 B | 250 B |
| MQTT command (largest, all keys) | ~520 B | S3 MQTT receive buffer, 1024 B by default |
| MQTT `c4002_config` reply | ~880 B | telemetry payload buffer, 1536 B |
| ESP-NOW `C4002_LIVE` | 46 B, at most 4 a second | 250 B |
| MQTT `c4002_live` | ~470 B | telemetry payload buffer, 1536 B |
| Settings saved in NVS | 76 B | |
| RAM | node: one 4 KB task + a 4-entry queue (~0.5 KB); S3: ~1.1 KB of state, no new task | C6 and S3: 512 KB SRAM |

## Menuconfig

* `RB_C4002_REMOTE_TUNING` (node, default on): accept commands.
* `RB_CTRL_C4002_REMOTE_TUNING` (controller, default on): subscribe and relay.
* `RB_C4002_LOCK_TIME_DS`: new menuconfig default for the lock time.
* `RB_C4002_LIVE_PERIOD_MS` (node, default 250, 0 = off): the live view stream.

## Not verified yet

These come from DFRobot's Arduino library, not from a test on our sensor:
the byte layout of the per-gate commands (0x62, 0x63), the threshold
read-back, and the lock time command (0x85). Calibration writing its
thresholds into the sensor is also the library's description. The first
bench check is: apply, then **Read from sensor**, and see that the values come
back.
