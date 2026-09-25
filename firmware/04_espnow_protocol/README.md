# 04 – Shared ESP-NOW protocol

TODO section 4. Target: either board (defaults to ESP32-C6).

The protocol is the `components/rb_protocol` component. The C6 sender (05) and
the S3 receiver (07 and later) both build it, so their definitions can't drift
apart. The wire format is specified in [`docs/protocol.md`](../../docs/protocol.md).

This project runs the codec on the target and prints `all protocol checks PASSED`:

```bash
idf.py build flash monitor
```

Compile-time checks: every packet size is `_Static_assert`ed against the
ESP-NOW v1 payload limit (250 bytes), and this project also checks that limit
against the ESP-IDF definition `ESP_NOW_MAX_DATA_LEN`.
