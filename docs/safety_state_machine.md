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
| UNATTENDED | the pan dropped below the cooking temperature (cooling policy = return-to-idle, the default) | IDLE |
| UNATTENDED | unattended ≥ `warning_timeout_ms`, unless held (see "Holds") | WARNING |
| WARNING | self-test not passed, or `safety_fault` | FAULT |
| WARNING | present for `presence_return_debounce_ms` (+ reset in exit-on-ack mode) | MONITORING |
| WARNING | the pan dropped below the cooking temperature (return-to-idle) | IDLE |
| WARNING | shutdown timer expired (see timing mode), unless held, and after at least 30 s of warning if a hold was involved | SHUTDOWN |
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

## Cooking judgment: is heat still going in?

"Cooking" starts when the hottest pixel reaches the cooking temperature (50 °C
on, 40 °C off, adjustable). Once it is on, the S3 asks a second question: is
heat still going in, or is the pan just cooling down? The two mistakes cost very
different amounts. Calling a cooling pan "cooking" only causes a nuisance (the
burner is already off); calling a lowered flame "off" is the dangerous one. A
falling temperature also means less heat goes in than comes out, which is a low
fire hazard: oil fires start when a pan keeps being heated unattended
([NIST TN 2133](https://nvlpubs.nist.gov/nistpubs/TechnicalNotes/NIST.TN.2133.pdf)).

The decision uses the **trend**, not the absolute temperature, so it also works
for a shiny pan that reads too cold:

1. Every 2 s the S3 stores the hottest pixel. A least-squares fit over the last
   2 minutes gives the rate (needs at least 12 samples over 60 s; until then the
   phase is *checking* and heat counts as going in).
2. From the cooling law (T falls toward its settling temperature as `e^(−t/τ)`),
   it works out where the temperature is heading: `T_settle = T + τ × rate`.
3. If `T_settle` is below the cooking-off temperature (40 °C, with a 3 °C
   hysteresis), the pan is **cooling**: nothing is keeping it hot. Otherwise heat
   is going in: **holding** (steady or settling hot: a lowered flame, a simmer,
   boiling water at 100 °C) or **heating** (rising faster than 0.5 °C/min).

τ comes from the **cookware profile**, chosen per stall on the dashboard
(Tuning → Rules) and saved on the controller. The values are engineering
estimates from typical mass and heat capacity; no source I found publishes
measured cooling constants for cookware, so check them against your own pans:

| Profile | For | τ |
|---|---|---|
| mixed / unknown (default) | ordinary pans, between light and heavy | 10 min |
| light | woks, thin aluminium or stainless pans | 5 min (3–8) |
| heavy | cast iron, thick clad pans | 15 min (10–20) |
| liquid | pots of soup, stock, boiling water | 40 min (20–60; longer with a lid) |

Extremes (searing pans, very large stock pots, deep fryers) are not profiled.

**The judgment checks itself.** `T + τ × rate` is exact when the profile fits
the pan: for a pan that is only cooling it stays put near the room temperature.
If the profile's τ is longer than the pan's, the estimate climbs as a lowered
flame's warm-up tail relaxes. So "cooling" is only believed while the estimate
has not risen by more than 8 °C over the last minute (and is dropped if it rises
by more than 16 °C/min). A profile that fits the pan holds the timers for a
cooling pan; a profile that does not simply falls back to the normal timers.

What a wrong profile costs, simulated on model cooling curves (not measured on
real cookware; `tools/diagnostics/cooling_sweep.c` and
`tools/diagnostics/cooling_delay.c` reproduce it):

- **Profile too short** (e.g. *light* for a heavy pot): a cooling pan is
  recognised late or never, so the buzzer and shutdown run as before. Nuisance,
  never dangerous.
- **Profile too long** (e.g. *liquid* for a light pan), lowered flame, buzzer
  due at 120 s with a 5 min pan: on time up to a profile 1.5× slower than the
  pan; for 2× to 8× slower it comes **36–68 s late**, never missed. A hold also
  never lasts longer than 4 τ (an hour at most), and after it the buzzer sounds
  for 30 s before any shutdown.
- **Matched profile**: a pan with the burner off is recognised after about 30-90 s
  and held until it is cold; the same pan on a lowered flame is not held.

So the demo needs the profile to be roughly right for the cookware on the stove.
When unsure, pick the shorter one.

## Holds: a due buzzer or shutdown that waits

A warning or shutdown that comes due can be held back, for two reasons, each
with a bound:

| Hold | When | How long |
|---|---|---|
| **cooling** | the pan is only cooling down (no heat going in) | until the trend flattens out hot (then it fires at once), or the pan drops below the cooking temperature (the timeline is cancelled); at most 4 τ of the profile, 1 h at most |
| **return** | the radar sees someone right now and the return is being confirmed, but `presence_return_debounce_ms` has not passed | until it is confirmed (the timeline is cancelled), or the radar loses them (the held step fires at once); at most `return_debounce + gap` |

The unattended clock keeps counting while a transition is held. After any hold,
the buzzer sounds for **at least 30 s** before the shutdown, even if the shutdown
was due long ago, so the first thing someone nearby hears is the warning.

The earlier rule "a fault never postpones a due shutdown" still applies in
FAULT. The return hold is the one bounded exception for presence.

The **buzzer and shutdown defaults are 3 min and 5 min** (`RB_SAFETY_WARNING_TIMEOUT_S`,
`RB_SAFETY_SHUTDOWN_TIMEOUT_S`; they were 20 s and 40 s). That is long enough for
the trend to be read (about 1-2 minutes) before the buzzer is due, which is what
lets the demo show the timers behaving correctly for the situation.

The trend can't be known without a working thermal camera: with the camera
lost, heat counts as going in and nothing is held for cooling. The trend can be
turned off in menuconfig (`RB_SAFETY_COOKING_TREND`).

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
