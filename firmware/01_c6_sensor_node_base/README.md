# 01 – ESP32-C6 sensor node base project

TODO section 1.1. Target: **DFRobot FireBeetle 2 ESP32-C6**.

This project checks the toolchain and board before any sensor code is added.

| Check | How |
|---|---|
| Build | `idf.py build` |
| Flash | `idf.py -p <PORT> flash` |
| Serial logging | `idf.py -p <PORT> monitor`: logs go over the native USB-Serial/JTAG |
| Stable after assembly | watch the reset reason at boot and the periodic heap line. A `brownout`/`watchdog` reset points to wiring or power problems. |
| Central pin/config definitions | `idf.py menuconfig` → *RB4107 configuration* → *Sensor node (ESP32-C6)* |

The boot log prints the node's Wi-Fi MAC address. You will need it later when
setting up ESP-NOW (sections 5 and 7).
