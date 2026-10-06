# RB4107 Cooking Safety System

A prototype that cuts the power to a cooking appliance when it is left
unattended. Two mmWave radars watch for a person at the stove and a thermal
camera watches the pan. If cooking is going on and nobody is there, the
controller sounds a buzzer, and if nobody comes back it switches off the
appliance through an external relay. A dashboard shows every station's state.

Built for NUS RB4107 (AY2026/27).

## How it works

```text
C4002 radar #1 → ESP32-C6 presence node A ─┐
C4002 radar #2 → ESP32-C6 presence node B ─┼─ ESP-NOW → ESP32-S3 controller
MLX90640       → ESP32-C6 thermal node    ─┘              ├── safety state machine → buzzer, shutdown relay
                                                          └── Ethernet → MQTT broker → Django → dashboard
```

1. Each sensor node reads one sensor and sends the readings to the controller
   over ESP-NOW.
2. The controller combines the two radars and runs the safety state machine:
   `IDLE → MONITORING → UNATTENDED → WARNING (buzzer, 60 s) → SHUTDOWN (relay, 90 s)`.
   A shutdown stays latched until someone resets it.
3. Missing or invalid sensor data is never treated as "safe": it puts the
   controller in `FAULT`.
4. The controller also publishes its state over MQTT. Django stores it and
   serves the dashboard. This telemetry path is optional: the controller
   protects the kitchen on its own when the network, broker or laptop is down.

## Hardware

| Part | Board |
|---|---|
| 2 × presence node | DFRobot DFR1117 (ESP32-C6) + DFRobot C4002 mmWave radar |
| Thermal node | DFRobot DFR1117 (ESP32-C6) + MLX90640 32×24 thermal camera |
| Controller | Waveshare ESP32-S3-POE-ETH-8DI-8DO (8 transistor outputs, buzzer, RTC, Ethernet) |
| Shutdown relay | external relay or contactor with a DC coil, switched by one of the controller's outputs (the board has no relays of its own) |
| Broker, backend, dashboard | a laptop running Mosquitto and Django |

## Repository

| Folder | What's in it |
|---|---|
| [`firmware/`](firmware) | ESP-IDF firmware: `presence_node`, `thermal_node`, `controller`, shared `components/`, and single-part bench tests in `testing/` |
| [`django/`](django) | MQTT subscriber, database and read-only API |
| [`frontend/`](frontend) | the monitoring dashboard (plain HTML/CSS/JS, served by Django) |
| [`tools/`](tools) | broker scripts, dev launcher, test and diagnostic scripts |
| [`docs/`](docs) | setup guide and design docs |

## Getting started

Setup, building, flashing and running everything: **[docs/setup.md](docs/setup.md)**.
All other documentation: [docs/README.md](docs/README.md).

## Status

The firmware, backend and dashboard are written and tested in emulation
(host unit tests, and the controller in QEMU with simulated nodes). Bench
testing on the real hardware is in progress. Open work is tracked in
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues).
