# Safety state machine

Implementation: [`firmware/components/safety`](../firmware/components/safety).
Unit tests: [`firmware/testing/unit_tests`](../firmware/testing/unit_tests).

```text
 BOOT ─→ SELF_TEST ─→ IDLE ─→ MONITORING ─→ UNATTENDED ─→ WARNING ─→ SHUTDOWN
                        ↑         │   ↑          │   │        │  │        │
                        └─────────┘   └──────────┘   │        │  │   operator reset → IDLE
                       heat stops    person returns  │        │  │
                                                     └────────┴──┴──→ FAULT (self-test / relay output only)
```

## Inputs

| Input | Source | Unknown/invalid handling |
|---|---|---|
| `presence` (tri-state) | the two C4002 presence nodes via ESP-NOW, combined strictly (`node_set_inputs`, `presence_fuse`): PRESENT if either radar sees a person, ABSENT only if both validly say absent | `RB_UNKNOWN` raises the **presence unknown** flag, never FAULT. With nothing cooking it starts no timer; while cooking it counts as absent (see "Lost sensors"). It is never read as "present". |
| `thermal_valid`, `cooking_temp_c` | MLX90640 features from the thermal node via ESP-NOW; `cooking_temp_c` is the hottest pixel (`max_temp_c`), the same number the dashboard shows and outlines | invalid raises the **thermal lost** flag, never FAULT. The last cooking state is kept: a timeline already running carries on, and no cooking is invented. |
| `self_test` | controller self-test (outputs working) | must read PASS after SELF_TEST; FAIL, timeout or not-yet-passed → FAULT, latched until it passes |
| `reset_request` | operator reset: dashboard button, console `reset` ([remote_reset.md](remote_reset.md)) | leaves SHUTDOWN; acknowledges WARNING in *exit-on-ack* mode |
| `safety_fault` | fault manager: the controller's own faults only (`shutdown_output_failure`, `self_test_failed`). A lost sensor or node is a safety-class fault in the fault list too, but it only raises the flags above | → FAULT |

## Transitions

| From | Condition (checked in this order) | To |
|---|---|---|
| BOOT | always | SELF_TEST |
| SELF_TEST | self-test FAIL, or no result within `self_test_timeout_ms` | FAULT |
| SELF_TEST | self-test PASS | IDLE |
| IDLE | self-test not passed, or `safety_fault` | FAULT |
| IDLE | hottest pixel ≥ `heat_on_temp_c` (the cooking temperature; menuconfig or dashboard) | MONITORING |
| MONITORING | self-test not passed, or `safety_fault` | FAULT |
| MONITORING | hottest pixel < `heat_off_temp_c` (hysteresis) | IDLE |
| MONITORING | absent for `absence_debounce_ms` | UNATTENDED |
| UNATTENDED | self-test not passed, or `safety_fault` | FAULT |
| UNATTENDED | present for `presence_return_debounce_ms` (see presence filter) | MONITORING |
| UNATTENDED | heat stopped **and** cooling policy = return-to-idle | IDLE |
| UNATTENDED | unattended ≥ `warning_timeout_ms` | WARNING |
| WARNING | self-test not passed, or `safety_fault` | FAULT |
| WARNING | present for `presence_return_debounce_ms` (+ reset in exit-on-ack mode) | MONITORING |
| WARNING | heat stopped **and** cooling policy = return-to-idle | IDLE |
| WARNING | shutdown timer expired (see timing mode) | SHUTDOWN |
| SHUTDOWN | operator reset (latched until then) | IDLE |
| FAULT | self-test PASS and no `safety_fault` again → IDLE / MONITORING / UNATTENDED depending on heat and presence | |
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
- The unattended timeline carries on through FAULT. A fault never delays a
  shutdown that is already due. If the fault clears while the person is still
  away, the timeline continues. If no timeline was running, it starts from the
  moment the fault began. `fault_shutdown_timeout_ms` now only applies to the
  controller's own faults (self-test, relay output).
- After an operator reset from SHUTDOWN all timing starts over.
- **The S3 does all the presence debouncing.** The radars report their raw
  state at once (the node sends on every change, see below) and hold nothing
  back: the C4002's disappear delay and lock time are fixed at 0 s and 0.2 s.
- Presence filter: absence must last `absence_debounce_ms` (default 1 s)
  before UNATTENDED. **A presence blip does not restart that count**: the
  absence timer starts at the first "absent" and only a *confirmed* return
  stops it, so radar noise can't keep absence from ever being confirmed.
  A return must last `presence_return_debounce_ms`
  (default 3 s) before it cancels UNATTENDED or WARNING, so a single reading
  (a hand waved at the radar, someone walking past) leaves the unattended
  timer running. "Absent" gaps shorter than `presence_return_gap_ms`
  (default 1 s) don't restart the return count, but the person must still be
  detected when it completes. UNKNOWN presence stops the return count at
  once. Blips more frequent than the gap tolerance for the whole return time
  still count as a return (a windowed vote could separate that from noise;
  not built). The dashboard can change all three, and the warning and
  shutdown times, until the next reboot
  ([remote_reset.md](remote_reset.md#presence-filter-and-timers)).
- The optional `timing_hook` lets the temperature trend shorten the
  warning/shutdown timeouts later. It can only make them stricter.

## Lost sensors: flags, not FAULT

A lost sensor is shown, not turned into a fault state. The safety state
machine raises two flags that reach the dashboard (`safety.presence_unknown`,
`safety.thermal_lost`; the per-node status comes from the node list):

| Situation | What happens |
|---|---|
| Presence UNKNOWN, nothing cooking | flag only: no timer, no shutdown |
| Presence UNKNOWN, cooking (per the thermal camera) | flag, and presence counts as **absent**: the absence debounce and the unattended timeline run. Only a confirmed return (radar TRUE for `presence_return_debounce_ms`) stops it |
| Thermal camera lost or invalid | flag only. The last known cooking state is kept, so a timeline already running carries on and a cold kitchen doesn't start one |
| Controller self-test or relay output failing | FAULT, as before |

The dashboard's Countdown card shows the phase, the clock and these flags
([dashboard.md](dashboard.md)).

## Defaults still to confirm

The timings, what happens when a person returns during WARNING or the pan
cools, and the fault shutdown time are all menuconfig options with a
provisional default. The team decision is tracked in [issue #24](https://github.com/Antonio-1110/RB4107_AY2627/issues/24), the cooking
temperature thresholds (placeholder 50 °C on / 40 °C off) in [issue #21](https://github.com/Antonio-1110/RB4107_AY2627/issues/21).
