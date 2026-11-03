# ESP32-S3 controller architecture

How the controller firmware (`firmware/controller`, wired by
`firmware/components/rb_controller_app`) is put together.

## Ground rules

1. Safety logic runs locally on the ESP32-S3. The C6 nodes only read and send.
2. MQTT, Django and the network are never needed for a shutdown.
3. No blocking waits in the safety path: every timer is `now - start >= timeout`.
4. Missing sensor data is never a safe reading: it is `UNKNOWN` / invalid and raises a flag. While something is cooking, unknown presence counts as absent; see [safety_state_machine.md](safety_state_machine.md#lost-sensors-flags-not-fault).
5. Drivers are separate from the state machine, which is plain C and unit-tested on a PC.
6. Every tunable is in one Kconfig ([configuration.md](configuration.md)); both protocols (ESP-NOW, MQTT) are versioned.

## Where the code lives (`firmware/components`)

One directory per component (`ls firmware/components`). The ones to know:
`rb_controller` (tasks, queues), `rb_controller_app` (wiring), `safety` (state
machine), `sensor_node` (per-node health, combining radars), `fault_manager` and
`rb_app` (faults), `rb_outputs`, `buzzer` and `shutdown_output` (outputs),
`rb_espnow` and `rb_protocol` (node link), `rb_mqtt`, `rb_telemetry` and
`rb_topics` (publishing), `rb_node_app` and `rb_node_sensors` (the C6 nodes),
`rb_config` (Kconfig).

## Tasks and queues

```text
presence node A, presence node B, thermal node (three ESP32-C6)
       │  ESP-NOW
ESP-NOW callback (Wi-Fi task)
       │  validated packet, never blocks (queue overflow counted)
       ▼
  rx queue ──→ safety task (high prio, 100 ms tick)        components/rb_controller
                 ├─ sensor_node: per node sequence, health, UNKNOWN handling;
                 │  combine the two radars
                 ├─ safety state machine                    components/safety
                 ├─ outputs → buzzer / shutdown output 1     components/rb_outputs
                 ├─ fault polling / node events              components/rb_app
                 ├─ snapshot (mutex, struct copy only)
                 └─ event queue (xQueueSend timeout 0) ──→ telemetry task (low prio) → MQTT
```

- **One safety task**, above telemetry (`RB_CTRL_SAFETY_TASK_PRIO` >
  `RB_CTRL_TELEMETRY_TASK_PRIO`; the console and continuity monitor are lower
  still). Drivers are called from it; the buzzer pattern runs on an `esp_timer`.
- **No shared state**: the safety task owns the node states and the state
  machine; others get copies (`rb_controller_get_snapshot`).
- Reset and config-change requests are flags under a spinlock, read on the next
  tick. The snapshot has a mutex; the board I2C bus a driver-level lock.
- The safety task waits on its queue for at most one tick; timers use the
  `esp_timer` clock (`components/rb_time`).

The safety task never waits on anything the telemetry side owns. Events are
posted with a zero timeout, the snapshot lock is only held for a struct copy,
and only the telemetry task calls the MQTT client. So an MQTT stall can't stop
safety processing (checked by the critical failure test).

## Receiving sensor data

There are three sensor nodes, each an ESP32-C6 with one sensor (packets:
[protocol.md](protocol.md)): presence A and B (`presence_node`, a C4002 reading
each) and thermal (`thermal_node`, MLX90640 features). Node IDs are menuconfig
`RB_CTRL_PRESENCE_A_NODE_ID`, `RB_CTRL_PRESENCE_B_NODE_ID` and
`RB_CTRL_THERMAL_NODE_ID`. `RB_CTRL_PRESENCE_NODE_COUNT = 1` runs with presence A
only (bench use).

The ESP-NOW callback validates each frame (length, magic, version, type, role,
CRC) and queues it without blocking. The safety task then
(`components/sensor_node`, `node_set_*`):

- routes the packet by node ID, and drops it if the ID isn't configured or the
  node has the wrong role (e.g. a thermal board flashed with a radar's ID). Both
  are counted and raise `espnow_unknown_node`;
- tracks sequence numbers per node: gaps, duplicates, out-of-order packets, node
  restarts;
- tracks link health, `NEVER_SEEN → ONLINE → STALE → OFFLINE`, from the time
  since the last packet (`RB_CTRL_NODE_STALE_MS`, `RB_CTRL_NODE_OFFLINE_MS`), and
  emits fault/recovery events per node.

**Missing data is not safe.** A node's reading only counts when the node is
ONLINE, the data is fresh and the sensor says it is valid. Otherwise that
radar's presence is `UNKNOWN` and the thermal reading is invalid.

**Combining the two radars (strict).** The state machine gets one presence
value, *at the stove* (`presence_fuse_all`, the default
`RB_CTRL_PRESENCE_FUSION_ALL`): ABSENT if either radar says absent; PRESENT if
at least one is PRESENT and the other is PRESENT or UNKNOWN; UNKNOWN only if
both are. Why, and what *nearby* means:
[safety_state_machine.md](safety_state_machine.md#who-is-at-the-stove-and-the-pause).
The older "either radar" rule (`presence_fuse`) is still a menuconfig and
dashboard option.

A node going offline or reporting an invalid sensor is its own SAFETY-class
fault in the fault list and raises the `presence_unknown` / `thermal_lost`
flags, but does not put the controller in FAULT: only its own faults (failed
self-test, relay output) do, and those shut down after `RB_SAFETY_FAULT_SHUTDOWN_S`.

## Faults

`components/fault_manager` is the one list of faults (the table of names and
classes is there), and `components/rb_app` raises and clears them. Every change
is logged and published on `events/fault`. Faults of every configured node start
raised at boot ("no data yet") and clear as each node is heard.

**Only SAFETY faults feed the state machine.** A telemetry fault such as *MQTT
disconnected* is logged and reported but never changes the safety state.

| SAFETY fault | Raised by |
|---|---|
| `presence_a_unavailable`, `presence_b_unavailable`, `thermal_unavailable` | that sensor's reading is invalid, missing or stale |
| `presence_a_node_offline`, `presence_b_node_offline`, `thermal_node_offline` | no packet from that node for `RB_CTRL_NODE_OFFLINE_MS` |
| `shutdown_output_failure` | shutdown output expander write/read-back failure |
| `self_test_failed` | outputs not usable at boot |

Telemetry-class faults (`espnow_invalid_packet`, `espnow_link_degraded`,
`node_restarting`, `espnow_unknown_node`, `rtc_failure`, `network_disconnected`,
`mqtt_disconnected`, `rx_queue_overflow`, `event_queue_overflow`) only report.
A dropped sensor packet on its own is not a safety problem: if packets stop
arriving, node health (STALE/OFFLINE) catches it, and that is safety-class.

## MQTT client

`components/rb_mqtt` wraps ESP-MQTT (`espressif/mqtt`). The broker address is
menuconfig (`RB_BROKER_HOST` / `RB_BROKER_PORT`), never hard-coded. It connects
once the network has an IP (`components/rb_connectivity`), detects disconnects
by `MQTT_EVENT_DISCONNECTED` plus keep-alive, and reconnects automatically
(`RB_MQTT_RECONNECT_MS`). Its connection state is logged on change and kept
separate from the safety state. Presence on the broker is a retained
`controller_status` on `controller/status` with a Last Will saying
`"online": false`.

The only incoming topic is `sensors/+/c4002_set` (radar tuning), handled in the
MQTT client's task by `rb_c4002_relay`, which forwards it over ESP-NOW and never
touches the safety task ([c4002_tuning.md](c4002_tuning.md)).

## Publishing strategy

`components/rb_telemetry`. Topics and QoS: [mqtt_schema.md](mqtt_schema.md#topics);
payloads: [mqtt_schema.md](mqtt_schema.md). In short:

- **Periodic** (`RB_MQTT_TELEMETRY_PERIOD_MS`) at QoS 0: state, heartbeat and
  per-node samples. A lost sample is replaced one period later.
- **Events** at QoS 1: state transitions, warning, shutdown, FAULT entered,
  fault raised/cleared, node health and radar tuning answers. They happen once
  and matter, so they are kept in the bounded outbox while the broker is
  unreachable.
- The thermal camera's picture (`thermal_frame`) is published separately for the
  dashboard only, at QoS 0; the safety logic never uses it.

Publishing everything through `esp_mqtt_client_enqueue()` was tried and
rejected: QoS 0 items flooded the outbox and the QoS 1 warning/shutdown events
were lost. `rb_mqtt` publishes directly from the telemetry task.

## Hardware

- Sensor nodes: DFRobot DFR1117 (ESP32-C6). Controller: **Waveshare
  ESP32-S3-POE-ETH-8DI-8DO** (not the 8DI-8RO, which has onboard relays).
- Its 8 outputs are opto-isolated Darlington sinks to GND (up to 500 mA each),
  driven by a TCA9554 at 0x20, so the shutdown switches an external relay or
  contactor coil. Code, Kconfig menus and some issues still say "relay": that
  is the external relay driven by output 1.
- Also a PCF85063 RTC at 0x51 (no battery; time comes from SNTP) and the buzzer on GPIO46.

## Design decisions

- **One node per sensor.** `presence_node` is one firmware flashed on two
  boards; node B is built with `sdkconfig.node_b` in its own build folder.
- **Radars are combined strictly for "at the stove".** ABSENT if any working
  radar says absent, PRESENT only if every working radar sees the person,
  UNKNOWN with no working radar (a flag; it counts as absent only while
  cooking, never as FAULT). "Either radar" is a menuconfig and dashboard
  option (`RB_CTRL_PRESENCE_FUSION`).
- **The S3 does all the presence debouncing.** Nodes send raw readings on
  every change and every 500 ms. The C4002 disappear delay and lock time stay
  at the menuconfig default of 1 s / 1 s (decided 2026-11-03; 0 / 0.2 s was
  considered) and are not on the dashboard.
- **Missing data is never "safe"**: UNKNOWN, invalid, or JSON `null`.
- **Versions.** ESP-NOW protocol v3 and MQTT schema v2. Django only accepts
  schema v2.
- **Wi-Fi only.** The board has a W5500 Ethernet chip, but the controller uses
  Wi-Fi (decided 2026-10-23).
- **The shutdown is output 1 of the board**: energised while safe, so a
  controller without power also cuts the appliance. There is no separate
  valve node.
- **Fixed settings.** Settings written `{=NAME}` in the rules table are fixed
  on purpose (decided 2026-11-03 to keep the dashboard simple): menuconfig or
  `safety.c` only.
- **Unconfirmed hardware values** stay configurable and are flagged
  `UNCONFIRMED` / `OPEN QUESTION` in the Kconfig.
