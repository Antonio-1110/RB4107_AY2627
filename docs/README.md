# Documentation

## Start here

| Doc | Read it when you want to know |
|---|---|
| [overview.md](overview.md) | what the project is, how it works, and its design rules, in five minutes |
| [status.md](status.md) | what is proven, on the bench or only in emulation, and what is open |
| [glossary.md](glossary.md) | what a term means (UNATTENDED, tier, flag, incident …) |
| [setup.md](setup.md) | how to build, flash, configure, run and test everything |

## How it is designed

| Doc | Read it when you want to know |
|---|---|
| [safety_state_machine.md](safety_state_machine.md) | why the rules are what they are: states, trend, tiers and holds |
| [architecture.md](architecture.md) | how the controller firmware is organised, and the design decisions |
| [demo_vs_deployment.md](demo_vs_deployment.md) | what the demo leaves out and what a real deployment would need |

## Features

| Doc | Read it when you want to know |
|---|---|
| [dashboard.md](dashboard.md) | what the dashboard shows and how firmware fields map to it |
| [c4002_tuning.md](c4002_tuning.md) | tuning and calibrating the radars from the dashboard |
| [remote_reset.md](remote_reset.md) | resetting a latched shutdown, and changing timers from the dashboard |
| [telegram_alerts.md](telegram_alerts.md) | sending buzzer and shutdown alerts to staff on Telegram |
| [logging.md](logging.md) | log tags, levels and how to change verbosity |

## Reference

| Doc | Contents |
|---|---|
| [safety_rules.md](safety_rules.md) | every rule the controller follows. **Generated** from the rules table |
| [configuration.md](configuration.md) | every menuconfig option. **Generated** from the Kconfig |
| [protocol.md](protocol.md) | the ESP-NOW packets between nodes and controller |
| [mqtt_schema.md](mqtt_schema.md) | MQTT topics and the JSON inside them ([machine-readable schema](schema/rb4107_mqtt.schema.json)) |

Per-project details live next to the code: [`firmware/`](../firmware/README.md)
(and each project's README), [`django/`](../django/README.md),
[`frontend/`](../frontend/README.md), [`tools/mqtt/`](../tools/mqtt/README.md).
Open work is tracked in
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues).
