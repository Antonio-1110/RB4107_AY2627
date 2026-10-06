# Test: buzzer driver

> **Test only, not part of the running system.** It checks the buzzer on its own. Flashing it
> replaces the system firmware on that board. To run the system, flash
> [`presence_node`](../../presence_node) on the two presence C6 boards,
> [`thermal_node`](../../thermal_node) on the thermal C6 board and
> [`controller`](../../controller) on the S3.

Target: **Waveshare ESP32-S3-POE-ETH-8DI-8DO**.

## Interface

The buzzer is on **GPIO46** and is **passive**: it needs a tone (PWM), and a
plain DC level only makes it click. Confirmed on the bench, and that is the
default (2.7 kHz tone). Active buzzers are still supported: menuconfig → *Controller board* → *Buzzer* → *Buzzer type*.

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
