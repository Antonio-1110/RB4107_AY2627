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
published by the controller firmware (run in QEMU against Mosquitto). The
broker integration test is skipped when no broker is running.
