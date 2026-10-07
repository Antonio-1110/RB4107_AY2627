# Handover for AI coding agents

Read this first when picking up the project without context, then
[`README.md`](../../README.md), [`docs/setup.md`](../setup.md) and the open
[GitHub issues](https://github.com/Antonio-1110/RB4107_AY2627/issues).

## Where things stand

- Firmware, Django backend and dashboard are written. Everything has run in
  emulation (host unit tests, the controller in QEMU with simulated nodes,
  Django against a real broker). The full S3 `idf.py build` passes on `main`.
- Bench testing on real hardware is in progress. Some pins found on the bench
  are not yet the Kconfig defaults (issue #19).
- Open work, bench checks and team decisions are GitHub issues. There is no
  TODO file: add an issue instead.

## The system

```text
C4002 #1 → ESP32-C6 presence node A (node ID 1) ─┐
C4002 #2 → ESP32-C6 presence node B (node ID 2) ─┼─ ESP-NOW → ESP32-S3 controller
MLX90640 → ESP32-C6 thermal node    (node ID 3) ─┘            ├── safety state machine → buzzer, shutdown relay
                                                              └── Ethernet → Mosquitto (laptop) → django/ → frontend/
```

- C6 boards: DFRobot DFR1117. S3 board: **Waveshare ESP32-S3-POE-ETH-8DI-8DO**
  (not the 8DI-8RO: no onboard relays). Its 8 outputs are opto-isolated
  Darlington sinks to GND (≤ 500 mA each) driven by a TCA9554 at 0x20, so the
  shutdown switches an external relay/contactor coil. Also PCF85063 RTC at
  0x51 (no battery; time comes from SNTP), W5500 Ethernet, buzzer on GPIO46.
  Code, Kconfig menus and issues #6–#9 still say "relay"; that is the
  external relay driven by an output.
- The S3 must keep protecting the kitchen when MQTT, Django, the laptop or
  the network is down. Never add a dependency from the safety path on the
  telemetry path.

## Layout

| Path | What |
|---|---|
| `firmware/presence_node`, `thermal_node`, `controller` | the three system firmwares |
| `firmware/components/` | shared code; map in [architecture.md](../architecture.md#where-the-code-lives-firmwarecomponents) |
| `firmware/testing/` | one bench-test project per hardware part, plus `unit_tests` |
| `firmware/components/rb_config/Kconfig` | every tunable. Regenerate `docs/configuration.md` after editing it (command at the end of that file) |
| `django/` | MQTT subscriber, SQLite, read-only API; serves `frontend/` at `/` |
| `tools/run_dev.sh` | starts broker + subscriber + Django for local dev |
| `tools/run_demo.sh` | same, with simulated stalls instead of the broker and subscriber (no hardware) |
| `tools/stop_dev.sh` | stops what `run_dev.sh` / `run_demo.sh` started, plus leftovers (this project only) |
| `tools/run_host_tests.sh` | firmware unit tests (linux target) + Django tests |

## Decisions already made (don't reopen without the user)

- One node per sensor. `presence_node` is one firmware flashed on two boards;
  node B is built with `sdkconfig.node_b` in its own build folder.
- Radars combined strictly: PRESENT if either sees a person; ABSENT only if
  both are valid and say absent; anything else is UNKNOWN → FAULT.
- The S3 decides; nodes only send readings (edge decisions: issue #24).
- Missing data is never "safe": UNKNOWN / invalid / JSON `null`.
- ESP-NOW protocol v2 and MQTT schema v2. Django only accepts schema v2.
- Unconfirmed hardware values stay configurable and are flagged
  `UNCONFIRMED` / `OPEN QUESTION` in the Kconfig.

## Gotchas

- `sdkconfig.defaults` only applies when a project has no `sdkconfig`.
  Delete `sdkconfig` after changing defaults.
- **Never commit `sdkconfig`.** A Wi-Fi password leaked once and history had
  to be rewritten.
- The node B build command is spelled out in [setup.md](../setup.md#firmware);
  a `$VAR` shortcut doesn't word-split in zsh.
- ESP-NOW and Wi-Fi share the radio, so with Wi-Fi the ESP-NOW channel must
  match the router on every board. Ethernet avoids this and is the default.
- DFRobot's Arduino C4002 example repeats the last reading when no new frame
  arrives. The ESP-IDF driver here doesn't; see `firmware/testing/c4002/README.md`.
- Flash over UART, not JTAG.

## How the user likes to work

- The user is learning ESP-IDF: explain options in plain terms and give
  step-by-step guidance.
- Don't change code without asking; explain the options first.
- Bench-test each part with its `firmware/testing/` project before the full
  system.
- Small PRs; never merge without the user.
