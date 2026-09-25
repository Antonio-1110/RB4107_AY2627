# RB4107 ESP-NOW protocol (v1)

Sensor node (ESP32-C6) → controller (ESP32-S3). The implementation is in
[`firmware/components/rb_protocol`](../firmware/components/rb_protocol), and
both firmwares build that same code.

## Design rules

- **Explicit serialisation.** Fields are written one by one in little-endian
  order. C structs are never copied onto the wire, so compiler padding and
  alignment can't change the format.
- **Versioned.** Any change to the layout bumps `RB_PROTOCOL_VERSION`. The
  receiver rejects every other version.
- **Self-checking.** Each packet has a fixed length per message type and ends
  with a CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) over every earlier
  byte. The receiver also checks the magic, version, type, exact length and
  node ID.
- **Unknown is not zero.** Missing values have sentinels (below) and decode
  to `NAN`, never to a plausible number.
- **Size-checked at compile time.** `_Static_assert`s keep every packet at or
  under the ESP-NOW v1 limit of 250 bytes. The largest packet is 41 bytes.

## Common header (16 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 2 | magic | `0x52 0x42` ("RB") |
| 2 | 1 | protocol_version | `1` |
| 3 | 1 | message_type | `1` SENSOR_DATA, `2` HEARTBEAT, `3` SENSOR_FAULT |
| 4 | 4 | node_id | `CONFIG_RB_NODE_ID` of the sender |
| 8 | 4 | sequence | +1 for every packet the node sends (all types) |
| 12 | 4 | uptime_ms | node monotonic time; lets the receiver spot a node reboot |

Every packet ends with a 2-byte CRC.

## SENSOR_DATA (type 1, 41 bytes total)

| Offset | Size | Field | Encoding |
|---|---|---|---|
| 16 | 1 | flags | bit0 presence valid, bit1 presence detected, bit2 moving, bit3 stationary, bit4 thermal valid |
| 17 | 2 | presence distance | cm, `0xFFFF` = unknown |
| 19 | 4 | presence timestamp | node ms |
| 23 | 2 | max temp | int16, 0.01 °C |
| 25 | 2 | min temp | int16, 0.01 °C |
| 27 | 2 | mean temp | int16, 0.01 °C |
| 29 | 2 | hot-region temp | int16, 0.01 °C |
| 31 | 2 | temp rate | int16, 0.01 °C/min |
| 33 | 2 | pixels above threshold | uint16 |
| 35 | 4 | thermal timestamp | node ms |
| 39 | 2 | CRC | |

Temperatures use `INT16_MIN` (`0x8000`) for "unknown" and saturate at
±327.67 °C.

## HEARTBEAT (type 2, 28 bytes total)

Sent every `CONFIG_RB_NODE_HEARTBEAT_PERIOD_MS`, including when both sensors
have failed, so the controller can tell a dead node from a node with dead
sensors.

| Offset | Size | Field |
|---|---|---|
| 16 | 2 | active sensor fault flags |
| 18 | 4 | ESP-NOW sends acknowledged |
| 22 | 4 | ESP-NOW sends failed |
| 26 | 2 | CRC |

## SENSOR_FAULT (type 3, 26 bytes total)

Sent right away whenever a fault flag changes, whether it is raised or
cleared.

| Offset | Size | Field |
|---|---|---|
| 16 | 2 | active sensor fault flags |
| 18 | 2 | flags that changed |
| 20 | 4 | detail (int32 driver error code, 0 if none) |
| 24 | 2 | CRC |

### Sensor fault flags

| Bit | Name | Meaning |
|---|---|---|
| 0 | `C4002_NO_DATA` | no report from the C4002 within its stale timeout |
| 1 | `C4002_INVALID` | reports arrive but fail validation |
| 2 | `MLX_NO_DATA` | MLX90640 missing or failing to deliver frames |
| 3 | `MLX_INVALID` | frames arrive but fail validation |
