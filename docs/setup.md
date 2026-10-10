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
| ESP32-C6 + C4002, presence node B | `firmware/presence_node` with `sdkconfig.node_b`, or the same build with `RB_NODE_ID_BY_MAC` | 2 |
| ESP32-C6 + MLX90640, thermal node | `firmware/thermal_node` | 3 |
| ESP32-S3 controller | `firmware/controller` | – |

```bash
. $IDF_PATH/export.sh
cd firmware/<project>
idf.py build                      # the chip (C6 or S3) comes from sdkconfig.defaults
idf.py -p <PORT> flash monitor
```

Both presence nodes can run the same build: list their MACs in
`CONFIG_RB_NODE_ID_BY_MAC` in `firmware/presence_node/sdkconfig.defaults`
(e.g. `"AA:BB:CC:DD:EE:01=1, AA:BB:CC:DD:EE:02=2"`; each board prints its MAC
at boot) and each board picks its node ID from its own MAC. Without that list,
presence node B uses its own build folder so it never shares a config with A:

```bash
idf.py -B build_b -D SDKCONFIG=build_b/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node_b" -p <PORT> flash monitor
```

**No configuration is needed.** The defaults are this project's hardware:
pins, relay polarity, buzzer, radar settings, node IDs, the S3's MAC on every
node (`44:B1:76:A9:BB:6C`) and the MacBook as the broker
(`AntoniodeMacBook-Air.local`, then `192.168.1.50`). Build and flash each
board in any order; at boot the controller logs `ONLINE` for each node as it
hears it.

If a project was built before these defaults changed, delete its
`sdkconfig` (and `build_b/sdkconfig` for node B) first: an existing
`sdkconfig` keeps its old values. Change a default only when the hardware
changes, e.g. `RB_NODE_CONTROLLER_MAC` in the nodes' `sdkconfig.defaults` if
the S3 board is replaced.

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

The controller and laptop must be on the same network. By default
(`RB_NET_TYPE`) the controller uses Ethernet, and ESP-NOW stays on
`RB_ESPNOW_CHANNEL`. Set `RB_WIFI_SSID` and `RB_WIFI_PASSWORD` as well, and it
switches to Wi-Fi after Ethernet has had no IP address for
`RB_NET_FALLBACK_S`, then back to Ethernet as soon as it has one. The broker
must be reachable from both networks.

With Wi-Fi, the router sets the channel, and ESP-NOW on the controller moves
with it (the controller logs `ESP-NOW channel 1 -> 6` and reports
`espnow_channel` in telemetry). The nodes follow on their own
(`RB_ESPNOW_FOLLOW_CHANNEL`): after a few unacknowledged packets a node probes
channels 1–13, stays on the one where the controller answers (log
`controller found on channel 6`) and remembers it for the next boot. This
takes about a second, also when the router changes channel later. It needs
`RB_NODE_CONTROLLER_MAC` set on every node; a node sending to broadcast stays
on `RB_ESPNOW_CHANNEL`, which must then match the router.

## Backend and dashboard

First time only:

```bash
cd django
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

Then, from the repository root:

```bash
tools/run_dev.sh
```

It starts Mosquitto, the MQTT subscriber and the Django server together and
opens the dashboard at http://127.0.0.1:8000/. Ctrl-C stops all three. From
another terminal, or after closing the one it ran in, `tools/stop_dev.sh` does
the same and also cleans up anything left over (for example "port 8000 already
in use" after a crash). It only touches this project's processes.
Stopping erases the recorded data (`django/db.sqlite3`, `demo.sqlite3`), so
every run starts with an empty dashboard. With `--demo` the made-up stalls come
back within seconds, since they are generated, not stored.
Options: `RB4107_SKIP_BROKER=1` if a broker is already running,
`RB4107_NO_BROWSER=1` to not open the browser.

For a demonstration, `tools/run_dev.sh --demo` adds simulated stalls next to
the real one: the cases a single bench stall can't show at once (several
incidents across sites, a stall that has cut its supply and is cooling down,
two stations in one stall, a radar node unplugged, a stall losing its
network). They come from `django/locations.demo.json` (`demo_scenario` per
stall), are tagged SIMULATED on the dashboard, and are kept in their own
`django/demo.sqlite3`, so they never mix with real data. The real controller
works as usual alongside them, Reset shutdown included. Without hardware the
demo still runs, just without the real stall.

Each controller publishes under its own `rb4107/<controller_id>` tree
(`RB_MQTT_TOPIC_PREFIX`, `rb4107/controller_01` for the bench) and Django
subscribes to `rb4107/+/#` (`RB4107_MQTT_TOPIC`), so real and simulated
controllers share the broker without overwriting each other.

Other machines on the same network open `http://192.168.1.50:8000/`. The
lab router gives the MacBook that fixed IP, and it is also in
`DJANGO_ALLOWED_HOSTS`. The controller finds the broker by the MacBook's mDNS
name (`RB_BROKER_HOST="AntoniodeMacBook-Air.local"` in
`firmware/controller/sdkconfig.defaults`), which works on any network, and
alternates with that IP (`RB_BROKER_FALLBACK_HOST`) after each failed attempt;
`mqtt` on the controller console shows which one is in use. The server has no login: anyone on the network can use the
dashboard, including **Reset shutdown** and C4002 tuning. That is fine for this
demo on a controlled network. On a shared network, start it with
`RB4107_HTTP_ADDR=127.0.0.1:8000` and use an SSH tunnel instead. More:
[django/README.md](../django/README.md).

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
