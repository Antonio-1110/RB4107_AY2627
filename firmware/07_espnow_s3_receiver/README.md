# 07 – ESP-NOW receiver (ESP32-S3)

TODO section 7. Target: **Waveshare ESP32-S3-ETH-8DI-8RO**. Pair it with
project 05 running on the C6.

```text
Sensors → C6 → ESP-NOW → S3 → serial monitor
```

| Check | Where |
|---|---|
| Protocol version, packet size, magic, type, CRC | `rb_espnow` receive callback (`rb_protocol_decode`) |
| Node ID | `sensor_node_on_packet()` against `RB_CTRL_NODE_ID` |
| Sequence tracking: gaps, duplicates, out-of-order, node restart | `sensor_node_on_packet()` |
| Last-packet timestamp | `sensor_node_state_t.last_received_ms` |
| Sensor-node state (`latest_data`, `last_sequence`, `online`, fault flags) | `sensor_node_state_t` |

The callback runs in the Wi-Fi task, so it never blocks. If the queue is full
the packet is dropped and counted in `overflow`.

Set `RB_ESPNOW_CHANNEL` to the same value on both boards.
