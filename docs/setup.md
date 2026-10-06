# Setup

How to build, flash and run the whole system. For what each part does, see
[README.md](README.md).

## What you need

- **ESP-IDF v6.1** for the firmware. The first build downloads
  `espressif/w5500` and `espressif/mqtt` from the component registry
  (offline builds: [firmware/README.md](../firmware/README.md#managed-dependencies-and-offline-builds)).
- **Python 3.11+** for Django and the tools.
- **Mosquitto 2.x** on the laptop that runs the broker (`brew install mosquitto`).

## Firmware

Each board gets one firmware:

| Board | Project | Node ID |
|---|---|---|
| ESP32-C6 + C4002, presence node A | `firmware/presence_node` | 1 |
| ESP32-C6 + C4002, presence node B | `firmware/presence_node` with `sdkconfig.node_b` | 2 |
| ESP32-C6 + MLX90640, thermal node | `firmware/thermal_node` | 3 |
| ESP32-S3 controller | `firmware/controller` | – |

```bash
. $IDF_PATH/export.sh
cd firmware/<project>
idf.py build                      # the chip (C6 or S3) comes from sdkconfig.defaults
idf.py -p <PORT> flash monitor
```

Presence node B uses its own build folder so it never shares a config with A:

```bash
idf.py -B build_b -D SDKCONFIG=build_b/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node_b" -p <PORT> flash monitor
```

Order:

1. **Controller:** in `idf.py menuconfig` → *RB4107 configuration* set the
   broker IP (`RB_BROKER_HOST`, see [Broker](#broker)) and the ESP-NOW channel
   (`RB_ESPNOW_CHANNEL`). Flash it and copy the MAC address it prints.
2. **Each C6 node:** set `RB_NODE_CONTROLLER_MAC` to that MAC and the same
   channel, then flash.
3. At boot the controller logs `ONLINE` for each node as it hears it.

**New to the hardware?** Check each part on its own first with the projects in
[`firmware/testing/`](../firmware/testing) (radar, thermal camera, buzzer,
relay, RTC, network). Confirm relay polarity and boot state **before**
connecting a real appliance. Flashing a test project replaces the system
firmware; flash the system firmware back afterwards.

## Changing settings

Every pin, timeout and threshold is in `idf.py menuconfig` → *RB4107
configuration*. All options: [configuration.md](configuration.md). Values
flagged `UNCONFIRMED` / `OPEN QUESTION` / `UNVERIFIED` still need checking
on hardware.

Settings can also go in a file, using the `CONFIG_` name:

- `<project>/sdkconfig.defaults` (committed): e.g. `CONFIG_RB_C4002_RX_GPIO=4`.
  Only applied when the project has no `sdkconfig` yet, so delete
  `<project>/sdkconfig` and rebuild after editing.
- `<project>/sdkconfig` (generated, only on your machine): edit and rebuild.

**Never commit `sdkconfig`.** It can hold the Wi-Fi password.

## Broker

On the laptop (details: [tools/mqtt/README.md](../tools/mqtt/README.md)):

```bash
tools/mqtt/start_broker.sh     # allow incoming connections if macOS asks
tools/mqtt/lan_ip.sh           # the IP to put in RB_BROKER_HOST on the controller
```

The controller and laptop must be on the same network. Ethernet is the
default (`RB_NET_TYPE`). With Wi-Fi, every board's `RB_ESPNOW_CHANNEL` must
match the router's channel.

## Backend and dashboard

First time only:

```bash
cd django
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
python manage.py migrate
```

Then, from the repository root:

```bash
tools/run_dev.sh
```

It starts Mosquitto, the MQTT subscriber and the Django server together and
opens the dashboard at http://127.0.0.1:8000/. Ctrl-C stops all three.
Options: `RB4107_SKIP_BROKER=1` if a broker is already running,
`RB4107_NO_BROWSER=1` to not open the browser.

No hardware? Set `RB4107_LOCATION_CATALOG_FILE=locations.demo.json` in
`django/.env` and run `python manage.py simulate_fleet --direct` (without the
subscriber) to fill the dashboard with made-up stalls.

The server has no login. Keep it on 127.0.0.1 and use an SSH tunnel to view
it from another computer. More: [django/README.md](../django/README.md).

## Tests

| What | Command |
|---|---|
| Everything that runs on a PC (firmware unit tests + Django tests) | `tools/run_host_tests.sh` (needs `libbsd-dev` on Linux) |
| Django only | `cd django && python manage.py test ingest` |
| Full MQTT → database → API path | `python tools/dashboard/smoke_test.py` (after `pip install -r django/requirements-test.txt`) |
| Controller without hardware | QEMU, see [firmware/controller/README.md](../firmware/controller/README.md#running-without-hardware-qemu) |
| End-to-end on hardware | `tools/diagnostics/e2e_check.py`, see [firmware/controller/README.md](../firmware/controller/README.md#end-to-end-test) |
| Safety keeps working with the broker down | `tools/diagnostics/critical_failure_test.py`, see [firmware/controller/README.md](../firmware/controller/README.md#critical-failure-test) |
| Watch MQTT traffic | `tools/mqtt/watch.sh`; validate captures with `tools/diagnostics/validate_json.py` |
