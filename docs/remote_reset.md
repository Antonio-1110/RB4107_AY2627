# Resetting a shutdown from the dashboard

A SHUTDOWN is latched: output 1 stays in its shutdown state until an operator
reset ([safety_state_machine.md](safety_state_machine.md)). The reset can come
from three places, and all of them do the same thing
(`rb_controller_request_reset()`):

| Way | Needs |
|---|---|
| **Reset shutdown** button on the dashboard (System status card) | `RB_CTRL_REMOTE_RESET` on (default), controller connected to the broker |
| `reset` in the serial diagnostic console | `RB_DIAG_CONSOLE` on, USB cable |
| Reboot or power cycle | nothing (the latch is not saved across restarts) |

## What the reset does

- In SHUTDOWN: back to IDLE, output 1 returns to its normal state on the next
  safety tick, and all timers start over. If the stove is still hot and nobody
  is there, the countdown simply starts again.
- In WARNING with "stay in WARNING until the person acknowledges": counts as
  the acknowledgement, but only while a person is detected.
- In any other state it does nothing.

The controller checks nothing else first. The dashboard asks for confirmation
and says to check the stove in person before resetting.

## Path

1. The button is enabled while the controller reports `reset_required: true`
   and is not offline. Pressing it (and confirming) calls
   `POST /api/devices/<id>/reset/` (`django/ingest/views.py`).
2. Django publishes once, QoS 1, not retained, on `<prefix>/controller/command`:

   ```json
   {"schema_version":2,"type":"controller_command","controller_id":"controller_01",
    "request_id":7,"action":"reset"}
   ```

   (`controller_command` in [the schema](schema/rb4107_mqtt.schema.json)). Not
   retained, so an old reset never replays when the controller reconnects.
3. The S3 subscribes to that topic (`rb_controller_app.c`), parses it with
   `rb_ctrl_cmd`, ignores a command for another `controller_id`, logs
   `operator reset from the dashboard (request N)` and requests the reset.
4. There is no separate reply: the next telemetry shows the state leaving
   SHUTDOWN and `reset_required: false`, and the button's status line says
   "Reset done". After 15 s with no change it says so.

## Limits (lab prototype)

The broker and the Django API have no login, so anyone on the network can send
the reset. That is accepted for the demo; turn `RB_CTRL_REMOTE_RESET` off in
menuconfig (Controller) for anything else, and the console and power cycle
still work.

## Presence filter and timers

The same command topic also changes the presence filter and the buzzer and
shutdown times of the running state machine
([safety_state_machine.md](safety_state_machine.md#timing-rules)): how long
absence must last before UNATTENDED, how long a return must last before it
cancels UNATTENDED or WARNING, how long an "absent" gap inside that return may
be without restarting it, and the unattended time before WARNING and SHUTDOWN.
The menuconfig values (`RB_SAFETY_ABSENCE_DEBOUNCE_MS`,
`RB_SAFETY_PRESENCE_RETURN_DEBOUNCE_MS`, `RB_SAFETY_PRESENCE_RETURN_GAP_MS`,
`RB_SAFETY_WARNING_TIMEOUT_S`, `RB_SAFETY_SHUTDOWN_TIMEOUT_S`) apply at boot.

1. **Presence filter and timers** in the Countdown card (rules 2 and 3): five fields in
   seconds, **Apply** and **Menuconfig values**. They call
   `POST /api/devices/<id>/presence_filter/` with
   `{"absence_ms":1000,"return_ms":5000,"return_gap_ms":1000,"warning_ms":180000,"shutdown_ms":300000}`
   or `{"defaults":true}`.
2. Django publishes on `<prefix>/controller/command`:

   ```json
   {"schema_version":2,"type":"controller_command","controller_id":"controller_01",
    "request_id":8,"action":"presence_filter","absence_ms":1000,"return_ms":5000,"return_gap_ms":1000,
    "warning_ms":180000,"shutdown_ms":300000}
   ```

   or `"action":"presence_filter_defaults"` without the five values. Limits,
   checked by Django and again by the controller: `absence_ms` and `return_ms`
   0 – 10000, `return_gap_ms` 0 – 5000, `warning_ms` and `shutdown_ms`
   1000 – 3600000. When shutdown counts from UNATTENDED, `shutdown_ms` must
   also be above `warning_ms`: the dashboard checks this before sending, and
   the controller refuses the whole set otherwise (`safety settings refused`
   in its log).
3. The controller (`RB_CTRL_REMOTE_PRESENCE_FILTER` on, the default) applies
   them at the next safety step, logs
   `presence filter (dashboard): ...`, and keeps them on top of the test
   timers (`timers test|normal` in the console) until a reboot or
   `presence_filter_defaults`. Nothing is saved.
4. The answer is `safety.presence_filter` in the next telemetry
   (`{"absence_ms":..,"return_ms":..,"return_gap_ms":..,"source":"dashboard"}`)
   with `safety.warning_after_ms` and `safety.shutdown_after_ms`, which the
   card shows as "In use".

The same limits as the reset apply: no login, so turn
`RB_CTRL_REMOTE_PRESENCE_FILTER` off outside the demo.

## Attendance: who is at the stove, and the pause budget

The Person rule (Tuning → Rules) also holds the **attendance** settings: whether
someone counts as at the stove when every working radar sees them or either
does, and how long a person passing may stop the unattended clock (per timeline,
in seconds and as a share of the shutdown time in force; 0 = never pause). The
"Stays this long to cancel" field above it is the 5 s that cancels the timeline.
**Apply attendance** calls `POST /api/devices/<id>/attendance/` with

```json
{"pause_budget_s": 30, "pause_budget_pct": 25, "fusion": "all"}
```

or `{"defaults": true}`. Limits: 0-600 s, 0-100 %, `fusion` `all` or `any`. The
controller keeps the values in NVS (they survive a reboot) and reports them in
`safety.presence_filter` (`fusion`, `pause_budget_cfg_ms`, `pause_budget_pct`,
`attendance_source`). Menuconfig: `RB_CTRL_REMOTE_ATTENDANCE`.

## Radar state and distance rules

Below the attendance settings. **Apply radar rules** calls
`POST /api/devices/<id>/radar-rules/` with

```json
{"mode": "observe", "quiet_gap_s": 1.5, "dwell_s": 4, "stationary_min_s": 1.5,
 "settle_window_s": 4, "settle_span_cm": 60, "zone_max_cm": 0}
```

or `{"defaults": true}`. `mode` is `off`, `observe` or `enforce` (see
[the rules](safety_state_machine.md#radar-state-and-distance-rules-placeholder));
limits 0-10 s, 0-60 s, 0-60 s, 0.5-60 s, 0-1000 cm, 0-1200 cm. The controller
keeps the values in NVS and reports them in `safety.radar_rules` (with
`source`). Menuconfig: `RB_CTRL_REMOTE_RADAR_RULES`.

## Other safety settings

Tuning → Logic → **Other settings** holds every other number and choice in the
[safety rules](safety_rules.md#changing-them-from-the-dashboard): where the
shutdown counts from, the cooling and warning-exit policies, the trend on or
off, the heat-on rate and off gap, the fault shutdown, self-test, node and valve
timings, and the fine tuning of the tiers, the trend and the holds. They come
from one table, [`rb_params.def`](../firmware/components/rb_params/include/rb_params.def).
**Apply settings** calls `POST /api/devices/<id>/params/` with every key in the
table's units (ms, 0.1 °C, 0.1 °C/min, choice index), for example
`{"shutdown_timing": 0, ..., "warning_min_after_hold_ms": 30000}`, or
`{"defaults": true}`. Django checks the ranges from
`django/ingest/safety_params.json`; the controller checks them again, refuses a
set that doesn't fit together and keeps the ones in use, saves the rest in NVS
(key `params`, dropped if a later firmware changes the table) and reports them as
`safety.params` with `safety.params_source`. Menuconfig: `RB_CTRL_REMOTE_PARAMS`.

## Temperature tiers

The Timer rule (Tuning → Rules) also holds the **temperature tiers**: from which
hottest-pixel temperature the buzzer and shutdown come sooner, and how soon (see
[safety_state_machine.md](safety_state_machine.md#temperature-tiers-the-hotter-the-pan-the-shorter-the-time)).
Eight fields: for *elevated* the temperature and the two times, for *critical*
the temperature, the "or rising this fast, above this temperature" rule and the
two times. **Apply tiers** calls `POST /api/devices/<id>/tiers/` with

```json
{"elevated": {"temp_c": 120, "warning_s": 60, "shutdown_s": 150},
 "critical": {"temp_c": 200, "rate_c_per_min": 20, "rate_min_temp_c": 150, "warning_s": 30, "shutdown_s": 120}}
```

or `{"defaults": true}` for the menuconfig values. A temperature of 0 turns a
tier off. Limits: temperatures 0-300 °C, times 1-3600 s, rate 0-1000 °C/min.
The controller keeps the values in NVS (they survive a reboot), refuses an
inconsistent set (a shutdown not after its warning, critical starting below
elevated) and keeps the ones in use, and reports the result as `safety.tiers`
with `source` `dashboard` or `menuconfig`. Menuconfig: `RB_CTRL_REMOTE_TIERS`.
A tier never gives more time than the base times.

## Cookware profile

The Cooking rule (Tuning → Rules) also has a **cookware profile**: mixed, light,
heavy or liquid (see [safety_state_machine.md](safety_state_machine.md#cooking-judgment-is-heat-still-going-in)).
**Apply** calls `POST /api/devices/<id>/cookware/` with `{"cookware":"light"}`,
or `{"defaults":true}` for the menuconfig profile. The controller saves it
(NVS key `cookware`) and reports it as `safety.cookware` (profile, tau_s,
source) in its telemetry. Menuconfig: `RB_CTRL_REMOTE_COOKWARE`.

## Cooking temperature

The same command topic also carries the cooking temperature: the controller
counts the stove as cooking (IDLE → MONITORING) when the thermal camera's
hottest pixel reaches it, and as no longer cooking below the "over" value,
which stays the menuconfig gap below it (10 °C by default). The menuconfig
value is `RB_SAFETY_HEAT_ON_DC` (50.0 °C). This is the only cooking threshold:
the dashboard outlines the pixels at or above it, and the cooking badge comes
from the controller's state.

1. **Cooking temperature** in the Countdown card (rule 1): a field in °C,
   **Apply** and **Menuconfig value**. They call
   `POST /api/devices/<id>/cooking_temp/` with `{"cooking_temp_c":62.5}`
   (0 – 300 °C, sent in 0.1 °C steps) or `{"defaults":true}`.
2. Django publishes on `<prefix>/controller/command`:

   ```json
   {"schema_version":2,"type":"controller_command","controller_id":"controller_01",
    "request_id":9,"action":"cooking_temp","cooking_temp_dc":625}
   ```

   or `"action":"cooking_temp_defaults"` without the value.
3. The controller (`RB_CTRL_REMOTE_COOKING_TEMP` on, the default) uses it from
   the next safety step and saves it in NVS, so it survives a reboot and wins
   over menuconfig. `cooking_temp_defaults` erases it; so does
   `idf.py erase-flash`. A normal `idf.py flash` keeps it. It logs
   `cooking temperature ... from the dashboard`.
4. The controller also sends the value to the thermal node as a THERMAL_CONFIG
   packet ([protocol.md](protocol.md#thermal_config-type-11-28-bytes-total)),
   so the node's `pixels_above_threshold` counts the same pixels. It learns the
   node's MAC from the node's packets, so the node must have been heard from
   since the controller booted. This part is display only.
5. The answer is `safety.cooking` in the next telemetry (`on_c`, `off_c`,
   `source: "dashboard"`), which the card shows as "In use".

The same limits as the reset apply: no login, so turn
`RB_CTRL_REMOTE_COOKING_TEMP` off outside the demo.
