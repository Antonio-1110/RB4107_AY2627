# AES dashboard integration (v2.2)

The AES frontend now runs inside the repository's existing Django project.
It uses the existing `mqtt_subscriber` and JSON-schema validator; there is no
second MQTT ingestion service or separate database to synchronize.

## Data path and files

1. Firmware publishes schema v2 on the configured MQTT prefix.
2. `ingest/subscriber.py` validates messages, preserving MQTT retain/QoS/DUP metadata.
3. `ingest/handlers.py` calls `storage.persist()` before writing its existing logs.
4. `ingest/normalization.py` adapts controller fields to the dashboard contract.
5. SQLite holds controller snapshots, field timestamps, packet records,
   temperature samples, events and worker heartbeat.
6. `ingest/views.py` serves JSON; `templates/dashboard.html` and
   `static/dashboard.js` poll the same Django origin.

The frontend is the existing lightweight Django-template/JavaScript dashboard,
including multi-stall overview, location filters, station selector and canvas
temperature graph. No frontend build process is required.

New frontend/config/docs/test-runner files use the `.jw` marker. Importable
Python modules, Django migration names and management commands use normal
Python/Django filenames so discovery and imports work.

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
| `safety.state == SHUTDOWN` AND `safety.shutdown == true` | `manual_reset_required` | Documented firmware latch semantics; see below |
| `safety.unattended_ms` | `unattended_seconds` | Unit conversion only; browser/server never advances this safety timer |
| `uptime_ms` | `uptime_seconds` | Unit conversion |
| `nodes[]` and sensor topics | `sensors[node_id]` | Distinct C4002/MLX90640 diagnostics for each controller |
| `safety.test_timers` | `test_timers` | Explicit firmware-test-timers badge, separate from simulation badge |

Schema v2 has no separate manual-reset boolean. The adapter derives it from
the published SHUTDOWN state and shutdown output because
[`docs/safety_state_machine.md`](safety_state_machine.md) and
`firmware/components/safety/src/safety.c` define SHUTDOWN as latched until
operator reset. It does not infer shutdown from temperature, absence duration
or network loss. If future firmware changes this contract, update this adapter
or consume a new explicit reset field.

The top sticky banner requires isolated supply AND manual reset required.
WARNING and UNATTENDED remain in summary, tiles, table and stall detail without
a sticky warning banner. Missing or invalid thermal values stay null. Cooking
active flags and warning/shutdown threshold settings are not sent by current
firmware, so those fields remain unknown rather than assuming 60/90 seconds.

## Locations and more controllers

Edit `backend/django/location_catalog.json`, keyed by actual `controller_id`.
The default `controller_01` entry is an undeployed integration bench. Other
entries are clearly labelled synthetic station assignments using the earlier
Changi reference catalogue. They appear only when those controllers actually
report. They are not a verified current airport floor plan or a claim of deployment.
Unknown IDs are accepted and displayed as unassigned locations.

Multiple controllers can share a `stall_id` while having different
`station_name` values. Their worst displayed severity is used in the overview;
each station remains selectable. Physical multi-controller deployments need
unique firmware prefixes (e.g. rb4107/controller_01) and unique controller IDs.
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
- MQTT DUP retransmissions with identical payloads are deduplicated. Identical
  normal messages are not globally deduplicated: firmware currently has no
  unique boot ID. With `timestamp=null`, definitive cross-reboot/out-of-order
  comparison is unavailable; non-retained snapshots use receipt time. A future
  boot_id + monotonic sequence contract should address that before deployment.
- No login, retention job, broker TLS setup or production HTTP service is added
  by this prototype integration. Keep runserver bound to localhost and use an
  SSH tunnel for remote development. Existing .env/database files are ignored
  by Git. No actuator/reset HTTP endpoint or MQTT command publisher is present.

## Running on servera with Fish

From the repository checkout:

```fish
cd backend/django
python3 -m venv .venv
source .venv/bin/activate.fish
python -m pip install -r requirements.txt
cp -n .env.example .env
python manage.py migrate
python manage.py runserver 127.0.0.1:8000
```

Second server terminal, after editing RB4107_MQTT_HOST in `.env` to the broker:

```fish
cd ~/RB4107_AY2627/backend/django
source .venv/bin/activate.fish
python manage.py mqtt_subscriber
```

For a UI demo instead of real MQTT ingestion, use `python manage.py simulate_fleet --direct`.
Change the checkout path above if you cloned elsewhere. These commands use the
repository's `manage.py`, not the old standalone `manage.jw.py`.

Windows PowerShell, kept open while browsing:

```powershell
ssh -o ExitOnForwardFailure=yes -N -L 8010:127.0.0.1:8000 shaohua@servera
```

Open http://127.0.0.1:8010/ on Windows. If 8000 is already occupied, identify the
old development server before restarting; running a second instance on the same
port fails. Header v2.2 identifies this integrated build.
