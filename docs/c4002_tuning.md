# Remote C4002 tuning

You can tune each presence node's C4002 radar from the dashboard's **Radar tuning** card:
range, gate size, sensitivity, report period, turning
individual distance gates on or off, per-gate thresholds, and environment
calibration. Above the settings, a live view shows what the radar reports,
result by result, so the effect of a change shows at once. It's built for
demos and bench tuning (issue #20, false presence),
not for production: anyone who can reach the API or publish to the broker can
change a safety sensor.

**Send to** picks the target. With two or more presence nodes it starts on
**All radars**: every button (apply, read, reset, calibrate, gates,
thresholds) sends the same command to each node at once, and the status lists
each node's answer. The form, gate table and live view then show the first
node. Sending thresholds to all replaces what each node learned in its own
calibration, so the dashboard asks first. Pick a single node to tune it alone.

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
| `disappear_delay_s` | how long the radar keeps reporting a target after it leaves | 0–65535 s (default 1; try 0–2) |
| `lock_time_ds` | detection paused after occupied → empty, 0.1 s units | 2–100 (default 10 = 1 s; the sensor's minimum is 0.2 s) |
| `report_period_ds` | result report period, 0.1 s units | 1–5 (default 5) |

**About the two holds.** The S3 is the only place that decides whether
someone is present and it does all the debouncing (`absence_debounce_ms`,
`presence_return_debounce_ms`, `presence_return_gap_ms`, adjustable from the
dashboard's presence filter). A hold inside the radar is a delay the S3 can't
see: while the radar holds a target for 1 s, the S3's own 1 s absence debounce
starts 1 s late, and the lock time makes the radar blind to someone who steps
back in. Both are settable again because a hold of 0 may make a **still person**
flicker to "none" (the radar may need a moment to confirm a stationary target;
not confirmed for the C4002). Keep them small: against 180 s / 300 s timers a
1-2 s hold costs about 1 %. To test: raise the hold on one node (0, 1, 2 s),
stand still at the stove and watch for orange cells in the live view.

| `motion_gates`, `presence_gates` | 25 × 0/1, gate *i* on or off | |
| `motion_thresholds`, `presence_thresholds` | 25 × 0–99 per gate (higher = less sensitive); sending them sets that sensitivity to `custom` | |

Arrays always have 25 entries. With 80 cm gates only the first 15 are used.
`custom` is only accepted once the node knows thresholds for that type, from a
calibration, from **Read from sensor**, or from sending them.

## Detections behind or beside the sensor

The radar reports **distance** (and energy, gates, direction of motion), never an
angle, so software cannot tell "in front" from "behind". What the settings can
do is act on distance:

- **Range** (`range_min_cm`, `range_max_cm`): set the maximum just beyond where
  the cook stands. Anyone farther away, in front or behind, is ignored. This is
  the strongest software lever, but it only helps if the people behind the
  sensor are farther away than the cook.
- **Gates** (`motion_gates`, `presence_gates`) and per-gate thresholds: switch
  off the distance bands where a passer-by or a fan shows up, or raise their
  thresholds. A band that holds both the cook and the passer-by can't be
  separated this way.
- **Sensitivity** lower: fewer weak echoes, also fewer real ones.

The rest is mechanical, and it is the part software can't replace. What I could
verify, and what I could not (searches done 2026-10-14, nothing measured here):

- Retailer listings give the C4002 (DFRobot SEN0691) a **120° × 120° detection angle**
  ([Mouser](https://www.mouser.in/en/new/dfrobot/dfrobot-sen0691-mmwave-human-presence-sensor/)).
  I found no datasheet that says how the angle is defined or what the pattern
  looks like to the sides and behind, so **the rear sensitivity is unknown until
  it is measured**.
- 24 GHz radar passes through glass, wood and thin partitions but **not through
  thick metal** (a 24 GHz presence-module datasheet:
  [MM5D91-00](https://cdn.sparkfun.com/assets/d/2/1/0/0/MM5D91-00_datasheet.pdf)),
  and a physical lip or shield can intercept off-axis energy (a millimetre-wave
  radar patent, [EP1118872](https://data.epo.org/publication-server/rest/v1.2/patents/EP1118872NWA3/document.html)).
  So metal behind and beside the sensor can reduce rear detections.
- Not verified, general engineering: metal also **reflects**. A bare metal box
  can send energy off the walls and hood and create new false targets, a metal
  wall within about a quarter wavelength (about 3 mm at 24 GHz) of the antenna can
  detune it, and the front must stay open (nothing metal, thick or wet in front
  of the radar). Radar-absorbing foam lining the back and sides is the usual
  remedy. Test the actual housing.

How to measure it with what is already here (Tuning → Radar, live view):

1. Mount the radar where it will go, with the housing.
2. Stand where the cook stands, then walk along the aisle behind the radar at
   1, 2 and 3 m, and past its sides. Watch which gates fire and how strong the
   energy is.
3. If the rear walker shows up at distances the cook doesn't use, set the range
   maximum below them. If not, change the mounting direction, add the metal back
   and absorber, and repeat.
4. The dashboard's presence is the OR of two radars: one radar's rear false
   target keeps the gas on. A second, independent presence signal (the thermal
   camera sees warm bodies) is not built; see [demo_vs_deployment.md](demo_vs_deployment.md).

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
| MQTT `c4002_config` reply | ~880 B | telemetry payload buffer, 3072 B |
| ESP-NOW `C4002_LIVE` | 46 B, at most 4 a second | 250 B |
| MQTT `c4002_live` | ~470 B | telemetry payload buffer, 3072 B |
| Settings saved in NVS | 76 B | |
| RAM | node: one 4 KB task + a 4-entry queue (~0.5 KB); S3: ~1.1 KB of state, no new task | C6 and S3: 512 KB SRAM |

## Menuconfig

* `RB_C4002_REMOTE_TUNING` (node, default on): accept commands.
* `RB_CTRL_C4002_REMOTE_TUNING` (controller, default on): subscribe and relay.
* `RB_C4002_REPORT_PERIOD_DS` (node, 1–5, default 5): the menuconfig report period. The disappear delay and lock time are not options.
* `RB_C4002_LIVE_PERIOD_MS` (node, default 250, 0 = off): the live view stream.

## Not verified yet

These come from DFRobot's Arduino library, not from a test on our sensor:
the byte layout of the per-gate commands (0x62, 0x63), the threshold
read-back, and the lock time command (0x85). Calibration writing its
thresholds into the sensor is also the library's description. The first
bench check is: apply, then **Read from sensor**, and see that the values come
back.
