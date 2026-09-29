# Handover: RB4107 firmware work (for a new Claude Code session)

Context for picking this project up in a new session, e.g. a local one that
can build and flash. Read this first, then `README.md`, `firmware/README.md`
and `TODO.md`.

**Dashboard update, 2026-09-29:** The original firmware handover below is
historical. The existing Django subscriber now persists schema-v2 messages and
serves the AES dashboard. See [`dashboard_integration.jw.md`](dashboard_integration.jw.md)
and `backend/django/README.md`; use `manage.py migrate` before starting it.
The firmware source and physical safety logic are unchanged by this integration.

## Where things stand

- **Branch:** `claude/sweet-hopper-j6lyov`. PR #1 (the first big
  implementation) is already merged into `main`. The commits after it are on
  the branch but **not in a PR yet**. Don't open one unless the user asks.
- **Everything is written and runs in emulation. Nothing has run on real
  hardware yet.** The unchecked boxes in `TODO.md` are bench work or team
  decisions.
- Keep `TODO.md` updated as items are finished (the user asked for this).

## The system

```text
C4002 #1 → ESP32-C6 presence node A (node ID 1) ─┐
C4002 #2 → ESP32-C6 presence node B (node ID 2) ─┼─ ESP-NOW → ESP32-S3 controller
MLX90640 → ESP32-C6 thermal node    (node ID 3) ─┘            ├── safety state machine → buzzer, shutdown relay
                                                              └── Ethernet/Wi-Fi → Mosquitto (MacBook) → Django
```

- C6 boards: **DFRobot DFR1117** (ESP32-C6 mini). S3 board: **Waveshare
  ESP32-S3-ETH-8DI-8RO**.
- The S3 must keep protecting the kitchen when MQTT, Django, the MacBook or
  the network is down.

## Repository layout

```text
firmware/
├── presence_node/   SYSTEM FIRMWARE: flash on both C6 + C4002 boards (node B: see its README)
├── thermal_node/    SYSTEM FIRMWARE: C6 + MLX90640
├── controller/      SYSTEM FIRMWARE: ESP32-S3
├── components/      shared code (drivers, protocol, safety logic, MQTT, ...)
└── testing/         test-only projects: c4002, mlx90640, s3_board, buzzer, relay, rtc, network, unit_tests
backend/django/      MQTT subscriber (validates and logs; no database or dashboard yet)
tools/mqtt/          Mosquitto config and scripts
tools/diagnostics/   e2e_check.py, critical_failure_test.py, validate_json.py, kconfig_doc.py, ...
docs/                protocol, MQTT topics and schema, architecture, configuration, open questions, ...
legacy/              old prototypes, reference only
```

Every setting (pins, timeouts, thresholds, broker, node IDs) is in
`firmware/components/rb_config/Kconfig` (menuconfig → *RB4107 configuration*).
`docs/configuration.md` is generated from it by `tools/diagnostics/kconfig_doc.py`;
regenerate it after editing the Kconfig (command at the end of that file).

## Decisions already made (don't reopen without the user)

- **Folder structure:** the three system firmwares at the top of `firmware/`,
  every test-only project under `firmware/testing/`, no numbers in names.
- **One node per sensor.** `presence_node` is one firmware flashed on two
  boards; node B is built with `sdkconfig.node_b` in its own build folder.
- **Radars combined strictly:** PRESENT if either radar sees a person;
  ABSENT only if both are valid and both say absent; anything else is
  UNKNOWN, which means FAULT. Every node going offline or reporting an
  invalid sensor is its own SAFETY fault. (`components/sensor_node`,
  `presence_fuse`.)
- **The S3 decides; nodes only send readings.** Whether nodes should decide
  anything ("edge") is still an open question.
- **Missing data is never "safe"**: it is UNKNOWN / invalid / `null`.
- **ESP-NOW protocol v2** (`docs/protocol.md`): PRESENCE_DATA and
  THERMAL_DATA packets; every packet carries the node role. **MQTT schema
  v2** (`docs/mqtt_schema.md`): telemetry has `presence_state` plus a `nodes`
  list. Django only accepts schema v2.
- Unconfirmed hardware values stay configurable and are flagged
  `UNCONFIRMED` / `OPEN QUESTION` (user's instruction: "leave pins
  configurable").

## Pins (DFR1117 schematic V1.0/V1.1)

| Function | Default | Status |
|---|---|---|
| C4002 → ESP RX | IO5 (header P3 pin 4) | from schematic; **RX/TX direction unconfirmed**: if the C4002 test shows `frames=0`, swap |
| ESP TX → C4002 | IO4 (header P3 pin 3) | same |
| Status LED | IO15 | confirmed on schematic |
| MLX90640 SDA / SCL | IO19 / IO20 (header P4) | **unconfirmed**; schematic has no I2C labels. The old Arduino sketch used 21/22, which also exist on this board |
| UART0 | IO16 TX / IO17 RX | left free (ROM prints there at boot); console is USB-Serial/JTAG |

Pins can be set in a file instead of menuconfig: see "Setting values in a
file" in `docs/configuration.md`.

## Verified so far (in the cloud, no hardware)

- All 11 projects build with ESP-IDF v6.1, 0 warnings (last build after the
  folder rename).
- 57 unit tests pass on the host (linux target) and on the ESP32-S3 in QEMU.
- 23 Django tests pass (1 skipped: needs a live broker).
- The controller in QEMU (emulated Ethernet, 3 simulated nodes) against real
  Mosquitto and Django:
  - `e2e_check.py` passed 10/10, all 524 captured messages schema-valid;
  - losing radar B → FAULT, and recovery when it returns;
  - broker outage: WARNING and SHUTDOWN still happened, continuity monitor
    PASS.
- The cloud had no access to the Espressif component registry, so it built
  with `IDF_COMPONENT_MANAGER=0` and local copies of `espressif/w5500` and
  `espressif/mqtt`. **A normal local build downloads them; that path has not
  been tried yet.**

## First things to do locally

```bash
. $IDF_PATH/export.sh
tools/run_host_tests.sh          # unit tests (linux target) + Django tests

cd firmware/controller && idf.py build
cd ../presence_node && idf.py build
cd ../thermal_node && idf.py build
# and each firmware/testing/* project if wanted
```

- On Linux the host unit tests need `libbsd-dev`. On macOS, check that the
  linux target builds at all; it has only been run on Linux.
- Try the controller without hardware: `firmware/controller/README.md`,
  section "Running without hardware (QEMU)".

## Gotchas

- **`sdkconfig` vs `sdkconfig.defaults`:** defaults only apply when a
  project has no `sdkconfig`. After changing defaults, delete `sdkconfig`
  and rebuild.
- **Old local `sdkconfig` files:** the folders were renamed
  (`29_end_to_end` → `controller`, `05_espnow_c6_sender` → `presence_node`,
  ...). Any git-ignored `sdkconfig` a checkout already had stays behind in
  the old folder. The controller's one holds the Wi-Fi password. Move it to
  `firmware/controller/sdkconfig` or re-enter the settings, then delete the
  old folders.
- **Never commit `sdkconfig`.** A Wi-Fi password leaked once and the user
  rewrote git history to remove it. Wi-Fi credentials live only in the
  git-ignored `sdkconfig`.
- **Node B build command** (spelled out, because a `$VAR` shortcut doesn't
  word-split in zsh):
  ```bash
  idf.py -B build_b -D SDKCONFIG=build_b/sdkconfig \
         -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.node_b" -p <PORT> flash monitor
  ```
- **The old DFRobot Arduino C4002 example** repeats the last reading forever
  when no new frame arrives (uninitialised return in `getNoteInfo()`). It
  also misparses the result struct. That explains the earlier "same data
  over and over". The ESP-IDF driver here parses the result packed and marks
  readings stale. See `firmware/testing/c4002/README.md`.

## Suggested bench order

1. `testing/c4002` and `testing/mlx90640` on the C6 boards: confirm the
   pins, UART direction and readings.
2. `testing/s3_board`, `testing/buzzer`, `testing/relay`, `testing/rtc`:
   confirm relay polarity and boot state **before** connecting a real load.
3. `presence_node` ×2, `thermal_node` and `controller`: radio link and the
   full safety loop, with the network unplugged.
4. `testing/network`, `tools/mqtt`, Django; then `tools/diagnostics/e2e_check.py`
   and `critical_failure_test.py` on real hardware.
5. Collect thermal data and tune thresholds and timings (`docs/open_questions.md`).

## Working preferences the user has shown

- Explicit, plain naming; the user asks when something is unclear, so
  explain in plain terms.
- Fewer, well-organised projects rather than many small ones.
- Keep `TODO.md` current. Don't create PRs unless asked.
- Push only to the working branch; never push the pre-rewrite history.
