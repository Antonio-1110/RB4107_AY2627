# 12 – FreeRTOS architecture

TODO section 12. Target: **ESP32-S3**. Pair it with the C6 (project 05), or
just watch the node go OFFLINE → FAULT on its own.

```text
ESP-NOW callback (Wi-Fi task)
       │  validated packet, never blocks (queue overflow counted)
       ▼
  rx queue ──→ safety task (prio 12, 100 ms tick)
                 ├─ sensor_node: sequence, health, UNKNOWN handling
                 ├─ safety state machine
                 ├─ outputs hook → buzzer / relay (sections 13–14)
                 ├─ snapshot (mutex, struct copy only)
                 └─ event queue (xQueueSend timeout 0) ──→ telemetry task (prio 3) → MQTT (section 19)
```

The runtime is the `components/rb_controller` component, which every later
S3 project reuses.

| Guideline | How |
|---|---|
| Safety above telemetry | `RB_CTRL_SAFETY_TASK_PRIO` (12) > `RB_CTRL_TELEMETRY_TASK_PRIO` (3) |
| No unnecessary tasks | one safety task. Drivers are called from it; the buzzer pattern runs on an `esp_timer`. |
| No uncontrolled global state | the safety task owns the node state and the state machine; others get copies (`rb_controller_get_snapshot`) |
| Queues | ESP-NOW → safety (`rx queue`), safety → telemetry (`event queue`) |
| Notifications / flags | reset and config-change requests are flags under a spinlock, read on the next tick |
| Shared resources protected | snapshot mutex; board I2C bus (driver-level lock) |

**Critical requirement demo:** the stand-in telemetry task stalls for 8 s
every 30 s. Watch `safety loops=` keep counting and state transitions keep
happening during the stall.
