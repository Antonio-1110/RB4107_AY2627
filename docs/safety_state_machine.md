# Safety state machine

Implementation: [`firmware/components/safety`](../firmware/components/safety).
Unit tests: [`firmware/testing/unit_tests`](../firmware/testing/unit_tests).
Every rule (transitions, outputs, current settings), generated from the table
the code runs: [safety_rules.md](safety_rules.md). Defaults for every setting
named below: [configuration.md](configuration.md). This page keeps the reasons.

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
| `presence` (tri-state) | the two C4002 presence nodes via ESP-NOW, combined strictly (see "Who is at the stove, and the pause") | `RB_UNKNOWN` raises the **presence unknown** flag, never FAULT. Nothing cooking: no timer. Cooking: counts as absent ("Lost sensors"). Never read as "present". |
| `thermal_valid`, `cooking_temp_c` | MLX90640 features from the thermal node; `cooking_temp_c` is the hottest pixel (`max_temp_c`), the number the dashboard shows | invalid raises the **thermal lost** flag, never FAULT. The last cooking state is kept: a running timeline carries on, no cooking is invented. |
| `self_test` | controller self-test (outputs working) | must read PASS after SELF_TEST; FAIL, timeout or not-yet-passed → FAULT, latched until it passes |
| `reset_request` | operator reset: dashboard button, console `reset` ([remote_reset.md](remote_reset.md)) | leaves SHUTDOWN; acknowledges WARNING in *exit-on-ack* mode |
| `safety_fault` | fault manager: the controller's own faults only (`shutdown_output_failure`, `self_test_failed`). A lost sensor or node is a safety-class fault in the fault list too, but only raises the flags above | → FAULT |

## Transitions and outputs

Generated from the table the firmware runs: **[safety_rules.md](safety_rules.md)**.
Shutdown output 1 is energised while safe, de-energised on shutdown or power
loss, so losing power also cuts the gas.

## Timing rules

- All timers are monotonic (`now - start >= timeout`, wrap-safe), checked on each
  `safety_step()`. Nothing blocks; wall-clock time is never used.
- The unattended timer starts when the absence **began** (not when the debounce
  confirmed it), but never earlier than the moment cooking was detected.
- The timeline carries on through FAULT: a fault never delays a shutdown that is
  already due. If the fault clears while the person is still away, the timeline
  continues; if none was running, it starts when the fault began.
  `fault_shutdown_timeout_ms` only applies to the controller's own faults.
- After an operator reset from SHUTDOWN all timing starts over.
- **The S3 does all the presence debouncing.** The nodes send raw radar state on
  every change; the C4002's own disappear delay and lock time are settable, but
  a small hold is enough ([c4002_tuning.md](c4002_tuning.md)).
- **Absence** must last `absence_debounce_ms` before UNATTENDED. A presence blip
  does not restart that count (it starts at the first "absent"; only a
  *confirmed* return stops it), so radar noise can't keep absence from ever
  being confirmed.
- **Return** must last `presence_return_debounce_ms` before it cancels UNATTENDED
  or WARNING, so a single reading (a hand waved at the radar, someone walking
  past) cancels nothing; the clock only pauses meanwhile. "Absent" gaps shorter
  than `presence_return_gap_ms` don't restart the count, but the person must be
  detected when it completes; UNKNOWN stops it at once. Blips more frequent than
  the gap for the whole return time still count as a return (a windowed vote
  could separate that from noise; not built). These and the warning/shutdown
  times can be changed from the dashboard until reboot
  ([remote_reset.md](remote_reset.md#presence-filter-and-timers)).
- The optional `timing_hook` lets the temperature trend shorten the
  warning/shutdown timeouts. It can only make them stricter.

## Radar state and distance rules (placeholder)

The C4002 reports each target as none, moving or stationary, with a distance.
The controller keeps one **episode** per radar: it starts when the radar first
sees a target, survives short gaps (`quiet_gap_ms`), and records its length, how
much was stationary, and how far the distance wandered. Verdict: `none` (no
episode); `dwelling` (long enough, enough of it stationary, distance span inside
`settle_span_cm`, optionally inside `zone_max_cm`); `passing` (anything else: a
target that keeps moving or sweeping range). Modes (`RB_CTRL_RADAR_RULES`):
**off**; **observe** (default: shown on the dashboard, changes nothing);
**enforce** (a radar that sees someone only *passing* does not count as "at the
stove", so a walker behind the stove neither pauses nor cancels the clock). Mode
and numbers are tunable (Tuning > Rules > Person); every number is a placeholder,
not tuned on real walks or cooks ([demo_vs_deployment.md](demo_vs_deployment.md)).

## Cooking judgment: is heat still going in?

"Cooking" is decided by the temperature and its direction (rules B1, B3, B6). It
starts when the hottest pixel reaches the cooking temperature (50 °C) unless the
pan is still falling, so a hot pan just put in view, or one that was switched
off, is not cooking until the fall stops. It ends when the pan falls below the
off temperature (47 °C by default, a 3 °C margin so a pan hovering at 50 °C
does not flip on every reading), or sooner when it is below 60 °C and falling
by 0.5 °C/min or more. A pan that holds steady or is turned down therefore
stays cooking until it is below 47 °C; and a pan in the 50-60 °C band
that stops falling counts as cooking again. The direction is the thermal node's
own rate; an unknown rate never stops cooking. These rate limits and the 60 °C
are fixed in `safety.c`. Once on, the S3 asks a second question: is heat
still going in, or is the pan just cooling down? The two mistakes cost very
different amounts. Calling a cooling pan "cooking" is a nuisance (the burner is
already off); calling a lowered flame "off" is the dangerous one. A falling
temperature also means less heat goes in than comes out, a low fire hazard: oil
fires start when a pan keeps being heated unattended
([NIST TN 2133](https://nvlpubs.nist.gov/nistpubs/TechnicalNotes/NIST.TN.2133.pdf)).

The decision uses the **trend**, not the absolute temperature, so it also works
for a shiny pan that reads too cold:

1. Every 2 s the S3 stores the **hot region** (mean of the 3×3 pixels around the
   hottest one; tiers still use the hottest pixel). A weighted least-squares
   line over the last 2 minutes gives the rate (at least 12 samples over 60 s;
   until then the phase is *checking* and heat counts as going in). The fit runs
   twice; the second leaves out samples the first missed by more than 3 robust
   standard deviations (stirring, steam, a reflection), so one jump can't swing
   it. The residuals give the rate's standard error.
2. From the cooling law, a pan with nothing keeping it hot falls at
   `(T − room) / τ`. The pan is **cooling** when it falls at least as fast as a
   pan settling 3 °C below the *cooling line* would, and the rate is clear of
   that by 2 standard errors. The line is 10 °C above the **room temperature**,
   never above the cooking temperature. This equals the test
   `T + τ × rate < line`, but compares rates, so the rate's noise is not
   multiplied by τ (40 min × 0.5 °C/min of noise would be ±20 °C on a
   prediction). Otherwise heat is going in: **holding** (steady or settling hot:
   a lowered flame, a simmer, boiling water) or **heating** (rising faster than
   0.5 °C/min).
3. Holding back a buzzer needs that confidence; letting go of a hold does not.
   The hold ends as soon as the pan falls slower than a pan settling 3 °C above
   the line would.

**The room temperature** is measured: the thermal node sends the 10th-percentile
pixel of each frame (the pan and a few cold pixels, such as a window or a cold
drink, don't move it). The controller clamps it to 36-45 °C and uses 38 °C when
the camera has no reading. In a hot kitchen the line moves up with the room, so
a pan cooling toward a 40 °C room is not mistaken for heat going in. These
numbers are fixed in `safety.c`.

τ comes from the **cookware profile**, chosen per stall on the dashboard
(Tuning → Rules) and saved on the controller. The values are engineering
estimates from typical mass and heat capacity; no source found publishes
measured cooling constants for cookware, so check them against your own pans:

| Profile | For | τ |
|---|---|---|
| mixed / unknown (default) | ordinary pans, between light and heavy | 10 min |
| light | woks, thin aluminium or stainless pans | 5 min (3–8) |
| heavy | cast iron, thick clad pans | 15 min (10–20) |
| liquid | pots of soup, stock, boiling water | 40 min (20–60; longer with a lid) |

Extremes (searing pans, very large stock pots, deep fryers) are not profiled.

**The judgment checks itself.** `T + τ × rate` stays near the room temperature
for a cooling pan when the profile fits; if τ is too long it climbs as a lowered
flame's warm-up tail relaxes. So "cooling" is only believed while the estimate
has not risen by more than 8 °C over the last minute, and is dropped if it rises
by more than 16 °C/min plus its own noise (2 × 1.4 × τ × the rate's standard
error). Without that allowance a cooling pot on the liquid profile lost its hold
every minute. A profile that doesn't fit falls back to the normal timers.

What a wrong profile costs, simulated on model cooling curves (not measured on
real cookware; `tools/diagnostics/cooling_sweep.c` and
`tools/diagnostics/cooling_delay.c` reproduce it):

- **Too short** (e.g. *light* for a heavy pot): a cooling pan is recognised late
  or never, so buzzer and shutdown run as before (a switched-off 260 °C pan then
  gets the buzzer once the falling hold's 90 s are used up and the shutdown 30 s
  later). Nuisance, never dangerous.
- **Too long** (e.g. *liquid* for a light pan), lowered flame, buzzer due at 120 s
  with a 5 min pan: on time up to a profile 1.5× slower than the pan; for 2× to
  8× slower it comes **36–68 s late**, never missed. A hold never lasts longer
  than 4 τ (an hour at most), and after it the buzzer sounds for 30 s before
  any shutdown.
- **Matched**: a pan with the burner off is recognised after about 30-90 s and
  held until cold; the same pan on a lowered flame is not held.

So the demo needs the profile roughly right for the cookware on the stove. When
unsure, pick the shorter one.

## Who is at the stove, and the pause

False presence is the dangerous mistake: a person walking behind the sensor can
cancel the timeline and leave the gas on with nobody at the stove. Two rules
limit it.

**1. Someone only counts as at the stove when every working radar sees them**
(`presence_fuse_all`, setting `RB_CTRL_PRESENCE_FUSION`, or Tuning, Rules, "At
the stove when"). A person seen by one radar only (a rear-lobe echo, someone
behind the sensor) is *nearby*, not at the stove. A radar that is down is not
asked; with no working radar the presence is unknown. The dashboard shows both:
"at the stove" is what the safety logic uses, "nearby" is either radar. The
older "either radar" rule is still selectable.

**2. A pass pauses the clock; staying cancels the timeline.** While someone is at
the stove on an unattended timeline (UNATTENDED or WARNING), the clock stands
still, so only time with nobody there counts. If they stay
`presence_return_debounce_ms`, the timeline is cancelled. If they leave sooner,
the clock resumes from where it stopped.

| Situation | Result |
|---|---|
| Walker passes in 2 s | clock paused for 2 s, nothing reset |
| Cook glances at the pot for 4 s | paused for 4 s, nothing reset |
| Cook stays 5 s or more | timeline cancelled |
| Walker lingers 5 s or more where every radar sees them | looks like a cook: cancelled |

(5 s is the default return time.) A pause postpones the buzzer and shutdown
while the pan keeps heating, so it has a **budget per timeline**:
`RB_SAFETY_PAUSE_BUDGET_S` in total and `RB_SAFETY_PAUSE_BUDGET_PCT` of the
shutdown time in force, whichever is smaller. With the defaults (30 s, 25 %),
oil in tier 5 (60 s shutdown) can be delayed by at most 15 s. The budget starts
over when the timeline is cancelled; 0 turns the pause off. The bounded "return
hold" below still applies when a transition comes due just as someone appears.
What is *not* done: telling a walker from a cook by the radar's moving or
stationary state, or by distance. That needs real recordings first; the pause
and the cancel do not depend on it.

## Temperature tiers: the hotter the pan, the shorter the time

The countdown is set by how dangerous the pan is now, not by how long cooks
usually step away (that varies between canteens, and we have no canteen data).
Two inputs decide it: the hottest pixel, and how fast it is rising.

**The anchor: 30 s of buzzer at the top.** NIST heated corn oil on an electric
coil from cold: time to auto-ignition was `460 s + 3.9 s per gram of oil`
(50-150 g: 11 to 17 minutes), ignition came at a pan-bottom temperature of
392 °C, and the last stage, from visible smoke to ignition, lasted only 30-60 s
([NIST TN 2133](https://nvlpubs.nist.gov/nistpubs/TechnicalNotes/NIST.TN.2133.pdf)).
So a pan at or past oil's smoke point (about 190-270 °C by oil) must give the
cook at least that 30 s of warning, and the shutdown comes 30 s after it. Gas
burners were not tested there and heat faster; a pan already hot when the cook
leaves has far less than 11 minutes left.

**The other tiers are a baseline.** The buzzer steps down evenly, 30 s per tier,
from the loosest (a simmer) to the strictest, and the shutdown is always twice
the buzzer. Default values (`RB_SAFETY_TIER_*`; tunable from the dashboard,
Tuning, Rules, [remote_reset.md](remote_reset.md#temperature-tiers)):

| Tier | Hottest pixel | Buzzer | Shutdown |
|---|---|---|---|
| 1 | below 100 °C (a simmer) | base time (150 s) | base time (300 s) |
| 2 | 100 °C and up (boiling dry, gentle frying) | 120 s | 240 s |
| 3 | 150 °C and up (shallow frying) | 90 s | 180 s |
| 4 | 200 °C and up (hot oil, near the lowest smoke points) | 60 s | 120 s |
| 5 | 250 °C and up (oil at or past its smoke point) | 30 s | 60 s |

The temperatures between tier 1 and 5 are evenly spaced estimates, not
measurements, to be tuned on real camera readings of hot oil. The base times are
long enough for the trend to be read (about 1-2 minutes) before the buzzer is
due. The MLX90640 reads objects up to 300 °C (datasheet); bare shiny metal may
read lower than the real temperature (low emissivity, not measured here), while
oil and food read closer to true.

**The rate: a pan heading for a tier gets it already.** A pan at 185 °C rising
2 °C/s is under tier 4 (200 °C), but reaches 245 °C in 30 s and 305 °C in 60 s.
Tier k applies when *temperature + rise rate × tier k's buzzer time* reaches
tier k's temperature, so the buzzer sounds no later than the moment the pan
would get there. The rate is measured over the last 30 s as the median of the
newer half minus the median of the older half: one bad frame (a reflection,
steam) can't fake a fast rise. Noise errs early (a buzzer too soon, never too
late), and only a rise counts. Setting: `RB_SAFETY_TIER_RATE`; the 30 s window is
fixed.

- **Tiers only shorten.** The time in force is the smaller of the base time and
  the tier's, so a stall that sets short base times for a demo keeps them.
- **A tier takes over after its condition has held for 4 s** (a reflection must
  not shorten the timers) and **gives way only 10 °C below its threshold, after
  20 s**. A lost camera keeps the tier it had.
- **A stricter tier never restarts the countdown.** The clock counts from when
  the person left; a stricter tier only lowers the limits, so time already away
  counts. A person away 100 s over a simmer whose pan then reaches 260 °C gets
  the buzzer within seconds, and the buzzer still sounds at least **10 s before
  the shutdown**.
- **Cooking judgment and tiers are separate.** The tier decides when the buzzer
  and shutdown are due; a hold decides whether a due one waits.

## Holds: a due buzzer or shutdown that waits

A warning or shutdown that comes due can be held back, for three reasons, each
with a bound:

| Hold | When | How long |
|---|---|---|
| **falling** | the temperature is falling over the last 30 s by at least 2 % per minute of its heat above the room (and at least 1 °C/min), and the trend has not yet confirmed heat going in | until it stops falling or heat is confirmed (then it fires at once), or the trend sees it is only cooling (the cooling hold takes over); at most 90 s per countdown |
| **cooling** | the pan is only cooling down (no heat going in) | until the trend flattens out hot (then it fires at once), or the pan drops below the cooking temperature (timeline cancelled); at most 4 τ of the profile, 1 h at most |
| **return** | the radar sees someone right now and the return is being confirmed, but `presence_return_debounce_ms` has not passed | until it is confirmed (timeline cancelled), or the radar loses them (the held step fires at once); at most `return_debounce + gap` |

**Why the falling hold.** A falling pan is moving away from danger, so a buzzer
or shutdown that comes due then can wait until the controller knows more. It
matters most at the hot tiers, whose times are shorter than the 1-2 minutes the
cooling judgment needs: without it, a burner switched off at 260 °C would get
the buzzer at 30 s and the gas cut at 60 s, for a stove that is already off.
With it, that pan is held, then the cooling hold takes over, and it cools to
idle without a buzzer. A burner turned down (still heating, settling lower) is
held only until the trend confirms heat is going in, or the 90 s limit; cold food
dropped into hot oil is held until the oil recovers and rises again. Heat counts
as confirmed when the predicted settling temperature (T + τ × rate) is above the
cooling line and has stopped moving (at most 8 °C/min). Setting:
`RB_SAFETY_FALLING_HOLD` and its limit; the thresholds are fixed in the firmware.

**Why "falling" is relative to the room.** A pan loses heat in proportion to how
far it is above the room (Newton's law). Near the room even a switched-off pan
falls slowly: a heavy pan (τ 15 min) at 60 °C in a 30 °C room falls only
2 °C/min, at 220 °C about 13 °C/min. A fixed threshold is therefore too strict
near the room and too loose when hot. Losing 2 % of the heat above the room per
minute is the cooling of a pan with τ = 50 min, slower than every profile (the
slowest, liquid, is 40 min), so any switched-off pan counts as falling. A pan at
220 °C falling only 3 °C/min (1.6 %) is a lowered flame, not a switched-off
burner, and is not held. The 1 °C/min floor keeps camera noise near the room
from counting as a fall.

**Checking: before the countdown starts.** A cook who switches off and walks
away is confirmed absent within a second, but a fall needs about 15 s (half the
30 s rate window) to show. So when the absence is confirmed, the controller
first looks for up to 15 s (not if the pan is rising, or the buzzer is already
due), then keeps waiting only while the pan falls, at most the falling hold's
90 s, which a later falling hold shares. The state stays MONITORING with `hold`
= `checking` and the dashboard says "Checking: stove off or turned down?". No
unattended episode opens, so a stove that was switched off and cools below
60 °C (B6) never reaches the Stall history. The time away still counts from when
the absence began, so a buzzer is never later because of a check: a steady pan
gets its countdown after the look with those seconds already counted.

The unattended clock keeps counting while a transition is held. After any hold,
the buzzer sounds for **at least 30 s** before the shutdown, even if the shutdown
was due long ago, so the first thing someone nearby hears is the warning. The
return hold is the one bounded exception for presence: in FAULT a due shutdown is
never postponed.

Without a working thermal camera the trend can't be known: heat counts as going
in and nothing is held for cooling or falling. The trend can be turned off
(`RB_SAFETY_COOKING_TREND`).

## Lost sensors: flags, not FAULT

A lost sensor is shown, not turned into a fault state. The state machine raises
two flags that reach the dashboard (`safety.presence_unknown`,
`safety.thermal_lost`; per-node status comes from the node list):

| Situation | What happens |
|---|---|
| Presence UNKNOWN, nothing cooking | flag only: no timer, no shutdown |
| Presence UNKNOWN, cooking (per the thermal camera) | flag, and presence counts as **absent**: the absence debounce and the unattended timeline run. Only a confirmed return (radar TRUE for `presence_return_debounce_ms`) stops it |
| Thermal camera lost or invalid | flag only. The last known cooking state is kept, so a running timeline carries on and a cold kitchen doesn't start one |
| Controller self-test or relay output failing | FAULT |

## Defaults still to confirm

The timings, what happens when a person returns during WARNING or the pan
cools, and the fault shutdown time all have provisional defaults. The team
decision is tracked in [issue #24](https://github.com/Antonio-1110/RB4107_AY2627/issues/24), the cooking
temperature thresholds in [issue #21](https://github.com/Antonio-1110/RB4107_AY2627/issues/21).
