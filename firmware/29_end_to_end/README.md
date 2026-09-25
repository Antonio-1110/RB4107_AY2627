# 29 – End-to-end integration test

TODO section 29. The complete system:

```text
C4002 ──┐
        ├→ ESP32-C6 (firmware/05_espnow_c6_sender)
MLX90640┘      ↓ ESP-NOW
           ESP32-S3 (this project)
               ↓
        Safety state machine
          ↙          ↘
       Buzzer        Relay
               │
               ↓ Ethernet
        Mosquitto on the MacBook (tools/mqtt)
               ↓
        Django subscriber (backend/django)
```

This project is the production controller firmware: every component wired
by `components/rb_controller_app`, real ESP-NOW, real outputs, and the
diagnostic console on the serial port.

## Setup

1. **MacBook:** `tools/mqtt/start_broker.sh`, then `tools/mqtt/lan_ip.sh`, and note the IP.
2. **MacBook:** `cd backend/django && python manage.py mqtt_subscriber`
3. **S3** (`idf.py menuconfig` here): `RB_BROKER_HOST` = MacBook IP, `RB_ESPNOW_CHANNEL`.
   Flash it and note the MAC it prints.
4. **C6** (`firmware/05_espnow_c6_sender`): `RB_NODE_CONTROLLER_MAC` = S3 MAC, same channel. Flash.
5. **MacBook:** `python3 tools/diagnostics/e2e_check.py --host <MacBook IP> --duration 600`
6. For a quicker run: `timers test` in the S3 console (warning 5 s, shutdown 10 s).

## Checklist

| Verify | How | Auto |
|---|---|---|
| Presence readings reach S3 | walk in/out of view; console `presence`; telemetry `presence.detected` flips | `e2e_check` |
| Thermal readings reach S3 | heat a pan; console `thermal`; `thermal.hot_region_c` rises | `e2e_check` |
| Safety state responds correctly | hot pan → MONITORING; walk away → UNATTENDED → WARNING → SHUTDOWN; return + `reset` | `e2e_check` (states) |
| Buzzer activates correctly | listen: warning pattern at WARNING, fast pattern at SHUTDOWN, double chirp in FAULT | by hand |
| Relay activates correctly | listen for the click; measure the contacts at SHUTDOWN | by hand |
| S3 connects to MacBook broker | `controller/status` `online: true`; console `mqtt` | `e2e_check` |
| Telemetry appears in Mosquitto | `tools/mqtt/watch.sh` | `e2e_check` |
| Django receives telemetry | `[TELEMETRY][INFO] controller_01 seq=...` lines | by hand |
| Django receives safety events | `[EVENT][WARNING] controller_01 WARNING/SHUTDOWN ...` | by hand |

## Hardware-free rehearsal (done)

The same firmware was run in QEMU with a simulated node and outputs,
emulated Ethernet, and test timers:

```text
RB_NET_QEMU_OPENETH=y  RB_BROKER_HOST="10.0.2.2"  RB_OUTPUTS_SIMULATED=y
RB_SIM_NODE=y  RB_DIAG_TEST_TIMERS_AT_BOOT=y
```

It was driven from the console (`sim temp 120`, `sim absent`, `sim present`,
`reset`) with Mosquitto, the Django subscriber and `e2e_check.py` running.
All 10 MQTT-side checks passed (88 messages, 0 invalid), and Django logged
the telemetry and the WARNING/SHUTDOWN events. What that run can't cover is
exactly what needs the bench: real sensors, the radio link, the buzzer and
the relay.
