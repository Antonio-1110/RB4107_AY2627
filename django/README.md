# Django backend

Receives the controller's MQTT messages, stores them in SQLite and serves a
JSON API plus the dashboard in [`frontend/`](../frontend). The API is read-only
except for two commands it forwards to the controller over MQTT: the shutdown
reset ([remote_reset.md](../docs/remote_reset.md)) and C4002 radar tuning
([c4002_tuning.md](../docs/c4002_tuning.md)). Safety logic and the relay stay
on the ESP32-S3; a reset only has an effect while it is in SHUTDOWN (or as the
acknowledgement in WARNING).

```text
Mosquitto → mqtt_subscriber → schema validation → SQLite → Django API → frontend/
```

First-time setup and running it: [docs/setup.md](../docs/setup.md#backend-and-dashboard).
API fields and how they map from the firmware: [docs/dashboard.md](../docs/dashboard.md).

## Commands

Run from this folder with the virtualenv active.

| Command | What it does |
|---|---|
| `python manage.py runserver 127.0.0.1:8000` | API + dashboard at http://127.0.0.1:8000/ |
| `python manage.py mqtt_subscriber` | persistent MQTT subscriber; runs until Ctrl-C. `--host`, `--stats-interval` override settings |
| `python manage.py simulate_fleet` | publishes made-up controllers over MQTT (needs the subscriber) |
| `python manage.py simulate_fleet --direct` | same data written straight to the database, no broker. Don't run it together with the subscriber |
| `python manage.py test ingest` | unit tests |

## Configuration (environment variables)

The defaults are the demo setup, so nothing needs setting. To change
something, copy `.env.example` to `django/.env` (loaded automatically);
exported environment variables win.

| Variable | Default | |
|---|---|---|
| `RB4107_MQTT_HOST` / `_PORT` | `localhost` / `1883` | broker |
| `RB4107_MQTT_TOPIC` | `rb4107/+/#` | one tree per controller, `rb4107/<controller_id>` (the firmware's `RB_MQTT_TOPIC_PREFIX`); `rb4107/#` for a single controller on the plain `rb4107` prefix |
| `RB4107_MQTT_QOS` | `1` | subscription QoS |
| `RB4107_MQTT_CLIENT_ID` | `rb4107-django-subscriber` | fixed, so the broker keeps a persistent session |
| `RB4107_MQTT_PERSISTENT_SESSION` | `1` | broker queues QoS 1 events while the subscriber is down |
| `RB4107_MQTT_USERNAME` / `_PASSWORD` | unset | if the broker requires auth |
| `RB4107_MQTT_RECONNECT_MIN_S` / `_MAX_S` | `1` / `30` | reconnect back-off |
| `RB4107_LOG_LEVEL` | `INFO` | `DEBUG` also logs every routine message |
| `RB4107_SCHEMA_FILE` | `../docs/schema/rb4107_mqtt.schema.json` | the schema the firmware is tested against |
| `RB4107_LOCATION_CATALOG_FILE` | `locations.json` | controller ID → site / stall / station. `locations.demo.json` is for `simulate_fleet` |
| `RB4107_SQLITE_PATH` | `django/db.sqlite3` | use an absolute path when overriding |
| `RB4107_DEVICE_STALE_SECONDS` | `15` | when the dashboard marks data stale (display only) |
| `RB4107_WORKER_STALE_SECONDS` | `10` | subscriber heartbeat timeout |
| `RB4107_FRONTEND_DIR` | `../frontend` | folder served at `/` |

Several real controllers need a unique `controller_id` and topic prefix each
(e.g. `rb4107/controller_01`), otherwise their retained messages overwrite
each other.

## How it works

| File | Job |
|---|---|
| `ingest/subscriber.py` | connects, resubscribes on every reconnect, backs off while the broker is down, disconnects cleanly on Ctrl-C / `SIGTERM` |
| `ingest/validation.py` | checks topic, UTF-8 JSON ≤ 8 KB, `schema_version` 2, `type` allowed on that topic, then the JSON Schema. Failures are logged as `rejected message` and dropped; nothing stops the subscriber |
| `ingest/handlers.py` | stores each message (`storage.py`) and logs it: telemetry at INFO, warnings/shutdowns/FAULT at WARNING, routine heartbeat/sensor messages at DEBUG |
| `ingest/normalization.py` | maps schema v2 to dashboard fields |
| `ingest/views.py` | `/api/health/`, `/api/devices/`, and per device `latest/`, `history/`, `events/` |

## Tests

```bash
python manage.py test ingest                  # the broker round trip is skipped without Mosquitto
pip install -r requirements-test.txt
python ../tools/dashboard/smoke_test.py       # full MQTT → SQLite → HTTP path, temporary broker and database
```

Set `RB4107_BROWSER_CHECK=1` for the smoke test to also drive a browser
(needs Playwright for Node). The fixtures in `ingest/tests/firmware_samples.jsonl`
are real messages from the controller firmware.
