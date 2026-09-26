# 28 – State machine testing

TODO section 28. Targets: **ESP32-S3** and the **host PC** (ESP-IDF `linux` target).

This project also covers the logic-only sections, which no longer have
projects of their own: 4 (protocol), 9 (state machine), 10 (behaviour with
the menuconfig timings), 11 (timers on a simulated clock) and 21 (JSON).

Unity tests for the logic that decides safety. It is all plain C with
timestamps passed in, so the tests run a simulated clock and need no
hardware.

```bash
# on the PC (needs libbsd-dev on Linux)
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig --preview set-target linux
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig build
./build_linux/rb4107_28_state_machine_tests.elf        # exit code 0 = all passed

# on the board
idf.py build flash monitor                              # prints "ALL TESTS PASSED"

# everything (firmware + Django)
../../tools/run_host_tests.sh
```

## Required scenarios

| TODO scenario | Test |
|---|---|
| IDLE → MONITORING | `test_idle_to_monitoring` (plus hysteresis) |
| MONITORING → UNATTENDED | `test_monitoring_to_unattended_after_debounce` |
| UNATTENDED → MONITORING | `test_unattended_to_monitoring_person_returns` |
| UNATTENDED → WARNING | `test_unattended_to_warning` |
| WARNING → SHUTDOWN | `test_warning_to_shutdown_total_time`, `test_shutdown_timing_after_warning_mode` |
| Person returns during UNATTENDED | `test_unattended_to_monitoring_person_returns` |
| Person returns during WARNING | `test_person_returns_during_warning` (both policies) |
| Temperature drops during the unattended period | `test_temperature_drops_during_unattended` (both policies) |
| C4002 becomes unavailable | `test_c4002_unavailable_is_not_absence`, `test_c4002_recovers`, `test_sensor_loss_never_delays_shutdown`, `test_invalid_reading_from_live_node` |
| MLX90640 becomes unavailable | `test_mlx_unavailable`, `test_fault_clears_with_person_absent_keeps_timeline` |
| Sensor node disappears / returns | `test_health_online_stale_offline_and_recovery`, `test_node_disappears_and_returns` (node health → state machine) |
| MQTT disconnects / reconnects | `test_mqtt_disconnect_and_reconnect_do_not_affect_safety` |

Plus: the menuconfig safety settings are valid and give the configured
warning/shutdown timing (`test_kconfig.c`); self-test pass/fail/timeout, latched SHUTDOWN and reset, reset
restarting the timing, heat detected after the person already left, the
optional rate threshold, the timing hook only ever shortening, 32-bit clock
wrap-around, config validation, the sequence tracking edge cases, protocol
corruption on every byte, C4002 frame resync and errors, thermal features,
JSON null/escaping/overflow, and the topic table.

**Result:** 49 tests, 0 failures, both on the host (linux target) and on
the ESP32-S3 (QEMU).

These tests found two real bugs, both fixed in `safety.c`. After a self-test
failure or timeout, FAULT recovered to IDLE straight away because its exit
condition only looked at the sensors. The self-test must now read PASS
before FAULT can clear.
