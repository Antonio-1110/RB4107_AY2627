# 15 – RTC / time

> **Test-only project, not part of the running system.** It checks one part on its own and replaces the system firmware on that board. To run the
> system, flash [`05a_c6_presence_node`](../05a_c6_presence_node) on the two
> presence C6 boards, [`05b_c6_thermal_node`](../05b_c6_thermal_node) on the
> thermal C6 board and [`29_end_to_end`](../29_end_to_end) on the S3.

TODO section 15. Target: **Waveshare ESP32-S3-ETH-8DI-8RO** (PCF85063 RTC at `0x51`).

There are two separate notions of time:

| | Monotonic | Wall clock |
|---|---|---|
| API | `rb_time_mono_ms()` (`components/rb_time`) | `rb_wallclock_iso8601()` (`components/rb_wallclock`) |
| Source | `esp_timer` | PCF85063 at boot, then SNTP (written back to the RTC) |
| Used for | unattended/warning timers, sensor and communication timeouts, state durations | event timestamps, logs, MQTT messages |
| When wrong | can't be wrong (never jumps) | timestamps are missing or wrong, and safety is unaffected |

The RTC stores **UTC**. `RB_TIME_TZ` (default `SGT-8`) only affects how
timestamps are formatted. An event's timestamp is worked out from its
monotonic time, so events queued during a network outage still get correct
wall-clock times.

## Persistence test

1. Enable `RB_TIME_SET_RTC_FROM_BUILD` (or run a networked build so SNTP sets it).
2. Flash and check the RTC reads `ok`.
3. Power the board off for a few minutes, then on again.
4. The boot log should say `wall clock restored from PCF85063` with the correct time.
