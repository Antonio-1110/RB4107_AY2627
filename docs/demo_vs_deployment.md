# Demo versus deployment

This project is a **demo**. This page keeps track of the gap to a real
deployment: what the demo deliberately leaves out, why that is fine for a demo,
and what a deployment would need. Update it when something changes. It is also
the list to draw on for the final presentation.

How to read the status: **Demo** = what the demo does today. **Deployment** =
what a real installation would need. *Checked* means I looked at the code or
measured it; *not checked* means it is an assumption.

## Information security (out of scope for the project; mention in the presentation)

| Topic | Demo | Deployment | Evidence |
|---|---|---|---|
| MQTT broker access | anonymous on the LAN (`allow_anonymous true`) | username/password and TLS | *checked*: `tools/mqtt/mosquitto.conf` |
| Dashboard API | no login; `csrf_exempt` on every command endpoint | authentication and authorisation per operator | *checked*: `django/rb4107_backend/urls.py`, `ingest/views.py` |
| Remote **reset** of a latched shutdown | any dashboard user can restore the gas | reset only at the stall (physical button), or an authenticated and logged operator action | *checked*: [remote_reset.md](remote_reset.md) |
| Remote safety settings (presence filter, timers, cooking temperature, cookware) | accepted within wide limits, so they can be set to weaken the protection | hard floors and ceilings on the S3 that the dashboard can't override; settings may only make things stricter without a physical key | *checked*: limits in `rb_ctrl_cmd.h` (warning/shutdown up to 1 h, cooking temperature up to 300 °C) |
| ESP-NOW between nodes and controller | unencrypted and unauthenticated (CRC only) | ESP-NOW encryption (PMK/LMK) or a message authentication code, plus replay protection | *checked*: `.encrypt = false` in `rb_espnow.c` |
| "Monitor / Tuning" switch on the dashboard | cosmetic: it hides the tools, it does not lock the API | server-side permission on the tuning endpoints | *checked*: [dashboard.md](dashboard.md#monitor-and-tuning-modes) |
| Personal data | presence of staff is logged | retention and access rules for presence and thermal data | retention exists (see Data); policy *not checked* |

## Safety hardware and fail-safe behaviour

| Topic | Demo | Deployment | Evidence |
|---|---|---|---|
| Relay polarity | de-energise to shut down: a controller with no power also cuts the supply | keep, and use a valve that is closed when unpowered | *checked*: `RB_SHUTDOWN_POLARITY` default in the Kconfig |
| Hung (not crashed) safety task | nothing ties it to the relay; the relay stays energised | a hardware or task watchdog that de-energises the relay if the safety loop stops | *checked*: no such watchdog found in the controller |
| Relay state during boot | released (appliance powered) is the default; marked an open question | decide and test the safe state | *checked*: `RB_SHUTDOWN_BOOT_STATE` |
| Reboot while a timeline is running | the timeline starts over from IDLE | keep the unattended time across a reboot, or treat a reboot with a hot stove as already unattended | *checked*: state is RAM only |
| Wireless valve | closes on its own timeout if the controller stops sending | same, tested with real valves | *checked*: `rb_valve_link.h`; hardware *not checked* |
| Certification | none | a certified valve and a documented fail-closed design, reviewed by whoever signs off on the safety case | *not checked* (standards not looked up) |

## Sensing and the cooking judgment

| Topic | Demo | Deployment | Evidence |
|---|---|---|---|
| Cooking temperature | 50 °C on, 40 °C off, placeholders | measured on the real cookware and stove ([issue #21](https://github.com/Antonio-1110/RB4107_AY2627/issues/21)); shiny pans read far too cold on infrared | placeholders *checked*; emissivity from [Stella Culinary](https://mobile.stellaculinary.com/node/3643) and [America's Test Kitchen](https://www.americastestkitchen.com/taste_tests/2452-infrared-thermometers) |
| Cookware time constants | estimates from typical mass and heat capacity | measured cool-down curves for the stall's actual cookware | estimates, *not measured*; simulated sweep in `tools/diagnostics/cooling_sweep.c` |
| Several burners | one trend for the hottest pixel of the whole frame, so one pan can hide another | track each burner separately | design limit, *checked* in `safety.c` |
| Spikes in the hottest pixel (a hand, steam) | the trend is a plain least-squares fit, so a spike skews it | median filter before the fit | design limit, *checked* |
| Presence | two radars OR-ed; a false target on either keeps the gas on ([issue #20](https://github.com/Antonio-1110/RB4107_AY2627/issues/20)) | a second, independent presence vote (the thermal camera sees warm bodies), zone and range tuning | radar OR *checked*; thermal vote *not built* |
| A lost sensor | a flag on the dashboard only; no local sound or light | a local indication that protection is degraded | *checked*: buzzer patterns exist for warning, shutdown and fault only |
| Kitchen environment | bench setup | grease, steam, heat and cleaning on the lens and the radar housing | *not checked* |

## Data and storage

| Topic | Demo | Deployment | Evidence |
|---|---|---|---|
| Database growth | `tools/run_dev.sh` erases the database every time it stops, so it never builds up | a retention job (built, defaults below) and sized disk | *measured*: about 0.86 GB per day per controller without pruning ([dashboard.md](dashboard.md#storage)) |
| Raw radar stream for the live view | on at 250 ms (about 320 MB per day if stored) | set `RB_C4002_LIVE_PERIOD_MS=0` on the presence nodes: it is only used by the Tuning radar view | *measured*; see below |
| Retention | built and tested; first run one minute after the subscriber starts | confirm the numbers with whoever owns the data | `ingest/retention.py` |

`RB_C4002_LIVE_PERIOD_MS` is a presence-node menuconfig option (*Sensor node,
C4002 presence sensor, "Send raw C4002 results for the dashboard's live view
every (ms)"*, default 250). It sets how often the node sends each raw radar
result to the controller, which publishes it over MQTT for the Tuning radar view.
Results can't come faster than the radar's report period (0.5 s), so the real
rate is about 2 per second per radar. **0 turns the stream off**: the live view
then stays empty, and the safety path is not affected. For the demo, keep the
default if you want to show the live radar view.

## Operations and testing

| Topic | Demo | Deployment | Evidence |
|---|---|---|---|
| Automated tests | host tests run by hand (safety, protocol, JSON, Django); the ESP-IDF unit-test app has to be built with ESP-IDF | CI that runs the host tests on every push | *checked*: no `.github` folder; the unit-test app did not compile for a while before it was noticed |
| Adding a node | node IDs by MAC address in `sdkconfig`, reflash to add one | provisioning without a rebuild | *checked*: `presence_node/sdkconfig.defaults` |
| Dashboard and flashed settings | settings sent from the dashboard may differ from the flashed defaults; the presence filter is lost on reboot | one place that shows what is running and where it came from | partly built (the telemetry reports the source of each setting) |
| Hardware-in-the-loop test | none | a repeatable end-to-end test with real sensors and a valve | *not built* |

## Demo notes (things to know when presenting)

- **Accelerated timers and the cooking judgment don't mix.** The temperature
  trend needs at least 60 seconds of history, so the cooling hold can't engage
  under test timers with a warning time of a few seconds. Show the cooling hold
  with the dashboard's demo scenario ("cooling" in `simulate_fleet`) or with real
  timers and a pan that is really cooling. A lowered flame versus an off burner
  needs several minutes on real hardware.
- **After any hold the buzzer sounds for at least 30 s before a shutdown**, so a
  demo of a held timeline ends with a visible warning first.
- **The cookware profile and the cooking temperature are saved on the
  controller**, so a profile set at rehearsal is still there on stage.
- **`tools/run_dev.sh` wipes the database on stop**, which is what you want
  for a clean demo and not what you want if you need yesterday's data.
