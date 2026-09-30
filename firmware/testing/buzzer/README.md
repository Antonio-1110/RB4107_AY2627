# Test: buzzer driver

> **Test only, not part of the running system.** It checks the buzzer on its own. Flashing it
> replaces the system firmware on that board. To run the system, flash
> [`presence_node`](../../presence_node) on the two presence C6 boards,
> [`thermal_node`](../../thermal_node) on the thermal C6 board and
> [`controller`](../../controller) on the S3.

Target: **Waveshare ESP32-S3-ETH-8DI-8RO**.

## Interface (UNCONFIRMED)

The earlier prototype controller drove the buzzer on **GPIO46, active high**, and that
is the default. Check the Waveshare schematic to confirm it, and to find out
whether the buzzer is **active** (DC level) or **passive** (needs a tone).
Both are supported: menuconfig → *Controller board* → *Buzzer* → *Buzzer type*.

## API (`components/buzzer`)

```c
buzzer_init(&cfg);                        // pin configured, silent
buzzer_on();                              // continuous
buzzer_off();
buzzer_set_pattern(BUZZER_PATTERN_WARNING);   // non-blocking, played by an esp_timer
```

| Safety state | Pattern |
|---|---|
| WARNING | 200 ms on / 800 ms off |
| SHUTDOWN | 150 ms on / 150 ms off |
| FAULT | double chirp every 2 s |

This demo plays each pattern for 4 s in a loop.
