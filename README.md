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
