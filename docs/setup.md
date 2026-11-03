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

Both presence nodes can share one build: list their MACs in
`CONFIG_RB_NODE_ID_BY_MAC` in `firmware/presence_node/sdkconfig.defaults`
(`"AA:BB:CC:DD:EE:01=1, AA:BB:CC:DD:EE:02=2"`; each board prints its MAC at boot).
Without that list, node B needs its own build folder so it never shares a config
with A:

```bash
idf.py -B build_b -D SDKCONFIG=build_b/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node_b" -p <PORT> flash monitor
```

**No configuration is needed.** The defaults in each project's
`sdkconfig.defaults` are this project's hardware: pins, relay polarity, node
IDs, the S3's MAC on every node and the MacBook as the broker. Build and flash
in any order; at boot the controller logs `ONLINE` for each node as it hears it.

If a project was built before its defaults changed, delete its `sdkconfig` (and
`build_b/sdkconfig`) first: an existing `sdkconfig` keeps its old values. Change
a default only when the hardware changes, e.g. `RB_NODE_CONTROLLER_MAC` in the
nodes' `sdkconfig.defaults` if the S3 board is replaced.

**New to the hardware?** Check each part first with the projects in
[`firmware/testing/`](../firmware/testing). Confirm relay polarity and boot
state **before** connecting a real appliance. A test project replaces the system
firmware; flash it back afterwards.

## Changing settings

Every pin, timeout and threshold is in `idf.py menuconfig` → *RB4107
configuration* (all options: [configuration.md](configuration.md)). Values
flagged `UNCONFIRMED` / `OPEN QUESTION` / `UNVERIFIED` still need checking on
hardware.

Settings can also go in a file, using the `CONFIG_` name:
`<project>/sdkconfig.defaults` (committed; only applied when the project has no
`sdkconfig` yet, so delete `<project>/sdkconfig` and rebuild after editing) or
`<project>/sdkconfig` (generated, only on your machine).

**Never commit `sdkconfig`.** It can hold the Wi-Fi password.

## Broker

On the laptop (details: [tools/mqtt/README.md](../tools/mqtt/README.md)):

```bash
tools/mqtt/start_broker.sh     # allow incoming connections if macOS asks
tools/mqtt/lan_ip.sh           # the IP to put in RB_BROKER_HOST on the controller
```

The controller and laptop must be on the same network. The controller is Wi-Fi
only (`RB_NET_TYPE` = Wi-Fi station): set `RB_WIFI_SSID` and `RB_WIFI_PASSWORD`
in menuconfig.

The router sets the channel and ESP-NOW on the controller moves with it
(`ESP-NOW channel 1 -> 6` in the log; `espnow_channel` in telemetry). The nodes
follow on their own (`RB_ESPNOW_FOLLOW_CHANNEL`): after a few unacknowledged
packets a node probes channels 1-13, stays on the one where the controller
answers and remembers it for the next boot, in about a second. This needs
`RB_NODE_CONTROLLER_MAC` set on every node; a node sending to broadcast stays on
`RB_ESPNOW_CHANNEL`, which must then match the router.

### Phone hotspot as a backup

For a site without the router, store the hotspot as a second network in
menuconfig → RB4107 configuration → Network:

| Setting | Value |
|---|---|
| `RB_WIFI_SSID_2`, `RB_WIFI_PASSWORD_2` | the hotspot's name and password |
| `RB_BROKER_FALLBACK_HOST_2` | the Mac's IP on the hotspot (`tools/mqtt/lan_ip.sh` while on it; usually `172.20.10.x` on an iPhone) |

The controller joins one network at a time. It starts on `RB_WIFI_SSID` and,
after `RB_WIFI_SWITCH_AFTER` failed attempts in a row, tries the hotspot,
alternating until one works (log `switching to '<ssid>'`; the `mqtt` console
command shows the SSID in use). With the router missing that takes about 10-20 s
after boot. It stays on the hotspot until the hotspot fails; reboot to go back to
the router. The broker is tried as `RB_BROKER_HOST` (the `.local` name, which
normally works on a hotspot too), then each fallback IP in turn.

On the phone: the S3 only does 2.4 GHz, so on an iPhone turn on Personal
Hotspot → **Maximize Compatibility**, and keep the Mac joined to the hotspot too
(it runs the broker). The nodes follow the hotspot's channel as above.

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

It starts Mosquitto, the MQTT subscriber and the Django server and opens the
dashboard at http://127.0.0.1:8000/. Ctrl-C stops all three. `tools/stop_dev.sh`
does the same from another terminal and cleans up leftovers (for example "port
8000 already in use" after a crash); it only touches this project's processes.
Stopping erases the recorded data (`django/db.sqlite3`, `demo.sqlite3`), so
every run starts with an empty dashboard; leave it running until you have looked
at the results. Options: `RB4107_SKIP_BROKER=1` if a broker is already running,
`RB4107_NO_BROWSER=1` to not open the browser.

`run_dev.sh` keeps a Mac awake with `caffeinate -i -s` (`RB4107_ALLOW_SLEEP=1`
turns that off). For a multi-day run:

- Keep the Mac on its charger (`-s` only blocks sleep on mains power).
- Don't close the lid unless an external display, keyboard and power are
  connected; otherwise `sudo pmset -a disablesleep 1` (undo with `0`).
- Turn off automatic macOS updates so it doesn't restart overnight.

If the Mac does sleep, the controller keeps protecting the kitchen on its own;
only telemetry, the dashboard and Reset shut-off are missing until it wakes.

`tools/run_dev.sh --demo` adds simulated stalls next to the real one (cases a
single bench stall can't show at once). They come from
`django/locations.demo.json` (`demo_scenario` per stall), are tagged SIMULATED on
the dashboard, and live in their own `django/demo.sqlite3`. The real controller
works as usual alongside them, Reset shut-off included.

Add `--no-telegram` (`tools/run_dev.sh --demo --no-telegram`) to keep Telegram
alerts off for that run, for example when demoing to visitors, even if
`django/.env` has a bot token. `.env` is not changed.

Each controller publishes under its own `rb4107/<controller_id>` tree
(`RB_MQTT_TOPIC_PREFIX`) and Django subscribes to `rb4107/+/#`
(`RB4107_MQTT_TOPIC`), so real and simulated controllers share the broker.

Other machines on the network open `http://<the Mac's IP>:8000/`. Django accepts
`DJANGO_ALLOWED_HOSTS` (`django/rb4107_backend/settings.py`) plus whatever IP and
name the Mac has when the server starts, so on another network use the address
`run_dev.sh` prints (`RB4107_ALLOW_OWN_HOSTS=0` turns that off). The controller
finds the broker by the Mac's mDNS name (`RB_BROKER_HOST`), alternating with
`RB_BROKER_FALLBACK_HOST` after each failed attempt; both are set in
`firmware/controller/sdkconfig.defaults`.

The server has no login: anyone on the network can use the dashboard, including
**Reset shut-off** and C4002 tuning. That is fine for this demo on a controlled
network. On a shared network start it with `RB4107_HTTP_ADDR=127.0.0.1:8000` and
use an SSH tunnel. More: [django/README.md](../django/README.md).

## Gotchas

- `sdkconfig.defaults` only applies when a project has no `sdkconfig`. Delete
  `sdkconfig` after changing defaults.
- **Never commit `sdkconfig`.** It holds the Wi-Fi password (it is gitignored).
- ESP-NOW and Wi-Fi share the radio, so with Wi-Fi the ESP-NOW channel must
  match the router on every board. The nodes find it themselves
  (`RB_ESPNOW_FOLLOW_CHANNEL`).
- DFRobot's Arduino C4002 example repeats the last reading when no new frame
  arrives. The ESP-IDF driver here doesn't; see `firmware/testing/c4002/README.md`.
- Flash over UART, not JTAG.
- The node B build command above is spelled out because a `$VAR` shortcut
  doesn't word-split in zsh.

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
