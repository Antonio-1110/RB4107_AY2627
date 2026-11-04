# Glossary

| Term | Meaning |
|---|---|
| **Stall** | One cooking station. A stall has one or more controllers |
| **Controller** | The ESP32-S3 that runs the safety logic, drives the buzzer and the shutdown output, and publishes over MQTT |
| **Presence node** | ESP32-C6 with a C4002 radar. Two are used (node IDs 1 and 2) |
| **Thermal node** | ESP32-C6 with an MLX90640 thermal camera (node ID 3) |
| **ESP-NOW** | Wi-Fi-based radio link from the nodes to the controller. No router needed ([protocol.md](protocol.md)) |
| **C4002** | The mmWave radar module. Reports none, moving or stationary targets with a distance |
| **MLX90640** | 32×24 thermal camera. The hottest pixel is the stove temperature |
| **Cooking** | Judged by the controller: the hottest pixel reaches the cooking temperature, and heat is still going in rather than the pan cooling |
| **Presence** | PRESENT, ABSENT or UNKNOWN, combined from both radars. UNKNOWN is never treated as PRESENT |
| **Absence / return debounce** | How long someone must be gone before the countdown starts, and back before it stops. Stops radar noise from starting or cancelling timers |
| **IDLE** | Nothing cooking |
| **MONITORING** | Cooking, and a person is there |
| **UNATTENDED** | Cooking and nobody there: the countdown runs |
| **WARNING** | Countdown reached the warning time: buzzer on |
| **SHUTDOWN** | Countdown reached the shutdown time: output 1 drops, appliance off. Latched until an operator reset |
| **FAULT** | The controller's own failure (self-test or output read-back). A lost sensor is a flag, not a FAULT |
| **Tier** | The temperature band that sets the warning and shutdown times. Hotter means shorter |
| **Hold** | A due buzzer or shut-off that waits ([safety_state_machine.md](safety_state_machine.md#holds-a-due-buzzer-or-shutdown-that-waits)). Three kinds: **cooling** (sure no heat is going in; long), **falling** (temperature falling, not sure yet whether it is cooling or a lowered flame; at most 90 s) and **return** (a person is being confirmed; a few seconds). Before a countdown starts, **checking** waits while the pan may have been switched off (at most 90 s, shared with falling; the time away still counts). The Countdown card names the kind |
| **Pause** | The countdown clock standing still while someone is at the stove (a passer-by). Limited by a budget per countdown |
| **Flag** | A sensor-problem marker (*presence unknown*, *thermal lost*). Shown on the dashboard; never turns into a fault |
| **Latched** | Stays in force until someone resets it |
| **Fail-safe** | The output's resting state is "appliance off": a power loss or wire break shuts it down |
| **Shutdown output / relay** | Output 1 of the controller, driving an external relay coil |
| **Incident** | One unattended-cooking episode, from the first UNATTENDED to its end. All are stored; the Stall history tab counts only those that reached the buzzer warning, because a countdown that ends before it is the time the cook is allowed to be away. One during which the pan was losing heat (a cooling or falling hold) is **unclear**: listed, not counted, except at tier 5 |
| **Telemetry** | The controller's JSON snapshots published over MQTT about once a second |
| **MQTT / broker** | The messaging layer between controller and backend. Mosquitto runs on the laptop |
| **Schema version** | Version of the MQTT JSON format (2). The ESP-NOW **protocol version** (3) is separate |
| **Kconfig / menuconfig** | ESP-IDF's build-time settings. Every tunable is in `firmware/components/rb_config/Kconfig` |
| **`rb_params.def`** | The list of safety settings the dashboard can change at run time |
| **Developer mode** | The dashboard's tuning tools (Safety, Radar, Thermal, Diagnostics, Logs), switched on with a password that is not a secret. Without it the page is watch-only |
