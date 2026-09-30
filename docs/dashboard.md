# Monitoring dashboard

The dashboard in [`frontend/`](../frontend) shows every controller's reported
state. It reads a read-only JSON API from the Django backend, which is fed by
the existing `mqtt_subscriber` and JSON-schema validator. It can not operate,
silence or reset anything: local protection stays on the ESP32-S3.

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
    ├── api.js        the three GET calls the page makes
    ├── dashboard.js  state, polling and rendering of both views
    ├── chart.js      temperature history canvas
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

## Locations and more controllers

Edit `backend/django/locations.json`, keyed by the firmware's `controller_id`
(`RB_MQTT_CONTROLLER_ID`). The `controller_01` entry is the lab bench.
`backend/django/locations.demo.json` holds made-up stalls for `simulate_fleet`;
select it with `RB4107_LOCATION_CATALOG_FILE=locations.demo.json`. Its terminal
layout is illustrative and its stall names are fictional.
Unknown IDs are accepted and displayed as unassigned locations.

Multiple controllers can share a `stall_id` while having different
`station_name` values. Their worst displayed severity is used in the overview;
each station remains selectable. Physical multi-controller deployments need
unique firmware topic prefixes (`RB_MQTT_TOPIC_PREFIX`, e.g. `rb4107/controller_01`) and unique controller IDs.
Use `RB4107_MQTT_TOPIC=rb4107/+/#` to subscribe to all controller namespaces.
The original rb4107/controller/state single-controller tree remains supported.

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
  by Git. No actuator/reset HTTP endpoint or MQTT command publisher is present.

## Running it

See [`backend/django/README.md`](../backend/django/README.md#setup).
