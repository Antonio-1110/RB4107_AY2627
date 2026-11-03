# Monitoring dashboard

The dashboard in [`frontend/`](../frontend) shows every controller's reported
state. It reads a JSON API from the Django backend, which is fed by the
`mqtt_subscriber`. It cannot operate or silence anything: protection stays on
the ESP32-S3. It sends two kinds of command: resetting a latched shutdown
([remote_reset.md](remote_reset.md)) and C4002 radar tuning
([c4002_tuning.md](c4002_tuning.md)). The page's files are listed in
[`frontend/README.md`](../frontend/README.md); this page covers what the numbers mean.

## Data path

1. The firmware publishes schema v2 ([mqtt_schema.md](mqtt_schema.md)).
2. `ingest/subscriber.py` validates each message; `ingest/handlers.py` stores it
   (`storage.persist()`) and `ingest/normalization.py` adapts it to the dashboard fields.
3. SQLite holds snapshots, field timestamps, raw messages, temperature samples,
   events and the worker heartbeat. `ingest/views.py` serves JSON under `/api/`.
4. The page polls the API every two seconds. Django serves it at `/`, so both share one origin.

## Firmware → dashboard fields

The main mappings (unit conversions and `null` from older firmware are not repeated):

| Firmware field | Dashboard field | Meaning |
|---|---|---|
| `controller_id` | `device_id` | One controller / cooking station |
| `presence_state` | `occupied` | PRESENT → true, ABSENT → false, UNKNOWN → null. No fusion in the backend |
| `thermal.max_c`, `mean_c`, `min_c` | `temperature_c`, `temperature_avg_c`, `temperature_min_c` | Only when `thermal.valid` is true |
| `safety.state` | `safety_state` | Firmware enum, lower case |
| `safety.buzzer` | `alarm_state` | OFF → clear |
| `safety.shutdown` | `relay_state` | true → isolated. **A report of the controller's output, not independent feedback from the appliance** |
| `safety.reset_required` | `manual_reset_required` | Older firmware: SHUTDOWN state and `safety.shutdown`. Never inferred from temperature, absence or network loss |
| `safety.warning_after_ms`, `shutdown_after_ms`, `shutdown_counts_from` | `*_seconds` | The controller's timer settings |
| `safety.unattended_ms`, `absent_ms`, `returning_ms`, `state_ms` | `*_seconds` | Time in the current stage. The Countdown card advances between reports and freezes ("last known") when the controller goes quiet; the backend never runs a safety timer |
| `safety.tier`, `effective_*_ms`, `tiers` | `tier`, `effective_*_seconds`, `tiers` | The temperature tier in force and the times it gives |
| `safety.cooking.*`, `safety.hold`, `safety.cookware` | `cooking_*`, `hold`, `cookware_*` | The cooking judgment (see [Active cooking](#active-cooking)) |
| `safety.presence_filter` | `presence_filter` | The debounce in use ([remote_reset.md](remote_reset.md#presence-filter-and-timers)) |
| `safety.presence_unknown`, `thermal_lost` | same | Sensor flags, shown as badges and never turned into a fault by the backend |
| `safety.test_timers` | `test_timers` | Badge: the firmware runs shortened test timers |
| `nodes[]` and sensor topics | `sensors[node_id]` | Per-node radar and thermal diagnostics |
| `c4002_live`, `c4002_config` | `sensors[node_id].c4002_live`, `.c4002` | Newest raw radar result; radar settings and the last tuning result ([c4002_tuning.md](c4002_tuning.md)) |
| `thermal_frame` | `GET /api/devices/<id>/thermal_frame/` | Newest heat-map picture per thermal node only. Display only, never used for state |
| `boot_id` + `sequence` | stored on the device | Orders messages and detects restarts (see [Freshness](#freshness-and-limitations)) |

The top sticky banner needs the supply isolated AND a manual reset required.
WARNING and UNATTENDED show in the summary, tiles and stall page but not as a
sticky banner. Missing or invalid thermal values stay null.

### Stall status

Django gives every controller exactly one status (`classify_display_state()` in
`ingest/locations.py`, tests in `ingest/tests/test_stall_status.py`). The first
rule that matches wins:

| # | Status | When |
|---|---|---|
| 1 | **Critical** | The supply is cut: output isolated or state SHUTDOWN. Kept even when the data is last known, because the cut is latched |
| 2 | **Warning** | State WARNING or UNATTENDED, or the warning buzzer. Kept when last known |
| 3 | **Fault** | Anything that leaves the stall unprotected or out of sight: **F1** the controller reports FAULT; **F2** it is not online (Last Will, silent for `RB4107_DEVICE_STALE_SECONDS` = 15 s, never heard, or the MQTT monitor is down); **F3** its safety state is older than 15 s; **F4** a sensor node it lists is not working (STALE, OFFLINE, NEVER_SEEN, or an invalid reading); **F5** it lists no sensor nodes |
| 4 | **Unknown** | Online and every node working, but the controller is starting (BOOT, SELF_TEST) or reports a state the dashboard doesn't know |
| 5 | **Normal** | IDLE or MONITORING, buzzer off, fresh data, every listed node working |

A stall with several controllers shows its worst one. Each stall is counted in
exactly one summary card; an offline stall is a fault. If the dashboard loses
Django, every stall that wasn't critical or warning shows "SERVER UNREACHABLE".

This is display only. The firmware keeps its own rules ([safety_rules.md](safety_rules.md)):
a lost sensor node does not put the controller in FAULT (rules A4, A8), so a
controller can report IDLE while the dashboard shows Fault.

## Stall page

The header has three buttons: **Stall history**, **Developer mode** and the
colour theme (**Auto** / Light / Dark); the RB4107 logo goes back to all stalls.
The connection status (server, MQTT, location catalogue, last report) is in the
footer.

**Developer mode** turns the tuning tools (Tuning) on and off. The first time in
a browser tab it asks for the password, `RB4107`, and after that it switches
straight back and forth until the tab is closed (`/?mode=tuning` asks for it
too). The password is in the dashboard's code on purpose: it is not a secret and
not security. Like Monitor itself it is cosmetic: it keeps the commissioning
tools out of a stall operator's or inspector's way, but the API still accepts
commands from anyone who can reach it.

- **Monitor** fits one laptop screen: the Countdown card (phase, clock and bar
  against the buzzer and shut-off marks, sensor chips), condition tiles, the
  readings with their freshness, a small heat map, temperature history, system
  status with **Reset shut-off**, the last events and a read-only **Safety logic** strip.
  When a due buzzer or shut-off is held back, the Countdown card names the
  kind of hold, what ends it and its limit: **cooling** (sure there is no heat
  input), **falling** (not sure yet whether it is cooling or a lowered flame, at
  most the falling hold maximum) or **return** (a person is being confirmed).
- **Tuning** adds tabs: **Safety** (settings and rules), **Radar** (C4002
  tuning and live view), **Thermal** (heat map, outline and pixel-size
  controls), **Diagnostics** and **Logs** ([logging.md](logging.md#logs-tab)).
- **Diagnostics** has nothing to change: it is for finding out why something
  looks wrong, during installation or when a stall misbehaves. It shows only
  what no other page does, each block headed by the question it answers: is
  each sensor node reporting (each node's own readings, link and last report),
  is the thermal node reading the pan (hottest pixel, hot region, rate of
  change, freshness), does the controller agree with itself (gas relay, reset
  flag, times in force, uptime), and what the controller sent (raw telemetry).
  The countdown, temperature history and event log are on Monitor.

The overview has a Tiles | Table switch.

**Safety tab.** A flow strip (Idle → Monitoring → Unattended → Warning →
Shutdown) with the values in force, then three cards: *Cooking*, *Presence
at the stove*, *Countdown times*. Each card has **More settings** and its
own **Apply**, which sends only that card's changes; there is no page-wide
save. **Restore menuconfig defaults** resets everything. Values are saved on
the controller and survive a reboot. The folded **Full rule table** comes from
`safety_rules_data.js`, generated from the firmware's rules table, so it
cannot drift (`tools/safety_rules.py check` fails if it is stale). Each
setting shows the value the controller reports, falling back to the menuconfig
default in grey. Underlined settings can be changed above; the rest are fixed
in the firmware.

### Active cooking

Whether the stove is in use is decided by the firmware and only displayed here
(`classify_cooking()` in `ingest/locations.py`). Cooking starts when the
thermal camera's hottest pixel reaches the cooking temperature
(`RB_SAFETY_HEAT_ON_DC`, settable on the Safety tab) or rises fast, and ends
below the "over" value; both come in `safety.cooking`. The thresholds are still
being tuned ([issue #21](https://github.com/Antonio-1110/RB4107_AY2627/issues/21)).

| Controller state | Shown as |
|---|---|
| MONITORING, UNATTENDED, WARNING | COOKING |
| IDLE | NOT COOKING |
| SHUTDOWN | GAS SHUT OFF |
| FAULT, BOOT, SELF_TEST, none | a guess from the hottest pixel: LIKELY COOKING, LIKELY NOT COOKING or COOKING UNKNOWN |

A stall with several stations shows the most active one. Stale data is marked *last known*.

## Storage

Every accepted message is stored with its raw JSON, and telemetry a second time
as a reading: about 0.86 GB per day for one controller at the default rates
(telemetry 1/s is the bulk, plus per-node topics and raw radar results).
Heat-map pictures are not stored beyond the newest per node. `tools/run_dev.sh`
erases the database when it stops, so this only builds up when Django runs
continuously. The subscriber therefore runs an hourly **retention job**
(`ingest/retention.py`):

| Rule | Default | Setting |
|---|---|---|
| radar live results, heartbeats, per-node topics: delete after | 24 h | `RB4107_RETENTION_LIVE_HOURS` |
| telemetry: full resolution for | 24 h | `RB4107_RETENTION_FULL_HOURS` |
| then thinned to one per | 60 s | `RB4107_RETENTION_THIN_SECONDS` |
| and deleted after | 30 days | `RB4107_RETENTION_DAYS` |
| faults, node status, tuning replies, rejected messages | 90 days | `RB4107_RETENTION_OTHER_DAYS` |
| controller log lines | 7 days | `RB4107_RETENTION_LOG_DAYS` |
| how often (0 = never) | 3600 s | `RB4107_RETENTION_INTERVAL_S` |

Safety events and the messages that carry them are never deleted, which keeps
one controller at roughly 1 GB. `python manage.py prune_history --dry-run`
shows what would go. SQLite does not return disk space by itself: run
`sqlite3 db.sqlite3 VACUUM` after a big clean-up.

## Stall history

The **Stall history** tab helps decide which stall to visit, for example by a
fire-safety inspector. Over a period chosen at the top (2 weeks, 4 weeks by
default, 3 months, or custom: last N days or a date range, at most 366 days) it
shows, per stall:

1. how often a countdown starts (count, per week, per cooking day);
2. how many reached the buzzer (count, share of countdowns);
3. how many escalated to a gas shut-off (count, share of buzzer warnings);
4. how long recovery takes (median and slowest).

**Compare stalls** lists every stall with a bar per figure, with search, area
and "show" filters, ranking (default: most shut-offs, then buzzer warnings),
and a ☆ star kept in the browser. Clicking a row opens that stall: its figures,
countdowns per week, how countdowns ended, and the countdown log.

An *incident* is one unattended-cooking episode (`ingest/incidents.py`), built
from live telemetry: it opens when the state first becomes UNATTENDED, WARNING
or SHUTDOWN and closes when the state returns to MONITORING or IDLE, or the
controller restarts. FAULT keeps it open. The end reason is inferred from the
next state, not reported by the controller. Incidents are never pruned.

Recovery is timed per outcome: countdown start → normal (back before the
buzzer); buzzer → normal (back after the buzzer, or stove turned off);
shut-off → the operator's reset. A **controller restart** during a countdown is
counted on its own row and never timed, since it can mean someone power-cycled
the controller to get round it. A *cooking day* is a day with at least one
stored reading in a cooking state, so only the last `RB4107_RETENTION_DAYS`
have any.

API: `GET /api/stall-history/` (all-time counts); add `?days=28` or
`?from=2026-10-01&to=2026-10-14` for each stall's `period` summary, and
`&stall=<stall_id>` for that stall's newest 500 incidents. Other combinations are a 400.

**Demo data.** `tools/run_dev.sh --demo` runs `seed_incident_history`: three
months of made-up incidents per simulated stall (marked "demo"), cooking days
and 24 hours of made-up events (`ingest/demo_events.py`). `--days N` changes
the range and `--clear` deletes it all. A real controller's history is only
what it did.

## Locations and more controllers

Edit `django/locations.json`, keyed by the firmware's `controller_id`
(`RB_MQTT_CONTROLLER_ID`); `controller_01` is the lab bench. Unknown IDs show as
unassigned. `django/locations.demo.json` holds made-up stalls for
`simulate_fleet` (`tools/run_dev.sh --demo`); each plays a `demo_scenario`
(monitoring, unattended, warning, shutdown, idle, fault_radars,
fault_node_offline, network_loss) and is tagged SIMULATED.

Controllers can share a `stall_id` with different `station_name`s; the overview
shows their worst severity. Each controller publishes under its own prefix
(`RB_MQTT_TOPIC_PREFIX`) and Django subscribes to `RB4107_MQTT_TOPIC=rb4107/+/#`.
Commands go to that controller's own tree.

## Freshness and limitations

- The worker writes a heartbeat every two seconds and reports ready only after
  the broker acknowledges its subscription. Broker loss, worker death, stale
  data and HTTP failure are separate visible conditions.
- Retained messages can fill absent values as **last known**. A retained
  "online" cannot prove liveness, a retained normal snapshot cannot overwrite a
  stored cutoff, and retained telemetry never enters the temperature graph.
- Heartbeats do not refresh temperature, output or reset timestamps. Queued old
  events do not overwrite current state. Older dated snapshots are stored as
  historical and cannot clear a newer shutdown.
- MQTT DUP retransmissions with identical payloads are deduplicated. Within one
  `boot_id`, a message whose `sequence` is not higher than the newest seen is
  stored as `historical` (or `duplicate`) and changes nothing. A new `boot_id`
  is a restart and is logged as `controller_restarted`.
- There is no login, broker TLS or production HTTP service. Keep `runserver`
  on localhost and use an SSH tunnel for remote work. The only command
  endpoints are C4002 tuning and the shutdown reset, and neither has a login.

Running it: [setup.md](setup.md#backend-and-dashboard).
