# 10 – Initial safety behaviour

TODO section 10. Target: **ESP32-S3**.

Runs the real state machine on a simulated clock with the timings and
policies from menuconfig, and prints PASS/FAIL for each required behaviour.
Change a setting in menuconfig, rebuild, and the checks follow.

| Behaviour | Setting (menuconfig → *Safety logic*) | Default |
|---|---|---|
| IDLE → MONITORING | `RB_SAFETY_HEAT_ON_DC` / `RB_SAFETY_HEAT_OFF_DC` | 50 °C / 40 °C (UNVERIFIED) |
| MONITORING → UNATTENDED | `RB_SAFETY_ABSENCE_DEBOUNCE_MS` | 2 s |
| UNATTENDED → MONITORING | `RB_SAFETY_PRESENCE_RETURN_DEBOUNCE_MS` | 0 s |
| UNATTENDED → WARNING | `RB_SAFETY_WARNING_TIMEOUT_S` | 60 s |
| WARNING → SHUTDOWN | `RB_SAFETY_SHUTDOWN_TIMEOUT_S` + `RB_SAFETY_SHUTDOWN_TIMING` | 90 s total unattended |
| Person returns during WARNING | `RB_SAFETY_WARNING_EXIT` | back to MONITORING |
| Temperature falls while unattended | `RB_SAFETY_COOLING` | keep the timers running |

**Open question:** does "90 s" mean total unattended time or 90 s after the
warning? Both work; pick one with `RB_SAFETY_SHUTDOWN_TIMING`.

**Temperature interaction:** `safety_config_t.timing_hook` lets a
thermal-trend policy shorten the warning/shutdown timeouts (it can never
lengthen them). None is installed by default. The demo installs an example
hook that halves the timeouts while the pan heats faster than 10 °C/min. No
thermal assumption is built in until experimental data exists.
