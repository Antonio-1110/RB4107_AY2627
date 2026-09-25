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

- [ ] Define state enum.
- [ ] Implement state-entry logic.
- [ ] Implement state-exit logic.
- [ ] Implement explicit transition conditions.
- [ ] Log every transition.
- [ ] Keep state logic independent from MQTT.

---

# 10. Initial Safety Behaviour

## IDLE → MONITORING

- [ ] Detect thermal condition indicating active cooking/heating.
- [ ] Transition into MONITORING.

## MONITORING → UNATTENDED

- [ ] Detect absence of person.
- [ ] Start unattended timer.

## UNATTENDED → MONITORING

- [ ] Detect person returning.
- [ ] Cancel/reset unattended timer.

## UNATTENDED → WARNING

Initial prototype target:

```text
~60 seconds unattended
```

- [ ] Make timeout configurable.
- [ ] Activate warning behaviour.

## WARNING → SHUTDOWN

Initial prototype target:

```text
~90 seconds
```

- [ ] Clarify whether 90 s means total unattended time or 90 s after warning.
- [ ] Keep timing configurable.
- [ ] Activate shutdown output.

## Temperature Interaction

- [ ] Keep architecture ready for temperature trend to modify timer/state behaviour.
- [ ] Do NOT hard-code unverified thermal assumptions.
- [ ] Collect experimental data first.

---

# 11. Non-Blocking Timing

Do NOT use:

```cpp
delay(60000);
```

for safety behaviour.

Use:

- [ ] `esp_timer`
- [ ] FreeRTOS timing
- [ ] monotonic timestamps
- [ ] state-entry timestamps

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

- [ ] Keep safety processing higher priority than telemetry.
- [ ] Avoid unnecessary tasks.
- [ ] Avoid uncontrolled global shared state.
- [ ] Use queues where appropriate.
- [ ] Use task notifications/event groups where appropriate.
- [ ] Protect genuinely shared resources.

Critical requirement:

```text
MQTT stalls
    ↓
Safety task MUST continue
```

---

# 13. Buzzer Driver

- [ ] Determine correct Waveshare buzzer interface/pin.
- [ ] Implement initialization.
- [ ] Implement ON.
- [ ] Implement OFF.
- [ ] Implement non-blocking warning pattern if needed.

Suggested abstraction:

```cpp
buzzer_init();
buzzer_on();
buzzer_off();
buzzer_set_pattern(...);
```

---

# 14. Relay / Shutdown Driver

- [ ] Identify correct Waveshare relay interface.
- [ ] Confirm relay polarity.
- [ ] Determine safe boot state.
- [ ] Implement initialization.
- [ ] Implement shutdown activation.
- [ ] Implement shutdown release/reset.

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

- [ ] Unattended timer.
- [ ] Warning timer.
- [ ] Sensor timeout.
- [ ] Communication timeout.
- [ ] State duration.

## RTC / Wall Clock

Use for:

- [ ] Event timestamps.
- [ ] Logs.
- [ ] MQTT messages.

- [ ] Initialize Waveshare RTC.
- [ ] Verify RTC persistence.
- [ ] Provide clean time API.

Safety timing must NOT depend on wall-clock correctness.

---

# 16. Fault Manager

Create centralized fault handling.

Potential faults:

- [ ] C4002 unavailable.
- [ ] MLX90640 unavailable.
- [ ] Sensor node offline.
- [ ] Invalid ESP-NOW packet.
- [ ] ESP-NOW communication failure.
- [ ] RTC failure.
- [ ] Network disconnected.
- [ ] MQTT disconnected.
- [ ] Internal queue overflow.
- [ ] Other hardware faults.

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

- [ ] Configure Waveshare Ethernet interface.
- [ ] Obtain IP address.
- [ ] Verify LAN connectivity.
- [ ] Verify communication with MacBook.
- [ ] Log network state.
- [ ] Implement reconnect/recovery behaviour.

Wi-Fi may remain available as an alternative if needed.

---

# 18. MacBook MQTT Broker

Use **Mosquitto** unless there is a strong reason to choose another broker.

- [ ] Install Mosquitto.
- [ ] Configure broker.
- [ ] Allow connections from LAN.
- [ ] Start broker.
- [ ] Determine MacBook LAN IP.
- [ ] Verify port 1883 availability.
- [ ] Test local publish/subscribe.
- [ ] Test publish/subscribe from another LAN device.
- [ ] Document commands in README.

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

- [ ] Initialize ESP-IDF MQTT client.
- [ ] Configure broker IP.
- [ ] Connect.
- [ ] Detect disconnect.
- [ ] Automatically reconnect.
- [ ] Track connection state.

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

- [ ] Finalize topic naming.
- [ ] Document topics.
- [ ] Keep topic prefix configurable.

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

- [ ] Define schema version.
- [ ] Implement serialization.
- [ ] Validate generated JSON.
- [ ] Document schema.
- [ ] Keep room for additional sensors.

---

# 22. MQTT Publishing Strategy

## Periodic telemetry

Initial target:

```text
~1 Hz
```

- [ ] Make frequency configurable.

## Event-driven publishing

Immediately publish:

- [ ] State transitions.
- [ ] Warning activation.
- [ ] Shutdown activation.
- [ ] Fault raised.
- [ ] Fault cleared.
- [ ] Sensor node offline.
- [ ] Sensor node restored.

Candidate QoS:

```text
Routine telemetry → QoS 0
Important events  → QoS 1
```

- [ ] Confirm final QoS choices.

Do not flood MQTT with unnecessary raw thermal frames.

---

# 23. Django Project

Scope is currently only MQTT ingestion.

- [ ] Create Django project.
- [ ] Create appropriate Django app.
- [ ] Add MQTT dependency.
- [ ] Add configuration for broker address.
- [ ] Connect to Mosquitto.
- [ ] Subscribe to `rb4107/#`.
- [ ] Receive messages.
- [ ] Parse JSON.
- [ ] Validate protocol/schema version.
- [ ] Validate required fields.
- [ ] Log incoming telemetry.
- [ ] Log incoming events.
- [ ] Handle malformed messages safely.

No frontend/dashboard is required yet.

---

# 24. Persistent Django MQTT Subscriber

Do not attach MQTT subscription lifecycle to HTTP requests.

Implement a persistent subscriber process.

Preferred interface:

```bash
python manage.py mqtt_subscriber
```

- [ ] Create Django management command.
- [ ] Connect to broker.
- [ ] Subscribe.
- [ ] Process messages.
- [ ] Reconnect after broker failure.
- [ ] Handle graceful shutdown.
- [ ] Log connection status.

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

- [ ] Add appropriate logging to every major subsystem.
- [ ] Avoid excessive logs inside high-frequency loops.
- [ ] Make debug verbosity configurable.

---

# 26. Configuration

Centralize configuration for:

- [ ] GPIO assignments.
- [ ] Relay polarity.
- [ ] ESP-NOW peer MAC address.
- [ ] Node IDs.
- [ ] Sensor timeouts.
- [ ] Presence filtering/debounce.
- [ ] Temperature thresholds.
- [ ] Temperature-rate thresholds.
- [ ] Warning timeout.
- [ ] Shutdown timeout.
- [ ] MQTT broker address.
- [ ] MQTT port.
- [ ] MQTT topic prefix.
- [ ] MQTT telemetry frequency.

Avoid magic numbers.

---

# 27. Diagnostic Mode

Provide development/diagnostic functionality.

- [ ] Print latest presence reading.
- [ ] Print thermal features.
- [ ] Print ESP-NOW packet information.
- [ ] Print node health.
- [ ] Print safety state.
- [ ] Print active faults.
- [ ] Print MQTT connection status.
- [ ] Allow simulated sensor inputs where practical.
- [ ] Allow accelerated safety timers for testing.

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

- [ ] IDLE → MONITORING.
- [ ] MONITORING → UNATTENDED.
- [ ] UNATTENDED → MONITORING.
- [ ] UNATTENDED → WARNING.
- [ ] WARNING → SHUTDOWN.
- [ ] Person returns during UNATTENDED.
- [ ] Person returns during WARNING.
- [ ] Temperature drops during unattended period.
- [ ] C4002 becomes unavailable.
- [ ] MLX90640 becomes unavailable.
- [ ] Sensor node disappears.
- [ ] Sensor node returns.
- [ ] MQTT disconnects.
- [ ] MQTT reconnects.

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
