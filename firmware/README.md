# Firmware

Every numbered folder in here is a standalone ESP-IDF project, numbered after
the [`TODO.md`](../TODO.md) section it starts from. They share code through
[`components/`](components): each project's `CMakeLists.txt` adds
`../components` to `EXTRA_COMPONENT_DIRS` and builds only the components its
`main` requires.

| Project | Role |
|---|---|
| **05_espnow_c6_sender** | complete C6 sensor-node firmware |
| **29_end_to_end** | complete S3 controller firmware (with diagnostic console and critical-failure monitor) |
| 28_state_machine_tests | unit tests, on the PC (linux target) or the board |
| 02_c4002_integration, 03_mlx90640_integration | C6 sensor bring-up: one sensor at a time |
| 06_s3_controller_base, 13_buzzer, 14_relay_shutdown, 15_rtc_time, 17_s3_network | S3 hardware bring-up: one board part at a time |

Build any project the usual way:

```bash
cd firmware/<NN_project>
idf.py build            # the target comes from sdkconfig.defaults
idf.py -p <PORT> flash monitor
idf.py menuconfig       # "RB4107 configuration" menu holds all tunables
```

See the root [`README.md`](../README.md) for the section → folder map.

## Managed dependencies and offline builds

Two components use packages from the Espressif component registry, which
`idf.py` downloads on the first build. The component manager reads the
manifests of every component in `firmware/components`, so any project may
download them once, even if it doesn't compile them:

| Component | Registry package | Why |
|---|---|---|
| `rb_net` | `espressif/w5500` | W5500 Ethernet driver (moved out of ESP-IDF in v6.0) |
| `rb_mqtt` | `espressif/mqtt` | ESP-MQTT client (moved out of ESP-IDF in v6.0) |

Without registry access, clone the sources
([esp-eth-drivers](https://github.com/espressif/esp-eth-drivers) `w5500` +
`wiznet_common`, and [esp-mqtt](https://github.com/espressif/esp-mqtt) as
`mqtt`) into one folder and build with the component manager turned off:

```bash
IDF_COMPONENT_MANAGER=0 idf.py -DEXTRA_COMPONENT_DIRS=/path/to/that/folder build
```

(In that mode `w5500`'s `CMakeLists.txt` must `REQUIRES wiznet_common`, which
the registry version gets from its manifest.)
