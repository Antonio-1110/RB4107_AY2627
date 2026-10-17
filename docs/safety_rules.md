# Safety rules: what the controller does, and when

A rule-by-rule list of every condition the S3 controller reacts to and what it
does. The reasons, examples and trade-offs behind each rule are in
[safety_state_machine.md](safety_state_machine.md); this page is the checklist.

Code: [`safety.c`](../firmware/components/safety/src/safety.c) (state machine),
[`sensor_node.c`](../firmware/components/sensor_node/src/sensor_node.c) (radar
fusion, node health), [`rb_controller.c`](../firmware/components/rb_controller/src/rb_controller.c)
(safety task), [`rb_valve_link.c`](../firmware/components/rb_valve_link/src/rb_valve_link.c)
(wireless gas valve). Defaults are the menuconfig defaults; **(dash)** marks the
ones the dashboard can change.

**Keep this page true.** Any change to the behaviour or defaults below updates
this page in the same PR. `python3 tools/check_safety_rules_doc.py` checks the
two tables at the end against the Kconfig and `safety.c`; the rules themselves
are checked by reading the code (see [CLAUDE.md](../CLAUDE.md)).

## What the controller outputs

| State | Buzzer | Shutdown output (DO1, latched) | Gas valve (wireless) |
|---|---|---|---|
| BOOT, SELF_TEST, IDLE, MONITORING, UNATTENDED | off | released | KEEP_OPEN every 1 s |
| WARNING | warning pattern | released | KEEP_OPEN |
| SHUTDOWN | shutdown pattern | **active** | CLOSE |
| FAULT | fault pattern | released (until FAULT → SHUTDOWN) | KEEP_OPEN |

## A. What the controller believes about its inputs

| # | Rule | Result | Code |
|---|---|---|---|
| A1 | A node's packets stop for 2 s (stale) or 10 s (offline), or its sensor says invalid | that sensor's reading is UNKNOWN | `sensor_node_evaluate()` |
| A2 | **At the stove** (default "every working radar"): any working radar says absent | ABSENT | `presence_fuse_all()` |
| A3 | Every working radar sees someone | PRESENT | `presence_fuse_all()` |
| A4 | No radar is working | UNKNOWN (dashboard flag, never FAULT) | `presence_fuse_all()` |
| A5 | Fusion set to "either radar" **(dash)** | PRESENT if any radar sees someone; ABSENT only if all validly say absent | `presence_fuse()` |
| A6 | Radar rules in *enforce* mode **(dash)**: a radar sees someone but the episode is not *dwelling* (≥ 4 s, ≥ 1.5 s stationary, distance within 60 cm over 4 s) | that radar counts as absent. Default mode is *observe*: computed and shown, not used | `node_set_inputs()` |
| A7 | UNKNOWN presence while cooking | counts as ABSENT | `effective_presence()` |
| A8 | UNKNOWN presence with nothing cooking | starts no timer | `effective_presence()` |
| A9 | Thermal camera lost or invalid | dashboard flag only; cooking state, tier and running timers are kept | `safety_step()`, `update_tier()` |

## B. Cooking on or off (hottest pixel)

| # | Rule | Controller does | Code |
|---|---|---|---|
| B1 | Not cooking, hottest pixel ≥ cooking temperature (50 °C) **(dash)** | cooking: IDLE → MONITORING | `safety_step()` |
| B2 | Optional: rising ≥ the "heat on" rate and above the off temperature (rate 0 = off by default) | cooking, as B1 | `safety_step()` |
| B3 | Cooking, hottest pixel < off temperature (cooking temperature − 10 °C = 40 °C) | not cooking. MONITORING → IDLE; UNATTENDED or WARNING → IDLE, timers cancelled (cooling policy *return to IDLE*) | `safety_step()`, `evaluate()` |
| B4 | The dashboard sets a new cooking temperature | the off temperature moves with it, keeping the menuconfig gap (10 °C) | `apply_cooking_temp()` |

## C. Someone leaves

| # | Rule | Controller does | Code |
|---|---|---|---|
| C1 | Cooking and ABSENT for 1 s **(dash)** | MONITORING → UNATTENDED | `evaluate()` |
| C2 | Unattended clock start | counts from the first "absent" reading, never from before cooking started | `on_enter()` |
| C3 | A presence blip | does not restart the absence count | `safety_step()` |

## D. The countdown (UNATTENDED → WARNING → SHUTDOWN)

| # | Rule | Controller does | Code |
|---|---|---|---|
| D1 | Unattended ≥ warning time (180 s) **(dash)**, no hold | UNATTENDED → WARNING: buzzer on | `evaluate()` |
| D2 | Unattended ≥ shutdown time (300 s, counted from the start of UNATTENDED) **(dash)**, no hold | WARNING → SHUTDOWN: shutdown output on, valve CLOSE, latched | `evaluate()` |
| D3 | Hottest pixel ≥ 120 °C for 4 s (ELEVATED) **(dash)** | times become 60 s buzzer / 150 s shutdown, if shorter | `update_tier()`, `apply_tier()` |
| D4 | Hottest pixel ≥ 200 °C, or rising ≥ 20 °C/min above 150 °C, for 4 s (CRITICAL) **(dash)** | times become 30 s / 120 s, if shorter | `update_tier()`, `apply_tier()` |
| D5 | Temperature 10 °C below the tier threshold for 20 s | back to the lower tier | `update_tier()` |
| D6 | A tier makes the shutdown due already when the buzzer starts | buzzer sounds at least 10 s before the shutdown | `evaluate()` (`late_warning`) |
| D7 | A hold (E1 or E3) happened on this countdown | buzzer sounds at least 30 s before the shutdown | `evaluate()` (`held_once`) |

## E. What delays the next step (nothing cancelled)

| # | Rule | Controller does | Code |
|---|---|---|---|
| E1 | **Cooling hold**: the pan is only cooling. Over the last 2 min (≥ 12 samples, ≥ 60 s), settle = T + τ × rate is more than 3 °C below the off temperature and rose ≤ 8 °C/min over the last minute. τ comes from the cookware profile (mixed = 10 min) **(dash)** | a buzzer or shutdown that is due waits. The clock keeps counting. A buzzer that is already sounding keeps sounding | `judge_heat()`, `hold_reason()` |
| E2 | Cooling hold ends | settle back ≥ off + 3 °C, its estimate rising > 16 °C/min, or the hold lasted 4 τ (1 h at most): the held step fires. The pan dropping below the off temperature cancels instead (B3) | `judge_heat()`, `hold_reason()` |
| E3 | **Return hold**: a step comes due while someone is seen but not yet confirmed (F1) | waits at most 5 s + 1 s, then fires | `hold_reason()` |
| E4 | **Pause**: someone is seen during UNATTENDED or WARNING | the clock stands still. Budget per countdown: 30 s, and at most 25 % of the shutdown time in force **(dash)** | `pause_clock()` |
| E5 | Trend not ready (under 60 s of data), camera lost, or trend off in menuconfig | heat counts as going in: no cooling hold | `judge_heat()` |

## F. What cancels the countdown

| # | Rule | Controller does | Code |
|---|---|---|---|
| F1 | PRESENT for 5 s **(dash)**, absent gaps under 1 s forgiven **(dash)**, still present at the end | UNATTENDED or WARNING → MONITORING: buzzer off, timers cleared | `present_confirmed()`, `evaluate()` |
| F2 | Pan below the off temperature | → IDLE (B3) | `evaluate()` |
| F3 | Warning exit set to *acknowledge* (not default) | WARNING needs F1 **and** a reset to leave | `evaluate()` |

## G. Shutdown, reset and faults

| # | Rule | Controller does | Code |
|---|---|---|---|
| G1 | In SHUTDOWN | stays there whatever the sensors say | `evaluate()` |
| G2 | Operator reset (dashboard button, console `reset`) in SHUTDOWN | → IDLE, all timing starts over. A reboot also clears it (not saved) | `evaluate()`, `on_exit()` |
| G3 | Self-test fails, or no result within 10 s | FAULT | `evaluate()` |
| G4 | The shutdown output fails its read-back (every 1 s) | FAULT | `rb_outputs.c` |
| G5 | In FAULT and the countdown's shutdown is already due | SHUTDOWN | `evaluate()` |
| G6 | In FAULT for 90 s (0 = never) | SHUTDOWN | `evaluate()` |
| G7 | Fault clears | → IDLE (not cooking), MONITORING (someone present) or UNATTENDED (counting from when the fault began, if no countdown was running) | `evaluate()` |
| G8 | Wireless valve: the safety task stops reporting for 3 ticks + 0.5 s | valve CLOSE | `rb_valve_link.c` |
| G9 | MQTT, Django, laptop or network down | no effect on any rule above | (by design) |

## Settings checked against the Kconfig

`tools/check_safety_rules_doc.py` compares this table with
`firmware/components/rb_config/Kconfig`; every `RB_SAFETY_*` option must be in
it. Values are in Kconfig units (DC = 0.1 °C). For a choice, the default is
the selected option.

<!-- settings:begin -->
| Setting | Default | Meaning |
|---|---|---|
| `RB_SAFETY_SELF_TEST_TIMEOUT_MS` | 10000 | G3 |
| `RB_SAFETY_HEAT_ON_DC` | 500 | B1, 50 °C |
| `RB_SAFETY_HEAT_OFF_DC` | 400 | B3, 40 °C |
| `RB_SAFETY_HEAT_ON_RATE_DC_PER_MIN` | 0 | B2, off |
| `RB_SAFETY_ABSENCE_DEBOUNCE_MS` | 1000 | C1 |
| `RB_SAFETY_PRESENCE_RETURN_DEBOUNCE_MS` | 5000 | F1, E3 |
| `RB_SAFETY_PRESENCE_RETURN_GAP_MS` | 1000 | F1, E3 |
| `RB_SAFETY_PAUSE_BUDGET_S` | 30 | E4 |
| `RB_SAFETY_PAUSE_BUDGET_PCT` | 25 | E4 |
| `RB_SAFETY_WARNING_TIMEOUT_S` | 180 | D1 |
| `RB_SAFETY_SHUTDOWN_TIMEOUT_S` | 300 | D2 |
| `RB_SAFETY_SHUTDOWN_TIMING` | RB_SAFETY_SHUTDOWN_FROM_UNATTENDED | D2 |
| `RB_SAFETY_WARNING_EXIT` | RB_SAFETY_WARNING_EXIT_PRESENCE | F3 |
| `RB_SAFETY_COOLING` | RB_SAFETY_COOLING_RETURN_TO_IDLE | B3 |
| `RB_SAFETY_COOKING_TREND` | y | E1, E5 |
| `RB_SAFETY_COOKWARE` | RB_SAFETY_COOKWARE_MIXED | E1, τ 10 min |
| `RB_SAFETY_TEMP_TIERS` | y | D3, D4 |
| `RB_SAFETY_TIER1_TEMP_DC` | 1200 | D3, 120 °C |
| `RB_SAFETY_TIER1_WARNING_S` | 60 | D3 |
| `RB_SAFETY_TIER1_SHUTDOWN_S` | 150 | D3 |
| `RB_SAFETY_TIER2_TEMP_DC` | 2000 | D4, 200 °C |
| `RB_SAFETY_TIER2_RATE_DC_PER_MIN` | 200 | D4, 20 °C/min |
| `RB_SAFETY_TIER2_RATE_MIN_TEMP_DC` | 1500 | D4, 150 °C |
| `RB_SAFETY_TIER2_WARNING_S` | 30 | D4 |
| `RB_SAFETY_TIER2_SHUTDOWN_S` | 120 | D4 |
| `RB_SAFETY_FAULT_SHUTDOWN_S` | 90 | G6 |
| `RB_CTRL_PRESENCE_FUSION` | RB_CTRL_PRESENCE_FUSION_ALL | A2, A5 |
| `RB_CTRL_RADAR_RULES` | RB_CTRL_RADAR_RULES_OBSERVE | A6 |
| `RB_CTRL_RADAR_DWELL_MS` | 4000 | A6 |
| `RB_CTRL_RADAR_STATIONARY_MIN_MS` | 1500 | A6 |
| `RB_CTRL_RADAR_SETTLE_WINDOW_MS` | 4000 | A6 |
| `RB_CTRL_RADAR_SETTLE_SPAN_CM` | 60 | A6 |
| `RB_CTRL_NODE_STALE_MS` | 2000 | A1 |
| `RB_CTRL_NODE_OFFLINE_MS` | 10000 | A1 |
| `RB_CTRL_VALVE_KEEPALIVE_MS` | 1000 | outputs table |
| `RB_SHUTDOWN_VERIFY_PERIOD_MS` | 1000 | G4 |
<!-- settings:end -->

## Fixed constants checked against `safety.c`

Not in menuconfig; changing one means changing the code.

<!-- constants:begin -->
| Constant | Value | Meaning |
|---|---|---|
| `TIER_UP_PERSIST_MS` | 4000 | D3, D4 |
| `TIER_DOWN_PERSIST_MS` | 20000 | D5 |
| `TIER_HYST_C` | 10.0 | D5 |
| `LATE_WARNING_MIN_MS` | 10000 | D6 |
| `WARNING_MIN_AFTER_HOLD_MS` | 30000 | D7 |
| `TREND_SAMPLE_MS` | 2000 | E1 |
| `TREND_DEFAULT_WINDOW_MS` | 120000 | E1 |
| `TREND_MIN_SPAN_MS` | 60000 | E1, E5 |
| `TREND_MIN_SAMPLES` | 12 | E1 |
| `TREND_HALF_LIFE_MS` | 30000.0 | E1, recent samples weigh more |
| `TREND_GAP_RESET_MS` | 15000 | E5, trend starts over after this gap |
| `TREND_STALE_MS` | 30000 | E5 |
| `SETTLE_MARGIN_C` | 3.0 | E1, E2 |
| `SETTLE_HIST_SAMPLE_MS` | 5000 | E1 |
| `SETTLE_DRIFT_WINDOW_MS` | 60000 | E1 |
| `SETTLE_MAX_DRIFT_C_PER_MIN` | 8.0 | E1 |
| `SETTLE_EXIT_DRIFT_C_PER_MIN` | 16.0 | E2 |
| `RISING_C_PER_MIN` | 0.5 | dashboard phase "heating" vs "holding" |
| `COOLING_HOLD_MAX_MS` | 3600000 | E2 |
| `COOLING_HOLD_TAUS` | 4.0 | E2 |
<!-- constants:end -->
