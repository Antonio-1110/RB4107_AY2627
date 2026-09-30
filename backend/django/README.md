# RB4107 Django backend

The MQTT subscriber stores validated firmware messages in SQLite, and a small
read-only JSON API serves them to the dashboard in [`frontend/`](../../frontend).
Run it on the MacBook (or any computer that can reach the broker). The ESP32-S3
keeps all local safety logic, relay control and manual reset.

```text
Mosquitto → mqtt_subscriber → schema validation → SQLite → Django GET API → frontend/
```

## Setup

```bash
cd backend/django
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
python manage.py migrate
python manage.py check
python manage.py runserver 127.0.0.1:8000
```

Open http://127.0.0.1:8000/ on that computer and leave this terminal running.
Start `python manage.py mqtt_subscriber` in a second activated terminal to
receive hardware data. Django serves the dashboard files from `frontend/` at
`/`, so there is no Node build, separate frontend port or CORS setup. On
Windows, activate with `.venv\Scripts\Activate.ps1` and copy with
`Copy-Item .env.example .env`.

To view the dashboard from another computer, keep `runserver` on 127.0.0.1 and
use an SSH tunnel (`ssh -N -L 8000:127.0.0.1:8000 <user>@<host>`) rather than
exposing it on the network: there is no login.

For a UI-only demo, set `RB4107_LOCATION_CATALOG_FILE=locations.demo.json` in
`.env`, stop the MQTT subscriber and run this in the second terminal:

```bash
python manage.py simulate_fleet --direct
```

This uses the same schema validation, normalization and storage as MQTT and
displays **Demo direct · MQTT bypassed**. It creates nine synthetic stations
across eight stalls. To test the complete MQTT path, keep the subscriber
running and use `python manage.py simulate_fleet` without `--direct`.
Do not run the direct demo and subscriber together against the same database.

## Configuration (environment variables)

`backend/django/.env` is loaded automatically; exported environment variables
take precedence. Broker addresses/passwords stay out of frontend JavaScript.

| Variable | Default | |
|---|---|---|
| `RB4107_MQTT_HOST` | `localhost` | Mosquitto host (the subscriber normally runs on the MacBook itself) |
| `RB4107_MQTT_PORT` | `1883` | |
| `RB4107_MQTT_TOPIC` | `rb4107/#` | its prefix must match the firmware's `RB_MQTT_TOPIC_PREFIX` |
| `RB4107_MQTT_QOS` | `1` | subscription QoS |
| `RB4107_MQTT_CLIENT_ID` | `rb4107-django-subscriber` | |
| `RB4107_MQTT_PERSISTENT_SESSION` | `1` | the broker queues QoS 1 events while the subscriber is down |
| `RB4107_MQTT_USERNAME` / `_PASSWORD` | unset | if the broker requires auth |
| `RB4107_MQTT_RECONNECT_MIN_S` / `_MAX_S` | `1` / `30` | reconnect back-off |
| `RB4107_LOG_LEVEL` | `INFO` | `DEBUG` also logs every routine message |
| `RB4107_SCHEMA_FILE` | `../../docs/schema/rb4107_mqtt.schema.json` | the same schema the firmware is tested against |
| `RB4107_LOCATION_CATALOG_FILE` | `locations.json` | controller ID → site / stall / station; relative paths use `backend/django`. `locations.demo.json` holds the made-up stalls for `simulate_fleet` |
| `RB4107_SQLITE_PATH` | `backend/django/db.sqlite3` | database file; use an absolute path when overriding |
| `RB4107_DEVICE_STALE_SECONDS` | `15` | display freshness; does not change ESP32 safety timers |
| `RB4107_WORKER_STALE_SECONDS` | `10` | subscriber heartbeat timeout |
| `RB4107_FRONTEND_DIR` | `../../frontend` | folder served at `/` |

For multiple real controllers, give each a unique `controller_id` and firmware
topic prefix, e.g. `rb4107/controller_01` and `rb4107/controller_02`, then set
`RB4107_MQTT_TOPIC=rb4107/+/#`. Sharing the same retained topic would overwrite
another controller's broker snapshot. The original single-controller
`rb4107/#` setup still works unchanged. The simulator publishes non-retained
packets and replaces `+` in the configured prefix with each demo controller ID.

## Run the persistent subscriber (TODO section 24)

```bash
python manage.py mqtt_subscriber                      # uses the environment variables above
python manage.py mqtt_subscriber --host 192.168.1.127 --stats-interval 30
```

It runs until Ctrl-C / `SIGTERM`. Example output:

```text
[MQTT][INFO] connecting to localhost:1883 as rb4107-django-subscriber
[MQTT][INFO] broker connected (localhost:1883, session new)
[MQTT][INFO] subscribed to rb4107/# (Granted QoS 1)
[TELEMETRY][INFO] controller_01 seq=4127 2026-09-26T00:00:00+08:00 | state=UNATTENDED unattended=18.4s ...
[EVENT][WARNING] controller_01 WARNING: UNATTENDED -> WARNING (unattended timeout), unattended 60.0s
[MQTT][WARNING] rejected message on rb4107/events/warning: malformed JSON: ...
[MQTT][WARNING] broker disconnected (Unspecified error); reconnecting
[MQTT][INFO] broker connected (localhost:1883, session new)
[MQTT][INFO] status: connected, received 1234, rejected 1 | event=3, faults=4, heartbeat=300, ...
[MQTT][INFO] shutting down
[MQTT][INFO] subscriber stopped (received 1240, rejected 1)
```

| Requirement | Implementation |
|---|---|
| Management command | `ingest/management/commands/mqtt_subscriber.py` |
| Connect / subscribe | `ingest/subscriber.py`, which resubscribes on every (re)connect |
| Reconnect after broker failure | paho `loop_forever(retry_first_connection=True)` with back-off `RECONNECT_MIN_S`–`RECONNECT_MAX_S`; also covers a broker that isn't up yet when the subscriber starts |
| Graceful shutdown | `SIGINT`/`SIGTERM` → clean MQTT disconnect, then exit |
| Connection status | logged on every connect/disconnect, plus a `status:` line every `--stats-interval` s |
| Missed events while down | persistent session (fixed client ID, `clean_session=False`), so the broker queues QoS 1 events |

These were checked by hand against Mosquitto 2.0.18: broker down at startup,
malformed message, broker restart, `SIGTERM`. `ingest/tests/test_subscriber.py`
automates the round trip whenever a broker is running.

To keep it running on the MacBook, use a terminal tab, `tmux`, or a launchd
agent.

## How messages are handled

`ingest/validation.py`: every message is

1. checked against the topic tree (`rb4107/controller/...`, `sensors/<node>/...`, `events/...`),
2. decoded as UTF-8 JSON (at most 8 KB, and it must be an object),
3. checked for a supported `schema_version` (currently 2),
4. checked for a `type` allowed on that topic,
5. validated against the JSON Schema, with required fields and field types.

Anything that fails is logged as `[MQTT][WARNING] rejected message on <topic>: <reason>`
and dropped. A malformed message never stops the subscriber, and neither does
a bug in a handler.

`ingest/handlers.py` is the application layer. It stores messages using
`ingest/storage.py`, then logs telemetry (INFO),
warnings, shutdowns and entering FAULT (WARNING), fault raise/clear, node
status, and controller online/offline. Periodic heartbeat, presence and
thermal messages are counted and logged only at DEBUG.

`ingest/normalization.py` maps schema v2 to dashboard fields. `ingest/views.py`
serves read-only `/api/health/`, `/api/devices/`, and per-device `latest/`,
`history/`, `events/` endpoints. A worker heartbeat distinguishes a disconnected
broker or stopped subscriber from a silent device. See
[`docs/dashboard.md`](../../docs/dashboard.md)
for the exact field contract, retained-message semantics and limitations.

## Tests

```bash
python manage.py test ingest
```

Full TCP MQTT → SQLite → HTTP smoke test, using an isolated temporary database
and local test broker (no hardware or existing broker needed):

```bash
pip install -r requirements-test.txt
python ../../tools/dashboard/smoke_test.py
```

Optional browser coverage: install Playwright for Node (`npm install playwright`),
then set `RB4107_BROWSER_CHECK=1` when running the smoke test. `RB4107_CHROMIUM_PATH`
can select an already-installed Chromium executable.

The fixtures in `ingest/tests/firmware_samples.jsonl` are real messages
published by the controller firmware (run in QEMU against Mosquitto). The
broker integration test is skipped when no broker is running.
