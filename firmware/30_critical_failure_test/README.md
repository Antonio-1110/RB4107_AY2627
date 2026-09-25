# 30 – Critical failure test

TODO section 30. The most important property of the system:

```text
MQTT / MacBook / Django unavailable
        ↓
ESP32-S3 safety controller keeps running: ESP-NOW, decisions, buzzer, relay
```

This project is the production controller (as in 29) plus a **continuity
monitor** (`main/continuity_monitor.c`, priority 1). While MQTT is down it
logs whether the safety loop is alive, the state, node link, buzzer and
relay every 5 s. After reconnect it prints a verdict:

```text
W CRITICAL: outage 29 s: max safety-loop gap 94 ms, 420 loops, 3 transitions during outage,
            mqtt fault reported, state across reconnect SHUTDOWN -> SHUTDOWN -> PASS
```

## Procedure

1. Run the system as in project 29 (flash this project on the S3 instead).
2. Heat the pan with the cook present (MONITORING). For a shorter test use
   `timers test` in the console.
3. On the MacBook: `python3 tools/diagnostics/critical_failure_test.py --host <MacBook IP> --outage 45`
   - it stops Mosquitto (`--stop-cmd`, default `pkill -x mosquitto`);
   - **while the broker is down, walk away.** The S3 must still go
     UNATTENDED → WARNING (buzzer) → SHUTDOWN (relay);
   - it restarts Mosquitto and checks that telemetry resumes, the controller
     didn't reboot, the safety loop kept running and the MQTT fault was
     reported. QoS 1 events from the outage are listed.
4. Check that the S3 serial log ends with `-> PASS`.
5. **Repeat with the MacBook disconnected entirely** (unplug its network or
   shut it down). The script can't watch that, so the S3 log is the record.
   Reconnect afterwards and check telemetry resumes.

| TODO item | Evidence |
|---|---|
| MQTT disconnects, S3 reports telemetry fault | `FAULT: TELEMETRY fault RAISED: mqtt_disconnected`; `fault_cleared` event after reconnect |
| ESP-NOW continues | continuity log `node ONLINE` throughout |
| Safety state machine continues | continuity log `safety loop ALIVE`, transitions during the outage |
| Buzzer / relay continue | continuity log `buzzer WARNING`, `relay ACTIVE`; listen and measure |
| Restart Mosquitto, S3 reconnects automatically | `MQTT: broker connected`; script `[PASS] telemetry resumed` |
| MQTT telemetry resumes | script checks heartbeats after the restart |
| Safety state never reset by MQTT failure | monitor `state across reconnect X -> X`; script before/after state |

## Rehearsal in QEMU (done)

With a simulated node and outputs (same overrides as project 29's
rehearsal), Mosquitto was killed for 25 s while the simulated cook walked
away:

```text
W (6637)  MQTT: broker disconnected; retrying every 5000 ms
W (6637)  FAULT: TELEMETRY fault RAISED: mqtt_disconnected (detail 0)
W (6707)  CRITICAL: MQTT DOWN: monitoring the safety path (state MONITORING)
I (15817) SAFETY: MONITORING -> UNATTENDED (person absent)
W (18917) SAFETY: UNATTENDED -> WARNING (unattended timeout)
W (23917) SAFETY: WARNING -> SHUTDOWN (shutdown timeout)
W (26707) CRITICAL: MQTT down 20 s: safety loop ALIVE (last iteration 90 ms ago, 280 loops so far), state SHUTDOWN, node ONLINE, buzzer SHUTDOWN, relay ACTIVE
I (36687) MQTT: broker connected (mqtt://10.0.2.2:1883)
W (36707) CRITICAL: outage 29 s: max safety-loop gap 94 ms, 420 loops, 3 transitions during outage, mqtt fault reported, state across reconnect SHUTDOWN -> SHUTDOWN -> PASS
```

The MacBook-side script reported all four checks PASS and received the
WARNING and SHUTDOWN events (QoS 1, queued on the S3) after the broker came
back.
