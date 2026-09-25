# 09 – Safety state machine

TODO section 9. Target: **ESP32-S3** (the logic itself is plain C and runs anywhere).

The state machine is the `components/safety` component. This project runs it
against a scripted scenario on a simulated clock and prints every transition:

```text
I SAFETY: t=   0.5s  SELF_TEST  -> IDLE       (self-test passed)  buzzer=OFF shutdown=off
I SAFETY: t=   2.0s  IDLE       -> MONITORING (heating detected)  buzzer=OFF shutdown=off
I SAFETY: t=   6.0s  MONITORING -> UNATTENDED (person absent)  buzzer=OFF shutdown=off
I SAFETY: t=  10.0s  UNATTENDED -> WARNING    (unattended timeout)  buzzer=WARNING shutdown=off
I SAFETY: t=  15.0s  WARNING    -> SHUTDOWN   (shutdown timeout)  buzzer=SHUTDOWN shutdown=ON
...
```

- State enum: `safety_state_t`.
- Entry and exit logic: `on_enter()` / `on_exit()` in `safety.c`. Outputs come
  from a per-state table and are applied on entry.
- Explicit transition conditions: one `case` per state in `evaluate()`.
- Every transition goes through `transition()`, which calls the transition
  callback (logging here; MQTT events in later sections).
- No MQTT, network or hardware dependency.

The full transition table and design notes are in
[`docs/safety_state_machine.md`](../../docs/safety_state_machine.md).
