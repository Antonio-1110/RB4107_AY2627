# 21 – MQTT JSON schema

TODO section 21. Target: **ESP32-S3** (or any target: the code is plain C).

| Item | Where |
|---|---|
| Schema version | `"schema_version": 1` in every message (`RB_JSON_SCHEMA_VERSION`) |
| Serialisation | `components/rb_json`: bounded writer (no heap), NaN written as `null`, strings escaped, returns 0 on overflow |
| Schema document | [`docs/mqtt_schema.md`](../../docs/mqtt_schema.md) |
| Machine-readable schema | [`docs/schema/rb4107_mqtt.schema.json`](../../docs/schema/rb4107_mqtt.schema.json) (JSON Schema 2020-12) |
| Validation | `tools/diagnostics/validate_json.py` (host) and the Django subscriber, both using the same schema file |
| Room for more sensors | extra fields are allowed; sensor data is keyed by `sensor_node`; new sensor objects can be added without breaking v1 readers |

```bash
idf.py build flash monitor | python3 ../../tools/diagnostics/validate_json.py
```

Each example message prints as one line, and the validator should report
`0 problems`. The serialisers are also unit-tested in project 28.
