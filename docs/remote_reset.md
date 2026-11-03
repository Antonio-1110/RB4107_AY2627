# Resetting a shutdown and changing safety settings from the dashboard

A SHUTDOWN is latched: output 1 stays in its shutdown state until an operator
reset ([safety_state_machine.md](safety_state_machine.md)). A reset can come
from three places, and all of them call `rb_controller_request_reset()`:

| Way | Needs |
|---|---|
| **Reset shut-off** button on the dashboard (countdown card) | `RB_CTRL_REMOTE_RESET` on (default), controller connected to the broker |
| `reset` in the serial diagnostic console | `RB_DIAG_CONSOLE` on, USB cable |
| Reboot or power cycle | nothing (the latch is not saved across restarts) |

## What the reset does

- In SHUTDOWN: back to IDLE, output 1 returns to its normal state on the next
  safety tick, and all timers start over. If the stove is still hot and nobody
  is there, the countdown simply starts again.
- In WARNING with "stay in WARNING until the person acknowledges": counts as
  the acknowledgement, but only while a person is detected.
- In any other state it does nothing.

The controller checks nothing else first; the dashboard asks for confirmation.

## How a dashboard command travels

Every setting below uses the same path:

1. The dashboard calls a Django endpoint (`django/ingest/views.py`). Django
   checks the value (`django/ingest/commands.py`; for the generic params also
   `django/ingest/safety_params.json`) and publishes a `controller_command` on
   `<prefix>/controller/command`: QoS 1, **not retained**, so an old command
   never replays when the controller reconnects. For example, the reset:

   ```json
   {"schema_version":2,"type":"controller_command","controller_id":"controller_01",
    "request_id":7,"action":"reset"}
   ```

   Fields per action: `controller_command` in
   [the schema](schema/rb4107_mqtt.schema.json).
2. The controller (`rb_controller_app.c`, parser `rb_ctrl_cmd`) ignores a
   command for another `controller_id`, checks the values again, applies them at
   the next safety step and logs it. A set that doesn't fit together is refused
   whole (`safety settings refused` in its log) and the values in use stay.
3. There is no separate reply: the next telemetry shows the result. The
   dashboard shows "saved" or "In use" once the telemetry matches what it sent.

| Setting | Endpoint | `action` | Menuconfig gate | NVS key | Telemetry |
|---|---|---|---|---|---|
| Reset | `POST /api/devices/<id>/reset/` | `reset` | `RB_CTRL_REMOTE_RESET` | – | `reset_required`, state |
| Presence filter and timers | `…/presence_filter/` | `presence_filter` | `RB_CTRL_REMOTE_PRESENCE_FILTER` | `filter` | `safety.presence_filter`, `safety.warning_after_ms`, `safety.shutdown_after_ms` |
| Attendance | `…/attendance/` | `attendance` | `RB_CTRL_REMOTE_ATTENDANCE` | `attend` | `safety.presence_filter` (`fusion`, `pause_budget_*`, `attendance_source`) |
| Radar state and distance rules | `…/radar-rules/` | `radar_rules` | `RB_CTRL_REMOTE_RADAR_RULES` | `radar` | `safety.radar_rules` |
| Other safety settings | `…/params/` | `params` | `RB_CTRL_REMOTE_PARAMS` | `params` | `safety.params`, `safety.params_source` |
| Temperature tiers | `…/tiers/` | `tiers` | `RB_CTRL_REMOTE_TIERS` | `tiers` | `safety.tiers` |
| Cookware profile | `…/cookware/` | `cookware` | `RB_CTRL_REMOTE_COOKWARE` | `cookware` | `safety.cookware` |
| Cooking temperature | `…/cooking_temp/` | `cooking_temp` | `RB_CTRL_REMOTE_COOKING_TEMP` | `cook_dc` | `safety.cooking` |

Every endpoint also takes `{"defaults": true}` ("Restore menuconfig defaults", action
`<action>_defaults`); each setting
saved in NVS survives a reboot and wins over menuconfig until then, or until
`idf.py erase-flash` (a normal `idf.py flash` keeps NVS). Menuconfig defaults
are in [configuration.md](configuration.md). The fields live on Tuning →
Safety; each card's **Apply** sends only its own settings.

### Limits (lab prototype)

The broker and the Django API have no login, so anyone on the network can send
any of these commands, the reset included. That is accepted for the demo. For
anything else turn the gates above off in menuconfig (Controller); the console
and power cycle still work.

## Presence filter and timers

Changes the presence filter and the buzzer and shutdown times of the running
state machine ([timing rules](safety_state_machine.md#timing-rules)): how long
absence must last before UNATTENDED, how long a return must last before it
cancels UNATTENDED or WARNING, how long an "absent" gap inside that return may
be, and the unattended time before WARNING and SHUTDOWN. The menuconfig values
apply at boot.

- Fields: `absence_ms`, `return_ms`, `return_gap_ms`, `warning_ms`,
  `shutdown_ms`. The dashboard doesn't offer the gap; it sends back the value in
  force.
- When shutdown counts from UNATTENDED, `shutdown_ms` must be above
  `warning_ms`. The dashboard checks this before sending; the controller
  refuses the whole set otherwise.
- The values are kept on top of the test timers (`timers test|normal` in the
  console) until `presence_filter_defaults`, and set again at boot, except
  while the diagnostic test timers at boot are on.

## Attendance: who is at the stove, and the pause budget

Whether someone counts as at the stove when every working radar sees them
(`fusion` `all`) or either does (`any`), and how long a person passing may stop
the unattended clock (`pause_budget_s`, per timeline; 0 = never pause). The
share of the shutdown time the pause may take (`pause_budget_pct`) is fixed on
the dashboard; the value in force is sent back.

## Radar state and distance rules

`mode` is `off`, `observe` or `enforce` (see
[the rules](safety_state_machine.md#radar-state-and-distance-rules-placeholder));
the other fields are `quiet_gap_s`, `dwell_s`, `stationary_min_s`,
`settle_window_s`, `settle_span_cm` and `zone_max_cm`.

## Other safety settings

The few [safety rules](safety_rules.md#changing-them-from-the-dashboard)
settings without a main field of their own sit under **More settings** on
Tuning → Safety. They come from one table,
[`rb_params.def`](../firmware/components/rb_params/include/rb_params.def); every
other number in the rules is fixed in the firmware (`{=NAME}` in
`safety_rules.def`). The request carries every key in the table's units (ms,
0.1 °C, 0/1). The NVS entry is dropped if a later firmware changes the table.

## Temperature tiers

From which hottest-pixel temperature the buzzer and shutdown come sooner, and
how soon ([the rule](safety_state_machine.md#temperature-tiers-the-hotter-the-pan-the-shorter-the-time)).
Tier 1 is the base times; tiers 2 to 5 are `{"temp_c", "warning_s", "shutdown_s"}`
entries in `levels`. A temperature of 0 turns a tier off. A tier never gives
more time than the base times.

The controller refuses an inconsistent set (a shutdown not after its warning, a
tier starting below the one before it) and keeps the ones in use. `source` in
`safety.tiers` is `dashboard` or `menuconfig`. Whether a rising pan gets the
tier it is heading for is `tier_rate`, one of the generic params.

## Cookware profile

`mixed`, `light`, `heavy` or `liquid` (see
[cooking judgment](safety_state_machine.md#cooking-judgment-is-heat-still-going-in)).
Request: `{"cookware":"light"}`; `safety.cookware` reports profile, `tau_s` and
source.

## Cooking temperature

The controller counts the stove as cooking (IDLE → MONITORING) when the thermal
camera's hottest pixel reaches this temperature, and as no longer cooking below
the "over" value, which stays the menuconfig gap below it. Menuconfig value:
`RB_SAFETY_HEAT_ON_DC`. This is the only cooking threshold: the dashboard
outlines the pixels at or above it, and the cooking badge comes from the
controller's state.

- Request: `{"cooking_temp_c":62.5}` (0.1 °C steps). Command field:
  `cooking_temp_dc`. Telemetry: `safety.cooking` (`on_c`, `off_c`,
  `source: "dashboard"`).
- `cooking_temp_defaults` erases the saved value.
- The controller also sends the value to the thermal node as a THERMAL_CONFIG
  packet ([protocol.md](protocol.md#thermal_config-type-11-28-bytes-total)), so
  the node's `pixels_above_threshold` counts the same pixels. It learns the
  node's MAC from the node's packets, so the node must have been heard from
  since the controller booted. This part is display only.
