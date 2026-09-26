# 29 – ESP32-S3 controller (end-to-end, diagnostics, critical failure test)

> **System firmware: flash this on the ESP32-S3.** Together with
> [`05_espnow_c6_sender`](../05_espnow_c6_sender) on the C6, this is the running system.

The production controller firmware for the **Waveshare ESP32-S3-ETH-8DI-8RO**.
It covers TODO sections **29** (end-to-end integration), **27** (diagnostic
mode) and **30** (critical failure test). Every component is wired in
`components/rb_controller_app`; the architecture is described in
[`docs/architecture.md`](../../docs/architecture.md).

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

## Setup

1. **MacBook:** `tools/mqtt/start_broker.sh`, then `tools/mqtt/lan_ip.sh`, and note the IP.
2. **MacBook:** `cd backend/django && python manage.py mqtt_subscriber`
3. **S3** (`idf.py menuconfig` here): `RB_BROKER_HOST` = MacBook IP, `RB_ESPNOW_CHANNEL`.
   Flash it and note the MAC it prints.
4. **C6** (`firmware/05_espnow_c6_sender`): `RB_NODE_CONTROLLER_MAC` = S3 MAC, same channel. Flash.

## Diagnostic console (section 27)

Type at the `rb4107>` prompt in `idf.py monitor`:

| Command | Shows / does |
|---|---|
| `status` | everything below |
| `presence` | latest presence reading (PRESENT / ABSENT / UNKNOWN) |
| `thermal` | thermal features |
| `espnow` | receive statistics, last sequence, missed/duplicate/out-of-order/restarts |
| `node` | node link state and sensor validity |
| `safety` | state, time in state, unattended time, outputs, test/normal timers |
| `faults` | active faults with class, time and count |
| `mqtt` | network and MQTT state, publish statistics |
| `timers test / normal` | warning 5 s / shutdown 10 s (menuconfig `RB_DIAG_TEST_*`) ↔ normal |
| `reset` | operator reset / acknowledge |
| `log <TAG> <level>` | change log verbosity at runtime |
| `sim on` then `sim present / absent / presence-invalid` | simulated C4002 (power off the real node first) |
| `sim temp <C> [rate]`, `sim thermal-invalid / thermal-valid` | simulated MLX90640 features |
| `sim node-off / node-on` | the simulated node disappears / returns |

**No duplicated safety logic.** Simulated inputs are real protocol packets
injected where ESP-NOW would deliver them. They pass through the same
sequence tracking, node health, fault manager, state machine and telemetry.
`timers test` only swaps the numbers in the same state machine, and MQTT
telemetry reports `"test_timers": true` while it is active.

## Running without hardware (QEMU)

`sdkconfig.qemu` switches to emulated Ethernet (broker on the host at
`10.0.2.2`), the simulated node, simulated outputs and test timers:

```bash
idf.py -B build_qemu -DSDKCONFIG=build_qemu/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" \
       qemu --qemu-extra-args="-nic user,model=open_eth"
```

Start Mosquitto on the host first. Never flash a build made with this
overlay onto the real controller.

## End-to-end test (section 29)

Run `python3 tools/diagnostics/e2e_check.py --host <MacBook IP> --duration 600`
on the MacBook. For a quicker run, type `timers test` in the console.

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

## Critical failure test (section 30)

The continuity monitor (`RB_DIAG_CONTINUITY_MONITOR`, on by default here,
priority 1) watches the safety path whenever MQTT is down. It logs a line
every 5 s and prints a verdict when MQTT reconnects:

```text
W CRITICAL: MQTT down 10 s: safety loop ALIVE (last iteration 90 ms ago, 140 loops so far), state SHUTDOWN, node ONLINE, buzzer SHUTDOWN, relay ACTIVE
W CRITICAL: outage 15 s: max safety-loop gap 92 ms, 210 loops, 3 transitions during outage,
            mqtt fault reported, state across reconnect SHUTDOWN -> SHUTDOWN -> PASS
```

Procedure:

1. Heat the pan with the cook present (MONITORING). For a shorter test, type `timers test`.
2. On the MacBook: `python3 tools/diagnostics/critical_failure_test.py --host <MacBook IP> --outage 45`
   - it stops Mosquitto (`--stop-cmd`, default `pkill -x mosquitto`);
   - **while the broker is down, walk away.** The S3 must still go
     UNATTENDED → WARNING (buzzer) → SHUTDOWN (relay);
   - it restarts Mosquitto and checks that telemetry resumes, the controller
     didn't reboot, the safety loop kept running and the MQTT fault was
     reported.
3. Check that the S3 serial log ends with `-> PASS`.
4. **Repeat with the MacBook disconnected entirely** (unplug its network or
   shut it down). The S3 log is the record for that run.

| TODO item | Evidence |
|---|---|
| MQTT disconnects, S3 reports telemetry fault | `FAULT: TELEMETRY fault RAISED: mqtt_disconnected`; `fault_cleared` event after reconnect |
| ESP-NOW continues | continuity log `node ONLINE` throughout |
| Safety state machine continues | continuity log `safety loop ALIVE`, transitions during the outage |
| Buzzer / relay continue | continuity log `buzzer WARNING`, `relay ACTIVE`; listen and measure |
| S3 reconnects automatically, telemetry resumes | `MQTT: broker connected`; script `[PASS] telemetry resumed` |
| Safety state never reset by MQTT failure | monitor `state across reconnect X -> X`; script before/after state |

## Done without hardware

All of the following were run in QEMU against Mosquitto on the host, with
the simulated node and outputs:

- **End to end:** driven from the console (`sim temp 120`, `sim absent`,
  `sim present`, `reset`) with the Django subscriber and `e2e_check.py`
  running. All 10 MQTT-side checks passed (88 messages, 0 invalid), and
  Django logged the telemetry and the WARNING/SHUTDOWN events.
- **Critical failure:** Mosquitto was killed while the simulated cook walked
  away. WARNING and SHUTDOWN happened with the broker down; the continuity
  monitor and `critical_failure_test.py` both reported PASS.
- **Publishing:** 510 messages in 125 s, all schema-valid, with no sequence
  gaps after the broker connected.

What these runs can't cover is exactly what needs the bench: real sensors,
the radio link, the buzzer and the relay.
