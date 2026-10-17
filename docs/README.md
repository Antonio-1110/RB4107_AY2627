# Documentation

Start with **[setup.md](setup.md)** to build, flash and run the system.

| Doc | Read it when you want to know |
|---|---|
| [setup.md](setup.md) | how to build, flash, configure and run everything, and how to test |
| [architecture.md](architecture.md) | how the controller firmware is organised: tasks, faults, MQTT publishing |
| [safety_rules.md](safety_rules.md) | every rule the controller follows and what it does (generated from the table the firmware runs) |
| [safety_state_machine.md](safety_state_machine.md) | why the rules are what they are: the state machine, trend, tiers and holds explained |
| [protocol.md](protocol.md) | the ESP-NOW packets between the nodes and the controller |
| [mqtt_topics.md](mqtt_topics.md) | which MQTT topics the controller publishes |
| [c4002_tuning.md](c4002_tuning.md) | tuning and calibrating the radars from the dashboard |
| [remote_reset.md](remote_reset.md) | resetting a latched shutdown from the dashboard |
| [mqtt_schema.md](mqtt_schema.md) | the JSON inside those messages ([machine-readable schema](schema/rb4107_mqtt.schema.json)) |
| [dashboard.md](dashboard.md) | how firmware fields map to the dashboard, and its limits |
| [logging.md](logging.md) | log tags, levels and how to change verbosity |
| [configuration.md](configuration.md) | every menuconfig option (generated from the Kconfig) |
| [demo_vs_deployment.md](demo_vs_deployment.md) | what the demo leaves out and what a real deployment would need |

Per-project details live next to the code: [`firmware/`](../firmware/README.md)
(and each project's README), [`django/`](../django/README.md),
[`frontend/`](../frontend/README.md), [`tools/mqtt/`](../tools/mqtt/README.md).

Open work and hardware questions are tracked in
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues).

[`agent-context/`](agent-context) holds a briefing for AI coding agents
picking up this project; people can skip it.
