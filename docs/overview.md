# The project in five minutes

## The problem

A pan left on a lit stove is a common start of a kitchen fire, and the cook
leaving for a minute is the usual cause. This project is a prototype that
notices "something is cooking and nobody is there" and cuts the power to the
appliance if the person doesn't come back. It was built for NUS RB4107
(AY2026/27) for food-stall kitchens, where a supervisor can watch many stalls
from one dashboard.

## How it works

```text
C4002 radar #1 → ESP32-C6 presence node A ─┐
C4002 radar #2 → ESP32-C6 presence node B ─┼─ ESP-NOW → ESP32-S3 controller ── output 1 → relay → appliance
MLX90640       → ESP32-C6 thermal node    ─┘              │      └── buzzer
                                                          └── Wi-Fi → MQTT broker → Django → dashboard
```

1. **Is it cooking?** The thermal camera sees the hottest spot on the stove.
   Above a set temperature (or rising fast), cooking is on.
2. **Is someone there?** Two mmWave radars watch the stove. They are combined
   strictly: absent if either working radar says absent.
3. **How long has it been?** When cooking is on and nobody is there, the
   controller counts up. At the *warning* time the buzzer sounds. At the
   *shutdown* time it switches the appliance off. The hotter the pan, the
   shorter the times:

   | Pan temperature | Buzzer after | Shutdown after |
   |---|---|---|
   | below 100 °C (simmer) | 150 s | 300 s |
   | 100 °C and up | 120 s | 240 s |
   | 150 °C and up | 90 s | 180 s |
   | 200 °C and up (hot oil) | 60 s | 120 s |
   | 250 °C and up (smoke point) | 30 s | 60 s |

   These are the defaults; the reasoning is in
   [safety_state_machine.md](safety_state_machine.md).
4. **Stays off.** A shutdown is latched. Someone has to reset it, at the
   controller or from the dashboard.

## The design rules that matter

- **The controller decides alone.** The ESP32-S3 runs the whole safety
  logic. If Wi-Fi, the broker, the laptop or the dashboard is down, it
  carries on protecting the stove. Nothing in the safety path depends on the
  network.
- **Unknown is never safe.** A radar or camera that stops reporting raises a
  flag. It never counts as "someone is there". While cooking, a lost radar
  counts as absent.
- **Fail-safe output.** Output 1 keeps the relay coil energised while all is
  well and drops it on shutdown, so a controller that loses power also cuts the
  appliance. See [wiring](#wiring).
- **The rules are a table.** The state machine runs from one file
  (`firmware/components/safety/src/safety_rules.def`). The rules doc and the
  dashboard's rule view are generated from it, so they cannot drift.
- **The dashboard only watches and tunes.** It can reset a shutdown and tune
  settings. It can't switch anything on.

## Wiring

The Waveshare ESP32-S3-POE-ETH-8DI-8DO has no relays of its own. Its eight
outputs are opto-isolated transistor sinks to GND (up to 500 mA each). Output 1
pulls one side of an external relay or contactor coil to GND:

```text
coil supply (+) ──── relay coil ──── controller output 1 ──── GND (when ON)
appliance supply ──── relay NO contact ──── appliance
```

While it is safe, output 1 is ON, the coil is energised and the normally-open
contact is closed, so the appliance has power. On shutdown, or if the
controller loses power, the coil drops and the contact opens. The polarity,
channel and boot state are Kconfig settings (`RB_SHUTDOWN_*`).

## What you can see

The dashboard shows every stall's state (normal, warning, critical, fault),
a countdown to the buzzer and shutdown, the heat map, the radar view and a
history of past countdowns per stall, so an inspector can see which stalls need
a visit. Screens and fields: [dashboard.md](dashboard.md).

## Where to go next

| You want to | Read |
|---|---|
| Know how far it has been proven | [status.md](status.md) |
| Look up a term | [glossary.md](glossary.md) |
| Understand why the rules are what they are | [safety_state_machine.md](safety_state_machine.md) |
| See how the firmware is organised | [architecture.md](architecture.md) |
| Build, flash and run it | [setup.md](setup.md) |
| See what a real deployment would still need | [demo_vs_deployment.md](demo_vs_deployment.md) |
