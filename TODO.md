# RB4107 Cooking Safety System — TODO

## Goal

Build the software stack for the RB4107 cooking-safety prototype:

```text
C4002 + MLX90640
        ↓
ESP32-C6 Sensor Node
        ↓ ESP-NOW
ESP32-S3 Central Controller
        ↓
Local Safety State Machine
        ├── Buzzer
        ├── Relay / Shutdown
        │
        ↓ Ethernet / Wi-Fi
MQTT Broker — MacBook
        ↓
Django MQTT Subscriber
```

> **Status legend:** `[x]` = implemented in this repository. Items that need
> physical hardware (flashing, wiring, measurements) stay `[ ]` until someone
> has checked them on the bench. See `README.md` for the section → folder map.

> **Critical architectural requirement:**  
> The local safety system must continue operating if MQTT, Django, the MacBook, or the network becomes unavailable.

Anything downstream of Django is currently out of scope.

---

# 0. Repository / Project Structure

- [x] Establish clean repository structure for the different software components.

Suggested structure:

```text
RB4107/
├── firmware/
│   ├── sensor_node_c6/
│   ├── controller_s3/
│   └── shared/
│
├── backend/
│   └── django/
│
├── tools/
│   ├── mqtt/
│   └── diagnostics/
│
├── docs/
│
├── TODO.md
└── README.md
```

- [x] Keep shared communication definitions synchronized between C6 and S3.
- [ ] Avoid monolithic firmware files.
- [ ] Centralize configurable parameters.
- [ ] Document build/flash/test procedures.

---

# 1. ESP32-C6 Sensor Node

Hardware:

- DFRobot FireBeetle 2 ESP32-C6
- C4002 mmWave presence sensor
- MLX90640 32 × 24 thermal sensor

The C6 should primarily perform:

```text
Sensor acquisition
        ↓
Basic processing / filtering
        ↓
Feature extraction
        ↓
Packet construction
        ↓
ESP-NOW transmission
```

The C6 should **not** contain the overall cooking safety state machine.

## 1.1 Basic ESP-IDF Project

- [x] Create ESP-IDF project for FireBeetle ESP32-C6.
- [x] Verify build.
- [ ] Verify flashing.
- [ ] Verify serial logging.
- [ ] Verify board remains stable after hardware assembly.
- [x] Create centralized pin/configuration definitions.

---

# 2. C4002 Integration

- [x] Identify exact C4002 communication interface being used.
- [ ] Confirm required UART/GPIO configuration.
- [x] Initialize C4002.
- [x] Read presence information.
- [x] Read moving/stationary target information if supported.
- [x] Read target distance if supported.
- [x] Add validity/error detection.
- [ ] Test continuous operation.
- [ ] Investigate current false static-presence detections.
- [x] Expose configurable C4002 sensitivity/detection parameters where supported.

Create a clean internal representation similar to:

```cpp
struct PresenceReading {
    bool valid;

    bool presence_detected;
    bool moving_target;
    bool stationary_target;

    float distance_m;

    uint32_t timestamp_ms;
};
```

> Only include fields actually supported by the hardware/API.

---

# 3. MLX90640 Integration

- [x] Configure I2C.
- [x] Initialize MLX90640.
- [x] Read complete 32 × 24 thermal frame.
- [x] Verify all 768 temperature readings.
- [x] Check sensor error conditions.
- [x] Add invalid-reading detection.
- [ ] Test thermal readings at different distances.
- [ ] Test thermal readings against different heat sources.

## 3.1 Thermal Feature Extraction

Implement derived features such as:

- [x] Maximum temperature.
- [x] Minimum temperature.
- [x] Mean temperature.
- [x] Hottest-region temperature.
- [x] Number of pixels above threshold.
- [x] Temperature rate of change.
- [x] Sensor validity.

Candidate structure:

```cpp
struct ThermalReading {
    bool valid;

    float max_temp_c;
    float mean_temp_c;
    float hot_region_temp_c;

    float temp_rate_c_per_min;

    uint16_t pixels_above_threshold;

    uint32_t timestamp_ms;
};
```

- [x] Make thresholds configurable.
- [x] Keep raw thermal frame accessible for diagnostics.
- [x] Do NOT continuously transmit the entire 768-pixel frame unless diagnostic mode requires it.

---

# 4. Shared ESP-NOW Protocol

Create a shared/versioned communication protocol.

- [x] Define protocol version.
- [x] Define node ID.
- [x] Define sequence number.
- [x] Define message types.
- [x] Define sensor validity flags.
- [x] Define heartbeat mechanism.
- [x] Define fault message.
- [x] Verify packet size against ESP-NOW limits.
- [x] Consider struct packing/alignment.
- [x] Add compile-time size checks.
- [x] Document protocol.

Example:

```cpp
enum class MessageType : uint8_t {
    SENSOR_DATA,
    HEARTBEAT,
    SENSOR_FAULT
};
```

Candidate packet:

```cpp
struct SensorPacket {
    uint8_t protocol_version;

    uint32_t node_id;
    uint32_t sequence_number;

    MessageType message_type;

    PresenceReading presence;
    ThermalReading thermal;

    uint32_t uptime_ms;
};
```

Do not assume arbitrary C structs are automatically safe wire protocols.

---

# 5. ESP-NOW — C6 Sender

- [x] Initialize ESP-NOW.
- [x] Configure S3 as peer.
- [x] Implement packet transmission.
- [x] Increment sequence numbers.
- [x] Track send success/failure.
- [x] Implement periodic heartbeat.
- [x] Implement sensor-data publishing.
- [x] Implement sensor-fault publishing.
- [x] Log useful diagnostics without flooding serial output.

Test:

```text
C4002 ──┐
        ├─→ C6 → ESP-NOW → S3
MLX90640┘
```

---

# 6. ESP32-S3 Central Controller

Target hardware:

**Waveshare ESP32-S3-ETH-8DI-8RO**

Use:

- ESP-IDF
- FreeRTOS

Suggested project structure:

```text
main/
├── app_main.cpp
│
├── safety/
│   ├── safety_manager.cpp
│   ├── safety_manager.h
│   ├── safety_state.cpp
│   └── safety_config.h
│
├── communication/
│   ├── espnow_manager.cpp
│   ├── espnow_manager.h
│   ├── mqtt_manager.cpp
│   ├── mqtt_manager.h
│   └── protocol.h
│
├── sensors/
│   ├── sensor_state.cpp
│   └── sensor_state.h
│
├── hardware/
│   ├── relay.cpp
│   ├── relay.h
│   ├── buzzer.cpp
│   ├── buzzer.h
│   ├── rtc.cpp
│   └── rtc.h
│
├── system/
│   ├── fault_manager.cpp
│   ├── fault_manager.h
│   └── system_state.h
│
└── config/
    ├── pins.h
    └── system_config.h
```

- [x] Adjust structure if ESP-IDF components provide cleaner separation.

---

# 7. ESP-NOW — S3 Receiver

- [x] Initialize ESP-NOW.
- [x] Receive C6 packets.
- [x] Validate protocol version.
- [x] Validate packet size.
- [x] Validate node ID.
- [x] Track sequence number.
- [x] Track last packet timestamp.
- [x] Detect duplicate/out-of-order packets where relevant.
- [x] Update internal sensor-node state.

Candidate:

```cpp
struct SensorNodeState {
    uint32_t node_id;

    SensorPacket latest_data;

    uint32_t last_received_ms;
    uint32_t last_sequence;

    bool online;
};
```

---

# 8. Sensor Node Health Monitoring

Implement:

```text
ONLINE
   ↓
STALE
   ↓
OFFLINE
```

- [x] Define configurable stale timeout.
- [x] Define configurable offline timeout.
- [x] Detect missing packets.
- [x] Detect invalid sensor readings.
- [x] Detect sensor recovery.
- [x] Generate fault events.
- [x] Generate recovery events.

> Missing sensor data must never automatically mean "safe."

For example, missing C4002 data must not simply become:

```text
presence = false
```

Missing data is a separate condition.

---

# 9. Safety State Machine

Implement the safety logic independently from hardware drivers.

Initial state model:

```text
BOOT
  ↓
SELF_TEST
  ↓
IDLE
  ↓
MONITORING
  ↓
UNATTENDED
  ↓
WARNING
  ↓
SHUTDOWN
```

Also support:

```text
FAULT
```

- [x] Define state enum.
- [x] Implement state-entry logic.
- [x] Implement state-exit logic.
- [x] Implement explicit transition conditions.
- [x] Log every transition.
- [x] Keep state logic independent from MQTT.

---

# 10. Initial Safety Behaviour

## IDLE → MONITORING

- [x] Detect thermal condition indicating active cooking/heating.
- [x] Transition into MONITORING.

## MONITORING → UNATTENDED

- [x] Detect absence of person.
- [x] Start unattended timer.

## UNATTENDED → MONITORING

- [x] Detect person returning.
- [x] Cancel/reset unattended timer.

## UNATTENDED → WARNING

Initial prototype target:

```text
~60 seconds unattended
```

- [x] Make timeout configurable.
- [x] Activate warning behaviour.

## WARNING → SHUTDOWN

Initial prototype target:

```text
~90 seconds
```

- [ ] Clarify whether 90 s means total unattended time or 90 s after warning. *(Both supported: `RB_SAFETY_SHUTDOWN_TIMING`; default = total unattended time.)*
- [x] Keep timing configurable.
- [x] Activate shutdown output.

## Temperature Interaction

- [x] Keep architecture ready for temperature trend to modify timer/state behaviour.
- [x] Do NOT hard-code unverified thermal assumptions.
- [ ] Collect experimental data first.

---

# 11. Non-Blocking Timing

Do NOT use:

```cpp
delay(60000);
```

for safety behaviour.

Use:

- [x] `esp_timer`
- [x] FreeRTOS timing
- [x] monotonic timestamps
- [x] state-entry timestamps

Pattern:

```cpp
if (state == UNATTENDED) {
    if (now - unattended_start >= warning_timeout) {
        transition_to(WARNING);
    }
}
```

The controller must continue processing sensor updates while timers run.

---

# 12. FreeRTOS Architecture

Candidate architecture:

```text
ESP-NOW callback
       ↓
Sensor Queue
       ↓
Safety Task
       ↓
State Machine
       ↓
Hardware Outputs
```

Other lower-priority functionality:

```text
MQTT Task
Network handling
Diagnostics
Fault monitoring
```

- [x] Keep safety processing higher priority than telemetry.
- [x] Avoid unnecessary tasks.
- [x] Avoid uncontrolled global shared state.
- [x] Use queues where appropriate.
- [x] Use task notifications/event groups where appropriate.
- [x] Protect genuinely shared resources.

Critical requirement:

```text
MQTT stalls
    ↓
Safety task MUST continue
```

---

# 13. Buzzer Driver

- [ ] Determine correct Waveshare buzzer interface/pin. *(Default GPIO46 active-high from the legacy code; GPIO vs PWM selectable in menuconfig.)*
- [x] Implement initialization.
- [x] Implement ON.
- [x] Implement OFF.
- [x] Implement non-blocking warning pattern if needed.

Suggested abstraction:

```cpp
buzzer_init();
buzzer_on();
buzzer_off();
buzzer_set_pattern(...);
```

---

# 14. Relay / Shutdown Driver

- [x] Identify correct Waveshare relay interface. *(TCA9554 @0x20 per legacy code; verify on board.)*
- [ ] Confirm relay polarity. *(Configurable: `RB_SHUTDOWN_POLARITY`, `RB_RELAY_ACTIVE_LEVEL`.)*
- [ ] Determine safe boot state. *(Configurable: `RB_SHUTDOWN_BOOT_STATE`; glitch-free init order implemented.)*
- [x] Implement initialization.
- [x] Implement shutdown activation.
- [x] Implement shutdown release/reset.

Suggested API:

```cpp
shutdown_output_init();
shutdown_activate();
shutdown_release();
```

Do not place raw GPIO operations throughout the state machine.

---

# 15. RTC / Time

Use two different concepts of time.

## Monotonic time

Use for:

- [x] Unattended timer.
- [x] Warning timer.
- [x] Sensor timeout.
- [x] Communication timeout.
- [x] State duration.

## RTC / Wall Clock

Use for:

- [x] Event timestamps.
- [x] Logs.
- [x] MQTT messages.

- [x] Initialize Waveshare RTC.
- [ ] Verify RTC persistence.
- [x] Provide clean time API.

Safety timing must NOT depend on wall-clock correctness.

---

# 16. Fault Manager

Create centralized fault handling.

Potential faults:

- [x] C4002 unavailable.
- [x] MLX90640 unavailable.
- [x] Sensor node offline.
- [x] Invalid ESP-NOW packet.
- [x] ESP-NOW communication failure.
- [x] RTC failure.
- [x] Network disconnected.
- [x] MQTT disconnected.
- [x] Internal queue overflow.
- [x] Other hardware faults.

Distinguish between:

```text
SAFETY-RELEVANT FAULT
```

and:

```text
TELEMETRY / NON-CRITICAL FAULT
```

For example:

```text
MQTT disconnected
```

must not stop the safety system.

---

# 17. S3 Network Connectivity

Preferred:

```text
ESP32-S3
    ↓
Ethernet
    ↓
Local LAN
    ↓
MacBook
```

- [x] Configure Waveshare Ethernet interface.
- [ ] Obtain IP address.
- [ ] Verify LAN connectivity.
- [ ] Verify communication with MacBook.
- [x] Log network state.
- [x] Implement reconnect/recovery behaviour.

Wi-Fi may remain available as an alternative if needed.

---

# 18. MacBook MQTT Broker

Use **Mosquitto** unless there is a strong reason to choose another broker.

- [ ] Install Mosquitto.
- [x] Configure broker.
- [x] Allow connections from LAN.
- [ ] Start broker.
- [ ] Determine MacBook LAN IP.
- [ ] Verify port 1883 availability.
- [ ] Test local publish/subscribe.
- [ ] Test publish/subscribe from another LAN device.
- [x] Document commands in README.

Initial architecture:

```text
ESP32-S3
    ↓
<MacBook LAN IP>:1883
    ↓
Mosquitto
```

Do not hard-code the MacBook address deep in firmware.

---

# 19. MQTT Client on S3

- [x] Initialize ESP-IDF MQTT client.
- [x] Configure broker IP.
- [x] Connect.
- [x] Detect disconnect.
- [x] Automatically reconnect.
- [x] Track connection state.

Internal states:

```text
DISCONNECTED
CONNECTING
CONNECTED
```

MQTT state must remain independent from safety state.

---

# 20. MQTT Topic Structure

Initial proposal:

```text
rb4107/
├── controller/
│   ├── status
│   ├── heartbeat
│   ├── state
│   └── faults
│
├── sensors/
│   └── <node_id>/
│       ├── presence
│       ├── thermal
│       └── status
│
└── events/
    ├── warning
    ├── shutdown
    └── fault
```

- [x] Finalize topic naming.
- [x] Document topics.
- [x] Keep topic prefix configurable.

---

# 21. MQTT JSON Schema

Use JSON initially.

Example telemetry:

```json
{
  "protocol_version": 1,
  "controller_id": "controller_01",
  "sensor_node": "node_01",
  "timestamp": "2026-09-26T00:00:00+08:00",
  "sequence": 4127,

  "presence": {
    "valid": true,
    "detected": false
  },

  "thermal": {
    "valid": true,
    "max_c": 84.2,
    "mean_c": 42.8,
    "rate_c_per_min": 1.7
  },

  "safety": {
    "state": "UNATTENDED",
    "unattended_ms": 18400,
    "buzzer": false,
    "shutdown": false
  },

  "faults": []
}
```

- [x] Define schema version.
- [x] Implement serialization.
- [x] Validate generated JSON.
- [x] Document schema.
- [x] Keep room for additional sensors.

---

# 22. MQTT Publishing Strategy

## Periodic telemetry

Initial target:

```text
~1 Hz
```

- [x] Make frequency configurable.

## Event-driven publishing

Immediately publish:

- [x] State transitions.
- [x] Warning activation.
- [x] Shutdown activation.
- [x] Fault raised.
- [x] Fault cleared.
- [x] Sensor node offline.
- [x] Sensor node restored.

Candidate QoS:

```text
Routine telemetry → QoS 0
Important events  → QoS 1
```

- [x] Confirm final QoS choices.

Do not flood MQTT with unnecessary raw thermal frames.

---

# 23. Django Project

Scope is currently only MQTT ingestion.

- [x] Create Django project.
- [x] Create appropriate Django app.
- [x] Add MQTT dependency.
- [x] Add configuration for broker address.
- [x] Connect to Mosquitto.
- [x] Subscribe to `rb4107/#`.
- [x] Receive messages.
- [x] Parse JSON.
- [x] Validate protocol/schema version.
- [x] Validate required fields.
- [x] Log incoming telemetry.
- [x] Log incoming events.
- [x] Handle malformed messages safely.

No frontend/dashboard is required yet.

---

# 24. Persistent Django MQTT Subscriber

Do not attach MQTT subscription lifecycle to HTTP requests.

Implement a persistent subscriber process.

Preferred interface:

```bash
python manage.py mqtt_subscriber
```

- [x] Create Django management command.
- [x] Connect to broker.
- [x] Subscribe.
- [x] Process messages.
- [x] Reconnect after broker failure.
- [x] Handle graceful shutdown.
- [x] Log connection status.

Architecture:

```text
Mosquitto
    ↓
Persistent MQTT subscriber
    ↓
Django application layer
```

---

# 25. Logging

Use structured, readable logs.

Examples:

```text
[SAFETY][INFO] MONITORING -> UNATTENDED
[ESPNOW][WARN] node_01 packet timeout
[SENSOR][INFO] node_01 restored
[MQTT][WARN] broker disconnected
[MQTT][INFO] broker reconnected
[THERMAL][ERROR] invalid MLX90640 reading
[SAFETY][WARN] warning timeout reached
[OUTPUT][WARN] shutdown relay activated
```

- [x] Add appropriate logging to every major subsystem.
- [x] Avoid excessive logs inside high-frequency loops.
- [x] Make debug verbosity configurable.

---

# 26. Configuration

Centralize configuration for:

- [x] GPIO assignments.
- [x] Relay polarity.
- [x] ESP-NOW peer MAC address.
- [x] Node IDs.
- [x] Sensor timeouts.
- [x] Presence filtering/debounce.
- [x] Temperature thresholds.
- [x] Temperature-rate thresholds.
- [x] Warning timeout.
- [x] Shutdown timeout.
- [x] MQTT broker address.
- [x] MQTT port.
- [x] MQTT topic prefix.
- [x] MQTT telemetry frequency.

Avoid magic numbers.

---

# 27. Diagnostic Mode

Provide development/diagnostic functionality.

- [x] Print latest presence reading.
- [x] Print thermal features.
- [x] Print ESP-NOW packet information.
- [x] Print node health.
- [x] Print safety state.
- [x] Print active faults.
- [x] Print MQTT connection status.
- [x] Allow simulated sensor inputs where practical.
- [x] Allow accelerated safety timers for testing.

Example:

```text
Normal:
warning = 60 s
shutdown = 90 s

Test:
warning = 5 s
shutdown = 10 s
```

Do not duplicate the actual safety logic just for test mode.

---

# 28. State Machine Testing

Make safety logic testable independently from physical hardware.

Test at minimum:

- [x] IDLE → MONITORING.
- [x] MONITORING → UNATTENDED.
- [x] UNATTENDED → MONITORING.
- [x] UNATTENDED → WARNING.
- [x] WARNING → SHUTDOWN.
- [x] Person returns during UNATTENDED.
- [x] Person returns during WARNING.
- [x] Temperature drops during unattended period.
- [x] C4002 becomes unavailable.
- [x] MLX90640 becomes unavailable.
- [x] Sensor node disappears.
- [x] Sensor node returns.
- [x] MQTT disconnects.
- [x] MQTT reconnects.

---

# 29. End-to-End Integration Test

Final prototype path:

```text
C4002 ─────┐
           │
           ├→ ESP32-C6
           │      ↓
MLX90640 ──┘   ESP-NOW
                  ↓
              ESP32-S3
                  ↓
           Safety State Machine
             ↙          ↘
          Buzzer        Relay
                  │
                  ↓
              Ethernet
                  ↓
              Mosquitto
              on MacBook
                  ↓
                MQTT
                  ↓
          Django Subscriber
```

Verify:

- [ ] Presence readings reach S3.
- [ ] Thermal readings reach S3.
- [ ] Safety state responds correctly.
- [ ] Buzzer activates correctly.
- [ ] Relay activates correctly.
- [ ] S3 connects to MacBook broker.
- [ ] Telemetry appears in Mosquitto.
- [ ] Django receives telemetry.
- [ ] Django receives safety events.

---

# 30. Critical Failure Test

Perform:

```text
System operating normally
        ↓
Turn off Mosquitto
        ↓
MQTT disconnects
        ↓
S3 reports telemetry fault
        ↓
BUT
        ↓
ESP-NOW continues
        ↓
Safety state machine continues
        ↓
Buzzer continues functioning
        ↓
Relay continues functioning
```

Then:

- [ ] Restart Mosquitto.
- [ ] Verify S3 reconnects automatically.
- [ ] Verify MQTT telemetry resumes.
- [ ] Verify safety state was never reset by MQTT failure.

Repeat similar test by disconnecting the MacBook entirely.

---

# 31. Development Order

Do not attempt everything simultaneously.

## Milestone 1 — C6 Hardware

- [ ] ESP-IDF C6 project works.
- [ ] C4002 works independently.
- [ ] MLX90640 works independently.

### Success

```text
Sensors → C6 → Serial Monitor
```

---

## Milestone 2 — ESP-NOW

- [ ] Define shared protocol.
- [ ] Implement C6 sender.
- [ ] Implement S3 receiver.
- [ ] Implement node health monitoring.

### Success

```text
Sensors → C6 → ESP-NOW → S3 → Serial Monitor
```

---

## Milestone 3 — Safety State Machine

Use simulated sensor data initially.

- [ ] Implement states.
- [ ] Implement timers.
- [ ] Implement transitions.
- [ ] Implement tests.

### Success

```text
Simulated Inputs
       ↓
Safety State Machine
       ↓
Correct State Transitions
```

---

## Milestone 4 — Physical Safety Outputs

- [ ] Integrate buzzer.
- [ ] Integrate relay.
- [ ] Verify boot state.
- [ ] Verify shutdown behaviour.

### Success

```text
State Machine
   ├→ Buzzer
   └→ Relay
```

---

## Milestone 5 — Full Sensor Integration

- [ ] Feed actual ESP-NOW sensor data into safety state machine.
- [ ] Tune presence behaviour.
- [ ] Collect thermal data.
- [ ] Begin threshold experimentation.

### Success

```text
Real Sensors
    ↓
Safety State Machine
    ↓
Physical Outputs
```

---

## Milestone 6 — Ethernet

- [ ] Configure S3 Ethernet.
- [ ] Connect to LAN.
- [ ] Verify MacBook connectivity.

---

## Milestone 7 — MQTT

- [ ] Install/configure Mosquitto.
- [ ] Implement S3 MQTT client.
- [ ] Publish telemetry.
- [ ] Publish events.
- [ ] Implement reconnect.

### Success

```text
S3 → Ethernet → Mosquitto → mosquitto_sub
```

---

## Milestone 8 — Django

- [ ] Create Django project.
- [ ] Implement persistent MQTT subscriber.
- [ ] Parse/validate telemetry.
- [ ] Log events.

### Success

```text
S3
 ↓
MQTT
 ↓
Mosquitto
 ↓
Django
 ↓
Validated/logged data
```

---

# 32. Important Open Questions

Do not silently invent answers to these.

- [ ] Exact C4002 API/library.
- [ ] Exact C4002 UART pins.
- [ ] Exact MLX90640 I2C pins.
- [ ] Exact S3 ESP-NOW/Wi-Fi configuration.
- [ ] Waveshare buzzer interface.
- [ ] Waveshare relay interface.
- [ ] Relay polarity.
- [ ] Safe relay state during boot.
- [ ] RTC implementation/details.
- [ ] Final cooking temperature threshold.
- [ ] Final temperature derivative algorithm.
- [ ] Presence debounce/filtering requirements.
- [ ] Exact warning timing.
- [ ] Exact shutdown timing.
- [ ] Behaviour if person returns during WARNING.
- [ ] Behaviour if temperature falls while unattended.
- [ ] Behaviour when a safety-critical sensor fails.
- [ ] Whether Ethernet or Wi-Fi will be used for final S3 → MQTT communication.

Mark unresolved hardware dependencies clearly instead of guessing.

---

# 33. Engineering Rules

1. **Safety logic runs locally on the ESP32-S3.**
2. **MQTT/Django must never be required for shutdown.**
3. **No blocking delays in safety logic.**
4. **Network failures must not block safety processing.**
5. **Missing sensor data is not equivalent to a safe reading.**
6. **Separate hardware drivers from safety logic.**
7. **Centralize configuration.**
8. **Version communication protocols.**
9. **Make experimental thresholds configurable.**
10. **Make the state machine independently testable.**
11. **Log state transitions and faults clearly.**
12. **Implement one milestone at a time and verify it before adding the next layer.**

---

# Definition of Done

The software stack is considered functionally integrated when this works:

```text
C4002 + MLX90640
        ↓
ESP32-C6
        ↓
ESP-NOW
        ↓
ESP32-S3
        ↓
Safety State Machine
     ↙       ↘
 Buzzer     Relay
        │
        ↓
Ethernet / Wi-Fi
        ↓
Mosquitto on MacBook
        ↓
Django MQTT Subscriber
```

AND:

```text
MQTT / MacBook / Django unavailable
                ↓
ESP32-S3 safety controller remains operational
                ↓
ESP-NOW sensor communication remains operational
                ↓
Safety decisions continue
                ↓
Buzzer and shutdown relay remain functional
```

That independence between the **local safety path** and the **telemetry path** is a core system requirement.
