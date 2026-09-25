# 22 – MQTT publishing strategy

TODO section 22. Target: **ESP32-S3** (or QEMU). By default a **simulated
sensor node** (`RB_SIM_NODE`) plays a looping scenario, so no C6 is needed.

## Strategy (`components/rb_telemetry`)

| Kind | What | QoS |
|---|---|---|
| Periodic, every `RB_MQTT_TELEMETRY_PERIOD_MS` (1 s) | `controller/state` (retained), `controller/heartbeat`, `sensors/<node>/presence`, `sensors/<node>/thermal` | 0 |
| State transition | fresh `controller/state` immediately | 1 |
| Warning activated | `events/warning` | 1 |
| Shutdown activated | `events/shutdown` | 1 |
| FAULT state entered | `events/fault` (`state_change`) | 1 |
| Fault raised / cleared (incl. sensor node offline / restored) | `events/fault` + retained `controller/faults` | 1 |
| Node health change | retained `sensors/<node>/status` | 1 |

QoS choice: a lost routine sample is replaced one period later, so QoS 0 is
enough. Events happen once and matter, so they use QoS 1 (they are kept in
the bounded outbox while the broker is unreachable). Raw thermal frames are
never published.

The telemetry task runs at priority 3 and only talks to the safety task
through the non-blocking event queue and the snapshot copy. If a publish
blocks, only this task waits.

## Verified in QEMU (Mosquitto on the host)

With `RB_NET_QEMU_OPENETH`, `RB_BROKER_HOST="10.0.2.2"` and
`RB_OUTPUTS_SIMULATED`, running the default 60 s / 90 s timings:

- 510 messages in 125 s (4 periodic topics a second plus events), with no
  gaps in `sequence` after the broker connected;
- all 510 messages valid against `docs/schema/rb4107_mqtt.schema.json`;
- `events/warning` at 60 s unattended, `events/shutdown` at 90 s, fault
  events for the MLX90640 dropout, retained `controller/faults` updates;
- the Last Will `online:false` published when QEMU was killed.

During testing, publishing everything through `esp_mqtt_client_enqueue()`
flooded the outbox with QoS 0 items, and the QoS 1 warning/shutdown events
were lost. `rb_mqtt` now publishes directly from the telemetry task.
