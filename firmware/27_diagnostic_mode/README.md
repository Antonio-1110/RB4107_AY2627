# 27 – Diagnostic mode

TODO section 27. Target: **ESP32-S3** (bare board or QEMU). This is the
complete controller (`components/rb_controller_app`) plus the diagnostic
console (`components/rb_diag`). By default it runs the **simulated sensor
node**; clear `RB_SIM_NODE` to diagnose a real C6 over ESP-NOW.

```bash
idf.py build flash monitor      # then type at the rb4107> prompt
```

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
| `sim present / absent / presence-invalid` | simulated C4002 |
| `sim temp <C> [rate]`, `sim thermal-invalid / thermal-valid` | simulated MLX90640 features |
| `sim node-off / node-on`, `sim on` | the node disappears / returns; start simulation |
| `timers test / normal` | warning 5 s / shutdown 10 s (menuconfig `RB_DIAG_TEST_*`) ↔ normal |
| `reset` | operator reset / acknowledge |
| `log <TAG> <level>` | change log verbosity at runtime |

**No duplicated safety logic.** Simulated inputs are real protocol packets
injected where ESP-NOW would deliver them. They pass through the same
sequence tracking, node health, fault manager, state machine and telemetry.
`timers test` only swaps the numbers in the same state machine
(`rb_controller_set_safety_config`), and MQTT telemetry reports
`"test_timers": true` while it is active.

## Session recorded in QEMU

```text
rb4107> timers test
timers test: warning 5 s, shutdown 10 s
rb4107> sim temp 120
I (5332) SAFETY: IDLE -> MONITORING (heating detected)
rb4107> sim absent
I (10332) SAFETY: MONITORING -> UNATTENDED (person absent)
W (13332) SAFETY: UNATTENDED -> WARNING (unattended timeout)
W (18332) SAFETY: WARNING -> SHUTDOWN (shutdown timeout)
rb4107> sim present
rb4107> reset
I (24232) SAFETY: SHUTDOWN -> IDLE (operator reset)
I (24232) SAFETY: IDLE -> MONITORING (heating detected)
rb4107> sim thermal-invalid
W (26332) FAULT: SAFETY fault RAISED: mlx90640_unavailable (detail 0)
W (26332) SAFETY: MONITORING -> FAULT (safety-relevant fault active)
```
