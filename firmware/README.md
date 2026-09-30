# Firmware

```text
firmware/
├── presence_node/    SYSTEM FIRMWARE  ESP32-C6 + C4002 radar (flash on both radar boards)
├── thermal_node/     SYSTEM FIRMWARE  ESP32-C6 + MLX90640
├── controller/       SYSTEM FIRMWARE  ESP32-S3 controller
├── valve_node/       SYSTEM FIRMWARE  any ESP32 + servo gas valve (optional, wired or wireless)
├── components/       shared code used by all of the above (drivers, protocol, safety logic, ...)
└── testing/          test-only projects, never part of the running system
    ├── c4002/        C6: radar on its own
    ├── mlx90640/     C6: thermal camera on its own
    ├── s3_board/     S3: board bring-up, prints its MAC
    ├── buzzer/       S3: buzzer on its own
    ├── relay/        S3: shutdown relay on its own (and the wired valve line)
    ├── servo/        valve node: servo on its own
    ├── valve_link/   S3: wireless valve commands, without the safety system
    ├── rtc/          S3: real-time clock on its own
    ├── network/      S3: Ethernet / Wi-Fi on its own
    └── unit_tests/   safety logic tests, on the PC or the S3
```

## System firmware: one per board

Each board runs exactly one firmware. There are three kinds of board, so
three system firmwares:

| Board | Flash | Node ID |
|---|---|---|
| ESP32-C6 + C4002, presence node A | **[`presence_node`](presence_node)** | 1 |
| ESP32-C6 + C4002, presence node B | **[`presence_node`](presence_node)**, built with `sdkconfig.node_b` | 2 |
| ESP32-C6 + MLX90640, thermal node | **[`thermal_node`](thermal_node)** | 3 |
| ESP32-S3 controller | **[`controller`](controller)** (includes the diagnostic console and critical-failure monitor) | – |
| Any ESP32 + servo, gas valve node (optional) | **[`valve_node`](valve_node)**, wired, or wireless with `sdkconfig.wireless` | 4 |

Setup order: [`controller/README.md`](controller/README.md#setup).

"One firmware" is one `idf.py flash`: ESP-IDF writes the bootloader, the
partition table and the application together, so you never pick files by
hand.

## Test-only projects (`testing/`)

These exist to check one part at a time, on the bench, before the full
firmware runs: is the radar wired correctly, does the relay click, does the
RTC keep time, and so on. When something misbehaves in the full system, they
also let you isolate the part. Each one is a complete, separate firmware, so
flashing one **replaces** the system firmware on that board; flash
`presence_node`, `thermal_node` or `controller` back when you're done.

| Project | Board | Checks |
|---|---|---|
| [`testing/c4002`](testing/c4002) | C6 | C4002 UART wiring, readings, false detections |
| [`testing/mlx90640`](testing/mlx90640) | C6 | MLX90640 I2C wiring, frames, temperatures |
| [`testing/s3_board`](testing/s3_board) | S3 | board boots, prints the MAC the C6 nodes need |
| [`testing/buzzer`](testing/buzzer) | S3 | buzzer pin and patterns |
| [`testing/relay`](testing/relay) | S3 | relay polarity, boot state, shutdown |
| [`testing/servo`](testing/servo) | valve node | servo pin, open/closed angles |
| [`testing/valve_link`](testing/valve_link) | S3 | wireless valve node: keep-open, CLOSE, lost link |
| [`testing/rtc`](testing/rtc) | S3 | RTC keeps time across power cycles |
| [`testing/network`](testing/network) | S3 | Ethernet/Wi-Fi, IP address, reaching the MacBook |
| [`testing/unit_tests`](testing/unit_tests) | PC or S3 | safety logic, protocol, JSON (no hardware needed) |

## Building

Any project, the usual way:

```bash
cd firmware/controller            # or presence_node, testing/relay, ...
idf.py build                      # the target (C6 or S3) comes from sdkconfig.defaults
idf.py -p <PORT> flash monitor
idf.py menuconfig                 # "RB4107 configuration" menu holds all tunables
```

Every project adds `firmware/components` to `EXTRA_COMPONENT_DIRS` and builds
only the components its `main` needs. Pins and other settings can also be
set in a file instead of menuconfig; see
[`docs/configuration.md`](../docs/configuration.md).

See the root [`README.md`](../README.md) for the TODO section → folder map.

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
