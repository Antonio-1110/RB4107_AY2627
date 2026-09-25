# RB4107 Django backend: MQTT ingestion

TODO sections 23 and 24. This runs on the MacBook, not on an ESP32. For now
its only job is MQTT ingestion (no frontend or dashboard yet).

```text
Mosquitto ──▶ persistent MQTT subscriber (manage.py mqtt_subscriber) ──▶ ingest.handlers (Django application layer)
```

## Setup

```bash
cd backend/django
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python manage.py check
```

## Configuration (environment variables)

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

## How messages are handled

`ingest/validation.py`: every message is

1. checked against the topic tree (`rb4107/controller/...`, `sensors/<node>/...`, `events/...`),
2. decoded as UTF-8 JSON (at most 8 KB, and it must be an object),
3. checked for a supported `schema_version` (currently 1),
4. checked for a `type` allowed on that topic,
5. validated against the JSON Schema, with required fields and field types.

Anything that fails is logged as `[MQTT][WARNING] rejected message on <topic>: <reason>`
and dropped. A malformed message never stops the subscriber, and neither does
a bug in a handler.

`ingest/handlers.py` is the application layer. It logs telemetry (INFO),
warnings, shutdowns and entering FAULT (WARNING), fault raise/clear, node
status, and controller online/offline. Periodic heartbeat, presence and
thermal messages are counted and logged only at DEBUG. To store or act on
data, add it here.

## Tests

```bash
python manage.py test ingest
```

The fixtures in `ingest/tests/firmware_samples.jsonl` are real messages
published by the firmware (project 22, run in QEMU against Mosquitto).
