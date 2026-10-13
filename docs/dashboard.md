# Monitoring dashboard

The dashboard in [`frontend/`](../frontend) shows every controller's reported
state. It reads a JSON API from the Django backend, which is fed by the
existing `mqtt_subscriber` and JSON-schema validator. It can not operate or
silence anything: local protection stays on the ESP32-S3. It sends two kinds of
command: the reset of a latched shutdown ([remote_reset.md](remote_reset.md))
and C4002 radar tuning ([c4002_tuning.md](c4002_tuning.md)).

## Data path and files

1. Firmware publishes schema v2 on the configured MQTT prefix.
2. `ingest/subscriber.py` validates messages, preserving MQTT retain/QoS/DUP metadata.
3. `ingest/handlers.py` calls `storage.persist()` before writing its existing logs.
4. `ingest/normalization.py` adapts controller fields to the dashboard contract.
5. SQLite holds controller snapshots, field timestamps, packet records,
   temperature samples, events and worker heartbeat.
6. `ingest/views.py` serves JSON under `/api/`.
7. `frontend/` (plain HTML, CSS and JavaScript modules, no build step) polls
   that API every two seconds. Django serves it at `/`, so both share one origin.

```text
frontend/
├── index.html        page layout: overview and stall detail views
├── css/dashboard.css
└── js/
    ├── config.js     API base URL, poll interval, request timeout
    ├── api.js        the GET calls the page makes
    ├── dashboard.js  state, polling and rendering of both views
    ├── chart.js      temperature history canvas
    ├── heatmap.js    thermal camera heat map canvas
    ├── countdown.js  Countdown card: phase, clock, bar, sensor flags, live rule values
    ├── mode.js       Monitor / Tuning mode, tuning tabs, overview layout, colour theme
    ├── dom.js        small DOM helpers
    └── format.js     value formatting
```

## Firmware → frontend contract

| Firmware field | Dashboard field | Meaning |
|---|---|---|
| `controller_id` | `device_id` | One controller / cooking station; used as catalogue key |
| `presence_state` | `occupied` | PRESENT → true, ABSENT → false, UNKNOWN → null; no backend radar fusion |
| `thermal.max_c` | `temperature_c` | Reported maximum, only when thermal.valid is true |
| `thermal.mean_c` / `min_c` | `temperature_avg_c` / `temperature_min_c` | Reported thermal measurements |
| `thermal.hot_region_c` / `rate_c_per_min` | same names | Shown below the maximum-temperature card |
| `safety.state` | `safety_state` | Firmware enum in lower case |
| `safety.buzzer` | `alarm_state` | OFF → clear; other enum values in lower case |
| `safety.shutdown` | `relay_state` | true → isolated, false → enabled; **firmware output report, not independent valve feedback** |
| `safety.reset_required` | `manual_reset_required` | Sent by the firmware. For older firmware without it: `safety.state == SHUTDOWN` AND `safety.shutdown` |
| `safety.warning_after_ms` / `shutdown_after_ms` / `shutdown_counts_from` | `warning_after_seconds` / `shutdown_after_seconds` / same | The controller's timer settings |
| `boot_id` + `sequence` | stored on the device | Orders messages and detects restarts (see below) |
| `safety.unattended_ms` | `unattended_seconds` | Unit conversion only; browser/server never advances this safety timer |
| `uptime_ms` | `uptime_seconds` | Unit conversion |
| `nodes[]` and sensor topics | `sensors[node_id]` | Distinct C4002/MLX90640 diagnostics for each controller |
| `safety.test_timers` | `test_timers` | Explicit firmware-test-timers badge, separate from simulation badge |
| `safety.presence_filter` | `presence_filter` (`absence_seconds`, `return_seconds`, `return_gap_seconds`, `source`) | Presence filter in use, shown and changed in the Countdown card ([remote_reset.md](remote_reset.md#presence-filter-and-timers)); `null` from older firmware |
| `safety.presence_unknown`, `safety.thermal_lost` | `presence_unknown`, `thermal_lost` (`null` from older firmware) | The sensor flags: shown as badges in the Countdown card, never turned into a fault by the backend |
| `safety.cooking.active` | `cooking_active` | The heat detector's state, as the controller sees it |
| `safety.tier`, `safety.effective_warning_ms` / `effective_shutdown_ms`, `safety.tiers` | `tier`, `effective_warning_seconds`, `effective_shutdown_seconds`, `tiers` (`null` from older firmware) | The temperature tier in force and the buzzer and shutdown times it gives. The Countdown card draws its clock, bar and marks from the times in force, shows a tier chip, and Tuning > Rules lists the tiers |
| `safety.hold`, `safety.cooking.phase` / `rate_c_per_min` / `settle_c`, `safety.cookware` | `hold`, `cooking_phase`, `cooking_rate_c_per_min`, `cooking_settle_c`, `cookware_profile`, `cookware_tau_s`, `cookware_source` (`null` from older firmware) | The cooking judgment: shown in the Countdown card ("held while the pan cools", the Cooking tile) and the cookware profile selector in Tuning → Rules |
| `safety.absent_ms`, `safety.returning_ms`, `safety.state_ms` | `absent_seconds`, `returning_seconds`, `state_seconds` | Progress of the absence / return debounce and time in the current state. The Countdown card advances its clock from them between reports and freezes it ("last known") when the controller goes quiet; the backend never advances a timer |
| `c4002_live` (per presence node) | `sensors[node_id].c4002_live` | Newest raw radar result only. The live radar view reads the last minute from `GET /api/devices/<id>/nodes/<node>/c4002/live/?after=<id>` every 0.5 s |
| `c4002_config` (per presence node) | `sensors[node_id].c4002` | Radar settings and the result of the last tuning command; a failed command keeps the last known `settings` ([c4002_tuning.md](c4002_tuning.md)) |
| `thermal_frame` message (`sensors/<node>/thermal_frame`) | `GET /api/devices/<id>/thermal_frame/` → `frames[]` | Latest heat-map picture per thermal node, with `received_at` and `age_seconds`. Only the newest picture is kept (table `ThermalFrame`), not logged as an inbound message and never used for state. The card outlines the pixels at or above the controller's cooking temperature (`safety.cooking.on_c`, the node's hot-pixel threshold with older firmware), lets you change that temperature ([remote_reset.md](remote_reset.md#cooking-temperature)), boxes its hot region, and works out the pixel size at a chosen distance for either lens (55° or 110°) |

The firmware publishes `safety.reset_required` (`safety_reset_required()` in
`firmware/components/safety`). For messages from older firmware without it,
the adapter falls back to SHUTDOWN state plus shutdown output, which
[`docs/safety_state_machine.md`](safety_state_machine.md) defines as latched
until operator reset; `reset_status_source` says which was used. It never
infers a shutdown from temperature, absence duration or network loss.

The top sticky banner requires isolated supply AND manual reset required.
WARNING and UNATTENDED remain in summary, tiles, table and stall detail without
a sticky warning banner. Missing or invalid thermal values stay null. The
warning and shutdown times shown are the ones the controller reports.

Display severity (`ingest/locations.py`, `classify_display_state`) uses only
the firmware's states: SHUTDOWN or an isolated supply is critical, WARNING and
UNATTENDED are warnings, FAULT is a fault, IDLE and MONITORING are normal, and
BOOT/SELF_TEST show as "controller starting".

## Monitor and Tuning modes

A **Monitor | Tuning** switch in the header (remembered in this browser; open
`/?mode=tuning` to start in Tuning) changes what the stall page shows. It is
cosmetic: Monitor hides the tuning tools, it does not stop the API from
accepting commands.

- **Monitor** (default, the deployment view) fits one laptop screen without
  scrolling: the Countdown hero with its clock, the three condition tiles
  (cooking, person, timer, values only), the four readings with their
  freshness, a compact thermal picture, the temperature history, system status
  with **Reset shutdown**, and the last five events. Nothing in it can be edited.
- **Tuning** keeps a slim countdown strip on top and adds tabs: **Rules** (the
  cooking temperature, person filter and timers, editable), **Radar** (C4002
  settings and the live radar view), **Thermal** (heat map with the outline and
  pixel-size controls), **Diagnostics** (readings, history, system status,
  events, sensor reports, raw telemetry). The header gets an orange underline
  so it is obvious which mode you are in. The live radar stream is only polled
  in Tuning.

The overview has a **Tiles | Table** switch and a one-row fleet strip. A
**Light | Dark | Auto** theme button follows the system setting by default.

## Countdown card

The biggest card on the stall page. It shows where the safety timeline is and
why, using only what the controller reports:

- **Phase and clock**: "Cooking: confirming that nobody is there" (the absence
  debounce), "Unattended: timer running" (seconds until the buzzer),
  "Warning" (seconds until shutdown), "Shutdown", "Idle". A bar shows the
  unattended time against the warning and shutdown marks.
- **Sensor chips**: each node as `OK`, `SENSOR PROBLEM` (node reachable, sensor
  bad: wiring) or `OFFLINE` (no packets: power, Wi-Fi), plus the
  *presence sensor unknown* and *thermal sensor lost* flags.
- **The three rules** with their live value and adjustable setting:
  1. *Cooking*: the cooking temperature (saved on the controller).
  2. *Person*: absent after / return after / ignore gaps up to.
  3. *Timer*: buzzer after / shut down after.
  Changes to 2 and 3 last until the controller reboots
  ([remote_reset.md](remote_reset.md)). The state itself is decided by the
  safety logic on the S3; the dashboard only displays it and sends settings.

## Storage

Every accepted message is stored with its raw JSON, and each telemetry message
a second time as a reading. Measured on this schema (SQLite, including indexes),
at the default rates with one controller:

| Message | On disk | Rate | Per day |
|---|---|---|---|
| controller telemetry | 3.7 KB | 1 / s | 320 MB |
| heartbeat | 0.58 KB | 1 / s | 50 MB |
| per-node presence and thermal topics | 0.65-0.69 KB | 3 / s | 170 MB |
| raw radar results (live view) | 0.92 KB | about 4 / s | 320 MB |
| **total** | | | **about 0.86 GB per day (26 GB per month)** |

Heat-map pictures are not stored (only the newest per node). `tools/run_dev.sh`
erases the database when it stops, so this only builds up when Django runs
continuously. Left alone, one controller would fill a 256 GB disk in months, so
the subscriber runs a **retention job** once an hour (`ingest/retention.py`;
first run a minute after start):

| Rule | Default | Setting |
|---|---|---|
| radar live results, heartbeats, per-node topics: delete after | 24 h | `RB4107_RETENTION_LIVE_HOURS` |
| telemetry: full resolution for | 24 h | `RB4107_RETENTION_FULL_HOURS` |
| then thinned to one per | 60 s | `RB4107_RETENTION_THIN_SECONDS` |
| and deleted after | 30 days | `RB4107_RETENTION_DAYS` |
| faults, node status, tuning replies, rejected messages: delete after | 90 days | `RB4107_RETENTION_OTHER_DAYS` |
| how often (0 = never) | 3600 s | `RB4107_RETENTION_INTERVAL_S` |

Safety events and the messages that carry them (including restarts) are never
deleted. That keeps one controller at about 0.4 GB of telemetry history plus a
day of detail (roughly 1 GB at the default rates, a few GB at most).
`python manage.py prune_history --dry-run` shows what would go; run it without
`--dry-run` for a first clean-up. SQLite does not give disk space back by
itself: run `sqlite3 db.sqlite3 VACUUM` once after a big clean-up.

## Active cooking

Every controller and stall shows whether its stove is in use (`cooking` in
`/api/devices/`, `classify_cooking()` in `django/ingest/locations.py`): a badge
on the stall tiles and roster, a banner on the stall page, and a *Cooking now*
count. It is display only. The firmware makes the decision: cooking starts
when the thermal camera's hottest pixel reaches the cooking temperature
(`RB_SAFETY_HEAT_ON_DC`, placeholder 50 °C, settable from the Countdown
card; or a fast rise) and ends below the "over" value (10 °C lower by
default). The controller reports both in `safety.cooking`, and the badge text
uses them; the thresholds are still being tuned ([issue #21](https://github.com/Antonio-1110/RB4107_AY2627/issues/21)).

| Controller state | Shown as |
|---|---|
| MONITORING, UNATTENDED, WARNING | COOKING |
| IDLE | NOT COOKING |
| SHUTDOWN | SUPPLY CUT (gas cut, stove cooling) |
| FAULT, BOOT, SELF_TEST or no state | a guess from the hottest pixel with the controller's reported thresholds (50/40 °C if it doesn't report them): LIKELY COOKING (≥ on), LIKELY NOT COOKING (< over), otherwise COOKING UNKNOWN (dashed badge) |

A stall with several stations shows the most active one, and how many of them
are cooking. Stale data is marked *last known*.

## Heat map

The stall detail view draws the thermal camera's latest picture (32 × 24) with
a light-to-dark warm colour scale fitted to each picture, marks the hottest
pixel, shows grey for pixels with no reading, and gives the temperature of the
pixel under the mouse. A picture older than `RB4107_DEVICE_STALE_SECONDS` is
dimmed and labelled stale. `simulate_fleet` also sends synthetic pictures.

The picture is as the sensor sees it. If it looks mirrored or upside down
compared with the kitchen, that comes from how the camera is mounted.

## Locations and more controllers

Edit `django/locations.json`, keyed by the firmware's `controller_id`
(`RB_MQTT_CONTROLLER_ID`). The `controller_01` entry is the lab bench.
`django/locations.demo.json` holds made-up stalls for `simulate_fleet`
(`tools/run_dev.sh --demo` selects it). Its terminal layout is illustrative
and its stall names are fictional. Each demo stall plays the scenario in its
`demo_scenario`: `monitoring`, `unattended`, `warning`, `shutdown` (supply
cut, cooling down), `idle`, `fault_radars` (both radars invalid),
`fault_node_offline` (one radar node unplugged: FAULT, as in the firmware) or
`network_loss` (online, then stale, then offline by Last Will, then back,
every 2 minutes). Simulated stalls are tagged SIMULATED.
Unknown IDs are accepted and displayed as unassigned locations.

Multiple controllers can share a `stall_id` while having different
`station_name` values. Their worst displayed severity is used in the overview;
each station remains selectable. Each controller
publishes under its own prefix (`RB_MQTT_TOPIC_PREFIX`, `rb4107/controller_01`
for the bench) with a unique controller ID, and Django subscribes to
`RB4107_MQTT_TOPIC=rb4107/+/#`. Commands (reset, C4002 tuning) go to that
controller's own tree. A single controller on the plain `rb4107` prefix still
works with `RB4107_MQTT_TOPIC=rb4107/#`.

## Freshness, events and limitations

- The worker writes a heartbeat every two seconds and reports ready only after
  the broker acknowledges subscription. Broker loss, worker death, stale data
  and HTTP failure are separate visible conditions.
- Retained messages can fill previously absent values as **last known**, with
  no live field timestamp. Retained online cannot prove liveness. A retained
  normal snapshot cannot overwrite a stored cutoff. Retained offline marks
  the device offline. Retained telemetry is not added to the temperature graph.
- Heartbeat reports do not refresh temperature, relay or reset field timestamps.
- Events are stored with their raw payload and reason. Queued old events do not
  overwrite current live state. The controller publishes fresh full telemetry
  on transitions; those snapshots drive current safety/relay banners.
- Valid source timestamps participate in field freshness. Older dated full
  snapshots are stored as historical and cannot clear newer shutdown state.
  Graph x-values are server receipt times; `source_at` is also available.
- MQTT DUP retransmissions with identical payloads are deduplicated.
- Within one `boot_id`, a message whose `sequence` is not higher than the
  newest one seen is stored as `historical` (or `duplicate` for the same
  number) and does not change the current state, even when `timestamp` is
  null. A new `boot_id` is a controller restart: it is accepted whatever its
  sequence and logged as a `controller_restarted` event.
- No login, retention job, broker TLS setup or production HTTP service is added
  by this prototype integration. Keep runserver bound to localhost and use an
  SSH tunnel for remote development. Existing .env/database files are ignored
  by Git. The only command endpoints are C4002 tuning and the shutdown reset ([remote_reset.md](remote_reset.md)); neither has a login.

## Running it

See [setup.md](setup.md#backend-and-dashboard).
