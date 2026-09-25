# MQTT JSON schema (schema_version 1)

Produced by [`firmware/components/rb_json`](../firmware/components/rb_json).
Machine-readable: [`schema/rb4107_mqtt.schema.json`](schema/rb4107_mqtt.schema.json).
Check any capture with `tools/diagnostics/validate_json.py`.

## Rules

- Every message has `schema_version`, `type`, `controller_id`, `timestamp`,
  `uptime_ms` and `sequence`, except `controller_status`, which has only
  `schema_version`, `type`, `controller_id` and `online`.
- `timestamp` is ISO 8601 with offset (`2026-09-26T00:00:00+08:00`). It is
  `null` while the controller's wall clock is unset (no RTC time and no SNTP
  yet). `uptime_ms` is always there.
- `sequence` counts every message a controller publishes. A gap means lost
  QoS 0 messages; a drop back to a small number means a reboot.
- **Unknown is `null`.** An invalid or missing presence reading has
  `"valid": false` and `"detected": null`, never `false`. Missing sensor data
  is not "nobody there".
- Readers must ignore fields they don't know. New sensors or fields can be
  added without bumping `schema_version`. Removing or changing the meaning of
  a field requires a bump.
- `protocol_version` in telemetry is the ESP-NOW protocol version the sensor
  data arrived with (see [protocol.md](protocol.md)).

## `telemetry` (topic `controller/state`)

```json
{
  "schema_version": 1,
  "type": "telemetry",
  "controller_id": "controller_01",
  "timestamp": "2026-09-26T00:00:00+08:00",
  "uptime_ms": 18400,
  "sequence": 4127,
  "protocol_version": 1,
  "sensor_node": "node_01",
  "node_link": "ONLINE",
  "presence": {"valid": true, "detected": false, "moving": false, "stationary": false, "distance_m": null},
  "thermal": {"valid": true, "max_c": 84.2, "min_c": 21.0, "mean_c": 42.8, "hot_region_c": 80.1,
              "rate_c_per_min": 1.7, "pixels_above_threshold": 37},
  "safety": {"state": "UNATTENDED", "state_ms": 16400, "unattended_ms": 18400,
             "buzzer": "OFF", "shutdown": false, "test_timers": false},
  "faults": ["mqtt_disconnected"]
}
```

## `event` (topics `events/warning`, `events/shutdown`, `events/fault`)

`event` is one of:

| `event` | Topic | When |
|---|---|---|
| `warning` | `events/warning` | the controller entered WARNING |
| `shutdown` | `events/shutdown` | the controller entered SHUTDOWN (relay activated) |
| `state_change` | `events/fault` | the controller entered the FAULT state |
| `fault_raised` / `fault_cleared` | `events/fault` | an individual fault changed (`fault` names it) |

Every state transition also publishes a fresh `telemetry` message on
`controller/state` straight away, at QoS 1.

Events that happen before the first broker connection (the boot-time "no
data yet" faults) are not replayed. The retained `controller/faults` message
always holds the current set.

```json
{
  "schema_version": 1, "type": "event", "controller_id": "controller_01",
  "timestamp": "2026-09-26T00:01:00+08:00", "uptime_ms": 78400, "sequence": 4128,
  "event": "warning",
  "safety": {"state": "WARNING", "from_state": "UNATTENDED", "reason": "unattended timeout", "unattended_ms": 60000},
  "fault": null
}
```

For fault events, `fault` is `{"name": "mqtt_disconnected", "class": "TELEMETRY"}`.

## Other types

| `type` | Topic | Extra fields |
|---|---|---|
| `heartbeat` | `controller/heartbeat` | `safety_state`, `safety_loop_count` (goes up while the safety task is alive) |
| `faults` | `controller/faults` | `faults: [{name, class}]` (active faults) |
| `presence` | `sensors/<node>/presence` | `sensor_node`, `presence{...}` |
| `thermal` | `sensors/<node>/thermal` | `sensor_node`, `thermal{...}` |
| `node_status` | `sensors/<node>/status` | `sensor_node`, `link`, `presence_valid`, `thermal_valid`, `node_fault_flags`, `missed_packets`, `restarts` |
| `controller_status` | `controller/status` | `online` (retained; the Last Will publishes `false`) |
