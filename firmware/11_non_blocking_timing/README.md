# 11 – Non-blocking timing

TODO section 11. Target: **ESP32-S3** (no peripherals needed).

The pattern every safety timer in this repository follows:

```c
// never: vTaskDelay(pdMS_TO_TICKS(60000));
if (state == UNATTENDED && now - unattended_start >= warning_timeout) {
    transition_to(WARNING);
}
```

| Rule | Where |
|---|---|
| `esp_timer` monotonic clock | `components/rb_time` (`rb_time_mono_ms()`) |
| FreeRTOS timing | the safety task blocks on its queue for at most one 100 ms tick (`xQueueReceive` timeout) |
| monotonic timestamps | `rb_time_elapsed(now, since, timeout)` is wrap-safe |
| state-entry timestamps | `safety_sm_t.state_entered_ms`, `unattended_start_ms`, `warning_start_ms` |

In this demo a simulated sensor task sends 5 updates/s. The safety task logs
once a second, and `sensor updates processed` keeps going up the whole time
the 5 s warning and 10 s shutdown timers run.
