# ESP32-S3 controller architecture

How the controller firmware (`firmware/29_end_to_end`, wired by
`firmware/components/rb_controller_app`) is put together. This covers TODO
sections 7, 8, 11, 12, 16, 19 and 22.

## Tasks and queues (sections 11, 12)

```text
ESP-NOW callback (Wi-Fi task)
       │  validated packet, never blocks (queue overflow counted)
       ▼
  rx queue ──→ safety task (prio 12, 100 ms tick)          components/rb_controller
                 ├─ sensor_node: sequence, health, UNKNOWN handling
                 ├─ safety state machine                    components/safety
                 ├─ outputs → buzzer / shutdown relay        components/rb_outputs
                 ├─ fault polling / node events              components/rb_app
                 ├─ snapshot (mutex, struct copy only)
                 └─ event queue (xQueueSend timeout 0) ──→ telemetry task (prio 3) → MQTT
```

| Guideline | How |
|---|---|
| Safety above telemetry | `RB_CTRL_SAFETY_TASK_PRIO` (12) > `RB_CTRL_TELEMETRY_TASK_PRIO` (3); the console runs at 2 and the continuity monitor at 1 |
| No unnecessary tasks | one safety task. Drivers are called from it; the buzzer pattern runs on an `esp_timer`. |
| No uncontrolled global state | the safety task owns the node state and the state machine; others get copies (`rb_controller_get_snapshot`) |
| Queues | ESP-NOW → safety (`rx queue`), safety → telemetry (`event queue`) |
| Notifications / flags | reset and config-change requests are flags under a spinlock, read on the next tick |
| Shared resources protected | snapshot mutex; board I2C bus (driver-level lock) |
| Non-blocking timing | the safety task waits on its queue for at most one tick; every timer is `now - start >= timeout` on the `esp_timer` clock (`components/rb_time`) |

The safety task never waits on anything the telemetry side owns. Events are
posted with a zero timeout, the snapshot lock is only held for a struct copy,
and only the telemetry task calls the MQTT client. So an MQTT stall can't
stop safety processing (checked by the critical failure test, section 30).

## Receiving sensor data (sections 7, 8)

The ESP-NOW callback validates each frame (length, magic, version, type, CRC)
and queues it without blocking. The safety task then:

- checks the node ID against `RB_CTRL_NODE_ID`;
- tracks sequence numbers: gaps (missed packets), duplicates, out-of-order
  packets, node restarts;
- tracks link health, `NEVER_SEEN → ONLINE → STALE → OFFLINE`, from the time
  since the last packet (`RB_CTRL_NODE_STALE_MS`, `RB_CTRL_NODE_OFFLINE_MS`),
  and emits fault/recovery events.

**Missing data is not safe.** Presence is only `PRESENT`/`ABSENT` when the
node is ONLINE, the data is fresh and the C4002 reading is valid; otherwise it
is `UNKNOWN`, which puts the state machine in FAULT.

## Faults (section 16)

`components/fault_manager` is the one list of faults, and
`components/rb_app` raises and clears them. Every change is logged and
published on `events/fault`.

| Fault | Class | Raised by |
|---|---|---|
| `c4002_unavailable` | **SAFETY** | node reports presence invalid/missing |
| `mlx90640_unavailable` | **SAFETY** | node reports thermal invalid/missing |
| `sensor_node_offline` | **SAFETY** | no packet for `RB_CTRL_NODE_OFFLINE_MS` |
| `shutdown_output_failure` | **SAFETY** | relay expander write/read-back failure |
| `self_test_failed` | **SAFETY** | outputs not usable at boot |
| `espnow_invalid_packet` | telemetry | bad length/magic/version/type/CRC counters rising |
| `espnow_link_degraded` | telemetry | node STALE |
| `rtc_failure` | telemetry | PCF85063 not responding |
| `network_disconnected` | telemetry | Ethernet/Wi-Fi down |
| `mqtt_disconnected` | telemetry | broker unreachable |
| `rx_queue_overflow` | telemetry | ESP-NOW packets dropped before the safety task saw them* |
| `event_queue_overflow` | telemetry | telemetry events dropped |

\* A dropped sensor packet on its own is not a safety problem. If packets stop
arriving, node health (STALE/OFFLINE) catches it, and that is safety-class.

**Only SAFETY faults feed the state machine.** A telemetry fault such as
*MQTT disconnected* is logged and reported but never changes the safety state.

## MQTT client (section 19)

`components/rb_mqtt` wraps ESP-MQTT (`espressif/mqtt`):

| Requirement | Implementation |
|---|---|
| Broker address | menuconfig `RB_BROKER_HOST` / `RB_BROKER_PORT` (never hard-coded) |
| Connect | started once the network has an IP (`components/rb_connectivity`) |
| Detect disconnect | `MQTT_EVENT_DISCONNECTED` plus keep-alive (`RB_MQTT_KEEPALIVE_S`) |
| Automatic reconnect | every `RB_MQTT_RECONNECT_MS` |
| Connection state | `DISCONNECTED` / `CONNECTING` / `CONNECTED`, logged on change, separate from the safety state |
| Presence on the broker | retained `controller_status` (`"online": true`) on `controller/status`, with a Last Will saying `"online": false` |

## Publishing strategy (section 22)

`components/rb_telemetry`:

| Kind | What | QoS |
|---|---|---|
| Periodic, every `RB_MQTT_TELEMETRY_PERIOD_MS` (1 s) | `controller/state` (retained), `controller/heartbeat`, `sensors/<node>/presence`, `sensors/<node>/thermal` | 0 |
| State transition | fresh `controller/state` immediately | 1 |
| Warning activated | `events/warning` | 1 |
| Shutdown activated | `events/shutdown` | 1 |
| FAULT state entered | `events/fault` (`state_change`) | 1 |
| Fault raised / cleared (incl. sensor node offline / restored) | `events/fault` + retained `controller/faults` | 1 |
| Node health change | retained `sensors/<node>/status` | 1 |

A lost routine sample is replaced one period later, so QoS 0 is enough.
Events happen once and matter, so they use QoS 1 and are kept in the bounded
outbox while the broker is unreachable. Raw thermal frames are never
published. Topics: [mqtt_topics.md](mqtt_topics.md); payloads:
[mqtt_schema.md](mqtt_schema.md).

Publishing everything through `esp_mqtt_client_enqueue()` was tried and
rejected: QoS 0 items flooded the outbox and the QoS 1 warning/shutdown
events were lost. `rb_mqtt` publishes directly from the telemetry task.
