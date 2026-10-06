# Valve node: servo gas valve (system firmware, optional)

A hobby servo turns a quarter-turn gas ball valve. In the demo it stands in
for a motorised gas valve. The controller decides when to shut down, and this
node only moves the valve. It runs on **any ESP32 chip** (ESP32, S3, C3 or
C6; tested here as builds only).

There are two versions, and both **fail closed**. The valve starts closed
and opens only while the controller keeps saying it is safe.

| | Wired | Wireless |
|---|---|---|
| Signal | controller output 2 pulls an input pin low | ESP-NOW `VALVE_COMMAND` about once a second |
| Opens when | the input has been low for 0.5 s | a `KEEP_OPEN` arrives |
| Closes when | the input is high for 50 ms (output off, wire cut, controller unpowered) | `CLOSE` arrives, or no `KEEP_OPEN` for 3 s (radio lost, controller crashed or unpowered) |
| Reports back | nothing | `VALVE_STATUS` (position, reason, whether it's still moving) |
| Controller setting | `RB_SHUTDOWN_VALVE_CHANNEL = 2` | `RB_CTRL_VALVE_WIRELESS = y` and the node's MAC |

Settings live in menuconfig under **RB4107 configuration → Valve node (servo
gas valve)**. The controller settings are under **Controller board → Gas
valve node**.

**Caveat for the report:** a hobby servo has no position feedback and stays
where it is if its own power fails. "Closed" means "commanded closed". A real
installation would use a spring-return valve, which closes by itself without
power.

## Default pins

The defaults avoid strapping, flash and USB pins on each chip. Change them
in menuconfig if your board doesn't have them.

| Chip | Servo signal | Wired input |
|---|---|---|
| ESP32 | GPIO 18 | GPIO 19 |
| ESP32-S3 | GPIO 4 | GPIO 5 |
| ESP32-C3 | GPIO 4 | GPIO 5 |
| ESP32-C6 (e.g. DFR1117) | GPIO 6 | GPIO 7 |

## Wiring

### Servo (both versions)

| Servo wire | Goes to |
|---|---|
| brown/black (GND) | servo supply − **and** an ESP32 GND pin |
| red (+) | a separate 5 V supply (≥1 A for an SG90, ≥2 A for an MG996R) |
| orange/yellow (signal) | the servo GPIO |

Don't power the servo from the ESP32's 3.3 V pin: its start-up current resets
the board. The grounds must be joined, or the signal has no reference.

### Wired version: controller output 2 → valve node

The controller outputs are sinking transistors: when an output is ON, its
terminal is pulled to GND. The valve node's internal pull-up holds the input
at 3.3 V otherwise.

```text
Controller (S3-POE-ETH-8DI-8DO)        Valve node ESP32
  output terminal 2  ───────────────── input GPIO (pull-up on)
  GND                ───────────────── GND
```

- Nothing else is needed: no resistor, and no connection to COM.
- Output ON (safe) → input LOW → valve opens.
- Output OFF (shutdown, boot, controller unpowered) or a cut wire → input
  HIGH → valve closes.
- Only the pull-up's current flows (well under 1 mA), so the output's supply
  voltage doesn't matter and the ESP32 pin only ever sees 0 V or 3.3 V.

### Wireless version

Only the servo and a power supply. The node must use the same ESP-NOW
channel as the controller (`RB_ESPNOW_CHANNEL`, default 1).

## Flashing

Pick the chip once with `set-target`. For a C6 that is `esp32c6`; the others
are `esp32`, `esp32s3` and `esp32c3`.

### Step 1: check the servo on its own

Use [`testing/servo`](../testing/servo) first. It swings between the closed
and open angles every 3 s, so you can set the angles before the valve is
attached.

### Step 2a: wired version

1. Controller: in `firmware/controller` (or `firmware/testing/relay` for a
   bench test), run `idf.py menuconfig` and set **Controller board → Gas
   valve node → Wired valve line: output channel** to `2`. Flash it.
2. Valve node:
   ```bash
   cd firmware/valve_node
   idf.py set-target esp32c6          # your chip
   idf.py -p <PORT> flash monitor
   ```
3. Check: the log shows `input LOW` then `valve OPEN` while the controller is
   safe. Pull the wire out: `input HIGH`, then `valve CLOSED`.

### Step 2b: wireless version

The wireless version is the same firmware built with `sdkconfig.wireless` on
top. Use its own build folder, so the two versions never share a
configuration:

```bash
cd firmware/valve_node
idf.py -B build_wireless -D SDKCONFIG=build_wireless/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wireless" set-target esp32c6   # your chip
idf.py -B build_wireless -D SDKCONFIG=build_wireless/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wireless" menuconfig          # optional, see below
idf.py -B build_wireless -D SDKCONFIG=build_wireless/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wireless" -p <PORT> flash monitor
```

Note the `my MAC` line it prints at boot.

Then on the controller (`firmware/controller`, or
[`testing/valve_link`](../testing/valve_link) for a bench test without
sensors), in **Controller board → Gas valve node**:

1. Turn on **Wireless valve node**.
2. Set **Valve node Wi-Fi STA MAC address** to the MAC the valve node
   printed.
3. Leave **Valve node ID** at 4 (the valve node's `RB_NODE_ID`).
4. Flash it.

Check: the valve node logs `valve OPEN` once keep-opens arrive, and the S3
logs `valve node_04 reports OPEN`. Unplug the S3, and about 3 s later the
node logs `valve CLOSED: no keep-open from the controller`.

`RB_NODE_CONTROLLER_MAC` left at broadcast (`FF:FF:FF:FF:FF:FF`) makes the
node accept commands from any controller. That is fine on the bench. Set it
to the S3's MAC for the demo, so a stray board can't open the valve.

## Other settings

| Option | Default | Meaning |
|---|---|---|
| `RB_VALVE_OPEN_ANGLE_DEG` / `RB_VALVE_CLOSED_ANGLE_DEG` | 90 / 0 | Servo angles for the two positions |
| `RB_VALVE_SERVO_MIN/MAX_PULSE_US` | 500 / 2500 | Pulse widths at 0° and full travel. Narrow them if the servo buzzes at an end. |
| `RB_VALVE_WIRED_OPEN_DELAY_MS` | 500 | How long the input must stay low before opening |
| `RB_VALVE_LINK_TIMEOUT_MS` | 3000 | Wireless: close after this long without a keep-open |
| `RB_VALVE_LATCH_CLOSED` | off | On: once closed, only a reset of the valve node reopens it |
