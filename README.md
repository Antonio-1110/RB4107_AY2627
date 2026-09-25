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

## Toolchain

- ESP-IDF **v6.1** (matches `legacy/main_controller/dependencies.lock`).
- Some components pull managed dependencies from the Espressif component
  registry (`espressif/mqtt`, `espressif/w5500`) the first time you build.

## Build / flash / test

```bash
. $IDF_PATH/export.sh
cd firmware/01_c6_sensor_node_base
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

All tunables (pins, timeouts, thresholds, broker address, node IDs, ...) are in
`idf.py menuconfig` → **RB4107 configuration**. Values that have not been
confirmed on real hardware are labelled `UNCONFIRMED` in their help text.
