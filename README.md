# RB4107 Cooking Safety System

Software for the RB4107 cooking-safety prototype. The work plan is [`TODO.md`](TODO.md).

```text
C4002 #1 → ESP32-C6 presence node A ─┐
C4002 #2 → ESP32-C6 presence node B ─┼─ ESP-NOW → ESP32-S3 controller
MLX90640 → ESP32-C6 thermal node    ─┘            ├── local safety state machine → buzzer, shutdown relay
                                                  └── Ethernet / Wi-Fi → Mosquitto (MacBook) → Django subscriber
```

The local safety path on the ESP32-S3 must keep working when MQTT, Django, the
MacBook or the network is down.

## Repository layout

```text
RB4107_AY2627/
├── firmware/                 ESP-IDF projects, numbered after the TODO section they start from
│   ├── components/           shared ESP-IDF components (drivers, protocol, safety logic, ...)
│   └── NN_<name>/            standalone ESP-IDF project
├── backend/django/           Django MQTT ingestion (TODO sections 23–24)
├── tools/
│   ├── mqtt/                 Mosquitto config and scripts (TODO section 18)
│   └── diagnostics/          host-side helper scripts
├── docs/                     protocol, topics, schema, configuration, test procedures
├── legacy/                   earlier prototypes, kept for reference only
└── TODO.md
```

## What to flash to run the system

The working system is **three firmwares on four boards**, plus the broker and
Django on the MacBook:

| Board | Flash this project | What it is |
|---|---|---|
| ESP32-C6 + C4002 radar, ×2 | [`firmware/05a_c6_presence_node`](firmware/05a_c6_presence_node) | **Presence node firmware**: reads one radar and sends it to the S3 over ESP-NOW. Same firmware on both boards, node ID 1 and 2 (see its README) |
| ESP32-C6 + MLX90640 | [`firmware/05b_c6_thermal_node`](firmware/05b_c6_thermal_node) | **Thermal node firmware**: reads the thermal camera, extracts features and sends them over ESP-NOW. Node ID 3 |
| ESP32-S3 (Waveshare ETH-8DI-8RO) | [`firmware/29_end_to_end`](firmware/29_end_to_end) | **Controller firmware**: combines the two radars, runs the safety state machine, buzzer, shutdown relay, RTC, Ethernet, MQTT, diagnostic console |
| MacBook | [`tools/mqtt`](tools/mqtt) + [`backend/django`](backend/django) | Mosquitto broker and Django subscriber (not ESP32 projects) |

Setup steps (broker, MAC address, channel):
[`firmware/29_end_to_end/README.md`](firmware/29_end_to_end/README.md#setup).

**Every other project is for testing only and is not part of the running
system.** Flashing one of them replaces the system firmware on that board;
flash 05a / 05b / 29 back afterwards.

| Project | Purpose |
|---|---|
| 02, 03 (C6) and 06, 13, 14, 15, 17 (S3) | hardware bring-up: test one sensor or board part on its own, to check the wiring and readings before running the full firmware, or to isolate a part that misbehaves |
| 28 | unit tests of the safety logic, protocol and JSON, on a PC or on the S3 |

The rest of each TODO section lives in shared components and docs (table
below). The broker and Django don't run on an ESP32 and live in `tools/` and
`backend/`.

## Section → folder map

| TODO section | Where | Target |
|---|---|---|
| 0 Repository structure | this layout | – |
| 1 ESP32-C6 base project | `firmware/components/rb_node_app` (boot info, MAC, health log) in 05a and 05b; sensor bring-up in 02/03 | ESP32-C6 |
| 2 C4002 integration | [`firmware/02_c4002_integration`](firmware/02_c4002_integration) | ESP32-C6 |
| 3 MLX90640 integration + thermal features | [`firmware/03_mlx90640_integration`](firmware/03_mlx90640_integration) | ESP32-C6 |
| 4 Shared ESP-NOW protocol | `firmware/components/rb_protocol`, spec in [`docs/protocol.md`](docs/protocol.md), tests in 28 | C6 + S3 |
| 5 ESP-NOW C6 sender (**system firmware: flash on the C6 boards**) | [`firmware/05a_c6_presence_node`](firmware/05a_c6_presence_node) (×2) and [`firmware/05b_c6_thermal_node`](firmware/05b_c6_thermal_node) | ESP32-C6 |
| 6 ESP32-S3 controller base | [`firmware/06_s3_controller_base`](firmware/06_s3_controller_base) | ESP32-S3 |
| 7 ESP-NOW S3 receiver | [`firmware/29_end_to_end`](firmware/29_end_to_end), described in [`docs/architecture.md`](docs/architecture.md), tests in 28 | ESP32-S3 |
| 8 Sensor node health monitoring | same as 7 | ESP32-S3 |
| 9 Safety state machine | `firmware/components/safety`, design in [`docs/safety_state_machine.md`](docs/safety_state_machine.md), tests in 28 | ESP32-S3 |
| 10 Initial safety behaviour | same as 9 (menuconfig *Safety logic*) | ESP32-S3 |
| 11 Non-blocking timing | same as 9; [`docs/architecture.md`](docs/architecture.md) | ESP32-S3 |
| 12 FreeRTOS architecture | `firmware/components/rb_controller`, [`docs/architecture.md`](docs/architecture.md) | ESP32-S3 |
| 13 Buzzer driver | [`firmware/13_buzzer`](firmware/13_buzzer) | ESP32-S3 |
| 14 Relay / shutdown driver | [`firmware/14_relay_shutdown`](firmware/14_relay_shutdown) | ESP32-S3 |
| 15 RTC / time | [`firmware/15_rtc_time`](firmware/15_rtc_time) | ESP32-S3 |
| 16 Fault manager | `firmware/components/fault_manager`, [`docs/architecture.md`](docs/architecture.md) | ESP32-S3 |
| 17 S3 network (Ethernet / Wi-Fi) | [`firmware/17_s3_network`](firmware/17_s3_network) | ESP32-S3 |
| 18 MacBook MQTT broker | [`tools/mqtt`](tools/mqtt) (not an ESP32 project) | MacBook |
| 19 MQTT client on the S3 | `firmware/components/rb_mqtt` in 29, [`docs/architecture.md`](docs/architecture.md) | ESP32-S3 |
| 20 MQTT topic structure | `firmware/components/rb_topics`, [`docs/mqtt_topics.md`](docs/mqtt_topics.md) | ESP32-S3 |
| 21 MQTT JSON schema | `firmware/components/rb_json`, [`docs/mqtt_schema.md`](docs/mqtt_schema.md), tests in 28 | ESP32-S3 |
| 22 MQTT publishing strategy | `firmware/components/rb_telemetry` in 29, [`docs/architecture.md`](docs/architecture.md) | ESP32-S3 |
| 23 Django project (MQTT ingestion) | [`backend/django`](backend/django) (not an ESP32 project) | MacBook |
| 24 Persistent Django MQTT subscriber | [`backend/django`](backend/django) → `python manage.py mqtt_subscriber` | MacBook |
| 25 Logging | cross-cutting: [`docs/logging.md`](docs/logging.md), `firmware/components/rb_log` | all |
| 26 Configuration | cross-cutting: [`docs/configuration.md`](docs/configuration.md), `firmware/components/rb_config/Kconfig` | all |
| 27 Diagnostic mode | [`firmware/29_end_to_end`](firmware/29_end_to_end) (console, `sdkconfig.qemu`) | ESP32-S3 |
| 28 State machine testing | [`firmware/28_state_machine_tests`](firmware/28_state_machine_tests), `tools/run_host_tests.sh` | ESP32-S3 + host (linux target) |
| 29 End-to-end integration (**system firmware: flash on the S3**) | [`firmware/29_end_to_end`](firmware/29_end_to_end) (+ 05 on the C6), `tools/diagnostics/e2e_check.py` | ESP32-S3 + ESP32-C6 |
| 30 Critical failure test | [`firmware/29_end_to_end`](firmware/29_end_to_end) (continuity monitor), `tools/diagnostics/critical_failure_test.py` | ESP32-S3 + MacBook |
| 31 Development order | milestone checkboxes in [`TODO.md`](TODO.md) | – |
| 32 Open questions | [`docs/open_questions.md`](docs/open_questions.md): each one is a menuconfig option or a bench check | – |
| 33 Engineering rules | [`docs/engineering_rules.md`](docs/engineering_rules.md): where each rule is enforced | – |

## Toolchain

- ESP-IDF **v6.1** (matches `legacy/main_controller/dependencies.lock`).
- `rb_net` and `rb_mqtt` pull `espressif/w5500` and `espressif/mqtt` from the
  Espressif component registry on the first build. The component manager reads
  every manifest in `firmware/components`, so any project may download them
  once. Offline builds: see [`firmware/README.md`](firmware/README.md).
- Backend: Python 3.11+, `backend/django/requirements.txt`. Broker: Mosquitto 2.x.

## Build / flash / test

```bash
. $IDF_PATH/export.sh
cd firmware/<NN_project>
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
idf.py menuconfig          # RB4107 configuration: pins, timeouts, thresholds, broker, node IDs
```

Values not yet confirmed on hardware are labelled `UNCONFIRMED` /
`OPEN QUESTION` / `UNVERIFIED` in menuconfig. They are listed in
[`docs/configuration.md`](docs/configuration.md) and
[`docs/open_questions.md`](docs/open_questions.md).

| What | Command |
|---|---|
| Unit tests on the PC (firmware + Django) | `tools/run_host_tests.sh` |
| Broker on the MacBook | `tools/mqtt/start_broker.sh` |
| Django subscriber | `cd backend/django && python manage.py mqtt_subscriber` |
| Watch / validate MQTT traffic | `tools/mqtt/watch.sh`, `tools/diagnostics/validate_json.py` |
| End-to-end checklist | `tools/diagnostics/e2e_check.py` (project 29) |
| Critical failure test | `tools/diagnostics/critical_failure_test.py` (project 29) |
| Try the controller without hardware | project 29 in QEMU with `sdkconfig.qemu`, see [`firmware/29_end_to_end/README.md`](firmware/29_end_to_end/README.md) |

## Verification status

What has been checked without hardware, and how:

| Checked | How |
|---|---|
| All 11 ESP-IDF projects build (ESP-IDF v6.1, C6 and S3) | clean builds, no warnings |
| Safety logic, node health, combining the two radars, faults, protocol, C4002 parser, thermal features, JSON | 57 Unity tests pass on the host (linux target) and on the ESP32-S3 in QEMU (project 28) |
| Real-time behaviour, MQTT client, publishing, diagnostic console, continuity monitor | controller firmware run in QEMU with emulated Ethernet against Mosquitto, fed by three simulated nodes |
| Losing one radar or the thermal node | QEMU: the node's SAFETY fault is raised and the controller goes to FAULT, and recovers when the node returns |
| JSON payloads | every captured message validated against `docs/schema/rb4107_mqtt.schema.json` |
| Django ingestion | 23 tests (fixtures are real firmware output), plus manual runs through broker outages and `SIGTERM` |
| Critical failure path | QEMU rehearsal: WARNING and SHUTDOWN with the broker down, continuity monitor verdict PASS |

Still to be done on the bench (the unchecked boxes in `TODO.md`): anything
involving the real sensors, the ESP-NOW radio link, the buzzer, the relay,
the RTC battery, the W5500 and the MacBook's network. The pin maps come from
the legacy code or board silkscreens and need confirming.
