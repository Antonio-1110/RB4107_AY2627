# RB4107 Cooking Safety System

Software for the RB4107 cooking-safety prototype. The work plan is [`TODO.md`](TODO.md).

```text
C4002 + MLX90640 → ESP32-C6 sensor node → ESP-NOW → ESP32-S3 controller
                                                     ├── local safety state machine → buzzer, shutdown relay
                                                     └── Ethernet / Wi-Fi → Mosquitto (MacBook) → Django subscriber
```

The local safety path on the ESP32-S3 must keep working when MQTT, Django, the
MacBook or the network is down.

## Repository layout

```text
RB4107_AY2627/
├── firmware/                 ESP-IDF projects, one folder per TODO section
│   ├── components/           shared ESP-IDF components (drivers, protocol, safety logic, ...)
│   └── NN_<section>/         standalone ESP-IDF project for TODO section NN
├── backend/django/           Django MQTT ingestion (TODO sections 23–24)
├── tools/
│   ├── mqtt/                 Mosquitto config and scripts (TODO section 18)
│   └── diagnostics/          host-side helper scripts
├── docs/                     protocol, topics, schema, configuration, test procedures
├── legacy/                   earlier prototypes, kept for reference only
└── TODO.md
```

Sections that run on an ESP32 each get their own ESP-IDF project under
`firmware/`. Sections that don't run on an ESP32 (the broker and Django) live in
`tools/` and `backend/`. Rules and cross-cutting sections have no project of
their own; the table says where they are applied.

## Section → folder map

| TODO section | Where | Target |
|---|---|---|
| 0 Repository structure | this layout | – |
| 1 ESP32-C6 sensor node base | [`firmware/01_c6_sensor_node_base`](firmware/01_c6_sensor_node_base) | ESP32-C6 |
| 2 C4002 integration | [`firmware/02_c4002_integration`](firmware/02_c4002_integration) | ESP32-C6 |
| 3 MLX90640 integration + thermal features | [`firmware/03_mlx90640_integration`](firmware/03_mlx90640_integration) | ESP32-C6 |
| 4 Shared ESP-NOW protocol | [`firmware/04_espnow_protocol`](firmware/04_espnow_protocol), spec in [`docs/protocol.md`](docs/protocol.md) | ESP32-C6 or S3 |
| 5 ESP-NOW C6 sender (complete sensor node) | [`firmware/05_espnow_c6_sender`](firmware/05_espnow_c6_sender) | ESP32-C6 |
| 6 ESP32-S3 controller base | [`firmware/06_s3_controller_base`](firmware/06_s3_controller_base) | ESP32-S3 |
| 7 ESP-NOW S3 receiver | [`firmware/07_espnow_s3_receiver`](firmware/07_espnow_s3_receiver) | ESP32-S3 |
| 8 Sensor node health monitoring | [`firmware/08_sensor_health`](firmware/08_sensor_health) | ESP32-S3 |
| 9 Safety state machine | [`firmware/09_safety_state_machine`](firmware/09_safety_state_machine), design in [`docs/safety_state_machine.md`](docs/safety_state_machine.md) | ESP32-S3 |
| 10 Initial safety behaviour | [`firmware/10_safety_behaviour`](firmware/10_safety_behaviour) | ESP32-S3 |
| 11 Non-blocking timing | [`firmware/11_non_blocking_timing`](firmware/11_non_blocking_timing) | ESP32-S3 |
| 12 FreeRTOS architecture | [`firmware/12_freertos_architecture`](firmware/12_freertos_architecture) | ESP32-S3 |
| 13 Buzzer driver | [`firmware/13_buzzer`](firmware/13_buzzer) | ESP32-S3 |
| 14 Relay / shutdown driver | [`firmware/14_relay_shutdown`](firmware/14_relay_shutdown) | ESP32-S3 |
| 15 RTC / time | [`firmware/15_rtc_time`](firmware/15_rtc_time) | ESP32-S3 |
| 16 Fault manager | [`firmware/16_fault_manager`](firmware/16_fault_manager) | ESP32-S3 |
| 17 S3 network (Ethernet / Wi-Fi) | [`firmware/17_s3_network`](firmware/17_s3_network) | ESP32-S3 |
| 18 MacBook MQTT broker | [`tools/mqtt`](tools/mqtt) (not an ESP32 project) | MacBook |
| 19 MQTT client on the S3 | [`firmware/19_mqtt_client`](firmware/19_mqtt_client) | ESP32-S3 |
| 20 MQTT topic structure | [`firmware/20_mqtt_topics`](firmware/20_mqtt_topics), spec in [`docs/mqtt_topics.md`](docs/mqtt_topics.md) | ESP32-S3 |
| 21 MQTT JSON schema | [`firmware/21_mqtt_json`](firmware/21_mqtt_json), spec in [`docs/mqtt_schema.md`](docs/mqtt_schema.md) | ESP32-S3 |
| 22 MQTT publishing strategy | [`firmware/22_mqtt_publishing`](firmware/22_mqtt_publishing) | ESP32-S3 |
| 23 Django project (MQTT ingestion) | [`backend/django`](backend/django) (not an ESP32 project) | MacBook |
| 24 Persistent Django MQTT subscriber | [`backend/django`](backend/django) → `python manage.py mqtt_subscriber` | MacBook |
| 25 Logging | cross-cutting: [`docs/logging.md`](docs/logging.md), `firmware/components/rb_log` | all |
| 26 Configuration | cross-cutting: [`docs/configuration.md`](docs/configuration.md), `firmware/components/rb_config/Kconfig` | all |
| 27 Diagnostic mode | [`firmware/27_diagnostic_mode`](firmware/27_diagnostic_mode) | ESP32-S3 |
| 28 State machine testing | [`firmware/28_state_machine_tests`](firmware/28_state_machine_tests), `tools/run_host_tests.sh` | ESP32-S3 + host (linux target) |
| 29 End-to-end integration | [`firmware/29_end_to_end`](firmware/29_end_to_end) (+ 05 on the C6), `tools/diagnostics/e2e_check.py` | ESP32-S3 + ESP32-C6 |
| 30 Critical failure test | [`firmware/30_critical_failure_test`](firmware/30_critical_failure_test), `tools/diagnostics/critical_failure_test.py` | ESP32-S3 + MacBook |
| 31 Development order | milestone checkboxes in [`TODO.md`](TODO.md); the numbered folders follow this order | – |
| 32 Open questions | [`docs/open_questions.md`](docs/open_questions.md): each one is a menuconfig option or a bench check | – |
| 33 Engineering rules | [`docs/engineering_rules.md`](docs/engineering_rules.md): where each rule is enforced | – |

The two firmwares you normally flash are **05** (C6 sensor node) and **29**
(S3 controller). The other folders are the step-by-step bring-up and test
projects for each section.

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
| Critical failure test | `tools/diagnostics/critical_failure_test.py` (project 30) |
| Try the controller without hardware | project 27 or 29 in QEMU, see [`firmware/19_mqtt_client/README.md`](firmware/19_mqtt_client/README.md) |

## Verification status

What has been checked without hardware, and how:

| Checked | How |
|---|---|
| All 26 ESP-IDF projects build (ESP-IDF v6.1, C6 and S3) | clean builds, no warnings |
| Safety logic, node health, faults, protocol, C4002 parser, thermal features, JSON | 47 Unity tests pass on the host (linux target) and on the ESP32-S3 in QEMU (project 28) |
| Real-time behaviour, MQTT client, publishing, diagnostic console | firmware run in QEMU with emulated Ethernet against Mosquitto (11, 19–22, 27, 29, 30) |
| JSON payloads | every captured message validated against `docs/schema/rb4107_mqtt.schema.json` |
| Django ingestion | 22 tests (fixtures are real firmware output), plus manual runs through broker outages and `SIGTERM` |
| Critical failure path | QEMU rehearsal: WARNING and SHUTDOWN with the broker down, both verdicts PASS |

Still to be done on the bench (the unchecked boxes in `TODO.md`): anything
involving the real sensors, the ESP-NOW radio link, the buzzer, the relay,
the RTC battery, the W5500 and the MacBook's network. The pin maps come from
the legacy code or board silkscreens and need confirming.
