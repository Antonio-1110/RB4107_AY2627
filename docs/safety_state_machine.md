# Safety state machine

Implementation: [`firmware/components/safety`](../firmware/components/safety).
Unit tests: [`firmware/testing/unit_tests`](../firmware/testing/unit_tests).
Every rule, generated from the table the code runs: [safety_rules.md](safety_rules.md).

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
| `presence` (tri-state) | the two C4002 presence nodes via ESP-NOW, combined strictly (`node_set_inputs`, `presence_fuse_all`): ABSENT if any working radar says absent, PRESENT only if every working radar sees the person (see "Who is at the stove, and the pause") | `RB_UNKNOWN` raises the **presence unknown** flag, never FAULT. With nothing cooking it starts no timer; while cooking it counts as absent (see "Lost sensors"). It is never read as "present". |
| `thermal_valid`, `cooking_temp_c` | MLX90640 features from the thermal node via ESP-NOW; `cooking_temp_c` is the hottest pixel (`max_temp_c`), the same number the dashboard shows and outlines | invalid raises the **thermal lost** flag, never FAULT. The last cooking state is kept: a timeline already running carries on, and no cooking is invented. |
| `self_test` | controller self-test (outputs working) | must read PASS after SELF_TEST; FAIL, timeout or not-yet-passed → FAULT, latched until it passes |
| `reset_request` | operator reset: dashboard button, console `reset` ([remote_reset.md](remote_reset.md)) | leaves SHUTDOWN; acknowledges WARNING in *exit-on-ack* mode |
| `safety_fault` | fault manager: the controller's own faults only (`shutdown_output_failure`, `self_test_failed`). A lost sensor or node is a safety-class fault in the fault list too, but it only raises the flags above | → FAULT |

## Transitions and outputs

The full, ordered list of transitions and what each state drives is
generated from the table the firmware runs from:
**[safety_rules.md](safety_rules.md)**. This page explains why the rules are
what they are.

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
  state at once (the node sends on every change, see below) and hold little
  back: the C4002's disappear delay and lock time are settable, default 1 s each
  (a small hold can help the radar keep a still person; see c4002_tuning.md).
- Presence filter: absence must last `absence_debounce_ms` (default 1 s)
  before UNATTENDED. **A presence blip does not restart that count**: the
  absence timer starts at the first "absent" and only a *confirmed* return
  stops it, so radar noise can't keep absence from ever being confirmed.
  A return must last `presence_return_debounce_ms`
  (default 5 s) before it cancels UNATTENDED or WARNING, so a single reading
  (a hand waved at the radar, someone walking past) cancels nothing; the
  clock only pauses meanwhile (see "Who is at the stove, and the pause").
  "Absent" gaps shorter than `presence_return_gap_ms`
  (default 1 s) don't restart the return count, but the person must still be
  detected when it completes. UNKNOWN presence stops the return count at
  once. Blips more frequent than the gap tolerance for the whole return time
  still count as a return (a windowed vote could separate that from noise;
  not built). The dashboard can change all three, and the warning and
  shutdown times, until the next reboot
  ([remote_reset.md](remote_reset.md#presence-filter-and-timers)).
- The optional `timing_hook` lets the temperature trend shorten the
  warning/shutdown timeouts later. It can only make them stricter.

## Radar state and distance rules (placeholder)

The C4002 reports each target as none, moving or stationary, with a distance.
The controller keeps a small **episode** per radar: it starts when the radar
first sees a target, survives gaps up to `quiet_gap_ms` (1.5 s), and records
how long it lasted, how much of it was a stationary target, and how far the
distance wandered (`span_cm`). The verdict is:

- `none`: no episode;
- `dwelling`: at least `dwell_ms` (4 s) long, with at least
  `stationary_min_ms` (1.5 s) stationary, and a distance span inside
  `settle_span_cm` (60 cm) over the last `settle_window_ms` (4 s), and
  (optionally) inside `zone_max_cm`;
- `passing`: anything else (a target that keeps moving or sweeping range).

Modes (`RB_CTRL_RADAR_RULES`): **off**; **observe** (default: verdicts are
computed and shown on the dashboard but change nothing); **enforce** (a radar
that sees someone but only *passing* does not count as "at the stove" in the
attendance fusion, so a walker behind the stove neither pauses nor cancels
the clock). Mode and numbers can be changed from the dashboard (Tuning > Rules > Person), saved on the controller. Every number is a placeholder, not tuned on real walks or cooks;
see [demo_vs_deployment.md](demo_vs_deployment.md).

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

## Who is at the stove, and the pause

False presence is the dangerous mistake: a person walking behind the sensor can
cancel the timeline and leave the gas on with nobody at the stove. Two rules
limit it.

**1. Someone only counts as at the stove when every working radar sees them.**
With two radars, a person seen by one radar only (a rear-lobe echo, someone
behind the sensor) is *nearby*, not at the stove. A radar that is down is not
asked, and with no working radar the presence is unknown. The dashboard shows
both: "at the stove" is what the safety logic uses, "nearby" is either radar
(the earlier, looser meaning). Setting: `RB_CTRL_PRESENCE_FUSION`, or
Tuning, Rules, "At the stove when".

**2. A pass pauses the clock; staying cancels the timeline.** While someone is
at the stove on an unattended timeline (UNATTENDED or WARNING), the clock stands
still, so only time with nobody there counts. If they stay
`presence_return_debounce_ms` (5 s), the timeline is cancelled as before. If
they leave sooner, the clock resumes from where it stopped.

| Situation | Result |
|---|---|
| Walker passes in 2 s | clock paused for 2 s, nothing reset |
| Cook glances at the pot for 4 s | paused for 4 s, nothing reset |
| Cook stays 5 s or more | timeline cancelled |
| Walker lingers 5 s or more where every radar sees them | looks like a cook: cancelled |

A pause postpones the buzzer and the shutdown while the pan keeps heating, so it
has a **budget per timeline**: at most `RB_SAFETY_PAUSE_BUDGET_S` (30 s) in total
and at most `RB_SAFETY_PAUSE_BUDGET_PCT` (25 %) of the shutdown time in force.
For oil in tier 5 (60 s shutdown) that is at most 15 s of delay. The
budget starts over when the timeline is cancelled. 0 turns the pause off. The
bounded "return hold" below still applies when a transition comes due just as
someone appears. What is *not* done: telling a walker from a cook by the radar's
moving or stationary state, or by distance. That needs real recordings first; the
pause and the 5 s cancel do not depend on it.

## Temperature tiers: the hotter the pan, the shorter the time

The countdown is set by how dangerous the pan is now, not by how long cooks
usually step away (that varies from one canteen to the next, and we have no
canteen data). Two inputs decide it: the hottest pixel, and how fast it is
rising.

**The anchor: 30 s of buzzer at the top.** NIST heated corn oil on an electric
coil from cold: time to auto-ignition was `460 s + 3.9 s per gram of oil`
(50-150 g: 11 to 17 minutes), ignition came at a pan-bottom temperature of
392 °C, and the last stage, from visible smoke to ignition, lasted only 30-60 s
([NIST TN 2133](https://nvlpubs.nist.gov/nistpubs/TechnicalNotes/NIST.TN.2133.pdf)).
So a pan at or past oil's smoke point (about 190-270 °C by oil) must give the
cook at least that 30 s of warning, and the shutdown comes 30 s after it. Gas
burners were not tested there and heat faster; a pan already hot when the cook
leaves has far less than 11 minutes left.

**The other tiers are a baseline.** The buzzer steps down evenly, 30 s per
tier, from the loosest (a simmer) to the strictest, and the shutdown is always
twice the buzzer:

| Tier | Hottest pixel | Buzzer | Shutdown |
|---|---|---|---|
| 1 | below 100 °C (a simmer) | 150 s (the base time) | 300 s (the base time) |
| 2 | 100 °C and up (boiling dry, gentle frying) | 120 s | 240 s |
| 3 | 150 °C and up (shallow frying) | 90 s | 180 s |
| 4 | 200 °C and up (hot oil, near the lowest smoke points) | 60 s | 120 s |
| 5 | 250 °C and up (oil at or past its smoke point) | 30 s | 60 s |

The temperatures between tier 1 and tier 5 are evenly spaced estimates, not
measurements. All of these are menuconfig values (`RB_SAFETY_TIER_*`), can be
changed from the dashboard (Tuning, Rules; saved on the controller, see
[remote_reset.md](remote_reset.md#temperature-tiers)), and are meant to be
tuned on real camera readings of hot oil. The MLX90640 reads objects up to
300 °C (datasheet); readings off bare shiny metal may be lower than the real
temperature (low emissivity, not measured here), while oil and food read
closer to true.

**The rate: a pan heading for a tier gets it already.** A pan at 185 °C rising
2 °C/s is under tier 4 (200 °C), but it reaches 245 °C in 30 s and 305 °C in
60 s. Tier k applies when *temperature + rise rate × tier k's buzzer time*
reaches tier k's temperature, so the buzzer sounds no later than the moment the
pan would get there. The rate is measured over the last 30 s, as the median of
the newer half of the window minus the median of the older half: one bad frame
(a reflection, steam) can't fake a fast rise. Noise in it errs early (a buzzer
too soon, never too late), and only a rise counts. Setting:
`RB_SAFETY_TIER_RATE` (on), and the window on the Other settings card.

How it behaves:

- **Tiers only shorten.** The time in force is the smaller of the base time and
  the tier's, so a stall that sets short base times for a demo keeps them.
- **A tier takes over after its condition has held for 4 s** (a reflection
  must not shorten the timers) and **gives way only 10 °C below its threshold,
  after 20 s**. A lost camera keeps the tier it had.
- **A stricter tier never restarts the countdown.** The clock counts from when
  the person left; a stricter tier only lowers the limits, so the time already
  away counts against them. A person away for 100 s over a simmer whose pan
  then reaches 260 °C gets the buzzer within seconds. The buzzer still sounds at
  least **10 s before the shutdown** in that case.
- **Cooking judgment and tiers are separate.** The tier decides when the buzzer
  and shutdown are due; a hold (below) decides whether a due one waits.

## Holds: a due buzzer or shutdown that waits

A warning or shutdown that comes due can be held back, for three reasons, each
with a bound:

| Hold | When | How long |
|---|---|---|
| **falling** | the temperature is falling (2 °C/min or faster over the last 30 s) and the trend has not yet confirmed heat going in | until it stops falling or heat is confirmed (then it fires at once), or the trend sees it is only cooling (the cooling hold takes over); at most 90 s per countdown |
| **cooling** | the pan is only cooling down (no heat going in) | until the trend flattens out hot (then it fires at once), or the pan drops below the cooking temperature (the timeline is cancelled); at most 4 τ of the profile, 1 h at most |
| **return** | the radar sees someone right now and the return is being confirmed, but `presence_return_debounce_ms` has not passed | until it is confirmed (the timeline is cancelled), or the radar loses them (the held step fires at once); at most `return_debounce + gap` |

**Why the falling hold.** A falling pan is moving away from danger, so a buzzer
or shutdown that comes due then can wait until the controller knows more. It
matters most at the hot tiers, whose times are shorter than the 1-2 minutes the
cooling judgment needs: without it, a burner switched off at 260 °C would get
the buzzer at 30 s and the gas cut at 60 s, for a stove that is already off.
With it, that pan is held, then the cooling hold takes over, and it cools to
idle without a buzzer. A burner turned down (still heating, settling lower) is
held only until the trend confirms heat is going in, or the 90 s limit; cold
food dropped into hot oil is held until the oil recovers and rises again. Heat
counts as confirmed when the predicted settling temperature (T + τ × rate, see
"Cooking judgment") is above the off temperature and has stopped moving (at
most 8 °C/min). Setting: `RB_SAFETY_FALLING_HOLD` (on); the threshold and the
limit are on the Other settings card.

The cooling judgment needs the cookware profile to be roughly right. Measured
in a simulation with the default "mixed" profile (τ 10 min), a burner switched
off at 260 °C is held and cools to idle with a matching pan; with a heavy pan
(τ 15 min) or a light one (τ 5 min) the trend does not settle on "cooling", so
the buzzer comes once the falling hold's 90 s are used up and the shutdown
follows 30 s later (about 2-3 min after leaving). Picking the right profile
fixes it.

The unattended clock keeps counting while a transition is held. After any hold,
the buzzer sounds for **at least 30 s** before the shutdown, even if the shutdown
was due long ago, so the first thing someone nearby hears is the warning.

The earlier rule "a fault never postpones a due shutdown" still applies in
FAULT. The return hold is the one bounded exception for presence.

The **base buzzer and shutdown times are 2.5 min and 5 min** (tier 1,
`RB_SAFETY_WARNING_TIMEOUT_S`, `RB_SAFETY_SHUTDOWN_TIMEOUT_S`; they were 3 min
and 5 min, and 20 s and 40 s before that). That is long enough for the trend to
be read (about 1-2 minutes) before the buzzer is due.

The trend can't be known without a working thermal camera: with the camera
lost, heat counts as going in and nothing is held for cooling or falling. The
trend can be turned off in menuconfig (`RB_SAFETY_COOKING_TREND`).

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
