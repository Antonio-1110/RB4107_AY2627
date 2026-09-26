# ESP32-S3 controller architecture

How the controller firmware (`firmware/29_end_to_end`, wired by
`firmware/components/rb_controller_app`) is put together. This covers TODO
sections 7, 8, 11, 12, 16, 19 and 22.

## Tasks and queues (sections 11, 12)

```text
presence node A, presence node B, thermal node (three ESP32-C6)
       │  ESP-NOW
ESP-NOW callback (Wi-Fi task)
       │  validated packet, never blocks (queue overflow counted)
       ▼
  rx queue ──→ safety task (prio 12, 100 ms tick)          components/rb_controller
                 ├─ sensor_node: per node sequence, health, UNKNOWN handling;
                 │  combine the two radars
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
| No uncontrolled global state | the safety task owns the node states and the state machine; others get copies (`rb_controller_get_snapshot`) |
| Queues | ESP-NOW → safety (`rx queue`), safety → telemetry (`event queue`) |
| Notifications / flags | reset and config-change requests are flags under a spinlock, read on the next tick |
| Shared resources protected | snapshot mutex; board I2C bus (driver-level lock) |
| Non-blocking timing | the safety task waits on its queue for at most one tick; every timer is `now - start >= timeout` on the `esp_timer` clock (`components/rb_time`) |

The safety task never waits on anything the telemetry side owns. Events are
posted with a zero timeout, the snapshot lock is only held for a struct copy,
and only the telemetry task calls the MQTT client. So an MQTT stall can't
stop safety processing (checked by the critical failure test, section 30).

## Receiving sensor data (sections 7, 8)

There are three sensor nodes, each an ESP32-C6 with one sensor (see
[protocol.md](protocol.md)):

| Slot | Node ID (menuconfig) | Firmware | Sends |
|---|---|---|---|
| presence A | `RB_CTRL_PRESENCE_A_NODE_ID` (1) | `05a_c6_presence_node` | C4002 reading |
| presence B | `RB_CTRL_PRESENCE_B_NODE_ID` (2) | `05a_c6_presence_node` | C4002 reading |
| thermal | `RB_CTRL_THERMAL_NODE_ID` (3) | `05b_c6_thermal_node` | MLX90640 features |

`RB_CTRL_PRESENCE_NODE_COUNT = 1` runs with presence A only (bench use).

The ESP-NOW callback validates each frame (length, magic, version, type,
role, CRC) and queues it without blocking. The safety task then
(`components/sensor_node`, `node_set_*`):

- routes the packet to its node by node ID, and drops it if the ID isn't
  configured or the node has the wrong role (e.g. a thermal board flashed
  with a radar's ID). Both are counted and raise `espnow_unknown_node`;
- tracks sequence numbers per node: gaps (missed packets), duplicates,
  out-of-order packets, node restarts;
- tracks each node's link health, `NEVER_SEEN → ONLINE → STALE → OFFLINE`,
  from the time since its last packet (`RB_CTRL_NODE_STALE_MS`,
  `RB_CTRL_NODE_OFFLINE_MS`), and emits fault/recovery events per node.

**Missing data is not safe.** A node's reading only counts when the node is
ONLINE, the data is fresh and the sensor says it is valid. Otherwise that
radar's presence is `UNKNOWN` and the thermal reading is invalid.

**Combining the two radars (strict).** The state machine gets one presence
value (`presence_fuse`):

| Radar A | Radar B | Presence |
|---|---|---|
| PRESENT | anything | PRESENT |
| anything | PRESENT | PRESENT |
| ABSENT | ABSENT | ABSENT |
| any other combination (one UNKNOWN, none PRESENT) | | UNKNOWN → FAULT |

So a person seen by either radar keeps the kitchen "attended", and "nobody
there" needs both radars working and agreeing. On top of that, every node
going offline or reporting an invalid sensor is its own SAFETY fault, so
losing any one of the three nodes puts the controller in FAULT (buzzer fault
pattern, SHUTDOWN after `RB_SAFETY_FAULT_SHUTDOWN_S`).

## Faults (section 16)

`components/fault_manager` is the one list of faults, and
`components/rb_app` raises and clears them. Every change is logged and
published on `events/fault`.

| Fault | Class | Raised by |
|---|---|---|
| `presence_a_unavailable`, `presence_b_unavailable` | **SAFETY** | that radar's reading is invalid, missing or stale |
| `thermal_unavailable` | **SAFETY** | the thermal reading is invalid, missing or stale |
| `presence_a_node_offline`, `presence_b_node_offline`, `thermal_node_offline` | **SAFETY** | no packet from that node for `RB_CTRL_NODE_OFFLINE_MS` |
| `shutdown_output_failure` | **SAFETY** | relay expander write/read-back failure |
| `self_test_failed` | **SAFETY** | outputs not usable at boot |
| `espnow_invalid_packet` | telemetry | bad length/magic/version/type/CRC counters rising |
| `espnow_link_degraded` | telemetry | any node STALE (detail: bit per slot) |
| `espnow_unknown_node` | telemetry | packets from an unconfigured node ID, or from a node with the wrong role |
| `rtc_failure` | telemetry | PCF85063 not responding |
| `network_disconnected` | telemetry | Ethernet/Wi-Fi down |
| `mqtt_disconnected` | telemetry | broker unreachable |
| `rx_queue_overflow` | telemetry | ESP-NOW packets dropped before the safety task saw them* |
| `event_queue_overflow` | telemetry | telemetry events dropped |

\* A dropped sensor packet on its own is not a safety problem. If packets stop
arriving, node health (STALE/OFFLINE) catches it, and that is safety-class.

The faults of every configured node start out raised at boot ("no data yet")
and clear as each node is heard.

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
