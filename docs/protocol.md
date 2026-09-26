# RB4107 ESP-NOW protocol (v2)

Sensor nodes (ESP32-C6) → controller (ESP32-S3). The implementation is in
[`firmware/components/rb_protocol`](../firmware/components/rb_protocol), and
every firmware builds that same code.

The system has three nodes, each with one sensor:

| Node | Firmware | Default node ID | Role | Sends |
|---|---|---|---|---|
| presence node A | `05a_c6_presence_node` | 1 | presence | PRESENCE_DATA, HEARTBEAT, SENSOR_FAULT |
| presence node B | `05a_c6_presence_node` (built with `sdkconfig.node_b`) | 2 | presence | PRESENCE_DATA, HEARTBEAT, SENSOR_FAULT |
| thermal node | `05b_c6_thermal_node` | 3 | thermal | THERMAL_DATA, HEARTBEAT, SENSOR_FAULT |

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
- **Role-checked.** A data message must match its sender's role (a thermal
  node can't send PRESENCE_DATA). The controller also checks that the role
  matches what it expects for that node ID, so a board flashed with the wrong
  firmware or ID is dropped and reported (`espnow_unknown_node`), never
  mistaken for another sensor.
- **Unknown is not zero.** Missing values have sentinels (below) and decode
  to `NAN`, never to a plausible number.
- **Size-checked at compile time.** `_Static_assert`s keep every packet at or
  under the ESP-NOW v1 limit of 250 bytes. The largest packet is 36 bytes.

## Common header (17 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | magic | `0x52 0x42` ("RB") |
| 2 | 1 | protocol_version | `2` |
| 3 | 1 | message_type | `1` PRESENCE_DATA, `2` THERMAL_DATA, `3` HEARTBEAT, `4` SENSOR_FAULT |
| 4 | 1 | node_role | `1` presence, `2` thermal |
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

## Edge decisions (not decided yet)

Today every node sends its reading and the controller decides (combining the
two radars, then the safety state machine). If the nodes later decide
something themselves (e.g. a presence node debouncing its own radar), that is
a new field or message type and a protocol version bump. The controller's
per-node tracking, health monitoring and faults stay the same.
