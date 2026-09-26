# 29 – ESP32-S3 controller (end-to-end, diagnostics, critical failure test)

> **System firmware: flash this on the ESP32-S3.** Together with
> [`05a_c6_presence_node`](../05a_c6_presence_node) on the two presence C6
> boards and [`05b_c6_thermal_node`](../05b_c6_thermal_node) on the thermal C6
> board, this is the running system.

The production controller firmware for the **Waveshare ESP32-S3-ETH-8DI-8RO**.
It covers TODO sections **29** (end-to-end integration), **27** (diagnostic
mode) and **30** (critical failure test). Every component is wired in
`components/rb_controller_app`; the architecture is described in
[`docs/architecture.md`](../../docs/architecture.md).

```text
C4002 #1 → C6 presence node A (05a, node 1) ─┐
C4002 #2 → C6 presence node B (05a, node 2) ─┼─ ESP-NOW
MLX90640 → C6 thermal node    (05b, node 3) ─┘    ↓
                               ESP32-S3 (this project)
               ↓
        combine the two radars (either sees a person → present)
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
   The node IDs (*Sensor node link* menu) default to presence A = 1,
   presence B = 2, thermal = 3; set *Number of presence nodes* to 1 to run
   with a single radar on the bench. Flash it and note the MAC it prints.
4. **The three C6 boards:** `RB_NODE_CONTROLLER_MAC` = S3 MAC, same channel.
   - presence node A and B: [`05a_c6_presence_node`](../05a_c6_presence_node)
     (its README shows how to build node B with ID 2),
   - thermal node: [`05b_c6_thermal_node`](../05b_c6_thermal_node).
5. At boot the S3 logs `sensor nodes: presence node_01 + node_02, thermal
   node_03`, then `ONLINE` for each node as it is heard.

## Diagnostic console (section 27)

Type at the `rb4107>` prompt in `idf.py monitor`:

| Command | Shows / does |
|---|---|
| `status` | everything below |
| `presence` | combined presence used by the state machine, and each radar's reading (PRESENT / ABSENT / UNKNOWN) |
| `thermal` | thermal features from the thermal node |
| `espnow` | receive statistics; per node: last sequence, missed/duplicate/out-of-order/restarts/wrong-role |
| `node` | every node's link state and sensor validity |
| `safety` | state, time in state, unattended time, outputs, test/normal timers |
| `faults` | active faults with class, time and count |
| `mqtt` | network and MQTT state, publish statistics |
| `timers test / normal` | warning 5 s / shutdown 10 s (menuconfig `RB_DIAG_TEST_*`) ↔ normal |
| `reset` | operator reset / acknowledge |
| `log <TAG> <level>` | change log verbosity at runtime |
| `sim on` | start the simulated nodes (all three; power off the real nodes first) |
| `sim present / absent` | what the simulated radars see |
| `sim temp <C> [rate]` | simulated MLX90640 features |
| `sim node-off / node-on [a\|b\|thermal\|presence\|all]` | a simulated node disappears / returns (default all) |
| `sim invalid / valid [a\|b\|thermal\|presence\|all]` | a simulated node's sensor reports invalid / valid |

**No duplicated safety logic.** Simulated inputs are real protocol packets
injected where ESP-NOW would deliver them. They pass through the same
sequence tracking, node health, fault manager, state machine and telemetry.
`timers test` only swaps the numbers in the same state machine, and MQTT
telemetry reports `"test_timers": true` while it is active.

## Running without hardware (QEMU)

`sdkconfig.qemu` switches to emulated Ethernet (broker on the host at
`10.0.2.2`), the three simulated nodes, simulated outputs and test timers:

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
| Presence readings reach S3 | walk in/out of view of each radar; console `presence`; telemetry `presence_state` and each node's `detected` flip | `e2e_check` |
| Thermal readings reach S3 | heat a pan; console `thermal`; `thermal.hot_region_c` rises | `e2e_check` |
| Losing one radar is caught | unplug presence node B; `presence_b_node_offline` fault, state FAULT | by hand |
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
W CRITICAL: MQTT down 10 s: safety loop ALIVE (last iteration 60 ms ago, 220 loops so far), state WARNING, presence ABSENT, buzzer WARNING, relay released
W CRITICAL: outage 25 s: max safety-loop gap 61 ms, 552 loops, 3 transitions during outage,
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
| ESP-NOW continues | console `node`: all three nodes ONLINE throughout; continuity log `presence` follows the radars |
| Safety state machine continues | continuity log `safety loop ALIVE`, transitions during the outage |
| Buzzer / relay continue | continuity log `buzzer WARNING`, `relay ACTIVE`; listen and measure |
| S3 reconnects automatically, telemetry resumes | `MQTT: broker connected`; script `[PASS] telemetry resumed` |
| Safety state never reset by MQTT failure | monitor `state across reconnect X -> X`; script before/after state |

## Done without hardware

All of the following were run in QEMU against Mosquitto on the host, with
the three simulated nodes and simulated outputs, driven from the console:

- **End to end:** `sim temp 120` → MONITORING, `sim absent` → UNATTENDED →
  WARNING → SHUTDOWN, `sim present` + `reset` → back to MONITORING, with the
  Django subscriber and `e2e_check.py` running. All 10 MQTT-side checks
  passed (130 messages, 0 invalid); Django received 322 messages and rejected
  none.
- **One radar lost:** `sim node-off b` while cooking. Radar A still saw the
  cook, but node B going STALE raised `presence_b_unavailable` (SAFETY) and
  the controller went to FAULT, then SHUTDOWN after the fault timeout.
  `sim node-on b` cleared both faults.
- **Thermal node invalid:** `sim invalid thermal` → `thermal_unavailable`,
  FAULT; `sim valid thermal` → back to MONITORING.
- **Critical failure:** Mosquitto was stopped while the simulated cook walked
  away. WARNING and SHUTDOWN happened with the broker down, and the
  continuity monitor reported PASS after the broker came back.
- **Payloads:** all 524 captured messages valid against the schema.

What these runs can't cover is exactly what needs the bench: real sensors,
the radio link, the buzzer and the relay.
