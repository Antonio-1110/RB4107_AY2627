# RB4107 ESP-NOW protocol (v2)

Sensor nodes (ESP32-C6) → controller (ESP32-S3), plus the optional wireless
valve node in both directions and C4002 tuning messages between the controller
and the presence nodes ([c4002_tuning.md](c4002_tuning.md)). The implementation is in
[`firmware/components/rb_protocol`](../firmware/components/rb_protocol), and
every firmware builds that same code.

The system has three nodes, each with one sensor:

| Node | Firmware | Default node ID | Role | Sends |
|---|---|---|---|---|
| presence node A | `presence_node` | 1 | presence | PRESENCE_DATA, HEARTBEAT, SENSOR_FAULT |
| presence node B | `presence_node` (built with `sdkconfig.node_b`) | 2 | presence | PRESENCE_DATA, HEARTBEAT, SENSOR_FAULT |
| thermal node | `thermal_node` | 3 | thermal | THERMAL_DATA, HEARTBEAT, SENSOR_FAULT |
| valve node (optional, wireless version) | `valve_node` (built with `sdkconfig.wireless`) | 4 | valve | VALVE_STATUS |
| controller | `controller` (with `RB_CTRL_VALVE_WIRELESS`) | 0 | controller | VALVE_COMMAND |

The controller only sends when the wireless valve node is turned on. Then it
is the one sender that isn't a node, so its node ID is 0.

Version 1 carried presence and thermal data in one SENSOR_DATA packet from a
single node. Version 2 splits them, and adds the sender's role to every
packet.

## Design rules

- **Explicit serialisation.** Fields are written one by one in little-endian
  order. C structs are never copied onto the wire, so compiler padding and
  alignment can't change the format.
- **Versioned.** Any change to the layout bumps `RB_PROTOCOL_VERSION`. The
  receiver rejects every other version.
- **Self-checking.** Each packet has a fixed length per message type and ends
  with a CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) over every earlier
  byte. The receiver also checks the magic, version, type, role, exact length
  and node ID.
- **Role-checked.** A message must match its sender's role (a thermal
  node can't send PRESENCE_DATA, and only the controller can command the
  valve). The controller also checks that the role
  matches what it expects for that node ID, so a board flashed with the wrong
  firmware or ID is dropped and reported (`espnow_unknown_node`), never
  mistaken for another sensor.
- **Unknown is not zero.** Missing values have sentinels (below) and decode
  to `NAN`, never to a plausible number.
- **Size-checked at compile time.** `_Static_assert`s keep every packet at or
  under the ESP-NOW v1 limit of 250 bytes. The largest packet is
  C4002_CONFIG, 102 bytes.

## Common header (17 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | magic | `0x52 0x42` ("RB") |
| 2 | 1 | protocol_version | `2` |
| 3 | 1 | message_type | `1` PRESENCE_DATA, `2` THERMAL_DATA, `3` HEARTBEAT, `4` SENSOR_FAULT, `5` VALVE_COMMAND, `6` VALVE_STATUS, `7` C4002_CONFIG, `8` C4002_CONFIG_ACK, `9` C4002_LIVE |
| 4 | 1 | node_role | `1` presence, `2` thermal, `3` valve, `4` controller |
| 5 | 4 | node_id | `CONFIG_RB_NODE_ID` of the sender |
| 9 | 4 | sequence | +1 for every packet the node sends (all types) |
| 13 | 4 | uptime_ms | node monotonic time; lets the receiver spot a node reboot |

Every packet ends with a 2-byte CRC.

## PRESENCE_DATA (type 1, 26 bytes total)

| Offset | Size | Field | Encoding |
|---|---|---|---|
| 17 | 1 | flags | bit0 valid, bit1 presence detected, bit2 moving, bit3 stationary |
| 18 | 2 | distance | cm, `0xFFFF` = unknown |
| 20 | 4 | timestamp | node ms of the reading |
| 24 | 2 | CRC | |

## THERMAL_DATA (type 2, 36 bytes total)

| Offset | Size | Field | Encoding |
|---|---|---|---|
| 17 | 1 | flags | bit0 valid |
| 18 | 2 | max temp | int16, 0.01 °C |
| 20 | 2 | min temp | int16, 0.01 °C |
| 22 | 2 | mean temp | int16, 0.01 °C |
| 24 | 2 | hot-region temp | int16, 0.01 °C |
| 26 | 2 | temp rate | int16, 0.01 °C/min |
| 28 | 2 | pixels above threshold | uint16 |
| 30 | 4 | timestamp | node ms of the frame |
| 34 | 2 | CRC | |

Temperatures use `INT16_MIN` (`0x8000`) for "unknown" and saturate at
±327.67 °C. Raw frames are never sent.

## HEARTBEAT (type 3, 29 bytes total)

Sent every `CONFIG_RB_NODE_HEARTBEAT_PERIOD_MS`, including when the sensor
has failed, so the controller can tell a dead node from a node with a dead
sensor.

| Offset | Size | Field |
|---|---|---|
| 17 | 2 | active sensor fault flags |
| 19 | 4 | ESP-NOW sends acknowledged |
| 23 | 4 | ESP-NOW sends failed |
| 27 | 2 | CRC |

## SENSOR_FAULT (type 4, 27 bytes total)

Sent right away whenever a fault flag changes, whether it is raised or
cleared.

| Offset | Size | Field |
|---|---|---|
| 17 | 2 | active sensor fault flags |
| 19 | 2 | flags that changed |
| 21 | 4 | detail (int32 driver error code, 0 if none) |
| 25 | 2 | CRC |

### Sensor fault flags

| Bit | Name | Meaning | Sent by |
|---|---|---|---|
| 0 | `C4002_NO_DATA` | no report from the C4002 within its stale timeout | presence nodes |
| 1 | `C4002_INVALID` | reports arrive but fail validation | presence nodes |
| 2 | `MLX_NO_DATA` | MLX90640 missing or failing to deliver frames | thermal node |
| 3 | `MLX_INVALID` | frames arrive but fail validation | thermal node |

## VALVE_COMMAND (type 5, 24 bytes total)

Controller → valve node. Sent every `RB_CTRL_VALVE_KEEPALIVE_MS` (1 s), and
right away when the command changes. Only the controller role may send it.

| Offset | Size | Field | Encoding |
|---|---|---|---|
| 17 | 1 | command | `1` KEEP_OPEN, `2` CLOSE. The node treats any other value as CLOSE. |
| 18 | 4 | valve node ID | the valve node this is for; other nodes ignore it |
| 22 | 2 | CRC | |

KEEP_OPEN is a dead-man signal: the valve node closes the valve if no
KEEP_OPEN arrives for `RB_VALVE_LINK_TIMEOUT_MS` (3 s). The controller sends
CLOSE on shutdown, and also when its safety task stops reporting.

## VALVE_STATUS (type 6, 26 bytes total)

Valve node → controller. Sent every `RB_VALVE_STATUS_PERIOD_MS` (1 s), and
right away when the valve moves. Only the valve role may send it.

| Offset | Size | Field | Encoding |
|---|---|---|---|
| 17 | 1 | position | `1` OPEN, `2` CLOSED (commanded: a hobby servo has no feedback) |
| 18 | 1 | flags | bit0 moving (within `RB_VALVE_TRAVEL_MS` of a move), bit1 latched closed until reset |
| 19 | 1 | reason | why it is closed: `0` open, `1` boot, `2` controller sent CLOSE, `3` no keep-open, `4` wired line high |
| 20 | 4 | last command sequence | `sequence` of the last VALVE_COMMAND accepted, 0 if none |
| 24 | 2 | CRC | |

The valve messages were added without bumping the version. The existing
messages are byte-for-byte unchanged, so the sensor nodes don't need
reflashing. An older controller rejects the new types as an unknown message
type.

## C4002 parameters block (70 bytes)

Used by both tuning messages. Field meanings and limits:
[c4002_tuning.md](c4002_tuning.md).

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | report period (0.1 s) |
| 1 | 2 | range min (cm) |
| 3 | 2 | range max (cm) |
| 5 | 1 | resolution: `0` 80 cm, `1` 20 cm |
| 6 | 1 | motion sensitivity: `0` low, `1` mid, `2` high, `3` custom |
| 7 | 1 | presence sensitivity (same codes) |
| 8 | 2 | disappear delay (s) |
| 10 | 1 | lock time (0.1 s) |
| 11 | 4 | motion gate mask (bit *i* = gate *i* on) |
| 15 | 4 | presence gate mask |
| 19 | 1 | thresholds known: bit0 motion, bit1 presence |
| 20 | 25 | motion thresholds, one per gate |
| 45 | 25 | presence thresholds |

## C4002_CONFIG (type 7, 102 bytes total)

Controller → one presence node, sent to the MAC the controller learned from
that node's packets. Header node_id is 0, role `4` controller. The node only
accepts it from `RB_NODE_CONTROLLER_MAC`.

| Offset | Size | Field |
|---|---|---|
| 17 | 4 | target node ID |
| 21 | 2 | request ID (echoed in the ACK) |
| 23 | 1 | action: `1` apply, `2` calibrate, `3` read, `4` reset |
| 24 | 2 | field mask (apply): which parameters to change, see `RB_C4002_F_*` |
| 26 | 2 | calibration delay (s) |
| 28 | 2 | calibration duration (s) |
| 30 | 70 | parameters block |
| 100 | 2 | CRC |

## C4002_CONFIG_ACK (type 8, 96 bytes total)

Presence node → controller, after every C4002_CONFIG, and again when a
calibration finishes. Uses the node's normal sequence counter.

| Offset | Size | Field |
|---|---|---|
| 17 | 2 | request ID (0 = the controller's automatic read) |
| 19 | 1 | action answered |
| 20 | 1 | result: `0` ok, `1` invalid, `2` sensor error |
| 21 | 2 | calibration seconds remaining (0 = none running) |
| 23 | 1 | settings saved in NVS (0/1) |
| 24 | 70 | parameters block: the settings now in use |
| 94 | 2 | CRC |

## C4002_LIVE (type 9, 46 bytes total)

Presence node → controller: the C4002's newest detection result, unfiltered,
at most every `RB_C4002_LIVE_PERIOD_MS` (default 250 ms), only when a new one
came in. For the dashboard's live radar view; the safety logic ignores it and
keeps using PRESENCE_DATA. Uses the node's normal sequence counter.

| Offset | Size | Field |
|---|---|---|
| 17 | 1 | target state: `0` none, `1` stationary, `2` moving |
| 18 | 1 | resolution in use: `0` 80 cm gates, `1` 20 cm gates |
| 19 | 4 | stationary gate mask (bit *i* = gate *i*) |
| 23 | 2 | hold countdown (s) |
| 25 | 2 | stationary target distance (cm) |
| 27 | 1 | stationary target energy (0–99) |
| 28 | 2 | moving target distance (cm) |
| 30 | 2 | moving target speed (cm/s, signed) |
| 32 | 1 | moving target energy (0–99) |
| 33 | 1 | direction: `0` away, `1` none, `2` approaching |
| 34 | 2 | light (0.1 lux) |
| 36 | 2 | calibration seconds remaining |
| 38 | 2 | age of the result when sent (ms) |
| 40 | 4 | results received from the sensor since boot |
| 44 | 2 | CRC |

Messages 7 to 9 were added the same way.

## Edge decisions (not decided yet)

Today every node sends its reading and the controller decides (combining the
two radars, then the safety state machine). If the nodes later decide
something themselves (e.g. a presence node debouncing its own radar), that is
a new field or message type and a protocol version bump. The controller's
per-node tracking, health monitoring and faults stay the same.
