# Documentation

Start with **[setup.md](setup.md)** to build, flash and run the system.

| Doc | Read it when you want to know |
|---|---|
| [setup.md](setup.md) | how to build, flash, configure and run everything, and how to test |
| [architecture.md](architecture.md) | how the controller firmware is organised: tasks, faults, MQTT publishing |
| [safety_state_machine.md](safety_state_machine.md) | exactly when the controller warns and shuts down |
| [protocol.md](protocol.md) | the ESP-NOW packets between the nodes and the controller |
| [mqtt_topics.md](mqtt_topics.md) | which MQTT topics the controller publishes |
| [c4002_tuning.md](c4002_tuning.md) | tuning and calibrating the radars from the dashboard |
| [mqtt_schema.md](mqtt_schema.md) | the JSON inside those messages ([machine-readable schema](schema/rb4107_mqtt.schema.json)) |
| [dashboard.md](dashboard.md) | how firmware fields map to the dashboard, and its limits |
| [logging.md](logging.md) | log tags, levels and how to change verbosity |
| [configuration.md](configuration.md) | every menuconfig option (generated from the Kconfig) |

Per-project details live next to the code: [`firmware/`](../firmware/README.md)
(and each project's README), [`django/`](../django/README.md),
[`frontend/`](../frontend/README.md), [`tools/mqtt/`](../tools/mqtt/README.md).

Open work and hardware questions are tracked in
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues).

[`agent-context/`](agent-context) holds a briefing for AI coding agents
picking up this project; people can skip it.
