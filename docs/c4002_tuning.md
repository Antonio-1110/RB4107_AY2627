# Remote C4002 tuning

Each presence node's C4002 radar can be tuned from the dashboard's **Radar
tuning** card: range, gate size, sensitivity, individual distance gates, and
environment calibration. A live view shows what the radar reports, result by
result. It is built for demos and bench tuning (issue #20, false presence), not
for production: anyone who can reach the API or publish to the broker can change
a safety sensor.

With two or more presence nodes, **Send to** starts on **All radars**: every
command goes to each node and the status lists each node's answer. Pick one node
to tune it alone.

```text
dashboard ── POST /api/devices/<controller>/nodes/<node>/c4002/ ──► Django
Django ── MQTT rb4107/sensors/<node>/c4002_set (QoS 1, not retained) ──► S3
S3 ── ESP-NOW C4002_CONFIG ──► presence node ── UART ──► C4002
presence node ── ESP-NOW C4002_CONFIG_ACK ──► S3
S3 ── MQTT rb4107/sensors/<node>/c4002_config (QoS 1, retained) ──► Django ──► dashboard

presence node ── ESP-NOW C4002_LIVE (each new result, ≤ 4/s) ──► S3
S3 ── MQTT rb4107/sensors/<node>/c4002_live (QoS 0) ──► Django ──► live view (polls every 0.5 s)
```

The safety state machine is never involved, and a node keeps sending presence
data while it is reconfigured. Live results never reach the safety logic, which
uses the node's filtered PRESENCE_DATA.

| Part | Code | Job |
|---|---|---|
| Dashboard | `frontend/js/tuning.js` | form, gate table, status of the last command |
| Django | `django/ingest/commands.py`, `views.c4002_command` | checks the command against the schema, publishes it |
| S3 | `rb_c4002_cmd` (parser), `rb_c4002_relay` | parses the JSON, sends it to the node's MAC, times out after 8 s, publishes the reply |
| Node | `rb_c4002_remote`, `rb_node_sensors` | merges, checks, applies, **saves in NVS**, answers |
| Shared | `rb_types/rb_c4002_params.h`, `rb_protocol` | one parameter struct and its limits |

The S3 learns each node's MAC from the packets it sends, so a node can only be
tuned after the controller has heard from it since it booted. On first hearing a
node, the controller asks for its settings so the card fills in by itself.

## Live view

The C4002 reports a verdict (none, stationary, moving), which gates hold a
stationary target, and the stationary and moving targets' distance, energy,
speed and direction. It does **not** report a per-gate energy, so the view shows
which gates fired, not how close each came to its threshold.

To check for false positives, leave the room empty, watch which gates fire,
turn those off or calibrate again, and watch them go quiet.

Django stores each result like any other message (about 4 rows a second per
node). `GET /api/devices/<controller>/nodes/<node>/c4002/live/?after=<id>`
returns up to the last minute, oldest first. `RB_C4002_LIVE_PERIOD_MS` = 0 on the
node turns the stream off.

## Settings

Message format and ranges: `c4002_command` in
[the schema](schema/rb4107_mqtt.schema.json); limits are shared in
`rb_c4002_params.h`. Points that are not obvious from the schema:

- `resolution_cm` is 80 (15 gates, ~11 m) or 20 (25 gates, ~4.9 m). Gate arrays
  always have 25 entries; with 80 cm gates only the first 15 are used.
- `motion_thresholds` / `presence_thresholds` (per gate, higher = less
  sensitive) set that sensitivity to `custom`. The dashboard only shows them;
  **Calibrate empty room** sets them. `custom` is only accepted once the node
  knows thresholds for that type (from a calibration, **Read from node**, or the
  same command).
- `disappear_delay_s`, `lock_time_ds` and `report_period_ds` are **not on the
  dashboard** (see below). The command still accepts them, so a script or
  `mosquitto_pub` can set them for an experiment; **Reset** puts a node back on
  its menuconfig values.

**Why the two holds stay at 1 s.** The S3 is the only place that decides
whether someone is present, and it does all the debouncing (the presence
filter, adjustable from the dashboard, see
[remote_reset.md](remote_reset.md#presence-filter-and-timers)). A hold inside the
radar is a delay the S3 can't see: while the radar holds a target for 1 s, the
S3's own absence debounce starts 1 s late, and the lock time makes the radar
blind to someone who steps back in. They stay at `RB_C4002_DISAPPEAR_DELAY_S` = 1
and `RB_C4002_LOCK_TIME_DS` = 10 (the sensor's minimum lock is 0.2 s). 0 s would
show absence about 1 s sooner, but may make a **still person** flicker to "none"
(not confirmed for the C4002). Against 150 s / 300 s timers, 1 s costs well
under 1 %. To try another value, send `disappear_delay_s` 0, 1, 2 to one node,
stand still at the stove and watch for orange cells in the live view.

## Detections behind or beside the sensor

The radar reports **distance** (and energy, gates, direction of motion), never an
angle, so software cannot tell "in front" from "behind". What the settings can
do is act on distance:

- **Range** (`range_min_cm`, `range_max_cm`): set the maximum just beyond where
  the cook stands. Anyone farther away, in front or behind, is ignored. This is
  the strongest software lever, but it only helps if the people behind the
  sensor are farther away than the cook.
- **Gates** and per-gate thresholds: switch off the distance bands where a
  passer-by or a fan shows up, or calibrate the empty room so their thresholds
  rise. A band that holds both the cook and the passer-by can't be separated this
  way.
- **Sensitivity** lower: fewer weak echoes, also fewer real ones.

The rest is mechanical, and software can't replace it. What could be verified,
and what could not (searches done 2026-10-14, nothing measured here):

- Retailer listings give the C4002 (DFRobot SEN0691) a **120° × 120° detection angle**
  ([Mouser](https://www.mouser.in/en/new/dfrobot/dfrobot-sen0691-mmwave-human-presence-sensor/)).
  No datasheet found says how the angle is defined or what the pattern looks like
  to the sides and behind, so **the rear sensitivity is unknown until it is
  measured**.
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

To measure it (Tuning → Radar, live view):

1. Mount the radar where it will go, with the housing.
2. Stand where the cook stands, then walk along the aisle behind the radar at
   1, 2 and 3 m, and past its sides. Watch which gates fire and how strong the
   energy is.
3. If the rear walker shows up at distances the cook doesn't use, set the range
   maximum below them. If not, change the mounting direction, add the metal back
   and absorber, and repeat.
4. Presence is the OR of two radars, so one radar's rear false target keeps the
   gas on. A second, independent presence signal (the thermal camera sees warm
   bodies) is not built; see [demo_vs_deployment.md](demo_vs_deployment.md).

## Actions

| `action` | What the node does |
|---|---|
| `apply` | merges the keys sent into its current settings, checks them, pushes everything to the C4002, saves in NVS. Nothing changes if a value is out of range. If the sensor refuses a command, the node re-pushes the previous settings. |
| `calibrate` | starts the C4002's environment calibration (`calibration_delay_s`, `calibration_duration_s`). When it ends, the node reads the learned thresholds back, switches both sensitivities to `custom`, saves, and reports again. |
| `read` | reads the thresholds the sensor uses now and reports the settings (not saved) |
| `reset` | erases the saved settings and goes back to the menuconfig values |

Saved settings win over menuconfig at every boot, until `reset`.

## The reply (`c4002_config`)

`result` is `ok`, `invalid` (a value was refused, see the node log),
`sensor_error` (the C4002 didn't accept a command), or one of the controller's:
`rejected` (bad command JSON, `error` says why), `unknown_node` (not heard from
since boot), `send_failed`, `no_reply` (8 s without an answer). `settings` is the
full current set, or `null` when the command never reached the node; the
dashboard then keeps showing the last known settings.

## Size limits

ESP-NOW packets must stay within the 250 B payload (`_Static_assert` in
`rb_protocol.h`; `C4002_CONFIG` is the largest, ~100 B). The MQTT command
(~520 B with every key) must fit the S3 MQTT receive buffer (1024 B by default)
and the reply (~880 B) the telemetry payload buffer (3072 B). Adding a field to
the parameter struct means re-checking both.

## Menuconfig

`RB_C4002_REMOTE_TUNING` (node) and `RB_CTRL_C4002_REMOTE_TUNING` (controller)
switch the feature off; `RB_C4002_LIVE_PERIOD_MS` sets the live stream. The rest
is in [configuration.md](configuration.md).

## Not verified yet

These come from DFRobot's Arduino library, not from a test on our sensor: the
byte layout of the per-gate commands (0x62, 0x63), the threshold read-back, the
lock time command (0x85), and calibration writing its thresholds into the
sensor. First bench check: apply, then **Read from node**, and see that the
values come back.
