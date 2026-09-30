# Safety state machine

Implementation: [`firmware/components/safety`](../firmware/components/safety).
Unit tests: [`firmware/testing/unit_tests`](../firmware/testing/unit_tests).

```text
 BOOT ─→ SELF_TEST ─→ IDLE ─→ MONITORING ─→ UNATTENDED ─→ WARNING ─→ SHUTDOWN
                        ↑         │   ↑          │   │        │  │        │
                        └─────────┘   └──────────┘   │        │  │   operator reset → IDLE
                       heat stops    person returns  │        │  │
                                                     └────────┴──┴──→ FAULT (any sensor unknown)
```

## Inputs

| Input | Source | Unknown/invalid handling |
|---|---|---|
| `presence` (tri-state) | the two C4002 presence nodes via ESP-NOW, combined strictly (`node_set_inputs`, `presence_fuse`): PRESENT if either radar sees a person, ABSENT only if both validly say absent | `RB_UNKNOWN` → FAULT. It is never read as "absent" or "present". |
| `thermal_valid`, `hot_region_temp_c` | MLX90640 features from the thermal node via ESP-NOW | invalid → FAULT |
| `self_test` | controller self-test (outputs working) | must read PASS after SELF_TEST; FAIL, timeout or not-yet-passed → FAULT, latched until it passes |
| `reset_request` | operator button | leaves SHUTDOWN; acknowledges WARNING in *exit-on-ack* mode |
| `safety_fault` | fault manager (safety-relevant faults) | → FAULT |

## Transitions

| From | Condition (checked in this order) | To |
|---|---|---|
| BOOT | always | SELF_TEST |
| SELF_TEST | self-test FAIL, or no result within `self_test_timeout_ms` | FAULT |
| SELF_TEST | self-test PASS | IDLE |
| IDLE | sensors not OK | FAULT |
| IDLE | hot-region temp ≥ `heat_on_temp_c` | MONITORING |
| MONITORING | sensors not OK | FAULT |
| MONITORING | hot-region temp < `heat_off_temp_c` (hysteresis) | IDLE |
| MONITORING | absent for `absence_debounce_ms` | UNATTENDED |
| UNATTENDED | sensors not OK | FAULT |
| UNATTENDED | present for `presence_return_debounce_ms` | MONITORING |
| UNATTENDED | heat stopped **and** cooling policy = return-to-idle | IDLE |
| UNATTENDED | unattended ≥ `warning_timeout_ms` | WARNING |
| WARNING | sensors not OK | FAULT |
| WARNING | present (+ reset in exit-on-ack mode) | MONITORING |
| WARNING | heat stopped **and** cooling policy = return-to-idle | IDLE |
| WARNING | shutdown timer expired (see timing mode) | SHUTDOWN |
| SHUTDOWN | operator reset (latched until then) | IDLE |
| FAULT | sensors OK **and** self-test PASS again → IDLE / MONITORING / UNATTENDED depending on heat and presence | |
| FAULT | shutdown already due on the unattended timeline | SHUTDOWN |
| FAULT | in FAULT for `fault_shutdown_timeout_ms` (0 = never) | SHUTDOWN |

## Outputs per state

| State | Buzzer | Shutdown relay |
|---|---|---|
| BOOT, SELF_TEST, IDLE, MONITORING, UNATTENDED | off | released |
| WARNING | warning pattern | released |
| SHUTDOWN | shutdown pattern | **activated** (latched) |
| FAULT | fault pattern | released until FAULT → SHUTDOWN |

## Timing rules

- All timers are monotonic (`now - start >= timeout`, wrap-safe) and checked
  on each `safety_step()`. Nothing blocks, and wall-clock time is never used.
- The unattended timer starts when the absence **began** (not when the
  debounce confirmed it), but never earlier than the moment cooking was
  detected.
- The unattended timeline carries on through FAULT. A sensor fault never
  delays a shutdown that is already due. If the fault clears while the person
  is still away, the timeline continues. If no timeline was running, it
  starts from the moment the fault began.
- After an operator reset from SHUTDOWN all timing starts over.
- The optional `timing_hook` lets the temperature trend shorten the
  warning/shutdown timeouts later. It can only make them stricter.

## Defaults still to confirm

The timings, what happens when a person returns during WARNING or the pan
cools, and the fault shutdown time are all menuconfig options with a
provisional default. The team decision is tracked in [issue #24](https://github.com/Antonio-1110/RB4107_AY2627/issues/24), the cooking
temperature thresholds (placeholder 50 °C on / 40 °C off) in [issue #21](https://github.com/Antonio-1110/RB4107_AY2627/issues/21).
